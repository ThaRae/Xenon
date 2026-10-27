/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Optional.h>
#include <AK/Vector.h>
#include <AK/kmalloc.h>
#include <LibWebView/SessionStore.h>

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>

class QColor;
class QFont;
class QMenu;
class QPainter;
class QPalette;
class QRectF;
class QSizeF;

namespace Ladybird {

class Tab;
class TabWidget;

// Xenon's group colors, in the order new groups take them. They share the rose accent's lightness and chroma, so no
// group's color stands out from the others.
enum class TabGroupColor : u8 {
    Rose,
    Sky,
    Amber,
    Jade,
    Orchid,
    Ember,
    Iris,
    Moss,
    Slate,
};
static constexpr int TAB_GROUP_COLOR_COUNT = 9;

QString tab_group_color_name(TabGroupColor);
// A group's color, for its header and the line along its tabs. It's lighter on a dark chrome.
QColor tab_group_color(TabGroupColor, QPalette const&);
// The color of the title on a group's header.
QColor tab_group_title_color(TabGroupColor, QPalette const&);

// A group's header shows its title on a chip of the group's color, or the color alone when the group has no title.
QFont tab_group_title_font(QFont);
QSizeF tab_group_chip_size(QString const& title, QFont const& title_font);
void paint_tab_group_chip(QPainter&, QRectF const& chip, QString const& title, TabGroupColor, QPalette const&, QFont const& title_font);
QString tab_group_tooltip(QString const& title, int tab_count);

class TabGroup final : public QObject {
    Q_OBJECT

public:
    AK_ALLOC_WITH_KMALLOC;

    QString const& title() const { return m_title; }
    TabGroupColor color() const { return m_color; }
    bool is_collapsed() const { return m_collapsed; }

private:
    friend class TabGroups;

    TabGroup(QObject* parent, TabGroupColor color)
        : QObject(parent)
        , m_color(color)
    {
    }

    QString m_title;
    TabGroupColor m_color { TabGroupColor::Rose };
    bool m_collapsed { false };
};

// Where a group's tabs are in the strip.
struct TabGroupSpan {
    TabGroup* group { nullptr };
    int first { 0 };
    int last { 0 };
};

// A window's tab groups, as Chromium has them: A group has a title and a color, and collapses to its header. Its tabs
// sit together, after the pinned tabs. The strip's order is the source of truth, and the groups follow it: A tab moved
// between two tabs of a group joins the group, and a tab moved away from its group's other tabs leaves it.
class TabGroups final : public QObject {
    Q_OBJECT

public:
    AK_ALLOC_WITH_KMALLOC;

    explicit TabGroups(TabWidget&);

    TabGroup* group_of(Tab const*) const;
    TabGroup* group_at(int index) const;
    // The groups, in the order they are in the strip.
    Vector<TabGroupSpan> const& spans() const { return m_spans; }
    Optional<TabGroupSpan> span_of(TabGroup const*) const;
    int tab_count(TabGroup const&) const;

    TabGroup& add_to_new_group(Tab&);
    void add_to_group(Tab&, TabGroup&);
    void remove_from_group(Tab&);

    void set_title(TabGroup&, QString const&);
    void set_color(TabGroup&, TabGroupColor);
    void set_collapsed(TabGroup&, bool);
    void ungroup(TabGroup&);
    void close_group(TabGroup&);
    void open_new_tab_in_group(TabGroup&);
    void move_group_to_new_window(TabGroup&);
    bool holds_every_tab(TabGroup const&) const;

    // A dragged tab joins the group its drop showed it in, which is next to it.
    void set_group_after_drop(Tab&, TabGroup*);
    // A tab that a page opens joins the group of the tab that opened it, at the group's end.
    void add_opened_tab(Tab& opener, Tab& opened);
    // Pinned tabs are never in a group.
    void will_pin_tab(Tab&);

    // The tab widget tells the groups about the tabs that come, go, and move, once their pages are in place.
    void did_insert_tab(int index);
    void will_remove_tab(int index);
    void did_remove_tab();
    void did_move_tab(int from, int to);

    // Puts the window's restored tabs back in the groups they were in when the session was saved.
    void restore(WebView::SessionWindowId);

signals:
    // Something about the groups changed that the strip shows.
    void changed();
    void edit_requested(TabGroup*);

private:
    Tab* tab_at(int index) const;
    int index_of(Tab const*) const;
    int first_unpinned_index() const;
    int count() const;
    TabGroupColor next_color() const;
    TabGroup& create_group();
    TabGroup& add_to_new_group_quietly(Tab&);
    void move_tab(int from, int to);
    void did_change_current_tab(int index);
    void keep_groups_together();
    void update_tab_visibility();
    void update();
    void save() const;

    TabWidget& m_tab_widget;
    QHash<Tab const*, TabGroup*> m_groups_of_tabs;
    Vector<TabGroupSpan> m_spans;
    // Set while a change of our own moves tabs, which then don't join or leave groups on their own.
    bool m_is_moving_tabs { false };
    // A restored window gets its tabs before its groups, so nothing is saved for it until it has had a group.
    mutable bool m_has_saved_groups { false };
};

// The tab context menu's items for adding the tab to a group and removing it from its group.
void add_tab_group_actions(QMenu&, Tab&);

}
