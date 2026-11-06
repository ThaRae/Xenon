/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/LexicalPath.h>
#include <LibCore/EventLoop.h>
#include <LibCore/Process.h>
#include <LibURL/InternalURLs.h>
#include <LibWebView/SessionStore.h>
#include <LibWebView/UserAgent.h>
#include <Rune/Bridge/Application.h>
#include <Rune/Bridge/Clipboard.h>
#include <Rune/Bridge/Engine.h>
#include <Rune/Bridge/View.h>
#include <UI/BlockingDashboard.h>
#include <UI/CertificateErrors.h>
#include <UI/Extensions/Registry.h>
#include <UI/FilterListUpdates.h>
#include <UI/NewTabPage.h>
#include <UI/PasswordManagers.h>
#include <UI/SettingsPage.h>
#include <UI/SiteSecurity.h>
#include <UI/Updater.h>
#include <UI/WebUIInterfaces.h>
#include <UI/XenonSettings.h>

namespace Xenon {

static void open_with_system_handler(Vector<ByteString> arguments)
{
#if defined(AK_OS_MACOS)
    auto process = Core::Process::spawn("/usr/bin/open"sv, arguments.span());
#else
    auto process = Core::Process::spawn({
        .executable = "xdg-open"sv,
        .search_for_executable_in_path = true,
        .arguments = arguments,
    });
#endif
    if (process.is_error())
        warnln("Xenon: Unable to open {}: {}", arguments.last(), process.error());
}

static void reveal_file(ByteString const& file)
{
#if defined(AK_OS_MACOS)
    open_with_system_handler({ ByteString { "-R"sv }, file });
#else
    open_with_system_handler({ LexicalPath::dirname(file) });
#endif
}

Application::Application()
{
    Ladybird::Extensions::Registry::the().register_url_scheme();
    Ladybird::BlockingDashboard::register_url_scheme();
    Ladybird::install_web_ui_interfaces();

    Ladybird::SettingsPage::set_host({
        .open_directory = [](ByteString const& directory) { open_with_system_handler({ directory }); },
        .reveal_file = [](ByteString const& file) { reveal_file(file); },
        .show_extensions = [] { },
        .install_update = [] { Ladybird::Updater::install_and_relaunch(); },
    });
}

Application::~Application() = default;

void Application::create_platform_arguments(Core::ArgsParser& args_parser)
{
    m_xenon_arguments.add_options(args_parser);
}

void Application::create_platform_options(WebView::BrowserOptions& browser_options, WebView::RequestServerOptions&, WebView::WebContentOptions& web_content_options)
{
    browser_options.prewarm_spare_web_content_process = WebView::PrewarmSpareWebContentProcess::No;

    if (web_content_options.user_agent_preset.has_value() || web_content_options.is_test_mode == WebView::IsTestMode::Yes)
        return;

#if defined(AK_OS_MACOS)
    static constexpr auto chrome_preset = "Chrome macOS Desktop"sv;
#elif defined(AK_OS_LINUX) && !defined(AK_OS_ANDROID)
    static constexpr auto chrome_preset = "Chrome Linux Desktop"sv;
#else
    static constexpr StringView chrome_preset {};
#endif
    if (!chrome_preset.is_empty() && WebView::user_agents.contains(chrome_preset))
        web_content_options.user_agent_preset = chrome_preset;
}

void Application::create_platform_actions()
{
    start_xenon_services();
}

void Application::start_xenon_services()
{
    auto languages = platform_preferred_languages();
    auto ui_language = languages.is_empty() ? "en-US"_string : languages.first();
    Ladybird::Extensions::Registry::the().initialize(profile().paths().data, m_xenon_arguments.extensions_to_load, move(ui_language));

    Ladybird::CertificateErrors::initialize();
    Ladybird::SiteSecurity::initialize();

    if (!settings().first_run_complete())
        settings().set_new_tab_page_url(URL::about_newtab());

    Ladybird::FilterLists::Updater::initialize();
    Ladybird::PasswordManagers::the().initialize(m_xenon_arguments.password_manager_choice);

    Core::deferred_invoke([] {
        Ladybird::Extensions::Registry::the().start_background_pages();
    });
}

void Application::did_create_view(View& view)
{
    WebView::ViewImplementation::NavigationListener listener;
    listener.on_load_finish = [&view](URL::URL const& url) {
        if (!Ladybird::NewTabPage::is_new_tab_page(url))
            return;
        auto show_recently_closed = Ladybird::XenonSettings::new_tab_page_shows_recently_closed()
            ? Ladybird::NewTabPage::ShowRecentlyClosed::Yes
            : Ladybird::NewTabPage::ShowRecentlyClosed::No;
        Ladybird::NewTabPage::populate(view, view.session(), show_recently_closed);
    };
    view.add_navigation_listener(move(listener));
}

WebView::SessionLaunchPolicy Application::launch_restore_policy() const
{
    return Engine::launch_restore_policy();
}

Optional<WebView::ViewImplementation&> Application::active_web_view() const
{
    if (auto* view = Engine::the().focused_view())
        return *view;
    return {};
}

void Application::open_url_in_new_window(URL::URL const& url, WebView::IsPrivate is_private)
{
    Engine::the().open_url_in_new_window(url, is_private);
}

// FIXME: Carry the navigation's method, body and referrer into the new window, as open_navigation_in_new_tab does.
void Application::open_navigation_in_new_window(Web::HTML::PreparedNavigationDescriptor navigation, WebView::IsPrivate is_private)
{
    Engine::the().open_url_in_new_window(navigation.url, is_private);
}

void Application::open_download(WebView::FileDownloader::Download const& download) const
{
    auto path = download_file_path_for_frontend_action(download);
    if (path.is_error()) {
        warnln("Xenon: Unable to open the downloaded file: {}", path.error());
        return;
    }
    open_with_system_handler({ path.release_value().to_byte_string() });
}

void Application::show_download_in_folder(WebView::FileDownloader::Download const& download) const
{
    auto path = download_file_path_for_frontend_action(download);
    if (path.is_error()) {
        warnln("Xenon: Unable to show the downloaded file: {}", path.error());
        return;
    }
    reveal_file(path.release_value().to_byte_string());
}

// FIXME: Show a dialog to name the bookmark and pick its folder, as the Qt frontend's BookmarkDialog does. Until
//        then, the bookmark is added straight away with the page's title, where Bookmarks keeps new bookmarks.
NonnullRefPtr<Application::BookmarkPromise> Application::display_add_bookmark_dialog(Optional<String const&> target_folder_id) const
{
    auto promise = BookmarkPromise::construct();
    auto view = active_web_view();
    if (!view.has_value()) {
        promise->reject(Error::from_string_literal("There is no page to bookmark"));
        return promise;
    }
    auto title = view->title().to_utf8();
    BookmarkDialogResult result {
        .data = WebView::BookmarkItem::Bookmark {
            .url = view->displayed_url(),
            .title = title.is_empty() ? Optional<String> {} : Optional<String> { move(title) },
            .favicon_hash = view->favicon_hash(),
        },
        .target_folder_id = target_folder_id.map([](auto const& id) { return id; }),
    };
    promise->resolve(move(result));
    return promise;
}

Optional<WebView::ViewImplementation&> Application::open_blank_new_tab(Web::HTML::ActivateTab activate_tab) const
{
    return Engine::the().open_view(0, activate_tab);
}

#ifdef AK_OS_MACOS
Utf16String Application::clipboard_text(ClipboardType type) const
{
    if (type != ClipboardType::Text || browser_options().headless_mode.has_value())
        return WebView::Application::clipboard_text(type);
    return Clipboard::text();
}

void Application::set_clipboard_text(String text, ClipboardType type)
{
    if (type != ClipboardType::Text || browser_options().headless_mode.has_value()) {
        WebView::Application::set_clipboard_text(move(text), type);
        return;
    }
    Clipboard::set_text(text);
}

Web::Clipboard::SystemClipboardItem Application::clipboard_item() const
{
    if (browser_options().headless_mode.has_value())
        return WebView::Application::clipboard_item();
    return Clipboard::item();
}

void Application::insert_clipboard_item(Web::Clipboard::SystemClipboardItem item)
{
    if (browser_options().headless_mode.has_value()) {
        WebView::Application::insert_clipboard_item(move(item));
        return;
    }
    Clipboard::set_item(item);
}
#endif

}
