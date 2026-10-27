/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/kmalloc.h>
#include <UI/RecentTabOrder.h>

#include <QObject>
#include <QPointer>

namespace Ladybird {

class BrowserWindow;
class Tab;
class TabWidget;

// Ctrl+Tab and Ctrl+Shift+Tab for one window. They switch tabs in the order the window's tabs were last used (see
// RecentTabOrder), or in the tab strip's order if the user turns that off. Ctrl+Page Down and Ctrl+Page Up always follow
// the tab strip. On macOS, this is the Control key, not Command.
class TabCycler final : public QObject {
    Q_OBJECT

public:
    AK_ALLOC_WITH_KMALLOC;

    TabCycler(BrowserWindow&, TabWidget&);

private:
    // QPointer, so a closed tab is never mistaken for a new one that takes its address.
    using Order = RecentTabOrder<QPointer<Tab>>;

    virtual bool eventFilter(QObject*, QEvent*) override;

    void update_order();
    void step(Order::Direction);
    void end_cycle();

    BrowserWindow& m_window;
    TabWidget& m_tabs_container;
    Order m_order;
};

}
