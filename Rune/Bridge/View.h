/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/NonnullOwnPtr.h>
#include <AK/Optional.h>
#include <AK/Time.h>
#include <AK/Utf16String.h>
#include <AK/WeakPtr.h>
#include <LibWebCommon/HTML/VisibilityState.h>
#include <LibWebCommon/PixelUnits.h>
#include <LibWebView/Menu.h>
#include <LibWebView/ViewImplementation.h>
#include <Rune/Bridge/XenonBridge.h>
#include <Rune/Bridge/XenonBridgePage.h>

namespace Xenon {

void flatten_menu(WebView::Menu&, Vector<XenonMenuItem>&, Vector<WeakPtr<WebView::Action>>&);
void drop_trailing_separator(Vector<XenonMenuItem>&);

void set_page_callbacks(XenonPageCallbacks const&);
XenonPageCallbacks const& page_callbacks();

template<typename Callback, typename... Arguments>
void notify_page(Callback XenonPageCallbacks::* callback, Arguments... arguments)
{
    auto const& callbacks = page_callbacks();
    if (auto function = callbacks.*callback)
        function(callbacks.context, arguments...);
}

class View final : public WebView::ViewImplementation {
public:
    AK_ALLOC_WITH_KMALLOC;

    static NonnullOwnPtr<View> create(XenonViewId, Web::DevicePixelSize, double device_pixel_ratio, WebView::IsPrivate, Web::HTML::VisibilityState, Optional<WebView::CanonicalTraversable&> = {}, WebView::EnableContentBlocker = WebView::EnableContentBlocker::Yes);

    virtual ~View() override;

    XenonViewId id() const { return m_id; }

    void load_user_input(StringView);
    void go_back();
    void go_forward();
    void set_viewport(Web::DevicePixelSize, double device_pixel_ratio);
    void set_visible(bool);
    void apply_system_theme();
    void toggle_mute();
    void run_blocking_action(u32 action);
    void report_content_blocking(WebView::ContentBlockingSnapshot const&);
    void show_history_menu(int direction);
    void set_focused(bool);
    void handle_mouse_event(XenonMouseEvent const&);
    void handle_key_event(Web::KeyEvent);
    void insert_text(StringView);

    void close_page_dialog(XenonPromptId, bool accepted, StringView text);
    void activate_context_menu_item(XenonPromptId, u32 item);
    void close_select_dropdown(XenonPromptId, Optional<u32> item);
    void close_file_picker(XenonPromptId, Vector<ByteString> const& paths);
    void find(StringView query, bool case_sensitive);
    void set_fullscreen(bool);
    void answer_permission(XenonPromptId, u32 outcome);
    void request_close_with_prompt();

    struct Favicon {
        u32 width { 0 };
        u32 height { 0 };
        Vector<u8> rgba;
    };
    Optional<Favicon> const& favicon() const { return m_favicon; }
    bool can_hibernate() const;

    virtual Web::DevicePixelSize viewport_size() const override { return m_viewport_size; }
    virtual Gfx::IntPoint to_content_position(Gfx::IntPoint widget_position) const override { return widget_position; }
    virtual Gfx::IntPoint to_widget_position(Gfx::IntPoint content_position) const override { return content_position; }

private:
    View(XenonViewId, Web::DevicePixelSize, double device_pixel_ratio, WebView::IsPrivate, WebView::EnableContentBlocker);

    enum class PageDialog : u8 {
        None,
        Alert,
        Confirm,
        Prompt,
        BeforeUnload,
        ExternalURL,
    };

    struct PendingPermission {
        XenonPromptId id { 0 };
        WebView::PermissionPromptRequest request;
        WebView::PermissionPromptCallback on_outcome;
    };

    void install_callbacks();
    void install_page_callbacks();
    XenonPromptId next_prompt_id() { return m_next_prompt_id++; }
    void open_page_dialog(PageDialog, StringView title, StringView message, StringView default_text = {}, Function<void(bool)> completion = {});
    void complete_page_dialog(bool accepted, Optional<Utf16String> const& text);
    void dismiss_page_dialog();
    void reset_page_prompts();
    String javascript_dialog_title() const;
    void show_tooltip(StringView text, Gfx::FloatPoint);
    void present_current_frame();
    void report_history_state();
    void report_zoom();
    void report_audio();
    void report_bookmarked();
    int click_count_for_press(XenonMouseEvent const&);

    virtual Core::AnonymousBuffer system_theme() override;
    virtual WebView::ViewImplementation* create_view_for_new_tab_or_window(WebView::IsPrivate) override;
    virtual void show_context_menu(WebView::Menu&, Gfx::IntPoint widget_position) override;

    XenonViewId m_id { 0 };
    Web::DevicePixelSize m_viewport_size;

    Optional<bool> m_reported_can_go_back;
    Optional<bool> m_reported_can_go_forward;
    u32 m_reported_zoom_percent { 100 };
    bool m_reported_bookmarked { false };

    int m_click_count { 0 };
    MonotonicTime m_last_press_time { MonotonicTime::now_coarse() };
    Gfx::FloatPoint m_last_press_position;
    u32 m_last_press_button { XENON_MOUSE_BUTTON_NONE };
    Gfx::FloatPoint m_last_mouse_position;

    XenonPromptId m_next_prompt_id { 1 };

    PageDialog m_page_dialog { PageDialog::None };
    XenonPromptId m_page_dialog_id { 0 };
    Utf16String m_prompt_text;
    Function<void(bool)> m_page_dialog_completion;
    bool m_suppress_dialogs_until_navigation { false };
    bool m_close_requested { false };

    XenonPromptId m_context_menu_id { 0 };
    RefPtr<WebView::Menu> m_shown_context_menu;
    Vector<WeakPtr<WebView::Action>> m_context_menu_actions;

    XenonPromptId m_select_dropdown_id { 0 };
    XenonPromptId m_file_picker_id { 0 };
    Vector<PendingPermission> m_pending_permissions;
    bool m_fullscreen_requested { false };
    Optional<Favicon> m_favicon;
};

}
