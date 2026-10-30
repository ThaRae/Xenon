/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/ByteString.h>
#include <AK/NeverDestroyed.h>
#include <AK/Vector.h>
#include <LibCore/Environment.h>
#include <LibTest/TestCase.h>
#include <UI/Popover.h>
#include <UI/RelaunchToUpdate.h>
#include <UI/UpdateButton.h>
#include <UI/Updater.h>

#include <QApplication>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QResizeEvent>
#include <QStyleHints>

using namespace Ladybird;

namespace Ladybird::Updater {

static Status s_status;
static Vector<Function<bool()>> s_listeners;
static int s_installs = 0;

Status status()
{
    return s_status;
}

void on_status_change(Function<bool()> listener)
{
    s_listeners.append(move(listener));
}

void install_and_relaunch()
{
    ++s_installs;
}

}

namespace Ladybird::RelaunchToUpdate {

static size_t s_private_windows = 0;

size_t private_window_count()
{
    return s_private_windows;
}

}

static void set_status(Optional<ByteString> ready_version)
{
    Updater::s_status.availability = Updater::Availability::Available;
    Updater::s_status.ready_version = move(ready_version);
    Updater::s_listeners.remove_all_matching([](auto& listener) { return !listener(); });
}

static void set_ready_version(StringView version)
{
    set_status(ByteString { version });
}

static void clear_ready_version()
{
    set_status({});
}

static QApplication& application()
{
    static int argc = 1;
    static char program[] = "TestXenonUpdateButton";
    static char* argv[] = { program, nullptr };
#if !defined(AK_OS_MACOS) && !defined(AK_OS_WINDOWS)
    if (!Core::Environment::has("DISPLAY"sv) && !Core::Environment::has("WAYLAND_DISPLAY"sv))
        qputenv("QT_QPA_PLATFORM", "offscreen");
#endif
    static NeverDestroyed<QApplication> application(argc, argv);
    return *application;
}

struct Toolbar {
    Toolbar()
    {
        resize(1000);
    }

    void resize(int width)
    {
        auto old_size = widget.size();
        widget.resize(width, 40);
        QResizeEvent event(widget.size(), old_size);
        QApplication::sendEvent(&widget, &event);
    }

    QWidget widget;
    UpdateButton button { widget, 34 };
};

static QWidget* popover_of(UpdateButton& button)
{
    return button.findChild<QWidget*>("XenonUpdatePopover");
}

static QPushButton* popover_button(UpdateButton& button, char const* name)
{
    auto* popover = popover_of(button);
    return popover ? popover->findChild<QPushButton*>(name) : nullptr;
}

static QString popover_text(UpdateButton& button, char const* name)
{
    auto* popover = popover_of(button);
    auto* label = popover ? popover->findChild<QLabel*>(name) : nullptr;
    return label ? label->text() : QString {};
}

static void save_screenshot(QWidget& widget, StringView name)
{
    auto directory = Core::Environment::get("XENON_SCREENSHOT_DIR"sv);
    if (!directory.has_value())
        return;
    auto path = ByteString::formatted("{}/{}.png", *directory, name);
    EXPECT(widget.grab().save(QString::fromUtf8(path.characters())));
}

TEST_CASE(hidden_until_an_update_is_ready)
{
    application();
    clear_ready_version();
    Toolbar toolbar;
    EXPECT(toolbar.button.isHidden());

    set_ready_version("0.2.0"sv);
    EXPECT(!toolbar.button.isHidden());
    EXPECT(toolbar.button.toolTip() == QString("Xenon 0.2.0 is ready to install"));

    clear_ready_version();
    EXPECT(toolbar.button.isHidden());
}

TEST_CASE(relaunch_to_update_from_the_popover)
{
    application();
    set_ready_version("0.3.0"sv);
    Toolbar toolbar;
    toolbar.button.click();

    auto* popover = popover_of(toolbar.button);
    VERIFY(popover);
    EXPECT(popover->isVisible());
    EXPECT(popover_text(toolbar.button, "LadybirdMessagePopoverTitle") == QString("Xenon 0.3.0 is ready"));
    EXPECT(popover_text(toolbar.button, "LadybirdMessagePopoverBody").contains("your windows and tabs reopen."));
    save_screenshot(static_cast<Popover&>(*popover).card(), "update-popover-light"sv);

    auto installs = Updater::s_installs;
    auto* relaunch = popover_button(toolbar.button, "LadybirdMessagePopoverAcceptButton");
    VERIFY(relaunch);
    EXPECT(relaunch->text() == QString("Relaunch to Update"));
    relaunch->click();
    EXPECT_EQ(Updater::s_installs, installs + 1);
    EXPECT(!popover->isVisible());
    EXPECT(!toolbar.button.isHidden());
}

TEST_CASE(later_hides_every_window_s_button_until_a_newer_update)
{
    application();
    set_ready_version("0.4.0"sv);
    Toolbar first;
    Toolbar second;
    EXPECT(!first.button.isHidden());
    EXPECT(!second.button.isHidden());

    auto installs = Updater::s_installs;
    first.button.click();
    auto* later = popover_button(first.button, "LadybirdMessagePopoverDismissButton");
    VERIFY(later);
    EXPECT(later->text() == QString("Later"));
    later->click();
    EXPECT_EQ(Updater::s_installs, installs);
    EXPECT(!popover_of(first.button)->isVisible());
    EXPECT(first.button.isHidden());
    EXPECT(second.button.isHidden());

    set_ready_version("0.4.0"sv);
    Toolbar third;
    EXPECT(first.button.isHidden());
    EXPECT(third.button.isHidden());

    set_ready_version("0.4.1"sv);
    EXPECT(!first.button.isHidden());
    EXPECT(!second.button.isHidden());
    EXPECT(!third.button.isHidden());
}

TEST_CASE(closing_the_popover_doesn_t_put_the_update_off)
{
    application();
    set_ready_version("0.5.0"sv);
    Toolbar toolbar;
    toolbar.button.click();
    popover_of(toolbar.button)->close();
    EXPECT(!toolbar.button.isHidden());
}

TEST_CASE(the_popover_says_private_windows_don_t_reopen)
{
    application();
    set_ready_version("0.6.0"sv);
    Toolbar toolbar;

    RelaunchToUpdate::s_private_windows = 2;
    toolbar.button.click();
    EXPECT(popover_text(toolbar.button, "LadybirdMessagePopoverBody").contains("but private windows don't"));
    popover_of(toolbar.button)->close();

    RelaunchToUpdate::s_private_windows = 1;
    toolbar.button.click();
    EXPECT(popover_text(toolbar.button, "LadybirdMessagePopoverBody").contains("but your private window doesn't"));
    popover_of(toolbar.button)->close();

    RelaunchToUpdate::s_private_windows = 0;
    toolbar.button.click();
    EXPECT(!popover_text(toolbar.button, "LadybirdMessagePopoverBody").contains("private"));
    popover_of(toolbar.button)->close();
}

TEST_CASE(a_narrow_toolbar_gets_the_icon_alone)
{
    application();
    set_ready_version("0.7.0"sv);
    Toolbar toolbar;
    auto labeled_width = toolbar.button.width();
    EXPECT(labeled_width > 34);
    EXPECT_EQ(toolbar.button.height(), 34);
    save_screenshot(toolbar.button, "update-button"sv);

    toolbar.resize(520);
    EXPECT_EQ(toolbar.button.width(), 34);
    save_screenshot(toolbar.button, "update-button-icon"sv);

    toolbar.resize(1000);
    EXPECT_EQ(toolbar.button.width(), labeled_width);
}

TEST_CASE(the_popover_in_dark_mode)
{
    application();
    set_ready_version("0.8.0"sv);
    application().styleHints()->setColorScheme(Qt::ColorScheme::Dark);
    QApplication::processEvents();
    Toolbar toolbar;
    toolbar.button.click();
    auto* popover = popover_of(toolbar.button);
    VERIFY(popover);
    save_screenshot(static_cast<Popover&>(*popover).card(), "update-popover-dark"sv);
    popover->close();
    application().styleHints()->setColorScheme(Qt::ColorScheme::Light);
}
