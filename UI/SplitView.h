/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Function.h>
#include <AK/Optional.h>
#include <AK/Vector.h>
#include <AK/kmalloc.h>
#include <LibWebView/Settings.h>

#include <QPointer>
#include <QWidget>

class QAction;
class QDragEnterEvent;
class QDragLeaveEvent;
class QDragMoveEvent;
class QDropEvent;
class QMenu;
class QPainter;
class QToolButton;
class QTimer;

namespace Ladybird {

class BrowserWindow;
class SplitViewDivider;
class SplitViewDropIndicator;
class SplitViewMiniToolbar;
class Tab;
class TabWidget;

enum class SplitViewSide : u8 {
    Left,
    Right,
};

class TabStack final : public QWidget {
    Q_OBJECT

public:
    AK_ALLOC_WITH_KMALLOC;

    explicit TabStack(TabWidget&);
    virtual ~TabStack() override;

    int count() const { return static_cast<int>(m_pages.size()); }
    QWidget* widget(int index) const;
    int indexOf(QWidget const*) const;
    int currentIndex() const { return m_current_index; }
    QWidget* currentWidget() const { return widget(m_current_index); }
    void setCurrentIndex(int);
    void insertWidget(int index, QWidget*);
    void removeWidget(QWidget*);

    Tab* split_partner(Tab const&) const;
    Optional<SplitViewSide> split_side(Tab const&) const;
    Tab* split_tab(Tab const&, SplitViewSide) const;
    bool is_shown(Tab const&) const;
    void activate(Tab&);

    void add_split(Tab& left, Tab& right);
    Tab* separate(Tab&);
    void update_split_order(Tab&);

    void set_frame(bool chrome_visible, Optional<WebView::VerticalTabsPosition> sidebar_position);

    void resize_shown_split(int divider_center_x);
    void reverse_shown_split();

    void update_drop_indicator(QPoint const& position);
    void hide_drop_indicator();

    QRect left_page_rect() const;
    QRect right_page_rect() const;

signals:
    void splits_changed();

private:
    struct Split {
        QPointer<Tab> left;
        QPointer<Tab> right;
        qreal ratio { 0.5 };
    };

    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;
    virtual bool event(QEvent*) override;
    virtual bool eventFilter(QObject*, QEvent*) override;
    virtual void paintEvent(QPaintEvent*) override;
    virtual void resizeEvent(QResizeEvent*) override;
    virtual void dragEnterEvent(QDragEnterEvent*) override;
    virtual void dragMoveEvent(QDragMoveEvent*) override;
    virtual void dragLeaveEvent(QDragLeaveEvent*) override;
    virtual void dropEvent(QDropEvent*) override;

    Split* split_for(Tab const&);
    Split const* split_for(Tab const&) const;
    Split* shown_split();
    Split const* shown_split() const;
    bool shows_single_page() const;
    QRect split_area() const;
    int divider_x(Split const&) const;
    void layout_pages();
    void update_split_controls();
    void schedule_split_validation();
    void validate_splits();
    void watch_tab(Tab&);
    void watch_view(Tab&);
    void unwatch_tab(Tab&);

    bool handle_tab_drag(QDropEvent&, QPoint const& position);
    Optional<SplitViewSide> drop_side_at(QPoint const& position) const;

    TabWidget& m_tab_widget;
    Vector<QPointer<QWidget>> m_pages;
    int m_current_index { -1 };
    Vector<Split> m_splits;

    bool m_chrome_visible { true };
    Optional<WebView::VerticalTabsPosition> m_sidebar_position;

    SplitViewDivider* m_divider { nullptr };
    SplitViewMiniToolbar* m_mini_toolbar { nullptr };
    SplitViewDropIndicator* m_drop_indicator { nullptr };
    QTimer* m_drop_delay_timer { nullptr };
    Optional<SplitViewSide> m_drop_side;
    QPoint m_drop_position;
    bool m_drop_armed { false };
    bool m_split_validation_scheduled { false };
};

namespace SplitView {

void add_tab_context_menu_actions(Tab&, QMenu&);
void add_link_context_menu_action(Tab&, QMenu&);
QToolButton* create_toolbar_button(Tab&, QWidget& toolbar, int size);
QAction* create_new_split_view_action(BrowserWindow&);

void keep_split_views_together(BrowserWindow&, Tab& moved_tab);

struct PendingDrop {
    QPointer<TabStack> stack;
    SplitViewSide side { SplitViewSide::Right };
};
Optional<PendingDrop> take_pending_drop();
void complete_drop(PendingDrop const&, Tab& dragged_tab);

void paint_tab_frame(QPainter&, QPalette const&, QRectF const& frame, bool is_shown);

}

}
