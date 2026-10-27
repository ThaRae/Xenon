/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <UI/BrowserWindow.h>
#include <UI/Tab.h>
#include <UI/TabBar.h>
#include <UI/TabCycler.h>
#include <UI/XenonSettings.h>

#include <QAction>
#include <QApplication>
#include <QKeyEvent>

namespace Ladybird {

// The physical Control key. Qt calls it Meta on macOS, where its Control is the Command key.
#if defined(AK_OS_MACOS)
static constexpr auto CYCLE_MODIFIER = Qt::MetaModifier;
static constexpr auto CYCLE_MODIFIER_KEY = Qt::Key_Meta;
#else
static constexpr auto CYCLE_MODIFIER = Qt::ControlModifier;
static constexpr auto CYCLE_MODIFIER_KEY = Qt::Key_Control;
#endif

TabCycler::TabCycler(BrowserWindow& window, TabWidget& tabs_container)
    : QObject(&window)
    , m_window(window)
{
    auto add_action = [&](char const* text, Qt::KeyboardModifiers modifiers, Order::Direction direction) {
        auto* action = new QAction(text, this);
        action->setShortcut(QKeySequence(QKeyCombination(modifiers, Qt::Key_Tab)));
        QObject::connect(action, &QAction::triggered, this, [this, direction] { step(direction); });
        m_window.addAction(action);
    };
    add_action("Next Recently Used Tab", CYCLE_MODIFIER, Order::Direction::LessRecent);
    add_action("Previous Recently Used Tab", CYCLE_MODIFIER | Qt::ShiftModifier, Order::Direction::MoreRecent);

    QObject::connect(&tabs_container, &TabWidget::current_tab_changed, this, [this] {
        update_order();
        if (auto* tab = m_window.current_tab())
            m_order.did_activate(tab);
    });
}

void TabCycler::update_order()
{
    Vector<QPointer<Tab>> tabs;
    tabs.ensure_capacity(m_window.tab_count());
    for (int index = 0; index < m_window.tab_count(); ++index)
        tabs.unchecked_append(m_window.tab_at(index));
    m_order.update(tabs);
}

void TabCycler::step(Order::Direction direction)
{
    if (!XenonSettings::cycle_tabs_in_recently_used_order()) {
        if (direction == Order::Direction::LessRecent)
            m_window.open_next_tab();
        else
            m_window.open_previous_tab();
        return;
    }

    update_order();
    auto was_cycling = m_order.is_cycling();
    auto tab = m_order.step(direction);
    if (!tab.has_value())
        return;

    // Letting go of Ctrl ends the cycle, as does anything that turns the user's attention elsewhere.
    if (!was_cycling)
        qApp->installEventFilter(this);

    m_window.activate_tab(m_window.tab_index(*tab));
}

void TabCycler::end_cycle()
{
    qApp->removeEventFilter(this);
    update_order();
    m_order.end_cycle();
}

bool TabCycler::eventFilter(QObject* watched, QEvent* event)
{
    switch (event->type()) {
    case QEvent::KeyRelease:
        if (static_cast<QKeyEvent*>(event)->key() == CYCLE_MODIFIER_KEY)
            end_cycle();
        break;
    case QEvent::MouseButtonPress:
    case QEvent::NonClientAreaMouseButtonPress:
        end_cycle();
        break;
    case QEvent::WindowDeactivate:
        if (watched == &m_window)
            end_cycle();
        break;
    default:
        break;
    }
    return QObject::eventFilter(watched, event);
}

}
