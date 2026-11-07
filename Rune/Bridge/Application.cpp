/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/LexicalPath.h>
#include <LibCore/EventLoop.h>
#include <LibCore/Process.h>
#include <LibURL/InternalURLs.h>
#include <LibWebView/PictureInPictureWindow.h>
#include <LibWebView/SessionStore.h>
#include <LibWebView/UserAgent.h>
#include <Rune/Bridge/Application.h>
#include <Rune/Bridge/Bookmarks.h>
#include <Rune/Bridge/Clipboard.h>
#include <Rune/Bridge/Engine.h>
#include <Rune/Bridge/ExtensionsHost.h>
#include <Rune/Bridge/PictureInPicture.h>
#include <Rune/Bridge/PlatformServices.h>
#include <Rune/Bridge/SaveLocation.h>
#include <Rune/Bridge/TaskManager.h>
#include <Rune/Bridge/View.h>
#include <Rune/Bridge/WindowLifecycle.h>
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
        .show_extensions = [] { ExtensionsHost::show_extensions(); },
        .install_update = [] { Ladybird::Updater::install_and_relaunch(); },
    });
}

Application::~Application() = default;

Optional<String> Application::ui_font_family() const
{
    return platform_ui_font_family();
}

Optional<String> Application::system_font_family() const
{
#if defined(AK_OS_MACOS)
    return {};
#else
    return ui_font_family();
#endif
}

void Application::resolve_external_url_handler(URL::URL const& url, WebView::ExternalURLHandlerCallback callback) const
{
    if (browser_options().headless_mode.has_value()) {
        WebView::Application::resolve_external_url_handler(url, move(callback));
        return;
    }
    resolve_platform_external_url_handler(url, move(callback));
}

void Application::display_crash_report_notification(ByteString const& report_name)
{
    Xenon::offer_crash_report(report_name);
}

NonnullRefPtr<Core::Promise<ByteString>> Application::ask_user_for_download_path(ByteString const& file) const
{
    return ask_ui_for_save_location(file);
}

bool Application::supports_picture_in_picture() const
{
    return !browser_options().headless_mode.has_value() || WebView::Application::supports_picture_in_picture();
}

OwnPtr<WebView::PictureInPictureWindow> Application::create_picture_in_picture_window(WebView::WebContentPage& requesting_page, WebView::CanonicalTraversable& traversable, Gfx::IntSize video_size)
{
    if (browser_options().headless_mode.has_value())
        return WebView::Application::create_picture_in_picture_window(requesting_page, traversable, video_size);
    return create_picture_in_picture_window_for(traversable, static_cast<View const&>(requesting_page.view()), video_size);
}

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
    TaskManager::add_open_action();
    start_xenon_services();
}

void Application::on_devtools_enabled() const
{
    WebView::Application::on_devtools_enabled();
    WindowLifecycle::the().devtools_changed(true);
}

void Application::on_devtools_disabled() const
{
    WebView::Application::on_devtools_disabled();
    WindowLifecycle::the().devtools_changed(false);
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
    if (!browser_options().headless_mode.has_value()) {
        initialize_platform_external_url_handler(Core::EventLoop::current());
        Ladybird::Updater::initialize();
    }
    Ladybird::PasswordManagers::the().initialize(m_xenon_arguments.password_manager_choice);
    ExtensionsHost::start();

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

NonnullRefPtr<Application::BookmarkPromise> Application::display_add_bookmark_dialog(Optional<String const&> target_folder_id) const
{
    Optional<URL::URL> current_url;
    Optional<String> current_title;
    Optional<String> current_favicon_hash;
    if (auto view = active_web_view(); view.has_value()) {
        current_url = view->displayed_url();
        current_title = view->title().to_utf8();
        current_favicon_hash = view->favicon_hash();
    }
    return Bookmarks::show_dialog({
        .kind = Bookmarks::DialogKind::AddBookmark,
        .url = move(current_url),
        .title = move(current_title),
        .favicon_hash = move(current_favicon_hash),
        .selected_folder_id = target_folder_id.copy(),
        .excluded_folder_id = {},
    });
}

NonnullRefPtr<Application::BookmarkPromise> Application::display_edit_bookmark_dialog(WebView::BookmarkItem const& current_bookmark, Optional<String const&> parent_folder_id) const
{
    auto const& bookmark = current_bookmark.bookmark();
    return Bookmarks::show_dialog({
        .kind = Bookmarks::DialogKind::EditBookmark,
        .url = bookmark.url,
        .title = bookmark.title,
        .favicon_hash = bookmark.favicon_hash,
        .selected_folder_id = parent_folder_id.copy(),
        .excluded_folder_id = {},
    });
}

NonnullRefPtr<Application::BookmarkPromise> Application::display_add_bookmark_folder_dialog(Optional<String const&> default_title, Optional<String const&> target_folder_id) const
{
    return Bookmarks::show_dialog({
        .kind = Bookmarks::DialogKind::AddFolder,
        .url = {},
        .title = default_title.copy(),
        .favicon_hash = {},
        .selected_folder_id = target_folder_id.copy(),
        .excluded_folder_id = {},
    });
}

NonnullRefPtr<Application::BookmarkPromise> Application::display_edit_bookmark_folder_dialog(WebView::BookmarkItem const& current_folder, Optional<String const&> parent_folder_id) const
{
    return Bookmarks::show_dialog({
        .kind = Bookmarks::DialogKind::EditFolder,
        .url = {},
        .title = current_folder.folder().title,
        .favicon_hash = {},
        .selected_folder_id = parent_folder_id.copy(),
        .excluded_folder_id = current_folder.id,
    });
}

void Application::show_bookmark_context_menu(Gfx::IntPoint position, Optional<WebView::BookmarkItem const&> item, Optional<String const&> target_folder_id, Optional<String const&> parent_folder_id)
{
    Bookmarks::show_context_menu_at_page_position(position, item, target_folder_id, parent_folder_id);
}

Optional<Application::BookmarkID> Application::bookmark_item_id_for_context_menu() const
{
    return Bookmarks::item_for_context_menu();
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
