/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/ByteString.h>
#include <AK/NeverDestroyed.h>
#include <LibCore/Environment.h>
#include <LibTest/TestCase.h>
#include <UI/BlockingPopover.h>

#include <QAbstractButton>
#include <QApplication>
#include <QFrame>
#include <QLabel>
#include <QPixmap>
#include <QStyleHints>
#include <QToolButton>

using namespace Ladybird;

// The popover is a widget, so it needs an application. A machine with no display gets Qt's offscreen platform.
static QApplication& application()
{
    static int argc = 1;
    static char program[] = "TestXenonBlockingPopover";
    static char* argv[] = { program, nullptr };
#if !defined(AK_OS_MACOS) && !defined(AK_OS_WINDOWS)
    if (!Core::Environment::has("DISPLAY"sv) && !Core::Environment::has("WAYLAND_DISPLAY"sv))
        qputenv("QT_QPA_PLATFORM", "offscreen");
#endif
    static NeverDestroyed<QApplication> application(argc, argv);
    return *application;
}

static WebView::ContentBlockingSnapshot snapshot(WebView::ContentBlockingState state, u64 blocked)
{
    return { .site = "news.example.com"_string, .state = state, .blocked_resources = {}, .blocked_request_count = blocked };
}

static Vector<BlockingDomain> domains()
{
    return {
        { .host = "cdn.tracker.example"_string, .requests = 4, .first_party = false },
        { .host = "news.example.com"_string, .requests = 23, .first_party = true },
        { .host = "static.example.com"_string, .requests = 9, .first_party = true },
        { .host = "fonts.example.net"_string, .requests = 2, .first_party = false },
    };
}

template<typename T>
static T* child(QWidget& parent, char const* name)
{
    return parent.findChild<T*>(name);
}

static Vector<QString> stat_values(BlockingPopover& popover)
{
    Vector<QString> values;
    for (auto* label : popover.findChildren<QLabel*>("XenonBlockingStatValue"))
        values.append(label->text());
    return values;
}

static Vector<QString> labels_named(BlockingPopover& popover, char const* name)
{
    Vector<QString> texts;
    for (auto* label : popover.findChildren<QLabel*>(name))
        texts.append(label->text());
    return texts;
}

// With XENON_SCREENSHOT_DIR set, the popover's card is saved there as it would be drawn, for looking at.
static void save_screenshot(BlockingPopover& popover, StringView name)
{
    auto directory = Core::Environment::get("XENON_SCREENSHOT_DIR"sv);
    if (!directory.has_value())
        return;
    popover.adjustSize();
    auto path = ByteString::formatted("{}/{}.png", *directory, name);
    EXPECT(popover.card().grab().save(QString::fromUtf8(path.characters())));
}

static void use_color_scheme(BlockingPopover& popover, Qt::ColorScheme scheme)
{
    application().styleHints()->setColorScheme(scheme);
    popover.update_chrome_style(QApplication::palette());
}

TEST_CASE(shows_what_was_blocked_and_where_the_page_connected)
{
    application();
    BlockingPopover popover(nullptr);
    use_color_scheme(popover, Qt::ColorScheme::Light);
    popover.set_snapshot(snapshot(WebView::ContentBlockingState::Enabled, 12));
    popover.set_blocked_request_total(48'213);
    popover.set_domains(domains());
    popover.set_site_switch(BlockingSiteSwitch::Popups, true);

    auto* power = child<QAbstractButton>(popover, "XenonBlockingPower");
    VERIFY(power);
    EXPECT(power->isChecked());
    EXPECT(power->isEnabled());

    auto values = stat_values(popover);
    EXPECT_EQ(values.size(), 3u);
    EXPECT(values[0] == QString("12"));
    EXPECT(values[2] == QString("4"));
    // 12 blocked out of 12 + 38 requests.
    EXPECT(labels_named(popover, "XenonBlockingStatCaption").first().contains("(24%)"));

    // The site's own hosts come first, the busiest first.
    auto first_party = labels_named(popover, "XenonBlockingFirstPartyDomain");
    EXPECT_EQ(first_party.size(), 2u);
    EXPECT(first_party[0] == QString("news.example.com"));
    EXPECT(first_party[1] == QString("static.example.com"));
    auto third_party = labels_named(popover, "XenonBlockingDomain");
    EXPECT_EQ(third_party.size(), 2u);
    EXPECT(third_party[0] == QString("cdn.tracker.example"));

    auto tools = popover.findChildren<QToolButton*>("XenonBlockingTool");
    EXPECT(tools.size() == 4);
    for (auto* tool : tools)
        EXPECT(tool->isEnabled());
    EXPECT(tools[2]->isChecked());
    EXPECT(!tools[3]->isChecked());

    save_screenshot(popover, "blocking-popover-light"sv);
    use_color_scheme(popover, Qt::ColorScheme::Dark);
    save_screenshot(popover, "blocking-popover-dark"sv);
    use_color_scheme(popover, Qt::ColorScheme::Light);
}

TEST_CASE(a_site_with_blocking_off)
{
    application();
    BlockingPopover popover(nullptr);
    use_color_scheme(popover, Qt::ColorScheme::Light);
    popover.set_snapshot(snapshot(WebView::ContentBlockingState::SiteDisabled, 0));
    popover.set_blocked_request_total(48'213);
    popover.set_domains(domains());

    auto* power = child<QAbstractButton>(popover, "XenonBlockingPower");
    EXPECT(!power->isChecked());
    EXPECT(power->isEnabled());
    EXPECT(child<QLabel>(popover, "XenonBlockingPowerCaption")->text() == QString("Not blocking on this site"));
    // Nothing blocked, so no share of the requests.
    EXPECT(labels_named(popover, "XenonBlockingStatCaption").first() == QString("blocked on this page"));

    save_screenshot(popover, "blocking-popover-site-off"sv);
}

TEST_CASE(blocking_off_everywhere_disables_the_power_button)
{
    application();
    BlockingPopover popover(nullptr);
    use_color_scheme(popover, Qt::ColorScheme::Light);
    popover.set_snapshot(snapshot(WebView::ContentBlockingState::Disabled, 0));
    EXPECT(!child<QAbstractButton>(popover, "XenonBlockingPower")->isEnabled());
    // Without domains yet, the domain count waits for them.
    EXPECT(stat_values(popover)[2] == QString("–"));
}

TEST_CASE(controls_report_to_their_callbacks)
{
    application();
    BlockingPopover popover(nullptr);
    use_color_scheme(popover, Qt::ColorScheme::Light);
    popover.set_snapshot(snapshot(WebView::ContentBlockingState::Enabled, 3));

    Optional<bool> toggled;
    popover.on_toggle = [&](bool enabled) { toggled = enabled; };
    Vector<BlockingSiteSwitch> switched;
    popover.on_site_switch_toggle = [&](BlockingSiteSwitch site_switch, bool enabled) {
        EXPECT(enabled);
        switched.append(site_switch);
    };
    int picks = 0;
    int zaps = 0;
    int dashboards = 0;
    popover.on_pick_element = [&] { ++picks; };
    popover.on_zap_element = [&] { ++zaps; };
    popover.on_open_dashboard = [&] { ++dashboards; };

    child<QAbstractButton>(popover, "XenonBlockingPower")->click();
    EXPECT(toggled == false);

    auto tools = popover.findChildren<QToolButton*>("XenonBlockingTool");
    tools[0]->click();
    tools[1]->click();
    tools[2]->click();
    tools[3]->click();
    child<QToolButton>(popover, "XenonBlockingDashboard")->click();
    EXPECT_EQ(picks, 1);
    EXPECT_EQ(zaps, 1);
    EXPECT_EQ(dashboards, 1);
    EXPECT_EQ(switched.size(), 2u);
    EXPECT(switched[0] == BlockingSiteSwitch::Popups);
    EXPECT(switched[1] == BlockingSiteSwitch::RemoteFonts);

    // Switches that change because the page did don't report back.
    switched.clear();
    popover.set_site_switch(BlockingSiteSwitch::Popups, false);
    EXPECT(switched.is_empty());
}
