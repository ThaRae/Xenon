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

// Split view, as Helium (and Chromium) have it: two tabs side by side in one window. The two tabs sit next to each other
// in the tab strip, in the order their pages show, and each keeps its own page and history. The current tab is the
// split's active side, whose toolbar and address bar show, and the other side is the inactive one.
enum class SplitViewSide : u8 {
    Left,
    Right,
};

// The tabs' pages, which TabWidget shows one at a time, like the QStackedWidget it stands in for, or two at a time while
// the current tab is in a split view.
class TabStack final : public QWidget {
    Q_OBJECT

public:
    AK_ALLOC_WITH_KMALLOC;

    explicit TabStack(TabWidget&);
    virtual ~TabStack() override;

    // The parts of QStackedWidget's interface that TabWidget uses.
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
    // Whether the tab's page shows, as the current tab or beside it.
    bool is_shown(Tab const&) const;
    // Makes one of the pages that show the active one.
    void activate(Tab&);

    // The two tabs must already sit next to each other in the tab strip, in this order.
    void add_split(Tab& left, Tab& right);
    // Returns the tab that was on the other side.
    Tab* separate(Tab&);
    // Follows the tab strip, after a move changed the order of a split's tabs.
    void update_split_order(Tab&);

    // Where the window's chrome leaves the page, as TabWidget tells each tab's web view.
    void set_frame(bool chrome_visible, Optional<WebView::VerticalTabsPosition> sidebar_position);

    // The divider sets how much of the width the left page takes.
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

// Adds the split view items to a tab's context menu in the tab strip.
void add_tab_context_menu_actions(Tab&, QMenu&);
// Adds "Open Link in Split View" to a page's link context menu, after its "Open in New Tab".
void add_link_context_menu_action(Tab&, QMenu&);
// The toolbar button that shows which side of a split view is active, with the split's menu. It shows only in a split.
QToolButton* create_toolbar_button(Tab&, QWidget& toolbar, int size);
// "New Split View": the current tab beside a new tab.
QAction* create_new_split_view_action(BrowserWindow&);

// Keeps a split view's tabs together in the tab strip after a tab moves.
void keep_split_views_together(BrowserWindow&, Tab& moved_tab);

// A tab dropped on the edge of a page during a tab drag, which opens beside it once the drag ends.
struct PendingDrop {
    QPointer<TabStack> stack;
    SplitViewSide side { SplitViewSide::Right };
};
Optional<PendingDrop> take_pending_drop();
void complete_drop(PendingDrop const&, Tab& dragged_tab);

// The frame that holds a split view's two tabs together in the tab strip.
void paint_tab_frame(QPainter&, QPalette const&, QRectF const& frame, bool is_shown);

}

}
