/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Math.h>
#include <AK/ScopeGuard.h>
#include <AK/Utf16String.h>
#include <LibCore/EventLoop.h>
#include <LibGfx/Bitmap.h>
#include <LibURL/URL.h>
#include <LibWebView/ExternalURLHandler.h>
#include <LibWebView/HistoryVisitTransition.h>
#include <LibWebView/Menu.h>
#include <LibWebView/Utilities.h>
#include <Rune/Bridge/Application.h>
#include <Rune/Bridge/Engine.h>
#include <Rune/Bridge/Input.h>
#include <Rune/Bridge/View.h>
#include <UI/Bangs.h>
#include <UI/XenonSettings.h>

namespace Xenon {

static constexpr auto multiple_click_interval = AK::Duration::from_milliseconds(500);
static constexpr double multiple_click_distance = 4;

static XenonStr xenon_str(StringView string)
{
    return { string.characters_without_null_termination(), string.length() };
}

class HistoryActionObserver final : public WebView::Action::Observer {
public:
    explicit HistoryActionObserver(Function<void()> on_change)
        : m_on_change(move(on_change))
    {
    }

    virtual void on_enabled_state_changed(WebView::Action&) override { m_on_change(); }

private:
    Function<void()> m_on_change;
};

class ActionStateObserver final : public WebView::Action::Observer {
public:
    explicit ActionStateObserver(Function<void()> on_change)
        : m_on_change(move(on_change))
    {
    }

    virtual void on_text_changed(WebView::Action&) override { m_on_change(); }
    virtual void on_visible_state_changed(WebView::Action&) override { m_on_change(); }
    virtual void on_checked_state_changed(WebView::Action&) override { m_on_change(); }
    virtual void on_engaged_state_changed(WebView::Action&) override { m_on_change(); }

private:
    Function<void()> m_on_change;
};

NonnullOwnPtr<View> View::create(XenonViewId id, Web::DevicePixelSize viewport_size, double device_pixel_ratio, WebView::IsPrivate is_private, Web::HTML::VisibilityState visibility_state, Optional<WebView::CanonicalTraversable&> traversable)
{
    auto view = adopt_own(*new View(id, viewport_size, device_pixel_ratio, is_private));
    view->initialize_tab(visibility_state, traversable);
    view->report_history_state();
    Application::the().did_create_view(*view);
    return view;
}

View::View(XenonViewId id, Web::DevicePixelSize viewport_size, double device_pixel_ratio, WebView::IsPrivate is_private)
    : ViewImplementation(is_private)
    , m_id(id)
    , m_viewport_size(viewport_size)
{
    m_device_pixel_ratio = device_pixel_ratio;
    m_window_size = viewport_size.to_type<int>();
    m_presents_from_cpu_memory = true;
    set_page_background_color_to_system_canvas(Engine::the().system_dark());
    install_callbacks();
}

View::~View()
{
    on_ready_to_paint = nullptr;
    on_title_change = nullptr;
    on_loading_state_change = nullptr;
    on_favicon_change = nullptr;
    on_cursor_change = nullptr;
    on_link_hover = nullptr;
    on_link_unhover = nullptr;
    on_new_web_view = nullptr;
    on_close = nullptr;
    on_web_content_crashed = nullptr;
}

void View::install_callbacks()
{
    auto& engine = Engine::the();

    on_ready_to_paint = [this] {
        present_current_frame();
    };

    on_title_change = [this, &engine](Utf16String const& title) {
        auto utf8_title = title.to_utf8();
        engine.notify(&XenonUiCallbacks::view_title_changed, m_id, xenon_str(utf8_title));
    };

    on_url_change = [this, &engine](URL::URL const& url) {
        m_suppress_dialogs_until_navigation = false;
        auto serialized_url = url.serialize();
        engine.notify(&XenonUiCallbacks::view_url_changed, m_id, xenon_str(serialized_url));
        report_history_state();
    };

    on_loading_state_change = [this, &engine](bool is_loading) {
        engine.notify(&XenonUiCallbacks::view_loading_changed, m_id, is_loading);
    };

    on_favicon_change = [this, &engine](Optional<Gfx::Bitmap const&> favicon) {
        if (!favicon.has_value()) {
            engine.notify(&XenonUiCallbacks::view_favicon_changed, m_id, 0u, 0u, static_cast<u8 const*>(nullptr));
            return;
        }

        auto const& bitmap = *favicon;
        auto is_premultiplied = bitmap.alpha_type() == Gfx::AlphaType::Premultiplied;
        Vector<u8> rgba;
        rgba.resize(static_cast<size_t>(bitmap.width()) * bitmap.height() * 4);
        size_t offset = 0;
        for (int y = 0; y < bitmap.height(); ++y) {
            for (int x = 0; x < bitmap.width(); ++x) {
                auto color = bitmap.get_pixel(x, y);
                auto unpremultiply = [&](u8 channel) -> u8 {
                    if (!is_premultiplied || color.alpha() == 0 || color.alpha() == 255)
                        return channel;
                    return static_cast<u8>(min(255u, (static_cast<u32>(channel) * 255 + color.alpha() / 2) / color.alpha()));
                };
                rgba[offset++] = unpremultiply(color.red());
                rgba[offset++] = unpremultiply(color.green());
                rgba[offset++] = unpremultiply(color.blue());
                rgba[offset++] = color.alpha();
            }
        }
        engine.notify(&XenonUiCallbacks::view_favicon_changed, m_id, static_cast<u32>(bitmap.width()), static_cast<u32>(bitmap.height()), static_cast<u8 const*>(rgba.data()));
    };

    on_cursor_change = [this, &engine](Gfx::Cursor const& cursor) {
        engine.notify(&XenonUiCallbacks::view_cursor_changed, m_id, static_cast<u32>(xenon_cursor_from(cursor)));
    };

    on_link_hover = [this, &engine](URL::URL const& url) {
        auto serialized_url = url.serialize();
        engine.notify(&XenonUiCallbacks::view_link_hovered, m_id, xenon_str(serialized_url));
    };

    on_link_unhover = [this, &engine] {
        engine.notify(&XenonUiCallbacks::view_link_hovered, m_id, xenon_str({}));
    };

    on_new_web_view = [this, &engine](Web::HTML::ActivateTab activate_tab, Web::HTML::WebViewHints, WebView::CanonicalTraversable& traversable) {
        return engine.open_view(m_id, activate_tab, traversable).handle();
    };

    on_close = [this, &engine] {
        engine.close_tab(m_id);
    };

    on_web_content_crashed = [this, &engine](WebContentCrashReason) {
        reset_page_prompts();
        engine.notify(&XenonUiCallbacks::view_crashed, m_id);
    };

    on_browser_history_traversal_complete = [this] {
        m_suppress_dialogs_until_navigation = false;
        report_history_state();
    };

    auto observe_history_action = [this](WebView::Action& action) {
        action.add_observer(make<HistoryActionObserver>([weak_this = make_weak_ptr<View>()] {
            if (weak_this)
                weak_this->report_history_state();
        }));
    };
    observe_history_action(navigate_back_action());
    observe_history_action(navigate_forward_action());

    reset_zoom_action().add_observer(make<ActionStateObserver>([weak_this = make_weak_ptr<View>()] {
        if (weak_this)
            weak_this->report_zoom();
    }));
    toggle_bookmark_action().add_observer(make<ActionStateObserver>([weak_this = make_weak_ptr<View>()] {
        if (weak_this)
            weak_this->report_bookmarked();
    }));

    install_page_callbacks();
}

void View::present_current_frame()
{
    Gfx::SharedImageBuffer const* shared_image_buffer = nullptr;
    Web::DevicePixelSize size;
    double device_pixel_ratio = m_device_pixel_ratio;

    if (m_has_usable_bitmap && m_front_bitmap.shared_image_buffer) {
        shared_image_buffer = m_front_bitmap.shared_image_buffer.ptr();
        size = m_front_bitmap.last_painted_size;
        device_pixel_ratio = m_front_bitmap.last_painted_device_pixel_ratio;
    } else if (m_backup_shared_image_buffer) {
        shared_image_buffer = m_backup_shared_image_buffer.ptr();
        size = m_backup_bitmap_size;
        device_pixel_ratio = m_backup_bitmap_device_pixel_ratio;
    }

    if (!shared_image_buffer)
        return;

    auto bitmap = shared_image_buffer->bitmap_if_present();
    if (!bitmap) {
        dbgln("Xenon: The Compositor presented a frame without CPU-readable pixels to view {}", m_id);
        return;
    }

    auto width = min(size.width().value(), bitmap->width());
    auto height = min(size.height().value(), bitmap->height());
    if (width <= 0 || height <= 0)
        return;

#ifdef AK_OS_MACOS
    auto const& iosurface = shared_image_buffer->iosurface_handle();
    if (!iosurface.lock_read_only()) {
        dbgln("Xenon: Couldn't lock the IOSurface of view {}'s frame for reading", m_id);
        return;
    }
    ScopeGuard unlock_iosurface = [&] { iosurface.unlock_read_only(); };
#endif

    XenonFrame frame {
        .width = static_cast<u32>(width),
        .height = static_cast<u32>(height),
        .stride = bitmap->pitch(),
        .bgra = bitmap->scanline_u8(0),
        .device_pixel_ratio = device_pixel_ratio,
    };
    Engine::the().notify(&XenonUiCallbacks::view_frame, m_id, static_cast<XenonFrame const*>(&frame));
}

void View::report_zoom()
{
    auto percent = static_cast<u32>(round_to<int>(zoom_level() * 100));
    if (m_reported_zoom_percent == percent)
        return;
    m_reported_zoom_percent = percent;
    Engine::the().notify(&XenonUiCallbacks::view_zoom_changed, m_id, percent);
}

void View::report_bookmarked()
{
    auto bookmarked = toggle_bookmark_action().engaged();
    if (m_reported_bookmarked == bookmarked)
        return;
    m_reported_bookmarked = bookmarked;
    Engine::the().notify(&XenonUiCallbacks::view_bookmarked_changed, m_id, bookmarked);
}

void View::report_history_state()
{
    auto can_go_back = navigate_back_action().enabled();
    auto can_go_forward = navigate_forward_action().enabled();
    if (m_reported_can_go_back == can_go_back && m_reported_can_go_forward == can_go_forward)
        return;

    m_reported_can_go_back = can_go_back;
    m_reported_can_go_forward = can_go_forward;
    Engine::the().notify(&XenonUiCallbacks::view_history_changed, m_id, can_go_back, can_go_forward);
}

void View::load_user_input(StringView input)
{
    auto trimmed_input = input.trim_whitespace();
    if (trimmed_input.is_empty())
        return;

    set_next_history_visit_transition(WebView::HistoryVisitTransition::Omnibox);

    if (Ladybird::XenonSettings::bangs_enabled()) {
        if (auto bang_navigation = Ladybird::Bangs::resolve(trimmed_input); bang_navigation.has_value()) {
            load(bang_navigation->url);
            return;
        }
    }

    load_from_user_input(trimmed_input);
}

void View::go_back()
{
    if (navigate_back_action().enabled())
        traverse_the_history_by_delta(-1);
}

void View::go_forward()
{
    if (navigate_forward_action().enabled())
        traverse_the_history_by_delta(1);
}

void View::set_viewport(Web::DevicePixelSize viewport_size, double device_pixel_ratio)
{
    if (viewport_size == m_viewport_size && device_pixel_ratio == m_device_pixel_ratio)
        return;

    m_viewport_size = viewport_size;
    m_device_pixel_ratio = device_pixel_ratio;
    set_window_size(viewport_size.to_type<int>());
    handle_resize();
}

void View::set_visible(bool visible)
{
    set_system_visibility_state(visible ? Web::HTML::VisibilityState::Visible : Web::HTML::VisibilityState::Hidden);
}

void View::set_focused(bool focused)
{
    set_has_system_focus(focused);
}

int View::click_count_for_press(XenonMouseEvent const& event)
{
    auto now = MonotonicTime::now();
    Gfx::FloatPoint position { event.x, event.y };
    auto distance = multiple_click_distance * m_device_pixel_ratio;
    auto continues_clicking = event.button == m_last_press_button
        && now - m_last_press_time < multiple_click_interval
        && AK::fabs(position.x() - m_last_press_position.x()) <= distance
        && AK::fabs(position.y() - m_last_press_position.y()) <= distance;

    auto click_count = continues_clicking ? m_click_count + 1 : 1;
    if (event.type == XENON_MOUSE_EVENT_DOUBLE_CLICK)
        click_count = max(click_count, 2);

    m_last_press_time = now;
    m_last_press_position = position;
    m_last_press_button = event.button;
    return click_count;
}

void View::handle_mouse_event(XenonMouseEvent const& event)
{
    Web::MouseEvent::Type type;
    switch (event.type) {
    case XENON_MOUSE_EVENT_DOWN:
    case XENON_MOUSE_EVENT_DOUBLE_CLICK:
        type = Web::MouseEvent::Type::MouseDown;
        break;
    case XENON_MOUSE_EVENT_UP:
        type = Web::MouseEvent::Type::MouseUp;
        break;
    case XENON_MOUSE_EVENT_MOVE:
        type = Web::MouseEvent::Type::MouseMove;
        break;
    case XENON_MOUSE_EVENT_WHEEL:
        type = Web::MouseEvent::Type::MouseWheel;
        break;
    case XENON_MOUSE_EVENT_LEAVE:
        type = Web::MouseEvent::Type::MouseLeave;
        break;
    default:
        return;
    }

    auto button = mouse_buttons_from(event.button);
    auto is_press_or_release = type == Web::MouseEvent::Type::MouseDown || type == Web::MouseEvent::Type::MouseUp;
    if (is_press_or_release && button == Web::UIEvents::MouseButton::None)
        return;

    if (type == Web::MouseEvent::Type::MouseDown)
        m_click_count = click_count_for_press(event);
    if (type != Web::MouseEvent::Type::MouseLeave)
        m_last_mouse_position = { event.x, event.y };

    Web::DevicePixelPoint position { static_cast<int>(AK::round(event.x)), static_cast<int>(AK::round(event.y)) };

    double wheel_delta_x = 0;
    double wheel_delta_y = 0;
    if (type == Web::MouseEvent::Type::MouseWheel) {
        wheel_delta_x = event.wheel_delta_x;
        wheel_delta_y = event.wheel_delta_y;
    }

    enqueue_input_event(Web::MouseEvent {
        type,
        position,
        position,
        button,
        mouse_buttons_from(event.buttons),
        key_modifiers_from(event.modifiers),
        wheel_delta_x,
        wheel_delta_y,
        Web::wheel_tick_120ths_from_pixel_delta(wheel_delta_x, Web::WheelDeltaPrecision::Precise),
        Web::wheel_tick_120ths_from_pixel_delta(wheel_delta_y, Web::WheelDeltaPrecision::Precise),
        Web::WheelDeltaPrecision::Precise,
        Web::ScrollGesturePhase::None,
        m_click_count,
        nullptr,
    });
}

void View::handle_key_event(Web::KeyEvent event)
{
    enqueue_input_event(move(event));
}

void View::insert_text(StringView text)
{
    auto utf8_text = String::from_utf8(text);
    if (utf8_text.is_error() || utf8_text.value().is_empty())
        return;
    commit_text_from_input_method(Utf16String::from_utf8(utf8_text.value()));
}

void View::apply_system_theme()
{
    set_page_background_color_to_system_canvas(Engine::the().system_dark());
    update_system_theme(Engine::the().theme());
}

Core::AnonymousBuffer View::system_theme()
{
    return Engine::the().theme();
}

WebView::ViewImplementation* View::create_view_for_new_tab_or_window(WebView::IsPrivate is_private)
{
    return &Engine::the().open_view_for_new_tab_or_window(m_id, is_private);
}

static constexpr auto before_unload_message = "Leave this page? Your changes may not be saved."sv;

static ByteString byte_string_from(Utf16String const& string)
{
    return string.to_utf8().to_byte_string();
}

static bool begins_group(Vector<XenonMenuItem> const& items)
{
    return items.is_empty() || items.last().kind == XENON_MENU_ITEM_SEPARATOR || items.last().kind == XENON_MENU_ITEM_SUBMENU_BEGIN;
}

void drop_trailing_separator(Vector<XenonMenuItem>& items)
{
    if (!items.is_empty() && items.last().kind == XENON_MENU_ITEM_SEPARATOR)
        items.take_last();
}

static XenonMenuItem menu_item(XenonMenuItemKind kind, StringView title = {}, u32 id = 0)
{
    return { .kind = static_cast<u32>(kind), .id = id, .title = xenon_str(title), .enabled = true, .checkable = false, .checked = false, .indented = false };
}

void flatten_menu(WebView::Menu& menu, Vector<XenonMenuItem>& items, Vector<WeakPtr<WebView::Action>>& actions)
{
    for (auto& item : menu.items()) {
        item.visit(
            [&](NonnullRefPtr<WebView::Action>& action) {
                if (!action->visible())
                    return;
                auto entry = menu_item(XENON_MENU_ITEM_ACTION, action->text(), static_cast<u32>(actions.size()));
                entry.enabled = action->enabled();
                entry.checkable = action->is_checkable();
                entry.checked = action->is_checkable() && action->checked();
                items.append(entry);
                actions.append(action->make_weak_ptr());
            },
            [&](NonnullRefPtr<WebView::Menu>& submenu) {
                if (!submenu->visible())
                    return;
                auto begin = items.size();
                items.append(menu_item(XENON_MENU_ITEM_SUBMENU_BEGIN, submenu->title()));
                flatten_menu(*submenu, items, actions);
                drop_trailing_separator(items);
                if (items.size() == begin + 1) {
                    items.shrink(begin);
                    return;
                }
                items.append(menu_item(XENON_MENU_ITEM_SUBMENU_END));
            },
            [&](WebView::Separator) {
                if (!begins_group(items))
                    items.append(menu_item(XENON_MENU_ITEM_SEPARATOR));
            });
    }
}

static Vector<StringView> extensions_for_file_type(Web::HTML::FileFilter::FileType type)
{
    switch (type) {
    case Web::HTML::FileFilter::FileType::Audio:
        return { "aac"sv, "flac"sv, "m4a"sv, "mp3"sv, "oga"sv, "ogg"sv, "opus"sv, "wav"sv, "weba"sv };
    case Web::HTML::FileFilter::FileType::Image:
        return { "avif"sv, "bmp"sv, "gif"sv, "ico"sv, "jpeg"sv, "jpg"sv, "png"sv, "svg"sv, "tif"sv, "tiff"sv, "webp"sv };
    case Web::HTML::FileFilter::FileType::Video:
        return { "avi"sv, "m4v"sv, "mkv"sv, "mov"sv, "mp4"sv, "mpeg"sv, "ogv"sv, "webm"sv };
    }
    VERIFY_NOT_REACHED();
}

static StringView file_type_title(Web::HTML::FileFilter::FileType type)
{
    switch (type) {
    case Web::HTML::FileFilter::FileType::Audio:
        return "Audio files"sv;
    case Web::HTML::FileFilter::FileType::Image:
        return "Image files"sv;
    case Web::HTML::FileFilter::FileType::Video:
        return "Video files"sv;
    }
    VERIFY_NOT_REACHED();
}

struct FileFilterText {
    ByteString name;
    ByteString extensions;
};

static Optional<FileFilterText> file_filter_text(Web::HTML::FileFilter::FilterType const& filter)
{
    return filter.visit(
        [](Web::HTML::FileFilter::FileType type) -> Optional<FileFilterText> {
            return FileFilterText { file_type_title(type), ByteString::join(' ', extensions_for_file_type(type)) };
        },
        [](Web::HTML::FileFilter::MimeType const& mime_type) -> Optional<FileFilterText> {
            auto value = byte_string_from(mime_type.value);
            auto parts = value.split_view('/');
            if (parts.size() != 2)
                return {};
            if (parts[1] == "*"sv) {
                if (parts[0] == "audio"sv)
                    return FileFilterText { value, ByteString::join(' ', extensions_for_file_type(Web::HTML::FileFilter::FileType::Audio)) };
                if (parts[0] == "image"sv)
                    return FileFilterText { value, ByteString::join(' ', extensions_for_file_type(Web::HTML::FileFilter::FileType::Image)) };
                if (parts[0] == "video"sv)
                    return FileFilterText { value, ByteString::join(' ', extensions_for_file_type(Web::HTML::FileFilter::FileType::Video)) };
                return {};
            }
            auto subtype = parts[1];
            if (auto plus = subtype.find('+'); plus.has_value())
                subtype = subtype.substring_view(0, *plus);
            if (subtype == "jpeg"sv)
                return FileFilterText { value, "jpeg jpg"sv };
            if (subtype == "plain"sv)
                return FileFilterText { value, "txt"sv };
            return FileFilterText { value, subtype };
        },
        [](Web::HTML::FileFilter::Extension const& extension) -> Optional<FileFilterText> {
            auto value = byte_string_from(extension.value);
            auto trimmed = value.view().trim("."sv, TrimMode::Left);
            if (trimmed.is_empty())
                return {};
            return FileFilterText { ByteString::formatted("*.{}", trimmed), trimmed };
        });
}

static Web::Permissions::PromptOutcome prompt_outcome_from(u32 outcome)
{
    switch (outcome) {
    case XENON_PERMISSION_ALLOW_ALWAYS:
        return Web::Permissions::PromptOutcome::AllowAlways;
    case XENON_PERMISSION_ALLOW_ONCE:
        return Web::Permissions::PromptOutcome::AllowOnce;
    case XENON_PERMISSION_BLOCK:
        return Web::Permissions::PromptOutcome::Block;
    default:
        return Web::Permissions::PromptOutcome::Dismissed;
    }
}

void View::install_page_callbacks()
{
    on_request_alert = [this](Utf16String const& message) {
        if (m_suppress_dialogs_until_navigation) {
            alert_closed();
            return;
        }
        open_page_dialog(PageDialog::Alert, javascript_dialog_title(), byte_string_from(message));
    };

    on_request_confirm = [this](Utf16String const& message) {
        if (m_suppress_dialogs_until_navigation) {
            confirm_closed(false);
            return;
        }
        open_page_dialog(PageDialog::Confirm, javascript_dialog_title(), byte_string_from(message));
    };

    on_request_prompt = [this](Utf16String const& message, Utf16String const& default_text) {
        if (m_suppress_dialogs_until_navigation) {
            prompt_closed({});
            return;
        }
        m_prompt_text = default_text;
        open_page_dialog(PageDialog::Prompt, javascript_dialog_title(), byte_string_from(message), byte_string_from(default_text));
    };

    on_request_set_prompt_text = [this](Utf16String const& text) {
        if (m_page_dialog != PageDialog::Prompt)
            return;
        m_prompt_text = text;
        auto utf8_text = byte_string_from(text);
        notify_page(&XenonPageCallbacks::dialog_text_changed, m_id, m_page_dialog_id, xenon_str(utf8_text));
    };

    on_request_before_unload = [this](String const& source, Function<void(bool)> on_complete) {
        open_page_dialog(PageDialog::BeforeUnload, source, before_unload_message, {}, move(on_complete));
    };

    on_request_accept_dialog = [this] {
        if (m_page_dialog == PageDialog::None)
            return;
        auto id = m_page_dialog_id;
        complete_page_dialog(true, m_prompt_text);
        notify_page(&XenonPageCallbacks::dialog_closed, m_id, id);
    };

    on_request_dismiss_dialog = [this] {
        dismiss_page_dialog();
    };

    on_before_browser_initiated_navigation = [this] {
        m_suppress_dialogs_until_navigation = true;
        dismiss_page_dialog();
    };

    add_navigation_listener({
        .on_load_start = nullptr,
        .on_load_finish = [this](URL::URL const&) { m_suppress_dialogs_until_navigation = false; },
    });

    on_request_external_url_confirmation = [this](URL::URL const& url, URL::Origin const& initiator_origin, WebView::ExternalURLHandler const& handler, Function<void(bool)> on_complete) {
        if (m_page_dialog != PageDialog::None) {
            on_complete(false);
            return;
        }
        auto initiator = initiator_origin.is_opaque() ? "This page"_string : initiator_origin.serialize();
        auto has_application_name = !handler.application_name().is_empty();
        auto application_name = has_application_name ? handler.application_name().bytes_as_string_view() : "another application"sv;
        auto title = has_application_name ? ByteString::formatted("Open {}?", application_name) : ByteString { "Open external application?"sv };
        auto message = ByteString::formatted("{} wants to open a {} link with {}.\n\n{}", initiator, url.scheme(), application_name, url.serialize());
        open_page_dialog(PageDialog::ExternalURL, title, message, {}, move(on_complete));
    };

    on_request_select_dropdown = [this](Gfx::IntPoint content_position, i32 minimum_width, Vector<Web::HTML::SelectItem> select_items) {
        Vector<ByteString> labels;
        size_t label_count = 0;
        for (auto const& item : select_items) {
            if (item.has<Web::HTML::SelectItemOption>())
                ++label_count;
            else if (item.has<Web::HTML::SelectItemOptionGroup>())
                label_count += 1 + item.get<Web::HTML::SelectItemOptionGroup>().items.size();
        }
        labels.ensure_capacity(label_count);

        Vector<XenonMenuItem> items;
        auto append_option = [&](Web::HTML::SelectItemOption const& option, bool indented) {
            labels.unchecked_append(byte_string_from(option.label));
            auto entry = menu_item(XENON_MENU_ITEM_ACTION, labels.last(), option.id);
            entry.enabled = !option.disabled;
            entry.checkable = true;
            entry.checked = option.selected;
            entry.indented = indented;
            items.append(entry);
        };
        for (auto const& item : select_items) {
            item.visit(
                [&](Web::HTML::SelectItemOption const& option) { append_option(option, false); },
                [&](Web::HTML::SelectItemOptionGroup const& group) {
                    labels.unchecked_append(byte_string_from(group.label));
                    items.append(menu_item(XENON_MENU_ITEM_HEADING, labels.last()));
                    for (auto const& option : group.items)
                        append_option(option, true);
                },
                [&](Web::HTML::SelectItemSeparator) {
                    if (!begins_group(items))
                        items.append(menu_item(XENON_MENU_ITEM_SEPARATOR));
                });
        }
        drop_trailing_separator(items);

        m_select_dropdown_id = next_prompt_id();
        auto position = to_widget_position(content_position);
        XenonMenu menu {
            .id = m_select_dropdown_id,
            .x = static_cast<float>(position.x()),
            .y = static_cast<float>(position.y()),
            .minimum_width = static_cast<float>(minimum_width),
            .items = items.data(),
            .item_count = items.size(),
        };
        notify_page(&XenonPageCallbacks::select_dropdown_requested, m_id, static_cast<XenonMenu const*>(&menu));
    };

    on_request_file_picker = [this](Web::HTML::FileFilter const& accepted_file_types, Web::HTML::AllowMultipleFiles allow_multiple_files) {
        Vector<FileFilterText> filter_texts;
        for (auto const& filter : accepted_file_types.filters) {
            if (auto text = file_filter_text(filter); text.has_value())
                filter_texts.append(text.release_value());
        }
        filter_texts.append({ "All files"sv, "*"sv });

        Vector<XenonFileFilter> filters;
        for (auto const& text : filter_texts)
            filters.append({ .name = xenon_str(text.name), .extensions = xenon_str(text.extensions) });

        m_file_picker_id = next_prompt_id();
        XenonFilePicker picker {
            .id = m_file_picker_id,
            .allow_multiple = allow_multiple_files == Web::HTML::AllowMultipleFiles::Yes,
            .filters = filters.data(),
            .filter_count = filters.size(),
        };
        notify_page(&XenonPageCallbacks::file_picker_requested, m_id, static_cast<XenonFilePicker const*>(&picker));
    };

    on_request_color_picker = [this](Color) {
        Core::deferred_invoke([weak_this = make_weak_ptr<View>()] {
            if (weak_this)
                weak_this->color_picker_update({}, Web::HTML::ColorPickerUpdateState::Closed);
        });
    };

    on_request_date_picker = [this](Utf16String const&, Utf16String const&, Utf16String const&, Utf16String const&) {
        Core::deferred_invoke([weak_this = make_weak_ptr<View>()] {
            if (weak_this)
                weak_this->date_picker_closed({});
        });
    };

    on_find_in_page = [this](size_t current_match_index, Optional<size_t> const& total_match_count) {
        notify_page(&XenonPageCallbacks::find_result, m_id, current_match_index, total_match_count.has_value(), total_match_count.value_or(0));
    };

    on_enter_tooltip_area = [this](ByteString const& tooltip) {
        show_tooltip(tooltip, m_last_mouse_position);
    };

    on_leave_tooltip_area = [this] {
        show_tooltip({}, {});
    };

    on_request_tooltip_override = [this](Gfx::IntPoint position, ByteString const& tooltip) {
        show_tooltip(tooltip, position.to_type<float>());
    };

    on_stop_tooltip_override = [this] {
        show_tooltip({}, {});
    };

    on_fullscreen_window = [this] {
        m_fullscreen_requested = true;
        notify_page(&XenonPageCallbacks::fullscreen_requested, m_id, true);
    };

    on_exit_fullscreen_window = [this] {
        m_fullscreen_requested = false;
        notify_page(&XenonPageCallbacks::fullscreen_requested, m_id, false);
    };

    on_request_permission = [this](WebView::PermissionPromptRequest const& request, WebView::PermissionPromptCallback on_outcome) {
        auto requester = request.top_level_origin.is_opaque() ? "this page"_string : request.top_level_origin.serialize();
        if (request.is_file)
            requester = "this file"_string;
        auto id = next_prompt_id();
        XenonPermissionRequest prompt {
            .id = id,
            .kind = static_cast<u32>(request.kind == WebView::PermissionPromptKind::SystemDeniedNotice ? XENON_PERMISSION_PROMPT_SYSTEM_DENIED_NOTICE : XENON_PERMISSION_PROMPT_QUESTION),
            .permission = xenon_str(Web::Permissions::to_string(request.name)),
            .requester = xenon_str(requester),
        };
        m_pending_permissions.append({ id, request, move(on_outcome) });
        notify_page(&XenonPageCallbacks::permission_requested, m_id, static_cast<XenonPermissionRequest const*>(&prompt));
    };

    on_cancel_permission_prompt = [this](WebView::PermissionPromptRequest const& request) {
        auto index = m_pending_permissions.find_first_index_if([&](auto const& pending) { return pending.request.is_same_request_as(request); });
        if (!index.has_value())
            return;
        auto pending = m_pending_permissions.take(*index);
        notify_page(&XenonPageCallbacks::permission_cancelled, m_id, pending.id);
    };
}

String View::javascript_dialog_title() const
{
    auto origin = url().origin();
    if (!origin.is_opaque())
        return origin.serialize();
    return MUST(String::formatted("{}://", url().scheme()));
}

void View::open_page_dialog(PageDialog kind, StringView title, StringView message, StringView default_text, Function<void(bool)> completion)
{
    if (m_page_dialog != PageDialog::None)
        dismiss_page_dialog();

    show_tooltip({}, {});
    m_page_dialog = kind;
    m_page_dialog_id = next_prompt_id();
    m_page_dialog_completion = move(completion);

    auto dialog_kind = XENON_DIALOG_ALERT;
    auto accept_label = "OK"sv;
    auto cancel_label = "Cancel"sv;
    switch (kind) {
    case PageDialog::Alert:
        cancel_label = {};
        break;
    case PageDialog::Confirm:
        dialog_kind = XENON_DIALOG_CONFIRM;
        break;
    case PageDialog::Prompt:
        dialog_kind = XENON_DIALOG_PROMPT;
        break;
    case PageDialog::BeforeUnload:
        dialog_kind = XENON_DIALOG_BEFORE_UNLOAD;
        accept_label = "Leave"sv;
        cancel_label = "Stay"sv;
        break;
    case PageDialog::ExternalURL:
        dialog_kind = XENON_DIALOG_EXTERNAL_URL;
        accept_label = "Open"sv;
        break;
    case PageDialog::None:
        VERIFY_NOT_REACHED();
    }

    XenonDialog dialog {
        .id = m_page_dialog_id,
        .kind = static_cast<u32>(dialog_kind),
        .title = xenon_str(title),
        .message = xenon_str(message),
        .default_text = xenon_str(default_text),
        .accept_label = xenon_str(accept_label),
        .cancel_label = xenon_str(cancel_label),
    };
    notify_page(&XenonPageCallbacks::dialog_opened, m_id, static_cast<XenonDialog const*>(&dialog));
}

void View::complete_page_dialog(bool accepted, Optional<Utf16String> const& text)
{
    auto kind = exchange(m_page_dialog, PageDialog::None);
    m_page_dialog_id = 0;
    auto completion = move(m_page_dialog_completion);

    switch (kind) {
    case PageDialog::None:
        break;
    case PageDialog::Alert:
        alert_closed();
        break;
    case PageDialog::Confirm:
        confirm_closed(accepted);
        break;
    case PageDialog::Prompt:
        prompt_closed(accepted ? text : Optional<Utf16String> {});
        break;
    case PageDialog::BeforeUnload:
        if (completion)
            completion(accepted);
        if (!accepted) {
            m_close_requested = false;
            m_suppress_dialogs_until_navigation = false;
        }
        break;
    case PageDialog::ExternalURL:
        if (completion)
            completion(accepted);
        break;
    }
}

void View::dismiss_page_dialog()
{
    if (m_page_dialog == PageDialog::None)
        return;
    auto id = m_page_dialog_id;
    complete_page_dialog(m_page_dialog == PageDialog::Alert, {});
    notify_page(&XenonPageCallbacks::dialog_closed, m_id, id);
}

void View::close_page_dialog(XenonPromptId id, bool accepted, StringView text)
{
    if (id == 0 || id != m_page_dialog_id)
        return;
    auto utf8_text = String::from_utf8(text);
    complete_page_dialog(accepted, Utf16String::from_utf8(utf8_text.is_error() ? String {} : utf8_text.release_value()));
}

void View::reset_page_prompts()
{
    m_suppress_dialogs_until_navigation = false;
    m_close_requested = false;
    if (m_page_dialog != PageDialog::None) {
        auto id = m_page_dialog_id;
        m_page_dialog = PageDialog::None;
        m_page_dialog_id = 0;
        m_page_dialog_completion = nullptr;
        notify_page(&XenonPageCallbacks::dialog_closed, m_id, id);
    }

    m_context_menu_id = 0;
    m_shown_context_menu = nullptr;
    m_context_menu_actions.clear();
    m_select_dropdown_id = 0;
    m_file_picker_id = 0;

    auto permissions = move(m_pending_permissions);
    for (auto& permission : permissions) {
        notify_page(&XenonPageCallbacks::permission_cancelled, m_id, permission.id);
        permission.on_outcome(Web::Permissions::PromptOutcome::Ignored);
    }

    show_tooltip({}, {});
    if (exchange(m_fullscreen_requested, false))
        notify_page(&XenonPageCallbacks::fullscreen_requested, m_id, false);
}

void View::show_tooltip(StringView text, Gfx::FloatPoint position)
{
    auto normalized = text.contains('\r') ? text.replace("\r\n"sv, "\n"sv, ReplaceMode::All).replace("\r"sv, "\n"sv, ReplaceMode::All) : ByteString { text };
    notify_page(&XenonPageCallbacks::tooltip_changed, m_id, xenon_str(normalized), position.x(), position.y());
}

void View::show_context_menu(WebView::Menu& menu, Gfx::IntPoint widget_position)
{
    m_shown_context_menu = menu;
    m_context_menu_actions.clear();
    Vector<XenonMenuItem> items;
    flatten_menu(menu, items, m_context_menu_actions);
    drop_trailing_separator(items);
    if (items.is_empty())
        return;

    show_tooltip({}, {});
    m_context_menu_id = next_prompt_id();
    XenonMenu shown_menu {
        .id = m_context_menu_id,
        .x = static_cast<float>(widget_position.x()),
        .y = static_cast<float>(widget_position.y()),
        .minimum_width = 0,
        .items = items.data(),
        .item_count = items.size(),
    };
    notify_page(&XenonPageCallbacks::context_menu_requested, m_id, static_cast<XenonMenu const*>(&shown_menu));
}

void View::activate_context_menu_item(XenonPromptId id, u32 item)
{
    if (id == 0 || id != m_context_menu_id || item >= m_context_menu_actions.size())
        return;
    auto action = m_context_menu_actions[item].strong_ref();
    if (!action || !action->enabled())
        return;
    if (action->is_checkable())
        action->set_checked(!action->checked());
    action->activate();
}

void View::close_select_dropdown(XenonPromptId id, Optional<u32> item)
{
    if (id == 0 || id != m_select_dropdown_id)
        return;
    m_select_dropdown_id = 0;
    select_dropdown_closed(item);
}

void View::close_file_picker(XenonPromptId id, Vector<ByteString> const& paths)
{
    if (id == 0 || id != m_file_picker_id)
        return;
    m_file_picker_id = 0;

    Vector<Web::HTML::SelectedFile> selected_files;
    for (auto const& path : paths) {
        if (auto file = WebView::create_selected_file(path); file.is_error())
            warnln("Xenon: Unable to open file {}: {}", path, file.error());
        else
            selected_files.append(file.release_value());
    }
    file_picker_closed(move(selected_files));
}

void View::find(StringView query, bool case_sensitive)
{
    auto utf8_query = String::from_utf8(query);
    if (utf8_query.is_error())
        return;
    find_in_page(Utf16String::from_utf8(utf8_query.value()), case_sensitive ? CaseSensitivity::CaseSensitive : CaseSensitivity::CaseInsensitive);
}

void View::set_fullscreen(bool fullscreen)
{
    if (!fullscreen && m_fullscreen_requested) {
        m_fullscreen_requested = false;
        exit_fullscreen();
    }
    set_is_fullscreen(fullscreen ? Web::ViewportIsFullscreen::Yes : Web::ViewportIsFullscreen::No);
}

void View::answer_permission(XenonPromptId id, u32 outcome)
{
    auto index = m_pending_permissions.find_first_index_if([&](auto const& pending) { return pending.id == id; });
    if (!index.has_value())
        return;
    auto pending = m_pending_permissions.take(*index);
    pending.on_outcome(prompt_outcome_from(outcome));
}

void View::request_close_with_prompt()
{
    if (handle().is_empty() || !page().is_open() || !needs_beforeunload_check() || m_close_requested) {
        Engine::the().close_tab(m_id);
        return;
    }

    m_close_requested = true;
    m_suppress_dialogs_until_navigation = true;
    dismiss_page_dialog();
    request_close();
}

}
