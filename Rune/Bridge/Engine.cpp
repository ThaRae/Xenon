/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/LexicalPath.h>
#include <LibGfx/SystemTheme.h>
#include <LibMain/Main.h>
#include <LibURL/InternalURLs.h>
#include <LibWebCommon/WebView/Utilities.h>
#include <LibWebView/BrowserProcess.h>
#include <Rune/Bridge/Application.h>
#include <Rune/Bridge/Engine.h>
#include <Rune/Bridge/View.h>
#include <UI/FilterLists.h>
#include <UI/MemoryPressureRelease.h>
#include <pthread.h>

namespace Xenon {

static constexpr size_t engine_thread_stack_size = 8 * MiB;
static constexpr Web::DevicePixelSize default_viewport_size { 800, 600 };

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

    auto theme_path = LexicalPath::join(WebView::s_ladybird_resource_root, "themes"sv, "Default.ini"sv);
    if (auto theme = Gfx::load_system_theme(theme_path.string()); !theme.is_error())
        m_theme = theme.release_value();
    else
        warnln("Xenon: Unable to load the system theme from {}: {}", theme_path, theme.error());

    if (!Application::settings().first_run_complete())
        Application::settings().set_new_tab_page_url(URL::about_newtab());

    auto& browser_process = application->browser_process();
    browser_process.on_new_tab = [this](auto const& urls) { open_urls_in_new_views(urls); };
    browser_process.on_new_window = [this](auto const& urls) { open_urls_in_new_views(urls); };
    browser_process.on_open_urls = [this](auto const& urls) { open_urls_in_new_views(urls); };

    Ladybird::MemoryPressureRelease::start();

    accept_commands();
    notify(&XenonUiCallbacks::engine_ready);

    auto result = application->execute();

    stop_accepting_commands();
    m_focused_view.clear();
    m_views.clear();

    if (result.is_error()) {
        warnln("Xenon: The engine stopped with an error: {}", result.error());
        return 1;
    }
    return result.value();
}

View* Engine::view(XenonViewId id)
{
    auto view = m_views.get(id);
    if (!view.has_value())
        return nullptr;
    return view.value();
}

void Engine::open_urls_in_new_views(Vector<URL::URL> const& urls)
{
    for (size_t i = 0; i < urls.size(); ++i)
        open_view(0, i == 0 ? Web::HTML::ActivateTab::Yes : Web::HTML::ActivateTab::No).load(urls[i]);
}

View& Engine::create_view(XenonViewId id, Web::DevicePixelSize viewport_size, double device_pixel_ratio, StringView input)
{
    auto& view = insert_view(View::create(id, viewport_size, device_pixel_ratio, Web::HTML::VisibilityState::Visible));
    view.load_user_input(input);
    return view;
}

View& Engine::open_view(XenonViewId opener_id, Web::HTML::ActivateTab activate_tab, Optional<WebView::CanonicalTraversable&> traversable)
{
    auto* opener = view(opener_id);
    auto id = allocate_view_id();
    notify(&XenonUiCallbacks::view_opened, opener ? opener_id : static_cast<XenonViewId>(0), id, activate_tab == Web::HTML::ActivateTab::Yes);

    auto viewport_size = opener ? opener->viewport_size() : default_viewport_size;
    auto device_pixel_ratio = opener ? opener->device_pixel_ratio() : 1.0;
    auto visibility_state = activate_tab == Web::HTML::ActivateTab::Yes ? Web::HTML::VisibilityState::Visible : Web::HTML::VisibilityState::Hidden;
    return insert_view(View::create(id, viewport_size, device_pixel_ratio, visibility_state, traversable));
}

View& Engine::insert_view(NonnullOwnPtr<View> view)
{
    auto& inserted_view = *view;
    m_views.set(view->id(), move(view));
    return inserted_view;
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

void Engine::set_focused_view(XenonViewId id, bool focused)
{
    if (focused)
        m_focused_view = id;
    else if (m_focused_view == id)
        m_focused_view.clear();
}

View* Engine::focused_view()
{
    if (!m_focused_view.has_value())
        return nullptr;
    return view(*m_focused_view);
}

}
