/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWebView/UserAgent.h>
#include <Rune/Bridge/Application.h>
#include <Rune/Bridge/Engine.h>
#include <Rune/Bridge/View.h>

namespace Xenon {

Application::Application() = default;
Application::~Application() = default;

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

Optional<WebView::ViewImplementation&> Application::active_web_view() const
{
    if (auto* view = Engine::the().focused_view())
        return *view;
    return {};
}

Optional<WebView::ViewImplementation&> Application::open_blank_new_tab(Web::HTML::ActivateTab activate_tab) const
{
    return Engine::the().open_view(0, activate_tab);
}

}
