/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Atomic.h>
#include <AK/ByteString.h>
#include <AK/Function.h>
#include <AK/HashMap.h>
#include <AK/Mutex.h>
#include <AK/NonnullOwnPtr.h>
#include <AK/Optional.h>
#include <AK/OwnPtr.h>
#include <AK/RefPtr.h>
#include <AK/Vector.h>
#include <LibCore/AnonymousBuffer.h>
#include <LibCore/EventLoop.h>
#include <LibCore/Forward.h>
#include <LibThreading/Thread.h>
#include <LibURL/Forward.h>
#include <LibWebCommon/HTML/ActivateTab.h>
#include <LibWebCommon/HTML/VisibilityState.h>
#include <LibWebCommon/PixelUnits.h>
#include <LibWebView/FileDownloader.h>
#include <LibWebView/Forward.h>
#include <LibWebView/SessionStore.h>
#include <Rune/Bridge/AppMenus.h>
#include <Rune/Bridge/BrowserModel.h>
#include <Rune/Bridge/XenonBridge.h>
#include <UI/XenonSettings.h>

namespace Xenon {

class View;

class Engine final : public BrowserModel::Observer {
public:
    static Engine& the();

    static WebView::SessionLaunchPolicy launch_restore_policy();

    bool start(XenonUiCallbacks const&, int argc, char const* const* argv);
    void shutdown();

    XenonViewId allocate_view_id() { return m_next_view_id.fetch_add(1); }
    XenonWindowId allocate_window_id() { return m_next_window_id.fetch_add(1); }
    void post(Function<void()>);

    template<typename Callback, typename... Arguments>
    void notify(Callback XenonUiCallbacks::* callback, Arguments... arguments) const
    {
        if (auto function = m_callbacks.*callback)
            function(m_callbacks.context, arguments...);
    }

    Core::AnonymousBuffer const& theme() const { return m_theme; }
    bool system_dark() const { return m_system_dark; }
    void set_system_dark(bool);

    BrowserModel* model() { return m_model.ptr(); }

    View* view(XenonViewId);
    View& open_view(XenonViewId opener, Web::HTML::ActivateTab, Optional<WebView::CanonicalTraversable&> = {});
    View& open_view_for_new_tab_or_window(XenonViewId opener, WebView::IsPrivate);
    void close_tab(XenonViewId);

    void set_window_viewport(XenonWindowId, Web::DevicePixelSize, double device_pixel_ratio);
    void record_view_viewport(XenonViewId, Web::DevicePixelSize, double device_pixel_ratio);
    XenonWindowId new_window(XenonWindowId, WebView::IsPrivate);
    void new_tab(XenonWindowId, XenonViewId, StringView input, Optional<size_t> index, Web::HTML::ActivateTab);

    void duplicate_tab(XenonViewId source, XenonViewId duplicate);
    void report_tab_memory(XenonViewId);
    void report_chrome_settings();
    void new_tab_in_active_window();
    void reopen_closed_tab_in_active_window();
    AppMenus& app_menus() { return m_app_menus; }
    void run_download_action(u64 download, u32 action);
    void report_downloads_soon();

    void set_focused_view(XenonViewId, bool focused);
    View* focused_view();

private:
    Engine() = default;

    intptr_t run();
    int run_application();
    void accept_commands();
    void stop_accepting_commands();
    void create_model();
    void open_startup_windows();
    void open_startup_tabs(XenonWindowId, Vector<URL::URL> const&);
    void open_urls_in_new_tabs(Vector<URL::URL> const&);
    void open_urls_in_new_window(Vector<URL::URL> const&);
    XenonWindowId window_for_new_tab(WebView::IsPrivate = WebView::IsPrivate::No);
    WebView::SessionStore* session_store_for(WebView::IsPrivate);
    void save_session_soon();

    struct Viewport {
        Web::DevicePixelSize size { 800, 600 };
        double device_pixel_ratio { 1.0 };
    };
    Viewport viewport_for_window(XenonWindowId) const;

    View& create_view_for_tab(BrowserModel::Tab const&, Web::HTML::VisibilityState);
    void load_tab(View&, BrowserModel::Tab const&);
    void ensure_view_for_tab(BrowserModel::Tab const&);
    void close_view(XenonViewId);
    void notify_tab_inserted(BrowserModel::Window const&, BrowserModel::Tab const&, size_t index);

    virtual void window_opened(BrowserModel::Window const&) override;
    virtual void window_closed(BrowserModel::Window const&) override;
    virtual void window_activated(BrowserModel::Window const&) override;
    virtual void tab_inserted(BrowserModel::Window const&, BrowserModel::Tab const&, size_t) override;
    virtual void tab_removed(BrowserModel::Window const&, BrowserModel::Tab const&) override;
    virtual void tab_moved(BrowserModel::Tab const&, XenonWindowId, size_t) override;
    virtual void tab_activated(BrowserModel::Window const&, BrowserModel::Tab const&, Optional<XenonViewId>) override;
    virtual void tab_pinned_changed(BrowserModel::Tab const&) override;
    virtual void closed_tabs_changed(bool) override;
    virtual void session_metadata_changed() override;

    enum class State : u8 {
        Idle,
        Starting,
        Running,
        Stopped,
    };

    XenonUiCallbacks m_callbacks {};
    Vector<ByteString> m_arguments;
    Vector<StringView> m_argument_views;
    Vector<char*> m_argv;

    Mutex m_mutex;
    State m_state { State::Idle };
    RefPtr<Core::WeakEventLoopReference> m_event_loop;
    Vector<Function<void()>> m_pending_commands;
    RefPtr<Threading::Thread> m_thread;

    Atomic<XenonViewId> m_next_view_id { 1 };
    Atomic<XenonWindowId> m_next_window_id { 1 };

    class SettingsObserver final : public Ladybird::XenonSettings::Observer {
        AK_ALLOC_WITH_KMALLOC;

    public:
        virtual void xenon_settings_changed() override { Engine::the().report_chrome_settings(); }
        virtual void xenon_chrome_layout_changed() override { Engine::the().report_chrome_settings(); }
    };
    OwnPtr<SettingsObserver> m_settings_observer;
    AppMenus m_app_menus;

    class DownloadsObserver final : public WebView::FileDownloaderObserver {
        AK_ALLOC_WITH_KMALLOC;

    public:
        virtual void download_added(WebView::FileDownloader::Download const&) override { Engine::the().report_downloads_soon(); }
        virtual void download_updated(WebView::FileDownloader::Download const&) override { Engine::the().report_downloads_soon(); }
        virtual void download_removed(u64, WebView::IsPrivate) override { Engine::the().report_downloads_soon(); }
    };
    OwnPtr<DownloadsObserver> m_downloads_observer;
    bool m_downloads_report_pending { false };
    void report_downloads();

    Core::AnonymousBuffer m_theme;
    bool m_system_dark { false };
    OwnPtr<BrowserModel> m_model;
    HashMap<XenonViewId, NonnullOwnPtr<View>> m_views;
    Optional<XenonViewId> m_focused_view;
    HashMap<XenonWindowId, Viewport> m_window_viewports;
    Viewport m_default_viewport;
    Optional<WebView::CanonicalTraversable&> m_traversable_for_next_view;
    RefPtr<WebView::BrowsingSession> m_private_session;
    RefPtr<Core::Timer> m_session_save_timer;
};

}
