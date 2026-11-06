/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/NonnullOwnPtr.h>
#include <AK/Optional.h>
#include <AK/Time.h>
#include <LibWebCommon/HTML/VisibilityState.h>
#include <LibWebCommon/PixelUnits.h>
#include <LibWebView/ViewImplementation.h>
#include <Rune/Bridge/XenonBridge.h>

namespace Xenon {

class View final : public WebView::ViewImplementation {
public:
    AK_ALLOC_WITH_KMALLOC;

    static NonnullOwnPtr<View> create(XenonViewId, Web::DevicePixelSize, double device_pixel_ratio, Web::HTML::VisibilityState, Optional<WebView::CanonicalTraversable&> = {});

    virtual ~View() override;

    XenonViewId id() const { return m_id; }

    void load_user_input(StringView);
    void go_back();
    void go_forward();
    void set_viewport(Web::DevicePixelSize, double device_pixel_ratio);
    void set_visible(bool);
    void set_focused(bool);
    void handle_mouse_event(XenonMouseEvent const&);
    void handle_key_event(Web::KeyEvent);
    void insert_text(StringView);

    virtual Web::DevicePixelSize viewport_size() const override { return m_viewport_size; }
    virtual Gfx::IntPoint to_content_position(Gfx::IntPoint widget_position) const override { return widget_position; }
    virtual Gfx::IntPoint to_widget_position(Gfx::IntPoint content_position) const override { return content_position; }

private:
    View(XenonViewId, Web::DevicePixelSize, double device_pixel_ratio);

    void install_callbacks();
    void present_current_frame();
    void report_history_state();
    int click_count_for_press(XenonMouseEvent const&);

    virtual Core::AnonymousBuffer system_theme() override;
    virtual WebView::ViewImplementation* create_view_for_new_tab_or_window(WebView::IsPrivate) override;

    XenonViewId m_id { 0 };
    Web::DevicePixelSize m_viewport_size;

    Optional<bool> m_reported_can_go_back;
    Optional<bool> m_reported_can_go_forward;

    int m_click_count { 0 };
    MonotonicTime m_last_press_time { MonotonicTime::now_coarse() };
    Gfx::FloatPoint m_last_press_position;
    u32 m_last_press_button { XENON_MOUSE_BUTTON_NONE };
};

}
