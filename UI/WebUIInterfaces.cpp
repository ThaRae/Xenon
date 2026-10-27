/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWebView/WebUI.h>
#include <UI/SettingsPage.h>
#include <UI/WebUIInterfaces.h>

namespace Ladybird {

void install_web_ui_interfaces()
{
    WebView::WebUI::set_embedder_interfaces([](WebView::WebUI& web_ui) {
        if (web_ui.host() == "settings"sv)
            SettingsPage::register_interfaces(web_ui);
    });
}

}
