/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Math.h>
#include <AK/Utf16String.h>
#include <LibCore/EventLoop.h>
#include <LibGfx/Bitmap.h>
#include <LibURL/URL.h>
#include <LibWebView/HistoryVisitTransition.h>
#include <LibWebView/Menu.h>
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

NonnullOwnPtr<View> View::create(XenonViewId id, Web::DevicePixelSize viewport_size, double device_pixel_ratio, Web::HTML::VisibilityState visibility_state, Optional<WebView::CanonicalTraversable&> traversable)
{
    auto view = adopt_own(*new View(id, viewport_size, device_pixel_ratio));
    view->initialize_tab(visibility_state, traversable);
    view->report_history_state();
    return view;
}

View::View(XenonViewId id, Web::DevicePixelSize viewport_size, double device_pixel_ratio)
    : m_id(id)
    , m_viewport_size(viewport_size)
{
    m_device_pixel_ratio = device_pixel_ratio;
    m_window_size = viewport_size.to_type<int>();
    m_presents_from_cpu_memory = true;
    set_page_background_color_to_system_canvas(false);
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
        engine.notify(&XenonUiCallbacks::view_close_requested, m_id);
    };

    on_web_content_crashed = [this, &engine](WebContentCrashReason) {
        engine.notify(&XenonUiCallbacks::view_crashed, m_id);
    };

    on_browser_history_traversal_complete = [this] {
        report_history_state();
    };

    on_request_alert = [this](Utf16String const&) {
        Core::deferred_invoke([weak_this = make_weak_ptr<View>()] {
            if (weak_this)
                weak_this->alert_closed();
        });
    };

    on_request_confirm = [this](Utf16String const&) {
        Core::deferred_invoke([weak_this = make_weak_ptr<View>()] {
            if (weak_this)
                weak_this->confirm_closed(false);
        });
    };

    on_request_prompt = [this](Utf16String const&, Utf16String const&) {
        Core::deferred_invoke([weak_this = make_weak_ptr<View>()] {
            if (weak_this)
                weak_this->prompt_closed({});
        });
    };

    on_request_before_unload = [](String const&, Function<void(bool)> on_complete) {
        on_complete(true);
    };

    on_request_select_dropdown = [this](Gfx::IntPoint, i32, Vector<Web::HTML::SelectItem>) {
        Core::deferred_invoke([weak_this = make_weak_ptr<View>()] {
            if (weak_this)
                weak_this->select_dropdown_closed({});
        });
    };

    on_request_file_picker = [this](Web::HTML::FileFilter const&, Web::HTML::AllowMultipleFiles) {
        Core::deferred_invoke([weak_this = make_weak_ptr<View>()] {
            if (weak_this)
                weak_this->file_picker_closed({});
        });
    };

    on_request_color_picker = [this](Color) {
        Core::deferred_invoke([weak_this = make_weak_ptr<View>()] {
            if (weak_this)
                weak_this->color_picker_update({}, Web::HTML::ColorPickerUpdateState::Closed);
        });
    };

    auto observe_history_action = [this](WebView::Action& action) {
        action.add_observer(make<HistoryActionObserver>([weak_this = make_weak_ptr<View>()] {
            if (weak_this)
                weak_this->report_history_state();
        }));
    };
    observe_history_action(navigate_back_action());
    observe_history_action(navigate_forward_action());
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

    XenonFrame frame {
        .width = static_cast<u32>(width),
        .height = static_cast<u32>(height),
        .stride = bitmap->pitch(),
        .bgra = bitmap->scanline_u8(0),
        .device_pixel_ratio = device_pixel_ratio,
    };
    Engine::the().notify(&XenonUiCallbacks::view_frame, m_id, static_cast<XenonFrame const*>(&frame));
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

Core::AnonymousBuffer View::system_theme()
{
    return Engine::the().theme();
}

WebView::ViewImplementation* View::create_view_for_new_tab_or_window(WebView::IsPrivate)
{
    return &Engine::the().open_view(m_id, Web::HTML::ActivateTab::Yes);
}

}
