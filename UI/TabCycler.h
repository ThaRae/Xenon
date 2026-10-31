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

class TabCycler final : public QObject {
    Q_OBJECT

public:
    AK_ALLOC_WITH_KMALLOC;

    TabCycler(BrowserWindow&, TabWidget&);

private:
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
