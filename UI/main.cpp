/*
 * Copyright (c) 2022, Andreas Kling <andreas@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibCore/GeolocationProvider.h>
#include <LibMain/Main.h>
#include <LibURL/InternalURLs.h>
#include <LibWebView/Application.h>
#include <LibWebView/URL.h>
#include <LibWebView/Utilities.h>
#include <UI/Application.h>
#include <UI/BlockingDashboard.h>
#include <UI/BrowserWindow.h>
#include <UI/CertificateErrors.h>
#include <UI/Extensions/Registry.h>
#include <UI/FilterListUpdates.h>
#include <UI/FilterLists.h>
#include <UI/HubConnection.h>
#include <UI/MemoryPressureRelease.h>
#include <UI/PasswordManagers.h>
#include <UI/SiteSecurity.h>
#include <UI/TabHibernation.h>
#include <UI/Updater.h>
#include <UI/WebUIInterfaces.h>
#if defined(LADYBIRD_QT_HAVE_POSITIONING)
#    include <UI/GeolocationProviderQt.h>
#endif
#include <UI/WebContentView.h>

#include <QCoreApplication>
#include <QStyleHints>
#include <QtGlobal>

#if defined(AK_OS_MACOS)
#    include <QColorSpace>
#    include <QSurfaceFormat>
#    include <UI/ApplicationIcon.h>
#    include <UI/MacWindow.h>
#    include <UI/SpellCheckerMac.h>
#endif

namespace Ladybird {

bool is_using_dark_system_theme(QWidget&);
bool is_using_dark_system_theme(QWidget& widget)
{
    auto color_scheme = QGuiApplication::styleHints()->colorScheme();
    if (color_scheme != Qt::ColorScheme::Unknown)
        return color_scheme == Qt::ColorScheme::Dark;

    auto color = widget.palette().color(widget.backgroundRole());
    auto luma = 0.2126f * color.redF() + 0.7152f * color.greenF() + 0.0722f * color.blueF();
    return luma <= 0.5f;
}

}

ErrorOr<int> ladybird_main(Main::Arguments arguments)
{
    AK::set_rich_debug_enabled(true);

    WebView::set_application_name("Xenon"sv);

    // The web content view is presented in a native child window. Without this attribute, a widget
    // gaining a native window would force every other widget in the browser window to become native
    // as well.
    QCoreApplication::setAttribute(Qt::AA_DontCreateNativeWidgetSiblings);

#ifdef AK_OS_MACOS
    auto surface_format = QSurfaceFormat::defaultFormat();
    surface_format.setColorSpace(QColorSpace::SRgb);
    QSurfaceFormat::setDefaultFormat(surface_format);
#endif

#if defined(LADYBIRD_QT_HAVE_POSITIONING)
    // Only fall back to Qt on platforms that have no geolocation provider of their own.
    if (!Core::GeolocationProvider::is_available())
        Ladybird::install_qt_geolocation_provider();
#endif

    Ladybird::Extensions::Registry::the().register_url_scheme();
    Ladybird::BlockingDashboard::register_url_scheme();
    Ladybird::install_web_ui_interfaces();

    auto app = TRY(Ladybird::Application::create(Ladybird::FilterLists::with_bundled_filter_lists(arguments)));
    if (auto exit_status = app->exit_status_if_done(); exit_status.has_value())
        return *exit_status;
#if defined(AK_OS_MACOS)
    WebView::Application::the().set_spell_checker(Ladybird::create_platform_spell_checker());
#endif

    app->initialize_extensions();

    Ladybird::CertificateErrors::initialize();
    Ladybird::SiteSecurity::initialize();

    if (!Ladybird::Application::settings().first_run_complete())
        Ladybird::Application::settings().set_new_tab_page_url(URL::about_newtab());

    Ladybird::FilterLists::Updater::initialize();
    Ladybird::PasswordManagers::the().initialize(app->password_manager_choice());

    if (auto const& browser_options = Ladybird::Application::browser_options(); !browser_options.headless_mode.has_value()) {
#if defined(AK_OS_MACOS)
        auto const& profile = WebView::Application::profile();
        Ladybird::set_profile_application_icon(profile.identifier(), profile.paths().identity, profile.is_temporary());
#endif
        Ladybird::Updater::initialize();

        app->on_open_file = [&](auto const& file_url) {
            if (auto* window = app->non_private_window_if_any()) {
                auto& tab = window->new_tab_from_url(file_url, Web::HTML::ActivateTab::Yes, Ladybird::BrowserWindow::TabLocation::end());
                tab.set_url_is_hidden(false);
                auto& view = tab.view();
#if defined(AK_OS_MACOS)
                Ladybird::make_appkit_window_first_responder(view);
#endif
                view.setFocus();
                window->show();
                window->activateWindow();
                window->raise();
                return;
            }
            app->new_window({ file_url });
        };

        // NB: URLs from outside the browser, such as a link clicked in another app, never open in a private window.
        app->on_new_tab = [&](auto const& urls) {
            auto* non_private_window = app->non_private_window_if_any();
            if (!non_private_window) {
                app->new_window(urls);
                return;
            }

            auto& window = *non_private_window;
            for (size_t i = 0; i < urls.size(); ++i) {
                window.new_tab_from_url(urls[i], (i == 0) ? Web::HTML::ActivateTab::Yes : Web::HTML::ActivateTab::No, Ladybird::BrowserWindow::TabLocation::end());
            }
            window.show();
            window.activateWindow();
            window.raise();
        };

        app->on_new_window = [&](auto const& urls) {
            app->new_window(urls, app->configuration_for_new_window());
        };

        auto configuration = app->configuration_for_new_window();
        if (auto* restored_window = app->restore_previous_session(configuration)) {
            // A whole restored session replaces the usual startup tab, so only the URLs the user passed open with it.
            // With only pinned tabs restored, the usual startup tabs open after them.
            auto restored_everything = app->launch_restore_policy() == Ladybird::SessionLaunchPolicy::Everything;
            if (!restored_everything || !browser_options.raw_urls.is_empty())
                app->open_startup_tabs(*restored_window, browser_options.urls);
        } else {
            auto& window = app->new_window(browser_options.urls, configuration);
            window.setWindowTitle("Xenon");
        }
    }

    // NB: Messages to the Hub arrive in order, so this comes after whatever these windows have asked to show.
    if (auto* hub = Ladybird::HubConnection::the_if_connected())
        hub->async_ui_did_start();

    Ladybird::Extensions::Registry::the().start_background_pages();
    Ladybird::TabHibernation::start();
    Ladybird::MemoryPressureRelease::start();

    return app->execute();
}
