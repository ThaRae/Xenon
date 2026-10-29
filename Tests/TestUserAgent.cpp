/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibTest/TestCase.h>
#include <LibWebCommon/Loader/UserAgent.h>
#include <LibWebView/UserAgent.h>

TEST_CASE(default_user_agent_is_chrome_shaped)
{
    auto user_agent = Web::default_user_agent;
    EXPECT(user_agent.starts_with("Mozilla/5.0 ("sv));
    EXPECT(user_agent.contains("AppleWebKit/537.36 (KHTML, like Gecko) Chrome/"sv));
    EXPECT(user_agent.ends_with(" Safari/537.36"sv));
    EXPECT(!user_agent.contains("Ladybird"sv));
    EXPECT(user_agent.contains("Chrome/" CHROME_MAJOR_VERSION ".0.0.0"sv));
}

TEST_CASE(chrome_desktop_preset_matches_the_default)
{
#if defined(AK_OS_MACOS)
    static constexpr auto preset_name = "Chrome macOS Desktop"sv;
#elif defined(AK_OS_LINUX) && !defined(AK_OS_ANDROID)
    static constexpr auto preset_name = "Chrome Linux Desktop"sv;
#else
    static constexpr StringView preset_name {};
#endif
    if (preset_name.is_empty())
        return;

    auto preset = WebView::user_agents.get(preset_name);
    EXPECT(preset.has_value());
    EXPECT_EQ(*preset, Web::default_user_agent);
}

TEST_CASE(navigator_compatibility_mode_defaults_to_chrome)
{
    EXPECT_EQ(Web::default_navigator_compatibility_mode, Web::NavigatorCompatibilityMode::Chrome);
}
