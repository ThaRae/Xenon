/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/TypeCasts.h>
#include <LibURL/URL.h>
#include <LibWebView/Application.h>
#include <UI/BrowserWindow.h>
#include <UI/ChromeStyle.h>
#include <UI/Icon.h>
#include <UI/SplitView.h>
#include <UI/StringUtils.h>
#include <UI/Tab.h>
#include <UI/TabBar.h>

#include <QAction>
#include <QApplication>
#include <QBoxLayout>
#include <QCursor>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QLabel>
#include <QLayout>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QStyleOptionToolButton>
#include <QStylePainter>
#include <QTimer>
#include <QToolButton>
#include <QWindow>

namespace Ladybird {

// The frame around a split view's pages matches the one around a page alone (Tab's WEB_VIEW_INSET), and runs along the
// top as well, so the active page's ring has room all the way around.
static constexpr int SPLIT_VIEW_INSET = 5;
static constexpr int SPLIT_VIEW_SIDEBAR_INSET = 4;
// The divider sits in the gap between the pages, with each page's ring on either side of its grip.
static constexpr int SPLIT_VIEW_GAP = 12;
static constexpr qreal SPLIT_VIEW_RING_WIDTH = 2.0;
static constexpr qreal SPLIT_VIEW_RING_OFFSET = 2.0;
static constexpr int SPLIT_VIEW_MIN_PAGE_WIDTH = 240;
// The divider snaps to the middle from this close, like Chromium's.
static constexpr int SPLIT_VIEW_SNAP_DISTANCE = 15;
static constexpr int SPLIT_VIEW_GRIP_WIDTH = 4;
static constexpr int SPLIT_VIEW_GRIP_HEIGHT = 36;

// Helium opens a split view once a dragged tab has rested on a page's edge for half a second.
static constexpr int SPLIT_VIEW_DROP_DELAY_MS = 500;
static constexpr qreal SPLIT_VIEW_DROP_EDGE_FRACTION = 0.2;
static constexpr int SPLIT_VIEW_DROP_EDGE_MIN_WIDTH = 64;

static constexpr int SPLIT_VIEW_MINI_TOOLBAR_HEIGHT = 26;
static constexpr int SPLIT_VIEW_MINI_TOOLBAR_MAX_LABEL_WIDTH = 220;

static constexpr auto LADYBIRD_TAB_MIME_TYPE = "application/x-ladybird-tab";
static constexpr auto SPLIT_VIEW_BUTTON_NAME = "XenonSplitViewButton";

static SplitViewSide opposite(SplitViewSide side)
{
    return side == SplitViewSide::Left ? SplitViewSide::Right : SplitViewSide::Left;
}

static BrowserWindow* browser_window_of(QWidget const& widget)
{
    return qobject_cast<BrowserWindow*>(widget.window());
}

// Native overlays stay above the web views, which are native themselves on these presentation paths.
static void make_overlay_native([[maybe_unused]] QWidget& widget)
{
#if defined(AK_OS_MACOS) || defined(LADYBIRD_QT_USE_RHI_WIDGET)
    widget.setAttribute(Qt::WA_DontCreateNativeAncestors);
    widget.setAttribute(Qt::WA_NativeWindow);
#endif
}

// Qt leaves a widget's native window behind when it lays the widget out while it is hidden, as a page's bookmarks bar is
// while its page is the inactive one. Lays the page out now, and moves its native windows to where their widgets are.
static void sync_native_windows(QWidget& page)
{
    if (auto* layout = page.layout())
        layout->activate();
    for (auto* child : page.findChildren<QWidget*>()) {
        if (child->isWindow() || child->isHidden() || !child->testAttribute(Qt::WA_NativeWindow))
            continue;
        auto* native_parent = child->nativeParentWidget();
        if (auto* window = child->windowHandle(); window && native_parent)
            window->setGeometry(QRect(child->mapTo(native_parent, QPoint()), child->size()));
    }
}

static QIcon create_icon(Function<void(QPainter&)> const& paint)
{
    QIcon icon;
    for (auto device_pixel_ratio : ICON_DEVICE_PIXEL_RATIOS) {
        QPixmap pixmap(physical_size_for_device_pixel_ratio({ CHROME_ICON_CANVAS_SIZE, CHROME_ICON_CANVAS_SIZE }, device_pixel_ratio));
        pixmap.setDevicePixelRatio(device_pixel_ratio);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        paint(painter);
        icon.addPixmap(pixmap);
    }
    return icon;
}

// Two panes, with the active one filled in the accent color.
static QIcon create_split_view_icon(QPalette const& palette, SplitViewSide active_side)
{
    return create_icon([&](QPainter& painter) {
        QRectF frame { 3.5, 5.0, 13.0, 10.0 };
        auto pane = active_side == SplitViewSide::Left ? QRectF(frame.left(), frame.top(), 6.5, frame.height()) : QRectF(frame.center().x(), frame.top(), 6.5, frame.height());
        QPainterPath frame_path;
        frame_path.addRoundedRect(frame, 2.2, 2.2);
        painter.save();
        painter.setClipPath(frame_path);
        painter.fillRect(pane, ChromeStyle::chrome_accent());
        painter.restore();

        painter.setPen(QPen(ChromeStyle::chrome_button_text(palette), 1.5));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(frame_path);
        painter.drawLine(QPointF(frame.center().x(), frame.top()), QPointF(frame.center().x(), frame.bottom()));
    });
}

static QIcon create_more_icon(QPalette const& palette)
{
    return create_icon([&](QPainter& painter) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(ChromeStyle::chrome_button_text(palette));
        for (auto x : { 5.5, 10.0, 14.5 })
            painter.drawEllipse(QPointF(x, 10.0), 1.4, 1.4);
    });
}

// Moves a tab next to another one, on the given side of it, in the same group of pinned or unpinned tabs.
static void place_beside(BrowserWindow& window, Tab& anchor, Tab& tab, SplitViewSide side)
{
    if (tab.is_pinned() != anchor.is_pinned()) {
        if (anchor.is_pinned())
            window.pin_tab(tab);
        else
            window.unpin_tab(tab);
    }

    auto anchor_index = window.tab_index(&anchor);
    auto index = window.tab_index(&tab);
    if (anchor_index < 0 || index < 0)
        return;

    auto destination = side == SplitViewSide::Right ? anchor_index + 1 : anchor_index;
    if (index < anchor_index)
        --destination;
    if (index != destination)
        window.move_tab(index, destination);
}

static void create_split(BrowserWindow& window, Tab& anchor, Tab& other, SplitViewSide other_side, Tab& tab_to_activate)
{
    auto& stack = window.tab_stack();
    stack.separate(anchor);
    stack.separate(other);

    place_beside(window, anchor, other, other_side);
    if (other_side == SplitViewSide::Right)
        stack.add_split(anchor, other);
    else
        stack.add_split(other, anchor);

    window.activate_tab(window.tab_index(&tab_to_activate));
}

// Chromium's "Add Tab to New Split View": the tab beside a new tab, which waits for an address like a new tab does.
static void open_new_tab_beside(BrowserWindow& window, Tab& tab, SplitViewSide new_tab_side)
{
    auto& new_tab = window.new_tab_from_url(WebView::Application::settings().new_tab_page_url(), Web::HTML::ActivateTab::No, BrowserWindow::TabLocation::after_tab(tab));
    new_tab.set_url_is_hidden(true);
    create_split(window, tab, new_tab, new_tab_side, new_tab);
    new_tab.focus_location_editor();
}

static void reverse_split(BrowserWindow& window, Tab& tab)
{
    auto& stack = window.tab_stack();
    auto* left = stack.split_tab(tab, SplitViewSide::Left);
    auto* right = stack.split_tab(tab, SplitViewSide::Right);
    if (!left || !right)
        return;

    // Moving the tabs in the strip moves their pages, through keep_split_views_together().
    window.move_tab(window.tab_index(right), window.tab_index(left));
}

static void close_split_tab(BrowserWindow& window, Tab& tab, SplitViewSide side)
{
    if (auto* closing = window.tab_stack().split_tab(tab, side))
        closing->request_close();
}

static void add_split_menu_actions(QMenu& menu, Tab& tab)
{
    auto* window = browser_window_of(tab);
    if (!window || !window->tab_stack().split_partner(tab))
        return;

    QPointer<Tab> tab_pointer = &tab;
    QPointer<BrowserWindow> window_pointer = window;
    auto with_tab = [tab_pointer, window_pointer](auto callback) {
        return [tab_pointer, window_pointer, callback] {
            if (tab_pointer && window_pointer)
                callback(*window_pointer, *tab_pointer);
        };
    };

    QObject::connect(menu.addAction("&Reverse Views"), &QAction::triggered, &menu, with_tab([](BrowserWindow& window, Tab& tab) {
        reverse_split(window, tab);
    }));
    QObject::connect(menu.addAction("Close &Left View"), &QAction::triggered, &menu, with_tab([](BrowserWindow& window, Tab& tab) {
        close_split_tab(window, tab, SplitViewSide::Left);
    }));
    QObject::connect(menu.addAction("Close R&ight View"), &QAction::triggered, &menu, with_tab([](BrowserWindow& window, Tab& tab) {
        close_split_tab(window, tab, SplitViewSide::Right);
    }));
    menu.addSeparator();
    QObject::connect(menu.addAction("&Separate Views"), &QAction::triggered, &menu, with_tab([](BrowserWindow& window, Tab& tab) {
        window.tab_stack().separate(tab);
    }));
}

// The handle between a split view's pages: Drag it to share the width differently, or double-click it to swap the
// pages, as in Chromium.
class SplitViewDivider final : public QWidget {
public:
    AK_ALLOC_WITH_KMALLOC;

    explicit SplitViewDivider(TabStack& stack)
        : QWidget(&stack)
        , m_stack(stack)
    {
        setCursor(Qt::SplitHCursor);
        setAttribute(Qt::WA_Hover);
        setToolTip("Drag to resize, or double-click to reverse views");
        hide();
    }

private:
    virtual void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        auto color = ChromeStyle::chrome_accent();
        if (!m_dragging && !underMouse()) {
            color = ChromeStyle::chrome_muted_text(window()->palette());
            color.setAlpha(110);
        }

        QRectF grip { (width() - SPLIT_VIEW_GRIP_WIDTH) / 2.0, (height() - SPLIT_VIEW_GRIP_HEIGHT) / 2.0, SPLIT_VIEW_GRIP_WIDTH, SPLIT_VIEW_GRIP_HEIGHT };
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawRoundedRect(grip, SPLIT_VIEW_GRIP_WIDTH / 2.0, SPLIT_VIEW_GRIP_WIDTH / 2.0);
    }

    virtual bool event(QEvent* event) override
    {
        if (event->type() == QEvent::HoverEnter || event->type() == QEvent::HoverLeave)
            update();
        return QWidget::event(event);
    }

    virtual void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton)
            return;
        m_dragging = true;
        m_grab_offset = event->position().x() - (width() / 2.0);
        update();
    }

    virtual void mouseMoveEvent(QMouseEvent* event) override
    {
        if (!m_dragging)
            return;
        auto center = mapToParent(event->position()).x() - m_grab_offset;
        m_stack.resize_shown_split(qRound(center));
    }

    virtual void mouseReleaseEvent(QMouseEvent*) override
    {
        m_dragging = false;
        update();
    }

    virtual void mouseDoubleClickEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton)
            m_stack.reverse_shown_split();
    }

    TabStack& m_stack;
    bool m_dragging { false };
    qreal m_grab_offset { 0 };
};

// The inactive page's corner shows which page it is, and holds the split view's menu for it, as Chromium's mini
// toolbar does. A click on it makes that page the active one.
class SplitViewMiniToolbar final : public QWidget {
public:
    AK_ALLOC_WITH_KMALLOC;

    explicit SplitViewMiniToolbar(TabStack& stack)
        : QWidget(&stack)
        , m_stack(stack)
    {
        setObjectName("XenonSplitViewMiniToolbar");
        make_overlay_native(*this);
        setAttribute(Qt::WA_OpaquePaintEvent);
        setCursor(Qt::PointingHandCursor);
        setFixedHeight(SPLIT_VIEW_MINI_TOOLBAR_HEIGHT);

        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(9, 0, 3, 0);
        layout->setSpacing(6);

        m_icon = new QLabel(this);
        m_icon->setFixedSize(TAB_ICON_SIZE, TAB_ICON_SIZE);
        m_label = new QLabel(this);
        m_menu_button = new QToolButton(this);
        m_menu_button->setAutoRaise(true);
        m_menu_button->setFocusPolicy(Qt::NoFocus);
        m_menu_button->setFixedSize(22, 22);
        m_menu_button->setIconSize({ 18, 18 });
        m_menu_button->setCursor(Qt::ArrowCursor);
        m_menu_button->setToolTip("Split view");
        QObject::connect(m_menu_button, &QToolButton::clicked, this, [this] {
            if (!m_tab)
                return;
            QMenu menu(this);
            QPointer<Tab> tab = m_tab;
            QObject::connect(menu.addAction("&Close This View"), &QAction::triggered, &menu, [tab] {
                if (tab)
                    tab->request_close();
            });
            menu.addSeparator();
            add_split_menu_actions(menu, *m_tab);
            menu.exec(m_menu_button->mapToGlobal(QPoint(0, m_menu_button->height())));
        });

        layout->addWidget(m_icon);
        layout->addWidget(m_label, 1);
        layout->addWidget(m_menu_button);
        hide();
    }

    void set_tab(Tab* tab)
    {
        if (tab != m_tab) {
            if (m_tab)
                QObject::disconnect(m_tab, nullptr, this, nullptr);
            m_tab = tab;
            if (m_tab) {
                QObject::connect(m_tab, &Tab::title_changed, this, [this] { refresh(); });
                QObject::connect(m_tab, &Tab::favicon_changed, this, [this] { refresh(); });
                QObject::connect(m_tab, &Tab::loading_changed, this, [this] { refresh(); });
            }
        }
        refresh();
    }

    void refresh()
    {
        if (!m_tab)
            return;

        // Like Chromium's, the label names the page's site, or the page itself where it has none.
        auto url = m_tab->url();
        auto host = url.host().has_value() ? url.serialized_host() : StringView {};
        m_label->setText(host.is_empty() ? m_tab->title() : qstring_from_ak_string(host));
        m_label->setToolTip(m_tab->title());
        auto icon = m_tab->favicon();
        if (icon.isNull())
            icon = create_chrome_icon(ChromeIcon::Globe, palette());
        m_icon->setPixmap(icon.pixmap(TAB_ICON_SIZE, TAB_ICON_SIZE));

        auto const& palette = window()->palette();
        m_label->setStyleSheet(QString("color: %1;").arg(ChromeStyle::style_sheet_color(ChromeStyle::chrome_text(palette))));
        m_menu_button->setIcon(create_more_icon(palette));

        auto label_width = min(SPLIT_VIEW_MINI_TOOLBAR_MAX_LABEL_WIDTH, m_label->fontMetrics().horizontalAdvance(m_label->text()));
        m_label->setText(m_label->fontMetrics().elidedText(m_label->text(), Qt::ElideRight, label_width));
        auto margins = layout()->contentsMargins();
        setFixedWidth(margins.left() + TAB_ICON_SIZE + 6 + label_width + 6 + m_menu_button->width() + margins.right());
        if (auto* stack = qobject_cast<TabStack*>(parentWidget()))
            stack->update();
    }

private:
    virtual void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        auto const& palette = window()->palette();
        painter.fillRect(rect(), ChromeStyle::chrome_background(palette));
    }

    virtual void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton && m_tab)
            m_stack.activate(*m_tab);
    }

    TabStack& m_stack;
    QPointer<Tab> m_tab;
    QLabel* m_icon { nullptr };
    QLabel* m_label { nullptr };
    QToolButton* m_menu_button { nullptr };
};

// Where a tab dropped on a page's edge would go.
class SplitViewDropIndicator final : public QWidget {
public:
    AK_ALLOC_WITH_KMALLOC;

    explicit SplitViewDropIndicator(QWidget& parent)
        : QWidget(&parent)
    {
        make_overlay_native(*this);
        setAttribute(Qt::WA_OpaquePaintEvent);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        hide();
    }

private:
    virtual void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        auto const& palette = window()->palette();
        painter.fillRect(rect(), ChromeStyle::chrome_background(palette));

        auto accent = ChromeStyle::chrome_accent();
        auto surface = ChromeStyle::mix(ChromeStyle::chrome_surface(palette), accent, ChromeStyle::is_dark(palette) ? 0.16 : 0.08);
        QRectF card = QRectF(rect()).adjusted(SPLIT_VIEW_RING_OFFSET, SPLIT_VIEW_RING_OFFSET, -SPLIT_VIEW_RING_OFFSET, -SPLIT_VIEW_RING_OFFSET);
        painter.setBrush(surface);
        QPen border(accent, SPLIT_VIEW_RING_WIDTH, Qt::DashLine, Qt::RoundCap);
        border.setDashPattern({ 3, 3 });
        painter.setPen(border);
        painter.drawRoundedRect(card, 10, 10);

        auto center = card.center();
        painter.setPen(QPen(accent, 2.4, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(center + QPointF(-9, -14), center + QPointF(9, -14));
        painter.drawLine(center + QPointF(0, -23), center + QPointF(0, -5));

        auto font = painter.font();
        font.setWeight(QFont::DemiBold);
        painter.setFont(font);
        painter.setPen(ChromeStyle::chrome_text(palette));
        painter.drawText(QRectF(card.left(), center.y() + 2, card.width(), 24), Qt::AlignCenter, "Create split view");
    }
};

// A toolbar button in the style of Tab's own.
class SplitViewButton final : public QToolButton {
public:
    AK_ALLOC_WITH_KMALLOC;

    SplitViewButton(Tab& tab, QWidget& parent)
        : QToolButton(&parent)
        , m_tab(&tab)
    {
        setObjectName(SPLIT_VIEW_BUTTON_NAME);
        setAutoRaise(true);
        setFocusPolicy(Qt::NoFocus);
        setIconSize({ 20, 20 });
        setPopupMode(QToolButton::InstantPopup);

        auto* menu = new QMenu(this);
        QObject::connect(menu, &QMenu::aboutToShow, this, [this, menu] {
            menu->clear();
            if (m_tab)
                add_split_menu_actions(*menu, *m_tab);
        });
        setMenu(menu);
        hide();
    }

    void set_side(Optional<SplitViewSide> side)
    {
        m_side = side;
        setVisible(side.has_value());
        if (!side.has_value())
            return;
        setIcon(create_split_view_icon(palette(), *side));
        setToolTip(side == SplitViewSide::Left ? "Split view: left view active" : "Split view: right view active");
    }

private:
    virtual bool event(QEvent* event) override
    {
        if (event->type() == QEvent::PaletteChange && m_side.has_value())
            setIcon(create_split_view_icon(palette(), *m_side));
        return QToolButton::event(event);
    }

    virtual void paintEvent(QPaintEvent*) override
    {
        QStyleOptionToolButton option;
        initStyleOption(&option);
        // The button opens its menu on a click, so it needs no menu arrow.
        option.features &= ~QStyleOptionToolButton::HasMenu;

        QStylePainter painter(this);
        ChromeStyle::paint_circular_control_frame(painter, *this);
        painter.drawComplexControl(QStyle::CC_ToolButton, option);
    }

    QPointer<Tab> m_tab;
    Optional<SplitViewSide> m_side;
};

static SplitViewButton* split_view_button_of(Tab& tab)
{
    return static_cast<SplitViewButton*>(tab.toolbar_container()->findChild<QToolButton*>(SPLIT_VIEW_BUTTON_NAME));
}

TabStack::TabStack(TabWidget& tab_widget)
    : QWidget(&tab_widget)
    , m_tab_widget(tab_widget)
{
    setAcceptDrops(true);

    m_divider = new SplitViewDivider(*this);
    m_mini_toolbar = new SplitViewMiniToolbar(*this);
    m_drop_indicator = new SplitViewDropIndicator(*this);

    m_drop_delay_timer = new QTimer(this);
    m_drop_delay_timer->setSingleShot(true);
    m_drop_delay_timer->setInterval(SPLIT_VIEW_DROP_DELAY_MS);
    QObject::connect(m_drop_delay_timer, &QTimer::timeout, this, [this] {
        m_drop_armed = m_drop_side.has_value();
        update_drop_indicator(m_drop_position);
    });

    // TabWidget reorders the pages after a move, so check the splits once it has.
    QObject::connect(m_tab_widget.tab_bar(), &QTabBar::tabMoved, this, [this] { schedule_split_validation(); }, Qt::QueuedConnection);
}

TabStack::~TabStack() = default;

QWidget* TabStack::widget(int index) const
{
    if (index < 0 || index >= count())
        return nullptr;
    return m_pages[index].data();
}

int TabStack::indexOf(QWidget const* widget) const
{
    for (int i = 0; i < count(); ++i) {
        if (m_pages[i].data() == widget)
            return i;
    }
    return -1;
}

void TabStack::setCurrentIndex(int index)
{
    if (index < 0 || index >= count())
        return;
    m_current_index = index;
    layout_pages();
}

void TabStack::insertWidget(int index, QWidget* widget)
{
    if (!widget || indexOf(widget) != -1)
        return;

    index = clamp(index, 0, count());
    widget->setParent(this);
    m_pages.insert(index, widget);
    if (m_current_index < 0)
        m_current_index = index;
    else if (index <= m_current_index)
        ++m_current_index;

    if (auto* tab = as_if<Tab>(widget))
        watch_tab(*tab);

    layout_pages();
    updateGeometry();
    schedule_split_validation();
}

void TabStack::removeWidget(QWidget* widget)
{
    auto index = indexOf(widget);
    if (index < 0)
        return;

    if (auto* tab = as_if<Tab>(widget))
        unwatch_tab(*tab);

    m_pages.remove(index);
    if (index < m_current_index)
        --m_current_index;
    else if (index == m_current_index)
        m_current_index = min(index, count() - 1);
    widget->hide();

    layout_pages();
    updateGeometry();
    schedule_split_validation();
}

TabStack::Split* TabStack::split_for(Tab const& tab)
{
    for (auto& split : m_splits) {
        if (split.left == &tab || split.right == &tab)
            return &split;
    }
    return nullptr;
}

TabStack::Split const* TabStack::split_for(Tab const& tab) const
{
    return const_cast<TabStack&>(*this).split_for(tab);
}

Tab* TabStack::split_partner(Tab const& tab) const
{
    auto const* split = split_for(tab);
    if (!split)
        return nullptr;
    return split->left == &tab ? split->right.data() : split->left.data();
}

Optional<SplitViewSide> TabStack::split_side(Tab const& tab) const
{
    auto const* split = split_for(tab);
    if (!split)
        return {};
    return split->left == &tab ? SplitViewSide::Left : SplitViewSide::Right;
}

Tab* TabStack::split_tab(Tab const& tab, SplitViewSide side) const
{
    auto const* split = split_for(tab);
    if (!split)
        return nullptr;
    return side == SplitViewSide::Left ? split->left.data() : split->right.data();
}

TabStack::Split* TabStack::shown_split()
{
    auto* current = as_if<Tab>(currentWidget());
    if (!current)
        return nullptr;
    auto* split = split_for(*current);
    if (!split || !split->left || !split->right || indexOf(split->left) < 0 || indexOf(split->right) < 0)
        return nullptr;
    return split;
}

TabStack::Split const* TabStack::shown_split() const
{
    return const_cast<TabStack&>(*this).shown_split();
}

// A page in fullscreen fills the screen, even from a split view. The window hides its chrome for it.
bool TabStack::shows_single_page() const
{
    return !m_chrome_visible && window()->isFullScreen();
}

bool TabStack::is_shown(Tab const& tab) const
{
    if (currentWidget() == &tab)
        return true;
    if (shows_single_page())
        return false;
    auto const* split = shown_split();
    return split && (split->left == &tab || split->right == &tab);
}

void TabStack::activate(Tab& tab)
{
    if (indexOf(&tab) >= 0 && currentWidget() != &tab)
        m_tab_widget.set_current_tab(&tab);
}

void TabStack::add_split(Tab& left, Tab& right)
{
    if (&left == &right || split_for(left) || split_for(right))
        return;
    m_splits.append({ &left, &right, 0.5 });
    layout_pages();
    update_split_controls();
}

Tab* TabStack::separate(Tab& tab)
{
    auto* partner = split_partner(tab);
    m_splits.remove_all_matching([&](auto const& split) {
        return split.left == &tab || split.right == &tab;
    });
    if (!partner)
        return nullptr;

    layout_pages();
    update_split_controls();
    return partner;
}

void TabStack::update_split_order(Tab& tab)
{
    auto* split = split_for(tab);
    if (!split || !split->left || !split->right)
        return;
    if (indexOf(split->left) <= indexOf(split->right))
        return;

    // The pages swap places, each keeping its width.
    split->left.swap(split->right);
    split->ratio = 1.0 - split->ratio;
    layout_pages();
    update_split_controls();
}

void TabStack::set_frame(bool chrome_visible, Optional<WebView::VerticalTabsPosition> sidebar_position)
{
    m_chrome_visible = chrome_visible;
    m_sidebar_position = sidebar_position;
    layout_pages();
    // NB: A page leaving fullscreen is still in it while the window shows its chrome again.
    QTimer::singleShot(0, this, [this] { layout_pages(); });
}

QRect TabStack::split_area() const
{
    if (!m_chrome_visible)
        return rect();

    auto left = m_sidebar_position == WebView::VerticalTabsPosition::Left ? SPLIT_VIEW_SIDEBAR_INSET : SPLIT_VIEW_INSET;
    auto right = m_sidebar_position == WebView::VerticalTabsPosition::Right ? SPLIT_VIEW_SIDEBAR_INSET : SPLIT_VIEW_INSET;
    return rect().adjusted(left, SPLIT_VIEW_INSET, -right, -SPLIT_VIEW_INSET);
}

static int minimum_page_width(int available_width)
{
    return min(SPLIT_VIEW_MIN_PAGE_WIDTH, available_width / 4);
}

int TabStack::divider_x(Split const& split) const
{
    auto area = split_area();
    auto available_width = max(0, area.width() - SPLIT_VIEW_GAP);
    auto minimum_width = minimum_page_width(available_width);
    auto left_width = clamp(qRound(available_width * split.ratio), minimum_width, max(minimum_width, available_width - minimum_width));
    return area.left() + left_width + (SPLIT_VIEW_GAP / 2);
}

QRect TabStack::left_page_rect() const
{
    auto const* split = shown_split();
    if (!split)
        return {};
    auto area = split_area();
    auto right = divider_x(*split) - (SPLIT_VIEW_GAP / 2);
    return QRect(area.left(), area.top(), right - area.left(), area.height());
}

QRect TabStack::right_page_rect() const
{
    auto const* split = shown_split();
    if (!split)
        return {};
    auto area = split_area();
    auto left = divider_x(*split) + (SPLIT_VIEW_GAP / 2);
    return QRect(left, area.top(), area.right() + 1 - left, area.height());
}

void TabStack::resize_shown_split(int divider_center_x)
{
    auto* split = shown_split();
    if (!split)
        return;

    auto area = split_area();
    auto available_width = max(1, area.width() - SPLIT_VIEW_GAP);
    auto left_width = divider_center_x - (SPLIT_VIEW_GAP / 2) - area.left();
    if (abs(left_width - (available_width / 2)) <= SPLIT_VIEW_SNAP_DISTANCE)
        left_width = available_width / 2;

    auto minimum_width = minimum_page_width(available_width);
    left_width = clamp(left_width, minimum_width, max(minimum_width, available_width - minimum_width));
    split->ratio = static_cast<qreal>(left_width) / available_width;
    layout_pages();
}

void TabStack::reverse_shown_split()
{
    auto* split = shown_split();
    auto* window = browser_window_of(*this);
    if (split && window && split->left)
        reverse_split(*window, *split->left);
}

void TabStack::layout_pages()
{
    auto* current = currentWidget();
    auto* split = shows_single_page() ? nullptr : shown_split();

    // Show the pages that are shown before hiding the others, so the window is never without a page.
    if (split) {
        split->left->ensure_loaded();
        split->right->ensure_loaded();
        split->left->set_in_split_view(true, current == split->left);
        split->right->set_in_split_view(true, current == split->right);

        // A bookmarks bar at the top of the page (in the compact layout) shows above the active page alone, and the
        // inactive page starts below it, level with the active one.
        auto* active = current == split->left ? split->left.data() : split->right.data();
        auto bookmarks_bar_height = 0;
        if (active->has_bookmarks_bar_in_page() && WebView::Application::settings().appearance().show_bookmarks_bar == WebView::ShowBookmarksBar::Always)
            bookmarks_bar_height = active->bookmarks_bar().sizeHint().height();
        auto page_rect = [&](Tab* tab, QRect rect) {
            return tab == active ? rect : rect.adjusted(0, bookmarks_bar_height, 0, 0);
        };

        split->left->setGeometry(page_rect(split->left, left_page_rect()));
        split->right->setGeometry(page_rect(split->right, right_page_rect()));
        split->left->show();
        split->right->show();
        sync_native_windows(*split->left);
        sync_native_windows(*split->right);
    } else if (current) {
        if (auto* tab = as_if<Tab>(current))
            tab->set_in_split_view(false);
        current->setGeometry(rect());
        current->show();
    }

    for (auto& page : m_pages) {
        if (!page || page == current || (split && (page == split->left || page == split->right)))
            continue;
        if (auto* tab = as_if<Tab>(page.data()))
            tab->set_in_split_view(false);
        page->hide();
    }

    if (split) {
        auto divider_center = divider_x(*split);
        auto area = split_area();
        m_divider->setGeometry(divider_center - (SPLIT_VIEW_GAP / 2), area.top(), SPLIT_VIEW_GAP, area.height());
        m_divider->show();
        m_divider->raise();

        auto* inactive = current == split->left ? split->right.data() : split->left.data();
        m_mini_toolbar->set_tab(inactive);
        auto inactive_rect = inactive->geometry();
        m_mini_toolbar->move(inactive_rect.right() + 1 - m_mini_toolbar->width(), inactive_rect.bottom() + 1 - m_mini_toolbar->height());
        m_mini_toolbar->show();
        m_mini_toolbar->raise();
    } else {
        m_divider->hide();
        m_mini_toolbar->set_tab(nullptr);
        m_mini_toolbar->hide();
    }

    update();
}

void TabStack::update_split_controls()
{
    for (auto& page : m_pages) {
        auto* tab = as_if<Tab>(page.data());
        if (!tab || !tab->is_loaded())
            continue;
        if (auto* button = split_view_button_of(*tab))
            button->set_side(split_side(*tab));
    }

    m_tab_widget.tab_bar()->update();
    emit splits_changed();
}

void TabStack::schedule_split_validation()
{
    if (m_split_validation_scheduled)
        return;
    m_split_validation_scheduled = true;
    QTimer::singleShot(0, this, [this] {
        m_split_validation_scheduled = false;
        validate_splits();
    });
}

// A split view's tabs stay next to each other in the strip, and in the same group of pinned or unpinned tabs. A split
// whose tabs no longer are, after a pin or another window took one, comes apart.
void TabStack::validate_splits()
{
    auto changed = false;
    m_splits.remove_all_matching([&](auto const& split) {
        auto left_index = split.left ? indexOf(split.left) : -1;
        auto right_index = split.right ? indexOf(split.right) : -1;
        auto valid = left_index >= 0 && right_index >= 0 && abs(left_index - right_index) == 1 && split.left->is_pinned() == split.right->is_pinned();
        changed |= !valid;
        return !valid;
    });

    for (auto& split : m_splits) {
        if (indexOf(split.left) > indexOf(split.right)) {
            split.left.swap(split.right);
            split.ratio = 1.0 - split.ratio;
            changed = true;
        }
    }

    if (changed) {
        layout_pages();
        update_split_controls();
    }
}

void TabStack::watch_tab(Tab& tab)
{
    // A tab that hasn't loaded, or that hibernated, gets a new view and toolbar once it loads.
    QObject::connect(&tab, &Tab::loaded, this, [this, tab = &tab] { watch_view(*tab); });
    if (tab.is_loaded())
        watch_view(tab);
}

void TabStack::watch_view(Tab& tab)
{
    tab.view().installEventFilter(this);
    if (auto* button = split_view_button_of(tab))
        button->set_side(split_side(tab));
}

void TabStack::unwatch_tab(Tab& tab)
{
    QObject::disconnect(&tab, &Tab::loaded, this, nullptr);
    if (tab.is_loaded())
        tab.view().removeEventFilter(this);
}

bool TabStack::event(QEvent* event)
{
    if (event->type() == QEvent::ChildRemoved) {
        // A page deleted without being removed first leaves its place.
        auto current = currentWidget();
        m_pages.remove_all_matching([](auto const& page) { return !page; });
        if (current)
            m_current_index = indexOf(current);
        else
            m_current_index = min(m_current_index, count() - 1);
    }
    return QWidget::event(event);
}

QSize TabStack::sizeHint() const
{
    QSize size;
    for (auto const& page : m_pages) {
        if (page)
            size = size.expandedTo(page->sizeHint());
    }
    return size;
}

QSize TabStack::minimumSizeHint() const
{
    QSize size;
    for (auto const& page : m_pages) {
        if (page)
            size = size.expandedTo(page->minimumSizeHint().expandedTo(page->minimumSize()));
    }
    return size;
}

static Tab* tab_for_view(QObject* object)
{
    for (auto* widget = qobject_cast<QWidget*>(object); widget; widget = widget->parentWidget()) {
        if (auto* tab = as_if<Tab>(widget))
            return tab;
    }
    return nullptr;
}

bool TabStack::eventFilter(QObject* watched, QEvent* event)
{
    switch (event->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::FocusIn:
        // A click on the inactive page makes it the active one, and still reaches the page. Scrolling it does not.
        if (auto* tab = tab_for_view(watched); tab && tab != currentWidget() && is_shown(*tab))
            activate(*tab);
        break;
    case QEvent::DragEnter:
    case QEvent::DragMove:
    case QEvent::Drop: {
        auto& drop_event = static_cast<QDropEvent&>(*event);
        auto position = mapFrom(static_cast<QWidget*>(watched), drop_event.position().toPoint());
        if (handle_tab_drag(drop_event, position))
            return true;
        break;
    }
    case QEvent::DragLeave:
        if (tab_being_dragged() && !rect().contains(mapFromGlobal(QCursor::pos())))
            hide_drop_indicator();
        break;
    default:
        break;
    }
    return QWidget::eventFilter(watched, event);
}

void TabStack::paintEvent(QPaintEvent*)
{
    auto const* split = shows_single_page() ? nullptr : shown_split();
    if (!split)
        return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    auto const& palette = window()->palette();
    painter.fillRect(rect(), ChromeStyle::chrome_background(palette));

    // The active page has the accent's ring, as Chromium's has a thicker border, and the inactive page a hairline.
    for (auto* tab : { split->left.data(), split->right.data() }) {
        auto is_active = tab == currentWidget();
        auto offset = is_active ? SPLIT_VIEW_RING_OFFSET : 0.5;
        auto radius = (tab->is_loaded() ? tab->view().viewport_corner_radius() : 0.0) + offset;
        auto ring_rect = QRectF(tab->geometry()).adjusted(-offset, -offset, offset, offset);

        QColor color;
        if (is_active) {
            color = ChromeStyle::chrome_accent();
            if (!isActiveWindow())
                color.setAlpha(150);
        } else {
            color = ChromeStyle::chrome_border(palette);
        }

        painter.setPen(QPen(color, is_active ? SPLIT_VIEW_RING_WIDTH : 1.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(ring_rect, radius, radius);
    }
}

void TabStack::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    layout_pages();
    if (m_drop_indicator->isVisible())
        update_drop_indicator(m_drop_position);
}

void TabStack::dragEnterEvent(QDragEnterEvent* event)
{
    if (!handle_tab_drag(*event, event->position().toPoint()))
        event->ignore();
}

void TabStack::dragMoveEvent(QDragMoveEvent* event)
{
    if (!handle_tab_drag(*event, event->position().toPoint()))
        event->ignore();
}

void TabStack::dragLeaveEvent(QDragLeaveEvent*)
{
    if (!rect().contains(mapFromGlobal(QCursor::pos())))
        hide_drop_indicator();
}

void TabStack::dropEvent(QDropEvent* event)
{
    if (!handle_tab_drag(*event, event->position().toPoint()))
        event->ignore();
}

static Optional<SplitView::PendingDrop> s_pending_drop;

// Whether a tab being dragged can open beside this window's page.
static bool can_drop_beside_current_tab(TabStack const& stack, Tab& dragged_tab)
{
    auto* window = browser_window_of(stack);
    auto* source_window = browser_window_of(dragged_tab);
    if (!window || !source_window || window->is_private() != source_window->is_private())
        return false;

    auto* current = window->current_tab();
    if (!current)
        return false;

    // The page already beside another one only takes the other side of its own split.
    if (stack.split_partner(*current))
        return stack.is_shown(dragged_tab);
    return true;
}

Optional<SplitViewSide> TabStack::drop_side_at(QPoint const& position) const
{
    auto edge_width = max(SPLIT_VIEW_DROP_EDGE_MIN_WIDTH, qRound(width() * SPLIT_VIEW_DROP_EDGE_FRACTION));
    // Once the indicator is up, the whole half it covers takes the drop.
    if (m_drop_armed && m_drop_side.has_value())
        edge_width = width() / 2;

    if (position.x() < edge_width)
        return SplitViewSide::Left;
    if (position.x() >= width() - edge_width)
        return SplitViewSide::Right;
    return {};
}

bool TabStack::handle_tab_drag(QDropEvent& event, QPoint const& position)
{
    if (!event.mimeData()->hasFormat(LADYBIRD_TAB_MIME_TYPE))
        return false;

    auto* dragged_tab = tab_being_dragged();
    if (!dragged_tab || !can_drop_beside_current_tab(*this, *dragged_tab) || !rect().contains(position)) {
        hide_drop_indicator();
        event.ignore();
        return true;
    }

    if (event.type() == QEvent::Drop) {
        Optional<SplitViewSide> armed_side;
        if (m_drop_armed)
            armed_side = m_drop_side;
        hide_drop_indicator();
        if (!armed_side.has_value()) {
            event.ignore();
            return true;
        }
        s_pending_drop = SplitView::PendingDrop { this, *armed_side };
        event.setDropAction(Qt::MoveAction);
        event.accept();
        return true;
    }

    update_drop_indicator(position);
    if (m_drop_armed) {
        event.setDropAction(Qt::MoveAction);
        event.accept();
    } else {
        event.ignore();
    }
    return true;
}

void TabStack::update_drop_indicator(QPoint const& position)
{
    m_drop_position = position;
    auto side = drop_side_at(position);
    if (side != m_drop_side) {
        m_drop_side = side;
        m_drop_armed = false;
        m_drop_delay_timer->stop();
        if (side.has_value())
            m_drop_delay_timer->start();
    }

    if (!m_drop_armed || !m_drop_side.has_value()) {
        m_drop_indicator->hide();
        return;
    }

    // A page alone joins the toolbar at its top, where a split view's pages have a frame.
    auto area = split_area();
    if (!shown_split() && m_chrome_visible)
        area.setTop(0);
    auto half_width = (area.width() - SPLIT_VIEW_GAP) / 2;
    auto indicator_rect = *m_drop_side == SplitViewSide::Left
        ? QRect(area.left(), area.top(), half_width, area.height())
        : QRect(area.right() + 1 - half_width, area.top(), half_width, area.height());
    m_drop_indicator->setGeometry(indicator_rect);
    m_drop_indicator->show();
    m_drop_indicator->raise();
}

void TabStack::hide_drop_indicator()
{
    m_drop_delay_timer->stop();
    m_drop_side.clear();
    m_drop_armed = false;
    m_drop_indicator->hide();
}

namespace SplitView {

void add_tab_context_menu_actions(Tab& tab, QMenu& menu)
{
    QPointer<Tab> tab_pointer = &tab;

    auto* add_to_new_split_view = menu.addAction("Add Tab to New Split &View");
    QObject::connect(add_to_new_split_view, &QAction::triggered, &tab, [tab_pointer] {
        if (auto* window = tab_pointer ? browser_window_of(*tab_pointer) : nullptr)
            open_new_tab_beside(*window, *tab_pointer, SplitViewSide::Right);
    });

    auto* split_with_current_tab = menu.addAction("New Split &View with Current Tab");
    QObject::connect(split_with_current_tab, &QAction::triggered, &tab, [tab_pointer] {
        auto* window = tab_pointer ? browser_window_of(*tab_pointer) : nullptr;
        if (!window || !window->current_tab() || window->current_tab() == tab_pointer)
            return;
        auto& current = *window->current_tab();
        create_split(*window, current, *tab_pointer, SplitViewSide::Right, current);
    });

    auto* reverse_views = menu.addAction("&Reverse Views");
    QObject::connect(reverse_views, &QAction::triggered, &tab, [tab_pointer] {
        if (auto* window = tab_pointer ? browser_window_of(*tab_pointer) : nullptr)
            reverse_split(*window, *tab_pointer);
    });

    auto* separate_views = menu.addAction("&Separate Views");
    QObject::connect(separate_views, &QAction::triggered, &tab, [tab_pointer] {
        if (auto* window = tab_pointer ? browser_window_of(*tab_pointer) : nullptr)
            window->tab_stack().separate(*tab_pointer);
    });

    QObject::connect(&menu, &QMenu::aboutToShow, &tab, [tab_pointer, add_to_new_split_view, split_with_current_tab, reverse_views, separate_views] {
        auto* window = tab_pointer ? browser_window_of(*tab_pointer) : nullptr;
        auto in_split = window && window->tab_stack().split_partner(*tab_pointer);
        auto* current = window ? window->current_tab() : nullptr;
        auto is_current = current == tab_pointer;
        auto current_in_split = current && window->tab_stack().split_partner(*current);

        add_to_new_split_view->setVisible(window && !in_split && is_current);
        split_with_current_tab->setVisible(window && !in_split && !is_current && current && !current_in_split);
        reverse_views->setVisible(in_split);
        separate_views->setVisible(in_split);
    });
}

void add_link_context_menu_action(Tab& tab, QMenu& menu)
{
    auto actions = menu.actions();
    if (actions.isEmpty())
        return;

    // The page's own "Open in New Tab" opens the link, with the referrer and the rest of how the page would open it.
    QPointer<QAction> open_in_new_tab = actions.first();
    QPointer<Tab> tab_pointer = &tab;

    auto* action = new QAction("Open Link in Split &View", &menu);
    menu.insertAction(actions.value(1), action);

    QObject::connect(&menu, &QMenu::aboutToShow, action, [tab_pointer, action] {
        auto* window = tab_pointer ? browser_window_of(*tab_pointer) : nullptr;
        Optional<SplitViewSide> side;
        if (window)
            side = window->tab_stack().split_side(*tab_pointer);
        if (!side.has_value())
            action->setText("Open Link in Split &View");
        else
            action->setText(side == SplitViewSide::Left ? "Open Link in R&ight View" : "Open Link in &Left View");
    });

    QObject::connect(action, &QAction::triggered, &tab, [tab_pointer, open_in_new_tab] {
        auto* window = tab_pointer ? browser_window_of(*tab_pointer) : nullptr;
        if (!window || !open_in_new_tab)
            return;

        Vector<Tab*> tabs_before;
        for (int i = 0; i < window->tab_count(); ++i)
            tabs_before.append(window->tab_at(i));
        open_in_new_tab->trigger();

        Tab* opened = nullptr;
        for (int i = 0; i < window->tab_count() && !opened; ++i) {
            if (!tabs_before.contains_slow(window->tab_at(i)))
                opened = window->tab_at(i);
        }
        if (!opened || !tab_pointer)
            return;

        // In a split view, the link takes the other side's place, and the page there stays open in a tab of its own.
        auto side = window->tab_stack().split_side(*tab_pointer);
        auto new_side = side.has_value() ? opposite(*side) : SplitViewSide::Right;
        create_split(*window, *tab_pointer, *opened, new_side, *tab_pointer);
    });
}

QToolButton* create_toolbar_button(Tab& tab, QWidget& toolbar, int size)
{
    auto* button = new SplitViewButton(tab, toolbar);
    button->setFixedSize(size, size);
    return button;
}

QAction* create_new_split_view_action(BrowserWindow& window)
{
    auto* action = new QAction("New Split &View", &window);
    // Chromium's shortcut for a new split view on macOS and ChromeOS (Cmd/Ctrl+Option/Alt+N).
    action->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_N));
    action->setToolTip("Open a new tab beside this one");
    window.addAction(action);

    QPointer<BrowserWindow> window_pointer = &window;
    QObject::connect(action, &QAction::triggered, &window, [window_pointer] {
        if (!window_pointer || !window_pointer->current_tab())
            return;
        auto& current = *window_pointer->current_tab();
        // A tab in a split view goes to the side of a new split view of its own, as a tab dragged out would.
        if (window_pointer->tab_stack().split_partner(current))
            return;
        open_new_tab_beside(*window_pointer, current, SplitViewSide::Right);
    });
    return action;
}

void keep_split_views_together(BrowserWindow& window, Tab& tab)
{
    auto& stack = window.tab_stack();
    if (auto* partner = stack.split_partner(tab)) {
        auto index = window.tab_index(&tab);
        auto partner_index = window.tab_index(partner);
        if (index < 0 || partner_index < 0)
            return;

        // A tab moved past its partner swaps the pages. A tab moved further away takes its partner along.
        if (abs(index - partner_index) == 1)
            stack.update_split_order(tab);
        else
            place_beside(window, tab, *partner, *stack.split_side(*partner));
        return;
    }

    // A tab dropped between a split view's tabs goes after them.
    auto index = window.tab_index(&tab);
    if (index <= 0 || index + 1 >= window.tab_count())
        return;
    if (stack.split_partner(*window.tab_at(index - 1)) == window.tab_at(index + 1))
        window.move_tab(index, index + 1);
}

Optional<PendingDrop> take_pending_drop()
{
    auto drop = s_pending_drop;
    s_pending_drop.clear();
    return drop;
}

void complete_drop(PendingDrop const& drop, Tab& tab)
{
    if (!drop.stack)
        return;

    auto* window = browser_window_of(*drop.stack);
    auto* source_window = browser_window_of(tab);
    if (!window || !source_window)
        return;
    auto* current = window->current_tab();
    if (!current)
        return;
    auto& stack = window->tab_stack();

    // A tab dropped on its own split view's edge moves to that side.
    if (stack.is_shown(tab) && stack.split_partner(tab)) {
        if (stack.split_side(tab) != drop.side)
            reverse_split(*window, tab);
        return;
    }

    // The page showing is the tab being dragged, which is the current tab of its window. It goes to the side it was
    // dropped on, beside a new tab.
    if (&tab == current) {
        open_new_tab_beside(*window, tab, opposite(drop.side));
        return;
    }

    if (source_window != window) {
        auto target_index = window->tab_index(current) + (drop.side == SplitViewSide::Right ? 1 : 0);
        source_window->move_tab_to_window(source_window->tab_index(&tab), *window, target_index);
        if (window->tab_index(&tab) < 0)
            return;
    }
    create_split(*window, *current, tab, drop.side, tab);
}

void paint_tab_frame(QPainter& painter, QPalette const& palette, QRectF const& frame, bool is_shown)
{
    auto radius = min(frame.width(), frame.height()) / 2.0;

    painter.save();
    if (is_shown) {
        // The tab beside the selected one shows its page too, so it looks half selected. The selected tab paints over its
        // own half.
        auto surface = ChromeStyle::chrome_active_tab_surface(palette);
        surface.setAlphaF(0.5);
        painter.setPen(Qt::NoPen);
        painter.setBrush(surface);
        painter.drawRoundedRect(frame, radius, radius);
    } else {
        auto dark = ChromeStyle::is_dark(palette);
        auto fill = dark ? QColor(255, 255, 255) : QColor(0, 0, 0);
        fill.setAlpha(dark ? 10 : 8);
        auto border = ChromeStyle::chrome_border(palette);
        border.setAlpha(dark ? 150 : 130);
        painter.setPen(QPen(border, 1.0));
        painter.setBrush(fill);
        painter.drawRoundedRect(frame.adjusted(0.5, 0.5, -0.5, -0.5), radius - 0.5, radius - 0.5);
    }
    painter.restore();
}

}

}
