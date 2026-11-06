/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Format.h>
#include <LibCore/System.h>
#include <LibWebCommon/WebView/Utilities.h>
#include <LibWebView/CrashReportStore.h>
#include <Rune/Bridge/XenonBridge.h>
#include <time.h>

int main(int argc, char** argv)
{
    tzset();

    if (auto result = Core::System::set_resource_limits(RLIMIT_NOFILE, 65536); result.is_error())
        warnln("Unable to increase open file limit: {}", result.error());

    AK::set_rich_debug_enabled(true);
    WebView::set_application_name("Xenon"sv);

    if (auto result = WebView::CrashReportStore::the().initialize_browser_crash_handler(); result.is_error())
        warnln("Could not prepare Browser crash reporting: {}", result.error());

    return xenon_ui_main(argc, argv);
}
