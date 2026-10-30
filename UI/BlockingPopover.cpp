/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/QuickSort.h>
#include <UI/BlockingPopover.h>
#include <UI/BrandColors.h>
#include <UI/ChromeStyle.h>
#include <UI/StringUtils.h>

#include <QAbstractButton>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>

namespace Ladybird {

static constexpr int CARD_WIDTH = 320;
static constexpr int ICON_SIZE = 20;
static constexpr int DOMAIN_LIST_MAX_HEIGHT = 132;

enum class BlockingIcon {
    Power,
    Zap,
    Pick,
    Dashboard,
    Popup,
    Font,
};

// Draws an icon on a 20×20 canvas.
static void paint_icon(QPainter& painter, BlockingIcon icon, QColor const& color)
{
    painter.setRenderHint(QPainter::Antialiasing);
    QPen pen(color, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    switch (icon) {
    case BlockingIcon::Power:
        // The gap is at the top, where the stroke goes in.
        painter.drawArc(QRectF(3.5, 4, 13, 13), 120 * 16, 300 * 16);
        painter.drawLine(QPointF(10, 2.5), QPointF(10, 9.5));
        break;
    case BlockingIcon::Zap: {
        QPainterPath bolt;
        bolt.moveTo(11.5, 2);
        bolt.lineTo(4.5, 11.5);
        bolt.lineTo(9.5, 11.5);
        bolt.lineTo(8.5, 18);
        bolt.lineTo(15.5, 8.5);
        bolt.lineTo(10.5, 8.5);
        bolt.closeSubpath();
        painter.setBrush(color);
        painter.drawPath(bolt);
        break;
    }
    case BlockingIcon::Pick:
        painter.drawEllipse(QPointF(10, 10), 5.5, 5.5);
        painter.drawLine(QPointF(10, 1.5), QPointF(10, 6));
        painter.drawLine(QPointF(10, 14), QPointF(10, 18.5));
        painter.drawLine(QPointF(1.5, 10), QPointF(6, 10));
        painter.drawLine(QPointF(14, 10), QPointF(18.5, 10));
        break;
    case BlockingIcon::Dashboard: {
        struct Slider {
            double y;
            double knob;
        };
        for (auto slider : { Slider { 5, 7 }, Slider { 10, 13 }, Slider { 15, 9 } }) {
            painter.drawLine(QPointF(3, slider.y), QPointF(17, slider.y));
            painter.setBrush(color);
            painter.drawEllipse(QPointF(slider.knob, slider.y), 2, 2);
            painter.setBrush(Qt::NoBrush);
        }
        break;
    }
    case BlockingIcon::Popup:
        painter.drawRoundedRect(QRectF(2.5, 4.5, 15, 12), 2, 2);
        painter.drawLine(QPointF(2.5, 8), QPointF(17.5, 8));
        painter.drawLine(QPointF(8, 14), QPointF(12, 10.5));
        painter.drawLine(QPointF(9.5, 10.5), QPointF(12, 10.5));
        painter.drawLine(QPointF(12, 10.5), QPointF(12, 13));
        break;
    case BlockingIcon::Font:
        painter.drawLine(QPointF(4, 17), QPointF(10, 3));
        painter.drawLine(QPointF(10, 3), QPointF(16, 17));
        painter.drawLine(QPointF(6.5, 12), QPointF(13.5, 12));
        break;
    }
}

// An icon in the given color, and in the accent color when its button is checked.
static QIcon blocking_icon(BlockingIcon icon, QColor const& color, QColor const& checked_color)
{
    QIcon result;
    auto add = [&](int ratio, QIcon::State state, QColor const& state_color) {
        QPixmap pixmap(ICON_SIZE * ratio, ICON_SIZE * ratio);
        pixmap.setDevicePixelRatio(ratio);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        paint_icon(painter, icon, state_color);
        painter.end();
        result.addPixmap(pixmap, QIcon::Normal, state);
    };
    for (auto ratio : { 1, 2, 3 }) {
        add(ratio, QIcon::Off, color);
        add(ratio, QIcon::On, checked_color);
    }
    return result;
}

// uBlock Origin's big power button: filled with the accent while the site is blocked, hollow while it isn't.
class BlockingPowerButton final : public QAbstractButton {
    AK_ALLOC_WITH_KMALLOC;

public:
    explicit BlockingPowerButton(QWidget* parent)
        : QAbstractButton(parent)
    {
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::StrongFocus);
        setAttribute(Qt::WA_Hover);
    }

    void set_palette_colors(QPalette const& palette)
    {
        m_off_fill = ChromeStyle::chrome_surface_recessed(palette);
        m_off_glyph = ChromeStyle::chrome_muted_text(palette);
        m_off_ring = ChromeStyle::chrome_border(palette);
        update();
    }

    virtual QSize sizeHint() const override { return { 76, 76 }; }

private:
    virtual void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        auto circle = QRectF(rect()).adjusted(4, 4, -4, -4);
        auto accent = ChromeStyle::chrome_accent();
        auto fill = isChecked() ? accent : m_off_fill;
        if (underMouse() && isEnabled())
            fill = isChecked() ? QColor(BrandColors::Rose300) : ChromeStyle::mix(m_off_fill, m_off_glyph, 0.12);
        auto glyph = isChecked() ? QColor(Qt::white) : m_off_glyph;
        if (!isEnabled()) {
            fill.setAlphaF(0.45);
            glyph.setAlphaF(0.6);
        }

        painter.setPen(isChecked() ? Qt::NoPen : QPen(m_off_ring, 1.5));
        painter.setBrush(fill);
        painter.drawEllipse(circle);

        // The power symbol, twice the size of the other icons.
        painter.save();
        painter.translate(circle.center() - QPointF(20, 20));
        painter.scale(2, 2);
        paint_icon(painter, BlockingIcon::Power, glyph);
        painter.restore();

        if (hasFocus()) {
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(accent, 2));
            painter.drawEllipse(circle.adjusted(-3, -3, 3, 3));
        }
    }

    QColor m_off_fill;
    QColor m_off_glyph;
    QColor m_off_ring;
};

static QLabel* make_label(QWidget* parent, char const* object_name, Qt::Alignment alignment = Qt::AlignLeading | Qt::AlignVCenter)
{
    auto* label = new QLabel(parent);
    label->setObjectName(object_name);
    label->setTextFormat(Qt::PlainText);
    label->setAlignment(alignment);
    return label;
}

static QToolButton* make_tool(QWidget* parent, QString const& text, QString const& tool_tip, bool checkable)
{
    auto* button = new QToolButton(parent);
    button->setObjectName("XenonBlockingTool");
    button->setText(text);
    button->setToolTip(tool_tip);
    button->setAccessibleName(tool_tip);
    button->setCheckable(checkable);
    button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    button->setIconSize({ ICON_SIZE, ICON_SIZE });
    button->setCursor(Qt::PointingHandCursor);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    return button;
}

static QString formatted_count(u64 count)
{
    return QLocale().toString(static_cast<qulonglong>(count));
}

BlockingPopover::BlockingPopover(QWidget* parent)
    : Popover(parent)
{
    setAttribute(Qt::WA_NoMouseReplay);
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName("Content blocking");
    card().setObjectName("XenonBlockingPopover");

    auto* card_layout = new QVBoxLayout(&card());
    card_layout->setContentsMargins(0, 0, 0, 0);
    card_layout->setSpacing(0);

    auto* layout = new QVBoxLayout;
    layout->setContentsMargins(16, 12, 12, 14);
    layout->setSpacing(12);
    card_layout->addLayout(layout);

    // The site, and the way to the dashboard.
    auto* heading = new QHBoxLayout;
    heading->setSpacing(8);
    m_favicon = new QLabel(&card());
    m_favicon->setFixedSize(20, 20);
    m_favicon->setAlignment(Qt::AlignCenter);
    m_favicon->hide();
    heading->addWidget(m_favicon, 0, Qt::AlignVCenter);
    m_site = make_label(&card(), "XenonBlockingSite");
    m_site->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    heading->addWidget(m_site, 1);
    m_dashboard_button = new QToolButton(&card());
    m_dashboard_button->setObjectName("XenonBlockingDashboard");
    m_dashboard_button->setToolTip("Open the blocking dashboard: filter lists, my filters, and trusted sites");
    m_dashboard_button->setAccessibleName("Open the blocking dashboard");
    m_dashboard_button->setIconSize({ ICON_SIZE, ICON_SIZE });
    m_dashboard_button->setCursor(Qt::PointingHandCursor);
    QObject::connect(m_dashboard_button, &QToolButton::clicked, this, [this] {
        close();
        if (on_open_dashboard)
            on_open_dashboard();
    });
    heading->addWidget(m_dashboard_button, 0, Qt::AlignVCenter);
    layout->addLayout(heading);

    // The power switch for the site.
    m_power = new BlockingPowerButton(&card());
    m_power->setObjectName("XenonBlockingPower");
    QObject::connect(m_power, &QAbstractButton::toggled, this, [this](bool enabled) {
        if (on_toggle)
            on_toggle(enabled);
    });
    layout->addWidget(m_power, 0, Qt::AlignHCenter);
    m_power_caption = make_label(&card(), "XenonBlockingPowerCaption", Qt::AlignCenter);
    m_power_caption->setWordWrap(true);
    layout->addWidget(m_power_caption);

    m_farbling_switch = make_tool(&card(), "Fingerprinting", "Protect this site against fingerprinting", true);
    m_farbling_switch->setObjectName("XenonBlockingFarbling");
    QObject::connect(m_farbling_switch, &QToolButton::toggled, this, [this](bool enabled) {
        if (on_farbling_toggle)
            on_farbling_toggle(enabled);
    });
    layout->addWidget(m_farbling_switch);

    // What was blocked, and where the page connected.
    auto* stats = new QFrame(&card());
    stats->setObjectName("XenonBlockingStats");
    auto* stats_layout = new QHBoxLayout(stats);
    stats_layout->setContentsMargins(8, 10, 8, 10);
    stats_layout->setSpacing(4);
    auto add_stat = [&](QLabel*& value, QLabel** caption_out, QString const& caption_text) {
        auto* column = new QVBoxLayout;
        column->setSpacing(0);
        value = make_label(stats, "XenonBlockingStatValue", Qt::AlignCenter);
        column->addWidget(value);
        auto* caption = make_label(stats, "XenonBlockingStatCaption", Qt::AlignCenter);
        caption->setText(caption_text);
        caption->setWordWrap(true);
        column->addWidget(caption);
        if (caption_out)
            *caption_out = caption;
        stats_layout->addLayout(column, 1);
    };
    add_stat(m_blocked_on_page, &m_blocked_on_page_caption, "blocked on this page");
    add_stat(m_blocked_in_total, nullptr, "blocked since install");
    add_stat(m_domain_count, nullptr, "domains connected");
    layout->addWidget(stats);

    // The element picker and zapper, and the site's switches.
    auto* tools = new QHBoxLayout;
    tools->setSpacing(6);
    m_pick_button = make_tool(&card(), "Pick", "Pick an element to hide on this site from now on", false);
    QObject::connect(m_pick_button, &QToolButton::clicked, this, [this] {
        close();
        if (on_pick_element)
            on_pick_element();
    });
    tools->addWidget(m_pick_button);
    m_zap_button = make_tool(&card(), "Zap", "Remove an element from this page until it reloads", false);
    QObject::connect(m_zap_button, &QToolButton::clicked, this, [this] {
        close();
        if (on_zap_element)
            on_zap_element();
    });
    tools->addWidget(m_zap_button);
    m_popups_switch = make_tool(&card(), "Pop-ups", "Block every pop-up this site opens", true);
    QObject::connect(m_popups_switch, &QToolButton::toggled, this, [this](bool enabled) {
        if (on_site_switch_toggle)
            on_site_switch_toggle(BlockingSiteSwitch::Popups, enabled);
    });
    tools->addWidget(m_popups_switch);
    m_fonts_switch = make_tool(&card(), "Fonts", "Block the fonts this site loads from servers", true);
    QObject::connect(m_fonts_switch, &QToolButton::toggled, this, [this](bool enabled) {
        if (on_site_switch_toggle)
            on_site_switch_toggle(BlockingSiteSwitch::RemoteFonts, enabled);
    });
    tools->addWidget(m_fonts_switch);
    layout->addLayout(tools);

    // The domains the page connected to.
    m_domains_heading = make_label(&card(), "XenonBlockingSectionHeading");
    layout->addWidget(m_domains_heading);
    auto* domain_list = new QWidget;
    domain_list->setObjectName("XenonBlockingDomainList");
    m_domains_layout = new QVBoxLayout(domain_list);
    m_domains_layout->setContentsMargins(0, 0, 4, 0);
    m_domains_layout->setSpacing(2);
    m_domains_scroll = new QScrollArea(&card());
    m_domains_scroll->setObjectName("XenonBlockingDomains");
    m_domains_scroll->setWidget(domain_list);
    m_domains_scroll->setWidgetResizable(true);
    m_domains_scroll->setFrameShape(QFrame::NoFrame);
    m_domains_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    layout->addWidget(m_domains_scroll);

    m_error = make_label(&card(), "XenonBlockingError");
    m_error->setWordWrap(true);
    m_error->hide();
    layout->addWidget(m_error);

    m_note = make_label(&card(), "XenonBlockingNote", Qt::AlignCenter);
    m_note->setText("If this site looks broken, turn blocking off for it.");
    m_note->setWordWrap(true);
    card_layout->addWidget(m_note);

    update_counts();
    update_domains();
}

void BlockingPopover::set_favicon(QIcon const& favicon)
{
    m_favicon->setVisible(!favicon.isNull());
    m_favicon->setPixmap(favicon.pixmap(m_favicon->size(), devicePixelRatioF()));
}

void BlockingPopover::set_snapshot(WebView::ContentBlockingSnapshot const& snapshot)
{
    auto site_changed = snapshot.site != m_snapshot.site;
    auto count_changed = snapshot.blocked_request_count != m_snapshot.blocked_request_count;
    m_snapshot = snapshot;

    auto site = snapshot.site.has_value() ? qstring_from_ak_string(*snapshot.site) : QString("This page");
    m_site->setToolTip(site);
    m_site->setAccessibleName(site);

    {
        QSignalBlocker blocker(m_power);
        m_power->setChecked(snapshot.enabled());
    }
    m_power->setEnabled(snapshot.can_toggle());
    m_power->setAccessibleName(snapshot.enabled() ? "Turn blocking off for this site" : "Turn blocking on for this site");
    m_power->setToolTip(m_power->accessibleName());

    switch (snapshot.state) {
    case WebView::ContentBlockingState::Disabled:
        m_power_caption->setText("Blocking is off for every site. Turn it on in Settings.");
        m_power->setToolTip("Blocking is off in Settings");
        break;
    case WebView::ContentBlockingState::SiteDisabled:
        m_power_caption->setText("Not blocking on this site");
        break;
    case WebView::ContentBlockingState::NoRules:
        m_power_caption->setText("On, but no filter lists are turned on");
        break;
    case WebView::ContentBlockingState::Enabled:
        m_power_caption->setText("Blocking ads and trackers on this site");
        break;
    case WebView::ContentBlockingState::Unavailable:
        m_power_caption->setText("There's nothing to block on this page");
        break;
    }

    for (auto* site_switch : { m_popups_switch, m_fonts_switch })
        site_switch->setEnabled(snapshot.site.has_value());
    m_farbling_switch->setEnabled(snapshot.site.has_value());
    for (auto* tool : { m_pick_button, m_zap_button })
        tool->setEnabled(snapshot.site.has_value());

    m_error->hide();
    if (site_changed) {
        m_domains = {};
        update_domains();
    }
    update_counts();
    update_size();
    if ((site_changed || count_changed) && isVisible() && on_refresh)
        on_refresh();
}

void BlockingPopover::set_blocked_request_total(u64 total)
{
    m_blocked_request_total = total;
    update_counts();
}

void BlockingPopover::set_domains(Optional<Vector<BlockingDomain>> domains)
{
    m_domains = AK::move(domains);
    if (m_domains.has_value()) {
        // The page's own hosts first, then the busiest.
        quick_sort(*m_domains, [](auto const& a, auto const& b) {
            if (a.first_party != b.first_party)
                return a.first_party;
            if (a.requests != b.requests)
                return a.requests > b.requests;
            return a.host < b.host;
        });
    }
    update_domains();
    update_counts();
    update_size();
}

void BlockingPopover::set_site_switch(BlockingSiteSwitch site_switch, bool enabled)
{
    auto* button = site_switch == BlockingSiteSwitch::Popups ? m_popups_switch : m_fonts_switch;
    QSignalBlocker blocker(button);
    button->setChecked(enabled);
}

void BlockingPopover::set_farbling_enabled(bool enabled)
{
    QSignalBlocker blocker(m_farbling_switch);
    m_farbling_switch->setChecked(enabled);
    m_farbling_switch->setAccessibleName(enabled ? "Turn fingerprinting protection off for this site" : "Turn fingerprinting protection on for this site");
    m_farbling_switch->setToolTip(m_farbling_switch->accessibleName());
}

void BlockingPopover::update_counts()
{
    m_blocked_on_page->setText(formatted_count(m_snapshot.blocked_request_count));
    m_blocked_in_total->setText(formatted_count(m_blocked_request_total));

    // As uBlock Origin does, the share of the page's requests that were blocked. Only the requests that went ahead reach
    // the page's performance timeline, which is where the domains come from, so the two add up to everything it asked for.
    auto caption = QString("blocked on this page");
    if (m_domains.has_value()) {
        size_t allowed = 0;
        for (auto const& domain : *m_domains)
            allowed += domain.requests;
        auto total = allowed + m_snapshot.blocked_request_count;
        if (total > 0 && m_snapshot.blocked_request_count > 0)
            caption = qformatted("blocked on this page ({}%)", (m_snapshot.blocked_request_count * 100 + total / 2) / total);
        m_domain_count->setText(formatted_count(m_domains->size()));
    } else {
        m_domain_count->setText("–");
    }
    m_blocked_on_page_caption->setText(caption);
}

void BlockingPopover::update_domains()
{
    while (auto* item = m_domains_layout->takeAt(0)) {
        delete item->widget();
        delete item;
    }

    if (!m_domains.has_value() || m_domains->is_empty()) {
        m_domains_heading->setText(m_domains.has_value() ? "No other domains" : "Domains connected");
        m_domains_heading->setVisible(m_domains.has_value());
        m_domains_scroll->hide();
        return;
    }

    m_domains_heading->setText("Domains connected");
    m_domains_heading->show();
    auto* list = m_domains_scroll->widget();
    for (auto const& domain : *m_domains) {
        auto* row = new QWidget(list);
        auto* row_layout = new QHBoxLayout(row);
        row_layout->setContentsMargins(0, 0, 0, 0);
        row_layout->setSpacing(8);
        auto* host = make_label(row, domain.first_party ? "XenonBlockingFirstPartyDomain" : "XenonBlockingDomain");
        host->setText(qstring_from_ak_string(domain.host));
        host->setToolTip(domain.first_party ? qformatted("{} (this site)", domain.host) : qstring_from_ak_string(domain.host));
        host->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        row_layout->addWidget(host, 1);
        auto* count = make_label(row, "XenonBlockingDomainCount", Qt::AlignTrailing | Qt::AlignVCenter);
        count->setText(formatted_count(domain.requests));
        count->setToolTip(qformatted("{} request{}", domain.requests, domain.requests == 1 ? ""sv : "s"sv));
        row_layout->addWidget(count);
        m_domains_layout->addWidget(row);
    }
    m_domains_layout->addStretch();

    auto row_height = m_domains_scroll->fontMetrics().height() + m_domains_layout->spacing() + 2;
    m_domains_scroll->setFixedHeight(qMin(DOMAIN_LIST_MAX_HEIGHT, static_cast<int>(m_domains->size()) * row_height));
    m_domains_scroll->show();
}

void BlockingPopover::update_chrome_style(QPalette const& palette)
{
    auto active_palette = palette;
    active_palette.setCurrentColorGroup(QPalette::Active);
    setPalette(active_palette);

    auto text_color = ChromeStyle::chrome_text(active_palette);
    auto accent = ChromeStyle::chrome_accent();
    auto dark = ChromeStyle::is_dark(active_palette);
    auto text = ChromeStyle::style_sheet_color(text_color);
    // Opaque, unlike the palette's placeholder text, since style sheets drop a color's alpha.
    auto muted = ChromeStyle::style_sheet_color(ChromeStyle::mix(text_color, ChromeStyle::chrome_surface(active_palette), 0.45));
    auto surface = ChromeStyle::style_sheet_color(ChromeStyle::chrome_surface(active_palette));
    auto recessed = ChromeStyle::style_sheet_color(ChromeStyle::mix(ChromeStyle::chrome_surface(active_palette), text_color, dark ? 0.08 : 0.05));
    auto hover = ChromeStyle::style_sheet_color(ChromeStyle::chrome_surface_hover(active_palette));
    auto border = ChromeStyle::style_sheet_color(ChromeStyle::chrome_border(active_palette));
    auto footer = ChromeStyle::style_sheet_color(ChromeStyle::chrome_background(active_palette));
    auto accent_color = ChromeStyle::style_sheet_color(accent);
    auto checked_surface = ChromeStyle::style_sheet_color(QColor(dark ? BrandColors::Rose950 : BrandColors::Rose10));

    setStyleSheet(qformatted(R"(
QFrame#XenonBlockingPopover {{
    color: {0};
    background: {2};
    border: 1px solid {5};
    border-radius: 10px;
}}
QFrame#XenonBlockingPopover QLabel {{
    color: {0};
}}
QFrame#XenonBlockingPopover QLabel#XenonBlockingSite {{
    font-weight: 600;
}}
QFrame#XenonBlockingPopover QLabel#XenonBlockingPowerCaption,
QFrame#XenonBlockingPopover QLabel#XenonBlockingStatCaption,
QFrame#XenonBlockingPopover QLabel#XenonBlockingSectionHeading,
QFrame#XenonBlockingPopover QLabel#XenonBlockingDomainCount,
QFrame#XenonBlockingPopover QLabel#XenonBlockingDomain {{
    color: {1};
}}
QFrame#XenonBlockingPopover QLabel#XenonBlockingStatValue {{
    font-size: 20px;
    font-weight: 600;
}}
QFrame#XenonBlockingPopover QLabel#XenonBlockingStatCaption,
QFrame#XenonBlockingPopover QLabel#XenonBlockingSectionHeading {{
    font-size: 11px;
}}
QFrame#XenonBlockingPopover QLabel#XenonBlockingSectionHeading {{
    font-weight: 600;
}}
QFrame#XenonBlockingPopover QLabel#XenonBlockingFirstPartyDomain {{
    font-weight: 600;
}}
QFrame#XenonBlockingPopover QLabel#XenonBlockingError {{
    color: {7};
}}
QFrame#XenonBlockingPopover QFrame#XenonBlockingStats {{
    background: {3};
    border-radius: 10px;
}}
QFrame#XenonBlockingPopover QToolButton#XenonBlockingTool,
QFrame#XenonBlockingPopover QToolButton#XenonBlockingFarbling {{
    color: {0};
    background: transparent;
    border: 1px solid {5};
    border-radius: 8px;
    padding: 6px 2px 4px 2px;
    font-size: 11px;
}}
QFrame#XenonBlockingPopover QToolButton#XenonBlockingTool:hover,
QFrame#XenonBlockingPopover QToolButton#XenonBlockingFarbling:hover {{
    background: {4};
}}
QFrame#XenonBlockingPopover QToolButton#XenonBlockingTool:checked,
QFrame#XenonBlockingPopover QToolButton#XenonBlockingFarbling:checked {{
    color: {7};
    background: {8};
    border-color: {7};
}}
QFrame#XenonBlockingPopover QToolButton#XenonBlockingTool:disabled,
QFrame#XenonBlockingPopover QToolButton#XenonBlockingFarbling:disabled {{
    color: {1};
}}
QFrame#XenonBlockingPopover QToolButton#XenonBlockingDashboard {{
    background: transparent;
    border: none;
    border-radius: 6px;
    padding: 4px;
}}
QFrame#XenonBlockingPopover QToolButton#XenonBlockingDashboard:hover {{
    background: {4};
}}
QFrame#XenonBlockingPopover QScrollArea#XenonBlockingDomains,
QFrame#XenonBlockingPopover QWidget#XenonBlockingDomainList {{
    background: transparent;
}}
QFrame#XenonBlockingPopover QLabel#XenonBlockingNote {{
    color: {1};
    background: {6};
    border-top: 1px solid {5};
    border-bottom-left-radius: 9px;
    border-bottom-right-radius: 9px;
    padding: 10px 16px;
}}
)",
        text, muted, surface, recessed, hover, border, footer, accent_color, checked_surface));

    m_power->set_palette_colors(active_palette);
    auto icon_color = ChromeStyle::chrome_button_text(active_palette);
    m_dashboard_button->setIcon(blocking_icon(BlockingIcon::Dashboard, icon_color, accent));
    m_pick_button->setIcon(blocking_icon(BlockingIcon::Pick, icon_color, accent));
    m_zap_button->setIcon(blocking_icon(BlockingIcon::Zap, icon_color, accent));
    m_popups_switch->setIcon(blocking_icon(BlockingIcon::Popup, icon_color, accent));
    m_fonts_switch->setIcon(blocking_icon(BlockingIcon::Font, icon_color, accent));
    update_size();
}

void BlockingPopover::show_error(QString const& message)
{
    m_error->setText(message);
    m_error->show();
    update_size();
}

void BlockingPopover::focus_switch()
{
    if (m_power->isEnabled())
        m_power->setFocus(Qt::PopupFocusReason);
    else
        setFocus(Qt::PopupFocusReason);
}

void BlockingPopover::showEvent(QShowEvent* event)
{
    Popover::showEvent(event);
    if (on_refresh)
        on_refresh();
}

void BlockingPopover::hideEvent(QHideEvent* event)
{
    Popover::hideEvent(event);
    if (on_hide)
        on_hide();
}

void BlockingPopover::mousePressEvent(QMouseEvent* event)
{
    // The transparent shadow margin can overlap the toolbar button. Treat it as outside the card.
    if (!card().geometry().contains(event->position().toPoint())) {
        close();
        event->accept();
        return;
    }

    Popover::mousePressEvent(event);
}

void BlockingPopover::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        close();
        if (on_keyboard_dismiss)
            on_keyboard_dismiss();
        event->accept();
        return;
    }

    Popover::keyPressEvent(event);
}

void BlockingPopover::update_size()
{
    auto available_width = qMax(200, screen() ? screen()->availableGeometry().width() - 48 : CARD_WIDTH);
    card().setFixedWidth(qMin(available_width, CARD_WIDTH));

    m_site->setText(m_site->fontMetrics().elidedText(m_site->toolTip(), Qt::ElideMiddle, card().width() - 90));

    card().layout()->activate();
    layout()->activate();
    adjustSize();
}

}
