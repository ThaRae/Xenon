/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/LexicalPath.h>
#include <LibCore/Timer.h>
#include <LibGfx/Palette.h>
#include <LibGfx/SystemTheme.h>
#include <LibMain/Main.h>
#include <LibURL/InternalURLs.h>
#include <LibWebCommon/WebView/Utilities.h>
#include <LibWebView/BrowserProcess.h>
#include <LibWebView/DownloadPresentation.h>
#include <LibWebView/BrowsingSession.h>
#include <LibWebView/HistoryStore.h>
#include <LibWebView/PlatformColors.h>
#include <Rune/Bridge/Application.h>
#include <Rune/Bridge/Engine.h>
#include <Rune/Bridge/TabGroupHooks.h>
#include <Rune/Bridge/TabMemory.h>
#include <Rune/Bridge/View.h>
#include <UI/FilterLists.h>
#include <UI/MemoryPressureRelease.h>
#include <pthread.h>

namespace Xenon {

static constexpr size_t engine_thread_stack_size = 8 * MiB;
static constexpr auto session_save_delay = AK::Duration::from_seconds(1);

static Core::AnonymousBuffer make_system_theme(bool dark)
{
    auto theme_path = LexicalPath::join(WebView::s_ladybird_resource_root, "themes"sv, "Default.ini"sv);
    auto theme_or_error = Gfx::load_system_theme(theme_path.string());
    if (theme_or_error.is_error()) {
        warnln("Xenon: Unable to load the system theme from {}: {}", theme_path, theme_or_error.error());
        return {};
    }
    auto theme = theme_or_error.release_value();

    auto palette = Gfx::Palette(Gfx::PaletteImpl::create_with_anonymous_buffer(theme));
#ifdef AK_OS_MACOS
    palette.set_color(Gfx::ColorRole::Selection, WebView::macos_web_selection_color());
    palette.set_color(Gfx::ColorRole::InactiveSelection, WebView::macos_web_inactive_selection_color());
    palette.set_color(Gfx::ColorRole::InactiveSelectionText, WebView::macos_web_inactive_selection_text_color());
#else
    palette.set_color(Gfx::ColorRole::InactiveSelection, dark ? Gfx::Color(0x60, 0x60, 0x60) : Gfx::Color(0xd4, 0xd4, 0xd4));
#endif
    palette.set_flag(Gfx::FlagRole::IsDark, dark);
    return theme;
}

Engine& Engine::the()
{
    static auto& engine = *new Engine;
    return engine;
}

bool Engine::start(XenonUiCallbacks const& callbacks, int argc, char const* const* argv)
{
    {
        MutexLocker locker(m_mutex);
        if (m_state != State::Idle)
            return false;
        m_state = State::Starting;
    }

    m_callbacks = callbacks;

    m_arguments.clear();
    if (argc <= 0 || !argv || !argv[0])
        m_arguments.append("xenon-rune"sv);
    for (int i = 0; argv && i < argc; ++i) {
        if (argv[i])
            m_arguments.append(argv[i]);
    }
    for (auto& argument : m_arguments) {
        m_argument_views.append(argument.view());
        m_argv.append(const_cast<char*>(argument.characters()));
    }
    m_argv.append(nullptr);

    m_thread = Threading::Thread::construct("Xenon Engine"sv, [this] { return run(); });
    m_thread->set_stack_size(engine_thread_stack_size);
    m_thread->start();
    return true;
}

void Engine::shutdown()
{
    {
        MutexLocker locker(m_mutex);
        if (m_state == State::Idle || m_state == State::Stopped)
            return;
    }

    post([this] {
        if (m_model)
            m_model->application_quitting();
        auto views = move(m_views);
        m_focused_view.clear();
        views.clear();
        Core::EventLoop::current().quit(0);
    });

    if (!m_thread || pthread_equal(pthread_self(), m_thread->tid()))
        return;
    if (m_thread->needs_to_be_joined())
        (void)m_thread->join();
}

void Engine::post(Function<void()> command)
{
    MutexLocker locker(m_mutex);
    switch (m_state) {
    case State::Idle:
    case State::Starting:
        m_pending_commands.append(move(command));
        return;
    case State::Running:
        if (auto event_loop = m_event_loop->take(); event_loop.is_alive())
            event_loop->deferred_invoke(move(command));
        return;
    case State::Stopped:
        return;
    }
}

void Engine::accept_commands()
{
    MutexLocker locker(m_mutex);
    m_event_loop = Core::EventLoop::current_weak();
    m_state = State::Running;
    for (auto& command : m_pending_commands)
        Core::deferred_invoke(move(command));
    m_pending_commands.clear();
}

void Engine::stop_accepting_commands()
{
    MutexLocker locker(m_mutex);
    m_state = State::Stopped;
    m_event_loop = nullptr;
    m_pending_commands.clear();
}

intptr_t Engine::run()
{
    auto exit_code = run_application();
    stop_accepting_commands();
    notify(&XenonUiCallbacks::engine_exited, exit_code);
    return exit_code;
}

int Engine::run_application()
{
    WebView::set_application_name("Xenon"sv);

    Main::Arguments arguments {
        .argc = static_cast<int>(m_argument_views.size()),
        .argv = m_argv.data(),
        .strings = m_argument_views.span(),
    };

    auto application_or_error = Application::create(Ladybird::FilterLists::with_bundled_filter_lists(arguments));
    if (application_or_error.is_error()) {
        warnln("Xenon: Unable to start the engine: {}", application_or_error.error());
        return 1;
    }
    auto application = application_or_error.release_value();
    if (application->should_exit_after_profile_coordination())
        return 0;

    m_theme = make_system_theme(m_system_dark);

    auto& browser_process = application->browser_process();
    browser_process.on_new_tab = [this](auto const& urls) { open_urls_in_new_tabs(urls); };
    browser_process.on_new_window = [this](auto const& urls) { open_urls_in_new_window(urls); };
    browser_process.on_open_urls = [this](auto const& urls) { open_urls_in_new_tabs(urls); };

    create_model();
    Ladybird::MemoryPressureRelease::start();

    m_settings_observer = make<SettingsObserver>();
    m_downloads_observer = make<DownloadsObserver>();

    accept_commands();
    notify(&XenonUiCallbacks::engine_ready);
    report_chrome_settings();
    Core::deferred_invoke([this] { open_startup_windows(); });

    auto result = application->execute();

    stop_accepting_commands();
    m_settings_observer = nullptr;
    m_downloads_observer = nullptr;
    m_focused_view.clear();
    m_views.clear();
    m_model = nullptr;
    m_private_session = nullptr;
    m_session_save_timer = nullptr;

    if (result.is_error()) {
        warnln("Xenon: The engine stopped with an error: {}", result.error());
        return 1;
    }
    return result.value();
}

WebView::SessionLaunchPolicy Engine::launch_restore_policy()
{
    if (WebView::Application::settings().restore_previous_session())
        return WebView::SessionLaunchPolicy::Everything;
    return WebView::SessionLaunchPolicy::PinnedOnly;
}

void Engine::create_model()
{
    BrowserModel::Allocators allocators {
        .view_id = [this] { return allocate_view_id(); },
        .window_id = [this] { return allocate_window_id(); },
    };
    m_model = make<BrowserModel>(*this, move(allocators), [this](WebView::IsPrivate is_private) { return session_store_for(is_private); });
    install_tab_group_hooks(*m_model);
    m_session_save_timer = Core::Timer::create_single_shot(session_save_delay, [] {
        if (auto* store = WebView::Application::session_store(WebView::IsPrivate::No))
            store->flush_dirty_state();
    });
}

WebView::SessionStore* Engine::session_store_for(WebView::IsPrivate is_private)
{
    if (is_private == WebView::IsPrivate::Yes && !m_private_session)
        m_private_session = WebView::Application::session_for_new_view(WebView::IsPrivate::Yes);
    return WebView::Application::session_store(is_private);
}

void Engine::save_session_soon()
{
    auto* store = WebView::Application::session_store(WebView::IsPrivate::No);
    if (!store || !store->is_persistent() || !m_session_save_timer || m_session_save_timer->is_active())
        return;
    m_session_save_timer->start();
}

void Engine::open_startup_windows()
{
    if (!m_model)
        return;

    auto const& browser_options = Application::browser_options();
    Vector<WebView::RestoredSessionWindow> plan;
    if (auto* store = WebView::Application::session_store(WebView::IsPrivate::No))
        plan = store->take_launch_plan();

    Optional<XenonWindowId> frontmost_window;
    for (auto const& restored : plan)
        frontmost_window = m_model->restore_window(restored);

    if (!frontmost_window.has_value()) {
        open_urls_in_new_window(browser_options.urls);
        return;
    }

    auto restored_everything = launch_restore_policy() == WebView::SessionLaunchPolicy::Everything;
    if (!restored_everything || !browser_options.raw_urls.is_empty())
        open_startup_tabs(*frontmost_window, browser_options.urls);
}

void Engine::open_startup_tabs(XenonWindowId window, Vector<URL::URL> const& urls)
{
    for (size_t i = 0; i < urls.size(); ++i)
        m_model->open_tab(window, { .url = urls[i], .activate = i == 0 ? Web::HTML::ActivateTab::Yes : Web::HTML::ActivateTab::No });
}

void Engine::open_urls_in_new_tabs(Vector<URL::URL> const& urls)
{
    if (!m_model || urls.is_empty())
        return;
    auto window = window_for_new_tab();
    open_startup_tabs(window, urls);
    m_model->activate_window(window);
}

void Engine::open_urls_in_new_window(Vector<URL::URL> const& urls)
{
    if (!m_model)
        return;
    auto window = m_model->open_window(WebView::IsPrivate::No);
    if (urls.is_empty())
        open_startup_tabs(window, { Application::settings().new_tab_page_url() });
    else
        open_startup_tabs(window, urls);
}

XenonWindowId Engine::window_for_new_tab(WebView::IsPrivate is_private)
{
    auto const& windows = m_model->windows_by_activation();
    for (size_t i = windows.size(); i-- > 0;) {
        if (auto const* window = m_model->window(windows[i]); window && window->is_private == is_private)
            return window->id;
    }
    return m_model->open_window(is_private);
}

XenonWindowId Engine::new_window(XenonWindowId id, WebView::IsPrivate is_private)
{
    if (!m_model)
        return id;
    m_model->open_window(is_private, id);
    open_startup_tabs(id, { Application::settings().new_tab_page_url() });
    return id;
}

void Engine::new_tab(XenonWindowId window_id, XenonViewId view_id, StringView input, Optional<size_t> index, Web::HTML::ActivateTab activate_tab)
{
    if (!m_model)
        return;
    auto const* window = window_id != XENON_WINDOW_NONE ? m_model->window(window_id) : nullptr;
    auto target = window ? window->id : window_for_new_tab();

    auto trimmed_input = input.trim_whitespace();
    URL::URL url;
    if (trimmed_input.is_empty())
        url = Application::settings().new_tab_page_url();
    m_model->open_tab(target, { .view = view_id, .url = url, .index = index, .activate = activate_tab });

    if (!trimmed_input.is_empty()) {
        if (auto* opened = view(view_id))
            opened->load_user_input(trimmed_input);
    }
}

void Engine::set_window_viewport(XenonWindowId window, Web::DevicePixelSize size, double device_pixel_ratio)
{
    Viewport viewport { size, device_pixel_ratio };
    m_default_viewport = viewport;
    if (window != XENON_WINDOW_NONE)
        m_window_viewports.set(window, viewport);
}

void Engine::record_view_viewport(XenonViewId view, Web::DevicePixelSize size, double device_pixel_ratio)
{
    if (!m_model)
        return;
    if (auto const* window = m_model->window_of(view))
        set_window_viewport(window->id, size, device_pixel_ratio);
}

Engine::Viewport Engine::viewport_for_window(XenonWindowId window) const
{
    return m_window_viewports.get(window).value_or(m_default_viewport);
}

View* Engine::view(XenonViewId id)
{
    auto view = m_views.get(id);
    if (!view.has_value())
        return nullptr;
    return view.value();
}

View& Engine::open_view(XenonViewId opener_id, Web::HTML::ActivateTab activate_tab, Optional<WebView::CanonicalTraversable&> traversable)
{
    VERIFY(m_model);
    Optional<XenonViewId> opener;
    XenonWindowId window = XENON_WINDOW_NONE;
    if (auto const* opener_window = opener_id != 0 ? m_model->window_of(opener_id) : nullptr) {
        opener = opener_id;
        window = opener_window->id;
    } else {
        window = window_for_new_tab();
    }

    m_traversable_for_next_view = traversable;
    auto id = m_model->open_tab(window, { .opener = opener, .activate = activate_tab });
    m_traversable_for_next_view.clear();

    auto* opened = view(id);
    VERIFY(opened);
    return *opened;
}

View& Engine::open_view_for_new_tab_or_window(XenonViewId opener_id, WebView::IsPrivate is_private)
{
    VERIFY(m_model);
    auto const* opener_window = m_model->window_of(opener_id);
    if (opener_window && opener_window->is_private == is_private)
        return open_view(opener_id, Web::HTML::ActivateTab::Yes);

    auto window = m_model->open_window(is_private);
    auto id = m_model->open_tab(window, { .activate = Web::HTML::ActivateTab::Yes });
    auto* opened = view(id);
    VERIFY(opened);
    return *opened;
}

void Engine::close_tab(XenonViewId id)
{
    Core::deferred_invoke([this, id] {
        if (m_model)
            m_model->close_tab(id);
    });
}

View& Engine::create_view_for_tab(BrowserModel::Tab const& tab, Web::HTML::VisibilityState visibility_state)
{
    auto const* window = m_model->window(tab.window);
    auto is_private = window ? window->is_private : WebView::IsPrivate::No;
    auto viewport = viewport_for_window(tab.window);
    auto traversable = m_traversable_for_next_view;
    m_traversable_for_next_view.clear();

    auto created = View::create(tab.view, viewport.size, viewport.device_pixel_ratio, is_private, visibility_state, traversable);
    auto& view = *created;
    m_views.set(tab.view, move(created));
    if (tab.session_tab_id.has_value())
        view.set_session_tab_id(*tab.session_tab_id);
    m_model->mark_tab_loaded(tab.view);

    if (!traversable.has_value())
        load_tab(view, tab);
    return view;
}

void Engine::load_tab(View& view, BrowserModel::Tab const& tab)
{
    if (auto history = m_model->take_history_for_load(tab.view); history.has_value()) {
        if (!view.restore_session_history_from_snapshot(history.release_value()).is_error())
            return;
    }
    if (tab.has_initial_url())
        view.load(tab.initial_url);
}

void Engine::ensure_view_for_tab(BrowserModel::Tab const& tab)
{
    if (!view(tab.view))
        create_view_for_tab(tab, Web::HTML::VisibilityState::Visible);
}

void Engine::close_view(XenonViewId id)
{
    auto view = m_views.take(id);
    if (!view.has_value())
        return;
    if (m_focused_view == id)
        m_focused_view.clear();

    auto& closing_view = *view.value();
    if (closing_view.handle().is_empty() || !closing_view.page().is_open())
        return;

    auto request_close = closing_view.prepare_for_close_without_prompting();
    Core::deferred_invoke(move(request_close));
}

static XenonStr xenon_str(StringView string)
{
    return { string.characters_without_null_termination(), string.length() };
}

void Engine::notify_tab_inserted(BrowserModel::Window const& window, BrowserModel::Tab const& tab, size_t index)
{
    auto url = tab.has_initial_url() ? tab.initial_url.serialize() : String {};
    String title;
    if (tab.deferred && tab.has_initial_url()) {
        if (auto const& history_store = WebView::Application::default_session().history_store) {
            if (auto entry = history_store->entry_for_url(tab.initial_url); entry.has_value() && entry->title.has_value())
                title = *entry->title;
        }
    }

    XenonTabInfo info {
        .window = window.id,
        .view = tab.view,
        .opener = tab.opener.value_or(0),
        .index = static_cast<u32>(index),
        .pinned = tab.pinned,
        .is_private = window.is_private == WebView::IsPrivate::Yes,
        .loaded = !tab.deferred,
        .is_new_tab_page = !tab.restored_from_session && tab.has_initial_url() && tab.initial_url == Application::settings().new_tab_page_url(),
        .url = xenon_str(url),
        .title = xenon_str(title),
    };
    notify(&XenonUiCallbacks::tab_inserted, static_cast<XenonTabInfo const*>(&info));
}

void Engine::window_opened(BrowserModel::Window const& window)
{
    notify(&XenonUiCallbacks::window_opened, window.id, window.is_private == WebView::IsPrivate::Yes);
}

void Engine::window_closed(BrowserModel::Window const& window)
{
    for (auto view : window.tabs)
        close_view(view);
    m_window_viewports.remove(window.id);
    notify(&XenonUiCallbacks::window_closed, window.id);

    if (window.is_private == WebView::IsPrivate::Yes) {
        auto other_private_window_open = false;
        for (auto id : m_model->windows_by_activation()) {
            auto const* other = m_model->window(id);
            if (other && other->id != window.id && other->is_private == WebView::IsPrivate::Yes)
                other_private_window_open = true;
        }
        if (!other_private_window_open)
            m_private_session = nullptr;
    }
}

void Engine::window_activated(BrowserModel::Window const& window)
{
    notify(&XenonUiCallbacks::window_activated, window.id);
}

void Engine::tab_inserted(BrowserModel::Window const& window, BrowserModel::Tab const& tab, size_t index)
{
    notify_tab_inserted(window, tab, index);
    if (!tab.deferred)
        create_view_for_tab(tab, Web::HTML::VisibilityState::Hidden);
}

void Engine::tab_removed(BrowserModel::Window const& window, BrowserModel::Tab const& tab)
{
    close_view(tab.view);
    notify(&XenonUiCallbacks::tab_removed, window.id, tab.view);
}

void Engine::tab_moved(BrowserModel::Tab const& tab, XenonWindowId from, size_t index)
{
    notify(&XenonUiCallbacks::tab_moved, tab.view, from, tab.window, static_cast<u32>(index));
}

void Engine::tab_activated(BrowserModel::Window const& window, BrowserModel::Tab const& tab, Optional<XenonViewId> previous)
{
    if (previous.has_value()) {
        if (auto* previous_view = view(*previous))
            previous_view->set_visible(false);
    }
    ensure_view_for_tab(tab);
    if (auto* activated = view(tab.view))
        activated->set_visible(true);
    notify(&XenonUiCallbacks::tab_activated, window.id, tab.view);
}

void Engine::tab_pinned_changed(BrowserModel::Tab const& tab)
{
    notify(&XenonUiCallbacks::tab_pinned_changed, tab.view, tab.pinned);
}

void Engine::closed_tabs_changed(bool can_reopen)
{
    notify(&XenonUiCallbacks::closed_tabs_changed, can_reopen);
}

void Engine::session_metadata_changed()
{
    save_session_soon();
}

void Engine::open_url_in_new_window(URL::URL const& url, WebView::IsPrivate is_private)
{
    if (!m_model)
        return;
    auto id = allocate_window_id();
    m_model->open_window(is_private, id);
    open_startup_tabs(id, { url });
}

void Engine::new_tab_in_active_window()
{
    if (!m_model)
        return;
    new_tab(m_model->active_window().value_or(XENON_WINDOW_NONE), allocate_view_id(), {}, {}, Web::HTML::ActivateTab::Yes);
}

void Engine::reopen_closed_tab_in_active_window()
{
    if (m_model)
        m_model->reopen_closed_tab(m_model->active_window());
}

void Engine::report_downloads_soon()
{
    if (m_downloads_report_pending)
        return;
    m_downloads_report_pending = true;
    Core::deferred_invoke([this] {
        m_downloads_report_pending = false;
        report_downloads();
    });
}

void Engine::report_downloads()
{
    auto const& downloads = WebView::Application::the().file_downloader().downloads();
    Vector<ByteString> file_names;
    Vector<String> status_texts;
    file_names.ensure_capacity(downloads.size());
    status_texts.ensure_capacity(downloads.size());
    Vector<XenonDownload> items;
    items.ensure_capacity(downloads.size());
    for (size_t i = downloads.size(); i > 0; --i) {
        auto const& download = downloads[i - 1];
        file_names.append(download.destination.basename());
        status_texts.append(WebView::download_status_text(download));
        auto progress = download.progress();
        items.append({
            .id = download.id,
            .file_name = xenon_str(file_names.last()),
            .status_text = xenon_str(status_texts.last()),
            .status = static_cast<u32>(download.status),
            .progress = progress.value_or(-1),
            .can_resume = download.can_resume,
            .is_private = download.is_private == WebView::IsPrivate::Yes,
        });
    }
    notify(&XenonUiCallbacks::downloads_changed, static_cast<XenonDownload const*>(items.data()), items.size());
}

void Engine::run_download_action(u64 id, u32 action)
{
    auto& application = WebView::Application::the();
    auto& downloader = application.file_downloader();
    auto download = downloader.download(id);
    if (!download.has_value())
        return;
    switch (action) {
    case XENON_DOWNLOAD_ACTION_OPEN:
        application.open_download(*download);
        break;
    case XENON_DOWNLOAD_ACTION_SHOW_IN_FOLDER:
        application.show_download_in_folder(*download);
        break;
    case XENON_DOWNLOAD_ACTION_CANCEL:
        downloader.cancel_download(id);
        break;
    case XENON_DOWNLOAD_ACTION_PAUSE:
        downloader.pause_download(id);
        break;
    case XENON_DOWNLOAD_ACTION_RESUME:
        downloader.resume_download(id);
        break;
    default:
        break;
    }
}

void Engine::report_chrome_settings()
{
    auto layout = static_cast<u8>(Ladybird::XenonSettings::chrome_layout());
    notify(&XenonUiCallbacks::chrome_settings_changed, layout, Ladybird::XenonSettings::cycle_tabs_in_recently_used_order());
}

void Engine::duplicate_tab(XenonViewId source_id, XenonViewId duplicate_id)
{
    if (!m_model)
        return;
    auto const* source = m_model->tab(source_id);
    auto const* window = m_model->window_of(source_id);
    if (!source || !window)
        return;

    Optional<WebView::SessionHistorySnapshot> history;
    URL::URL url = source->initial_url;
    if (auto* source_view = view(source_id); source_view && !source->deferred) {
        history = source_view->session_history_snapshot();
        url = source_view->url();
    } else if (source->pending_history.has_value()) {
        history = source->pending_history;
    } else if (source->session_tab_id.has_value()) {
        if (auto* store = session_store_for(window->is_private))
            history = store->load_tab_history(*source->session_tab_id);
    }
    if (history.has_value() && history->used_steps.is_empty())
        history.clear();

    auto index = window->index_of(source_id).map([](auto index) { return index + 1; });
    m_model->open_tab(window->id, { .view = duplicate_id, .url = url, .index = index, .opener = source_id, .history = move(history) });
}

void Engine::report_tab_memory(XenonViewId id)
{
    auto const* tab = m_model ? m_model->tab(id) : nullptr;
    if (!tab)
        return;
    auto* tab_view = tab->deferred ? nullptr : view(id);
    auto description = TabMemory::description(tab_view);
    notify(&XenonUiCallbacks::tab_memory, id, xenon_str(description));
}

void Engine::set_system_dark(bool dark)
{
    if (m_system_dark == dark)
        return;
    m_system_dark = dark;
    m_theme = make_system_theme(dark);
    for (auto& it : m_views)
        it.value->apply_system_theme();
}

void Engine::set_focused_view(XenonViewId id, bool focused)
{
    if (focused)
        m_focused_view = id;
    else if (m_focused_view == id)
        m_focused_view.clear();
}

View* Engine::focused_view()
{
    if (m_focused_view.has_value())
        return view(*m_focused_view);
    if (!m_model)
        return nullptr;
    auto active_window = m_model->active_window();
    if (!active_window.has_value())
        return nullptr;
    auto const* window = m_model->window(*active_window);
    if (!window || !window->active_tab.has_value())
        return nullptr;
    return view(*window->active_tab);
}

}
