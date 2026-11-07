/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibCore/EventLoop.h>
#include <LibWebView/Application.h>
#include <LibWebView/FileDownloader.h>
#include <Rune/Bridge/Engine.h>
#include <Rune/Bridge/View.h>
#include <Rune/Bridge/WindowLifecycle.h>

namespace Xenon {

WindowLifecycle& WindowLifecycle::the()
{
    static auto& lifecycle = *new WindowLifecycle;
    return lifecycle;
}

void WindowLifecycle::set_callbacks(XenonWindowCallbacks const& callbacks)
{
    m_callbacks = callbacks;
}

static XenonWindowGeometry xenon_geometry(WebView::SessionWindowGeometry const& geometry, bool has_position)
{
    return {
        .x = geometry.x,
        .y = geometry.y,
        .width = geometry.width,
        .height = geometry.height,
        .maximized = geometry.maximized,
        .has_position = has_position,
    };
}

XenonWindowGeometry WindowLifecycle::placement_for(Optional<WebView::SessionWindowGeometry> const& restored, Optional<WebView::SessionWindowGeometry> const& previous_window, DefaultGeometry const& default_geometry)
{
    if (restored.has_value())
        return xenon_geometry(*restored, true);
    if (previous_window.has_value()) {
        auto cascaded = *previous_window;
        cascaded.x += new_window_cascade_offset;
        cascaded.y += new_window_cascade_offset;
        return xenon_geometry(cascaded, true);
    }
    return xenon_geometry(default_geometry.geometry, default_geometry.has_position);
}

Vector<ViewId> WindowLifecycle::close_order(BrowserModel::Window const& window)
{
    Vector<ViewId> order;
    if (window.active_tab.has_value())
        order.append(*window.active_tab);
    for (auto view : window.tabs) {
        if (view != window.active_tab)
            order.append(view);
    }
    return order;
}

static WindowLifecycle::DefaultGeometry default_geometry()
{
    auto const& settings = WebView::Application::settings().window_settings();
    WindowLifecycle::DefaultGeometry geometry {
        .geometry = {
            .width = settings.size.width(),
            .height = settings.size.height(),
            .maximized = settings.maximized,
        },
    };
    if (settings.position.has_value()) {
        geometry.geometry.x = settings.position->x();
        geometry.geometry.y = settings.position->y();
        geometry.has_position = true;
    }
    return geometry;
}

void WindowLifecycle::window_opened(BrowserModel::Window const& window)
{
    Optional<WebView::SessionWindowGeometry> previous_window;
    if (auto* model = Engine::the().model()) {
        if (auto active = model->active_window(); active.has_value() && *active != window.id)
            previous_window = m_geometries.get(*active);
    }
    auto placement = placement_for(window.geometry, previous_window, default_geometry());
    if (window.geometry.has_value())
        m_geometries.set(window.id, *window.geometry);
    notify(&XenonWindowCallbacks::window_placement, window.id, static_cast<XenonWindowGeometry const*>(&placement));
}

void WindowLifecycle::window_closed(BrowserModel::Window const& window)
{
    if (auto geometry = m_geometries.take(window.id); geometry.has_value() && window.is_private == WebView::IsPrivate::No) {
        WebView::Application::settings().set_window_settings({
            .position = Gfx::IntPoint { geometry->x, geometry->y },
            .size = Gfx::IntSize { geometry->width, geometry->height },
            .maximized = geometry->maximized,
        });
    }
    m_unexposed_windows.remove(window.id);
    if (auto closing = m_closing.take(window.id); closing.has_value()) {
        auto finished = closing.release_value();
        Core::deferred_invoke([finished = move(finished)]() mutable {
            if (auto on_complete = move(finished->on_complete))
                on_complete(true);
        });
    }
    m_windows_awaiting_quit.remove_all_matching([&](auto id) { return id == window.id; });
}

void WindowLifecycle::set_exposed(WindowId id, bool exposed)
{
    if (exposed == is_exposed(id))
        return;
    if (exposed)
        m_unexposed_windows.remove(id);
    else
        m_unexposed_windows.set(id);

    auto* model = Engine::the().model();
    auto const* window = model ? model->window(id) : nullptr;
    if (!window || !window->active_tab.has_value())
        return;
    if (auto* view = Engine::the().view(*window->active_tab))
        view->set_visible(exposed);
}

void WindowLifecycle::geometry_reported(WindowId id, WebView::SessionWindowGeometry geometry)
{
    auto* model = Engine::the().model();
    auto const* window = model ? model->window(id) : nullptr;
    if (!window)
        return;
    m_geometries.set(id, geometry);
    if (window->session_window_id.has_value()) {
        if (auto* store = WebView::Application::session_store(window->is_private))
            store->window_geometry_changed(*window->session_window_id, geometry);
    }
    for (auto view_id : window->tabs) {
        if (auto* view = Engine::the().view(view_id))
            view->set_window_position({ geometry.x, geometry.y });
    }
}

void WindowLifecycle::request_frame(ViewId view, u32 request, Gfx::IntPoint position, Gfx::IntSize size)
{
    auto* model = Engine::the().model();
    auto const* window = model ? model->window_of(view) : nullptr;
    if (!window)
        return;
    request_window_frame(window->id, request, position, size);
}

void WindowLifecycle::request_window_frame(WindowId window, u32 request, Gfx::IntPoint position, Gfx::IntSize size)
{
    notify(&XenonWindowCallbacks::window_frame_requested, window, request, position.x(), position.y(), size.width(), size.height());
}

void WindowLifecycle::install_page_window_requests(View& view)
{
    auto id = view.id();
    view.on_reposition_window = [this, id](Gfx::IntPoint position) {
        request_frame(id, XENON_WINDOW_FRAME_MOVE, position);
    };
    view.on_resize_window = [this, id](Gfx::IntSize size) {
        request_frame(id, XENON_WINDOW_FRAME_RESIZE, {}, size);
    };
    view.on_maximize_window = [this, id] {
        request_frame(id, XENON_WINDOW_FRAME_MAXIMIZE);
    };
    view.on_minimize_window = [this, id] {
        request_frame(id, XENON_WINDOW_FRAME_MINIMIZE);
    };
    view.on_restore_window = [this, id] {
        request_frame(id, XENON_WINDOW_FRAME_RESTORE);
    };
}

void WindowLifecycle::raise(WindowId window) const
{
    notify(&XenonWindowCallbacks::window_raise, window);
}

void WindowLifecycle::run_preflight(WindowId id, Function<void(bool)> on_complete)
{
    auto* model = Engine::the().model();
    auto const* window = model ? model->window(id) : nullptr;
    if (!window || m_closing.contains(id)) {
        on_complete(false);
        return;
    }

    auto closing = make<Closing>();
    closing->original_active_tab = window->active_tab;
    closing->on_complete = move(on_complete);
    auto& coordinator = closing->coordinator;
    m_closing.set(id, move(closing));

    Vector<Ladybird::WindowCloseCoordinator::Preflight> preflights;
    for (auto view_id : close_order(*window)) {
        preflights.append([this, id, view_id](Function<void(bool)> on_tab_complete) {
            auto* model = Engine::the().model();
            auto const* tab = model ? model->tab(view_id) : nullptr;
            auto* view = Engine::the().view(view_id);
            if (!tab || tab->deferred || !view || view->handle().is_empty() || !view->page().is_open() || !view->needs_beforeunload_check()) {
                on_tab_complete(true);
                return;
            }
            model->activate_tab(view_id);
            raise(id);
            view->request_close_preflight(move(on_tab_complete));
        });
    }

    coordinator.start(
        move(preflights),
        [this, id] { finish_preflight(id, true); },
        [this, id] { finish_preflight(id, false); });
}

void WindowLifecycle::finish_preflight(WindowId id, bool approved)
{
    auto closing = m_closing.take(id);
    if (!closing.has_value())
        return;
    auto finished = closing.release_value();
    if (!approved && finished->original_active_tab.has_value()) {
        auto* model = Engine::the().model();
        if (model && model->tab(*finished->original_active_tab))
            model->activate_tab(*finished->original_active_tab);
    }
    Core::deferred_invoke([finished = move(finished), approved]() mutable {
        if (auto on_complete = move(finished->on_complete))
            on_complete(approved);
    });
}

static bool is_last_window(BrowserModel const& model, WindowId id)
{
    for (auto other : model.windows_by_activation()) {
        if (other != id)
            return false;
    }
    return true;
}

void WindowLifecycle::request_close(WindowId id)
{
    auto* model = Engine::the().model();
    if (!model || !model->window(id) || m_closing.contains(id) || m_quitting)
        return;

    auto close_after_checks = [this, id] {
        run_preflight(id, [this, id](bool approved) {
            auto* model = Engine::the().model();
            if (!model || !model->window(id))
                return;
            if (approved)
                model->close_window(id);
            else
                raise(id);
        });
    };

    if (is_last_window(*model, id) && WebView::Application::the().file_downloader().has_active_downloads()) {
        confirm_stop_downloads([this, id, close_after_checks = move(close_after_checks)](bool stopped) mutable {
            if (stopped)
                close_after_checks();
            else
                raise(id);
        });
        return;
    }
    close_after_checks();
}

void WindowLifecycle::request_quit()
{
    auto* model = Engine::the().model();
    if (!model || m_quitting)
        return;
    m_quitting = true;
    m_windows_awaiting_quit.clear();
    auto const& windows = model->windows_by_activation();
    for (size_t i = windows.size(); i-- > 0;)
        m_windows_awaiting_quit.append(windows[i]);
    ask_next_window_to_quit();
}

void WindowLifecycle::ask_next_window_to_quit()
{
    auto* model = Engine::the().model();
    while (model && !m_windows_awaiting_quit.is_empty()) {
        auto id = m_windows_awaiting_quit.take_first();
        if (!model->window(id))
            continue;
        run_preflight(id, [this](bool approved) {
            if (approved)
                ask_next_window_to_quit();
            else
                cancel_quit();
        });
        return;
    }

    confirm_stop_downloads([this](bool stopped) {
        if (!stopped) {
            cancel_quit();
            return;
        }
        notify(&XenonWindowCallbacks::quit_approved);
    });
}

void WindowLifecycle::cancel_quit()
{
    m_quitting = false;
    m_windows_awaiting_quit.clear();
}

void WindowLifecycle::confirm_stop_downloads(Function<void(bool)> on_complete)
{
    auto& downloader = WebView::Application::the().file_downloader();
    if (!downloader.has_unresumable_downloads()) {
        downloader.pause_active_downloads();
        on_complete(true);
        return;
    }
    auto request = m_next_downloads_request++;
    m_downloads_requests.set(request, move(on_complete));
    notify(&XenonWindowCallbacks::confirm_stop_downloads, request);
}

void WindowLifecycle::answer_stop_downloads(u64 request, bool stop)
{
    auto on_complete = m_downloads_requests.take(request);
    if (!on_complete.has_value())
        return;
    if (stop) {
        auto& downloader = WebView::Application::the().file_downloader();
        downloader.cancel_unresumable_downloads();
        downloader.pause_active_downloads();
    }
    (*on_complete)(stop);
}

void WindowLifecycle::devtools_changed(bool enabled)
{
    auto port = WebView::Application::browser_options().devtools_port.value_or(0);
    notify(&XenonWindowCallbacks::devtools_changed, enabled, port);
}

void WindowLifecycle::launch_devtools_client()
{
    if (auto result = WebView::Application::the().launch_devtools_client(); result.is_error())
        warnln("Xenon: Unable to launch the DevTools client: {}", result.error());
}

void WindowLifecycle::disable_devtools()
{
    if (auto result = WebView::Application::the().toggle_devtools_enabled(); result.is_error())
        warnln("Xenon: Unable to disable DevTools: {}", result.error());
}

}
