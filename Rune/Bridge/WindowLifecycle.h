/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Function.h>
#include <AK/HashMap.h>
#include <AK/HashTable.h>
#include <AK/NonnullOwnPtr.h>
#include <AK/Optional.h>
#include <AK/Vector.h>
#include <LibGfx/Point.h>
#include <LibGfx/Size.h>
#include <LibWebView/SessionStore.h>
#include <Rune/Bridge/BrowserModel.h>
#include <Rune/Bridge/XenonBridgeWindows.h>
#include <UI/WindowCloseCoordinator.h>

namespace Xenon {

class View;

class WindowLifecycle {
public:
    static WindowLifecycle& the();

    static constexpr int new_window_cascade_offset = 32;

    struct DefaultGeometry {
        WebView::SessionWindowGeometry geometry;
        bool has_position { false };
    };
    static XenonWindowGeometry placement_for(Optional<WebView::SessionWindowGeometry> const& restored, Optional<WebView::SessionWindowGeometry> const& previous_window, DefaultGeometry const&);
    static Vector<ViewId> close_order(BrowserModel::Window const&);

    void set_callbacks(XenonWindowCallbacks const&);

    void window_opened(BrowserModel::Window const&);
    void window_closed(BrowserModel::Window const&);

    bool is_exposed(WindowId window) const { return !m_unexposed_windows.contains(window); }
    void set_exposed(WindowId, bool exposed);
    void geometry_reported(WindowId, WebView::SessionWindowGeometry);
    Optional<WebView::SessionWindowGeometry> geometry_of(WindowId window) const { return m_geometries.get(window); }

    void install_page_window_requests(View&);

    void request_close(WindowId);
    void request_quit();
    void answer_stop_downloads(u64 request, bool stop);
    bool is_closing(WindowId window) const { return m_closing.contains(window); }
    bool is_quitting() const { return m_quitting; }

    void devtools_changed(bool enabled);
    void launch_devtools_client();
    void disable_devtools();

private:
    WindowLifecycle() = default;

    struct Closing {
        AK_ALLOC_WITH_KMALLOC;

        Ladybird::WindowCloseCoordinator coordinator;
        Optional<ViewId> original_active_tab;
        Function<void(bool)> on_complete;
    };

    template<typename Callback, typename... Arguments>
    void notify(Callback XenonWindowCallbacks::* callback, Arguments... arguments) const
    {
        if (auto function = m_callbacks.*callback)
            function(m_callbacks.context, arguments...);
    }

    void run_preflight(WindowId, Function<void(bool)> on_complete);
    void finish_preflight(WindowId, bool approved);
    void confirm_stop_downloads(Function<void(bool)> on_complete);
    void ask_next_window_to_quit();
    void cancel_quit();
    void raise(WindowId) const;
    void request_frame(ViewId, u32 request, Gfx::IntPoint = {}, Gfx::IntSize = {});

    XenonWindowCallbacks m_callbacks {};
    HashTable<WindowId> m_unexposed_windows;
    HashMap<WindowId, WebView::SessionWindowGeometry> m_geometries;
    HashMap<WindowId, NonnullOwnPtr<Closing>> m_closing;
    Vector<WindowId> m_windows_awaiting_quit;
    bool m_quitting { false };
    u64 m_next_downloads_request { 1 };
    HashMap<u64, Function<void(bool)>> m_downloads_requests;
};

}
