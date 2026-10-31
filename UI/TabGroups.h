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
QColor tab_group_color(TabGroupColor, QPalette const&);
QColor tab_group_title_color(TabGroupColor, QPalette const&);

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

struct TabGroupSpan {
    TabGroup* group { nullptr };
    int first { 0 };
    int last { 0 };
};

class TabGroups final : public QObject {
    Q_OBJECT

public:
    AK_ALLOC_WITH_KMALLOC;

    explicit TabGroups(TabWidget&);

    TabGroup* group_of(Tab const*) const;
    TabGroup* group_at(int index) const;
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

    void set_group_after_drop(Tab&, TabGroup*);
    void add_opened_tab(Tab& opener, Tab& opened);
    void will_pin_tab(Tab&);

    void did_insert_tab(int index);
    void will_remove_tab(int index);
    void did_remove_tab();
    void did_move_tab(int from, int to);

    void restore(WebView::SessionWindowId);

signals:
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
    bool m_is_moving_tabs { false };
    mutable bool m_has_saved_groups { false };
};

void add_tab_group_actions(QMenu&, Tab&);

}
