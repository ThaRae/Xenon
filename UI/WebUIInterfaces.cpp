/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWebView/WebUI.h>
#include <UI/PasswordManagers.h>
#include <UI/SettingsPage.h>
#include <UI/WebUIInterfaces.h>

namespace Ladybird {

void install_web_ui_interfaces()
{
    WebView::WebUI::set_embedder_interfaces([](WebView::WebUI& web_ui) {
        if (web_ui.host().is_one_of("settings"sv, "welcome"sv)) {
            PasswordManagers::the().register_web_ui_interfaces(web_ui);
            SettingsPage::register_interfaces(web_ui);
        }
    });
}

}
