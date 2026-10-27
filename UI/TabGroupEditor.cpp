/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <UI/ChromeStyle.h>
#include <UI/StringUtils.h>
#include <UI/TabGroupEditor.h>

#include <QAbstractButton>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLayout>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace Ladybird {

static constexpr int TAB_GROUP_EDITOR_WIDTH = 264;
static constexpr int TAB_GROUP_EDITOR_ANCHOR_GAP = 4;
static constexpr int TAB_GROUP_COLOR_SWATCH_SIZE = 24;

class TabGroupColorSwatch final : public QAbstractButton {
public:
    AK_ALLOC_WITH_KMALLOC;

    TabGroupColorSwatch(TabGroupColor color, QWidget* parent)
        : QAbstractButton(parent)
        , m_color(color)
    {
        setCheckable(true);
        setAutoExclusive(true);
        setFocusPolicy(Qt::TabFocus);
        setFixedSize(TAB_GROUP_COLOR_SWATCH_SIZE, TAB_GROUP_COLOR_SWATCH_SIZE);
        setToolTip(tab_group_color_name(color));
        setAccessibleName(tab_group_color_name(color));
    }

    TabGroupColor color() const { return m_color; }

private:
    virtual void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        auto color = tab_group_color(m_color, palette());
        QRectF bounds = rect();

        // The chosen color has a ring around it.
        if (isChecked() || hasFocus()) {
            painter.setPen(QPen(color, 2));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(bounds.adjusted(1, 1, -1, -1));
        }

        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        auto inset = isChecked() ? 5.0 : (underMouse() ? 3.0 : 4.0);
        painter.drawEllipse(bounds.adjusted(inset, inset, -inset, -inset));
    }

    virtual void enterEvent(QEnterEvent* event) override
    {
        QAbstractButton::enterEvent(event);
        update();
    }

    virtual void leaveEvent(QEvent* event) override
    {
        QAbstractButton::leaveEvent(event);
        update();
    }

    TabGroupColor m_color;
};

static QString tab_group_editor_style_sheet(QPalette const& palette)
{
    auto text = ChromeStyle::style_sheet_color(ChromeStyle::chrome_text(palette));
    auto surface = ChromeStyle::style_sheet_color(ChromeStyle::chrome_surface(palette));
    auto border = ChromeStyle::style_sheet_color(ChromeStyle::chrome_border(palette));
    auto control_border = ChromeStyle::style_sheet_color(ChromeStyle::chrome_control_border(palette));
    auto hover_surface = ChromeStyle::style_sheet_color(ChromeStyle::chrome_surface_hover(palette));
    auto pressed_surface = ChromeStyle::style_sheet_color(ChromeStyle::chrome_surface_pressed(palette));
    auto separator = ChromeStyle::style_sheet_color(ChromeStyle::chrome_separator(palette));
    auto accent = ChromeStyle::style_sheet_color(ChromeStyle::chrome_accent());

    return qformatted(R"(
QFrame#XenonTabGroupEditor {{
    color: {0};
    background: {1};
    border: 1px solid {2};
    border-radius: 8px;
}}

QLineEdit#XenonTabGroupEditorTitle {{
    color: {0};
    background: {1};
    border: 1px solid {3};
    border-radius: 6px;
    padding: 5px 8px;
}}

QLineEdit#XenonTabGroupEditorTitle:focus {{
    border: 1px solid {7};
}}

QFrame#XenonTabGroupEditorSeparator {{
    background: {6};
    border: none;
    min-height: 1px;
    max-height: 1px;
}}

QPushButton#XenonTabGroupEditorAction {{
    color: {0};
    background: transparent;
    border: none;
    border-radius: 6px;
    padding: 6px 8px;
    text-align: left;
}}

QPushButton#XenonTabGroupEditorAction:hover {{
    background: {4};
}}

QPushButton#XenonTabGroupEditorAction:pressed {{
    background: {5};
}}
)",
        text, surface, border, control_border, hover_surface, pressed_surface, separator, accent);
}

TabGroupEditor::TabGroupEditor(QWidget* parent)
    : Popover(parent)
{
    card().setObjectName("XenonTabGroupEditor");
    card().setFixedWidth(TAB_GROUP_EDITOR_WIDTH);

    auto* layout = new QVBoxLayout(&card());
    layout->setContentsMargins(12, 12, 12, 8);
    layout->setSpacing(10);

    m_title_field = new QLineEdit(&card());
    m_title_field->setObjectName("XenonTabGroupEditorTitle");
    m_title_field->setPlaceholderText("Name this group");
    m_title_field->setAccessibleName("Tab group title");
    m_title_field->setAttribute(Qt::WA_MacShowFocusRect, false);
    QObject::connect(m_title_field, &QLineEdit::textEdited, this, [this](QString const& title) {
        if (m_groups && m_group)
            m_groups->set_title(*m_group, title);
    });
    QObject::connect(m_title_field, &QLineEdit::returnPressed, this, [this] { close(); });
    layout->addWidget(m_title_field);

    auto* swatches_layout = new QHBoxLayout;
    swatches_layout->setSpacing(2);
    swatches_layout->setContentsMargins(0, 0, 0, 0);
    for (int index = 0; index < TAB_GROUP_COLOR_COUNT; ++index) {
        auto* swatch = new TabGroupColorSwatch(static_cast<TabGroupColor>(index), &card());
        QObject::connect(swatch, &QAbstractButton::clicked, this, [this, swatch] {
            if (m_groups && m_group)
                m_groups->set_color(*m_group, swatch->color());
        });
        swatches_layout->addWidget(swatch);
        m_swatches.append(swatch);
    }
    swatches_layout->addStretch(1);
    layout->addLayout(swatches_layout);

    auto add_separator = [&] {
        auto* separator = new QFrame(&card());
        separator->setObjectName("XenonTabGroupEditorSeparator");
        layout->addWidget(separator);
    };

    // Each action closes the editor first, since some of them end the group.
    auto add_action = [&](char const* text, auto action) {
        auto* button = new QPushButton(text, &card());
        button->setObjectName("XenonTabGroupEditorAction");
        button->setFocusPolicy(Qt::TabFocus);
        button->setCursor(Qt::PointingHandCursor);
        QObject::connect(button, &QPushButton::clicked, this, [this, action] {
            QPointer<TabGroups> groups = m_groups;
            QPointer<TabGroup> group = m_group;
            close();
            if (groups && group)
                action(*groups, *group);
        });
        layout->addWidget(button);
        return button;
    };

    // The actions are in Chromium's order.
    add_separator();
    layout->setSpacing(2);
    add_action("New Tab in Group", [](TabGroups& groups, TabGroup& group) { groups.open_new_tab_in_group(group); });
    m_move_to_new_window_button = add_action("Move Group to New Window", [](TabGroups& groups, TabGroup& group) { groups.move_group_to_new_window(group); });
    add_action("Close Group", [](TabGroups& groups, TabGroup& group) { groups.close_group(group); });
    add_separator();
    add_action("Ungroup", [](TabGroups& groups, TabGroup& group) { groups.ungroup(group); });
}

void TabGroupEditor::edit(TabGroups& groups, TabGroup& group, QRect const& anchor, Placement placement, bool focus_title)
{
    if (m_groups)
        QObject::disconnect(m_groups, nullptr, this, nullptr);

    m_groups = &groups;
    m_group = &group;
    QObject::connect(&groups, &TabGroups::changed, this, [this] {
        if (!m_group || !m_groups->span_of(m_group).has_value())
            close();
        else
            update_from_group();
    });

    setPalette(parentWidget()->palette());
    setStyleSheet(tab_group_editor_style_sheet(palette()));
    update_from_group();

    adjustSize();
    place(anchor, placement);
    show();
    raise();
    activateWindow();

    if (focus_title) {
        m_title_field->setFocus();
        m_title_field->selectAll();
    } else {
        setFocus();
    }
}

void TabGroupEditor::update_from_group()
{
    if (!m_group)
        return;

    if (m_title_field->text() != m_group->title()) {
        QSignalBlocker blocker(m_title_field);
        m_title_field->setText(m_group->title());
    }
    for (auto* swatch : m_swatches)
        swatch->setChecked(swatch->color() == m_group->color());
    m_move_to_new_window_button->setVisible(m_groups && !m_groups->holds_every_tab(*m_group));
}

// The card goes below its anchor with their left edges aligned, or beside it, and stays on the anchor's screen. Only the
// shadow around the card may reach past the edge of the screen.
void TabGroupEditor::place(QRect const& anchor, Placement placement)
{
    auto margins = layout()->contentsMargins();
    auto card_size = size().shrunkBy(margins);

    QPoint card_position;
    if (placement == Placement::Below)
        card_position = { anchor.left(), anchor.bottom() + 1 + TAB_GROUP_EDITOR_ANCHOR_GAP };
    else
        card_position = { anchor.right() + 1 + TAB_GROUP_EDITOR_ANCHOR_GAP, anchor.top() };

    if (auto* screen = QGuiApplication::screenAt(anchor.center())) {
        auto available_geometry = screen->availableGeometry();
        if (placement == Placement::Beside && card_position.x() + card_size.width() > available_geometry.right())
            card_position.setX(anchor.left() - TAB_GROUP_EDITOR_ANCHOR_GAP - card_size.width());
        card_position.setX(clamp(card_position.x(), available_geometry.left(), max(available_geometry.left(), available_geometry.right() + 1 - card_size.width())));
        if (placement == Placement::Below && card_position.y() + card_size.height() > available_geometry.bottom())
            card_position.setY(anchor.top() - TAB_GROUP_EDITOR_ANCHOR_GAP - card_size.height());
        card_position.setY(clamp(card_position.y(), available_geometry.top(), max(available_geometry.top(), available_geometry.bottom() + 1 - card_size.height())));
    }

    move(card_position - QPoint(margins.left(), margins.top()));
}

void TabGroupEditor::hideEvent(QHideEvent* event)
{
    Popover::hideEvent(event);
    if (m_groups)
        QObject::disconnect(m_groups, nullptr, this, nullptr);
    m_group = nullptr;
}

}
