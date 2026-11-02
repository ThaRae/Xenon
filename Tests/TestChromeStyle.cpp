/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/NeverDestroyed.h>
#include <LibCore/Environment.h>
#include <LibTest/TestCase.h>
#include <UI/ChromeStyle.h>

#include <QApplication>
#include <QToolButton>
#include <QVBoxLayout>

using namespace Ladybird;

static QApplication& application()
{
    static int argc = 1;
    static char program[] = "TestXenonChromeStyle";
    static char* argv[] = { program, nullptr };
#if !defined(AK_OS_MACOS) && !defined(AK_OS_WINDOWS)
    if (!Core::Environment::has("DISPLAY"sv) && !Core::Environment::has("WAYLAND_DISPLAY"sv))
        qputenv("QT_QPA_PLATFORM", "offscreen");
#endif
    static NeverDestroyed<QApplication> application(argc, argv);
    application->setStyleSheet(ChromeStyle::application_style_sheet(application->palette()));
    return *application;
}

struct VerticalTabsColumn {
    explicit VerticalTabsColumn(QSize new_tab_button_size)
    {
        column.setObjectName("LadybirdVerticalTabBar");
        column.setStyleSheet(ChromeStyle::tab_widget_style_sheet(column.palette()));
        auto* layout = new QVBoxLayout(&column);
        new_tab_button = new QToolButton(&column);
        new_tab_button->setObjectName("LadybirdNewTabButton");
        new_tab_button->setText("New Tab");
        new_tab_button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        new_tab_button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        layout->addWidget(new_tab_button, 0, Qt::AlignLeft);
        layout->addStretch(1);
        column.resize(new_tab_button_size.width() + 10, 300);
        column.show();
        QApplication::processEvents();

        new_tab_button->setFixedSize(new_tab_button_size);
        layout->activate();
    }

    void change_theme()
    {
        for (auto* widget : column.findChildren<QWidget*>()) {
            QEvent theme_change(QEvent::ThemeChange);
            QApplication::sendEvent(widget, &theme_change);
        }
        column.layout()->activate();
        QApplication::processEvents();
    }

    QWidget column;
    QToolButton* new_tab_button { nullptr };
};

TEST_CASE(new_tab_button_keeps_its_size_through_a_theme_change)
{
    application();

    for (auto size : { QSize { 222, 37 }, QSize { 40, 37 } }) {
        VerticalTabsColumn sidebar { size };
        EXPECT_EQ(sidebar.new_tab_button->width(), size.width());
        EXPECT_EQ(sidebar.new_tab_button->height(), size.height());

        sidebar.change_theme();
        EXPECT_EQ(sidebar.new_tab_button->width(), size.width());
        EXPECT_EQ(sidebar.new_tab_button->height(), size.height());
    }
}
