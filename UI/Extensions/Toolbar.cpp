/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/LexicalPath.h>
#include <UI/Application.h>
#include <UI/BrowserWindow.h>
#include <UI/Extensions/Browser.h>
#include <UI/Extensions/Popup.h>
#include <UI/Extensions/Registry.h>
#include <UI/Extensions/Toolbar.h>
#include <UI/StringUtils.h>
#include <UI/Tab.h>

#include <QFontMetrics>
#include <QHBoxLayout>
#include <QIcon>
#include <QPainter>
#include <QPainterPath>

namespace Ladybird::Extensions {

static Vector<QPointer<ActionToolbar>>& toolbars()
{
    static Vector<QPointer<ActionToolbar>> toolbars;
    return toolbars;
}

static void install_open_popup_handler()
{
    auto& handler = Browser::open_popup_handler();
    if (handler)
        return;

    handler = [](Extension const& extension) {
        auto* window = Application::the().active_window_if_any();
        if (!window || !window->current_tab())
            return;
        for (auto const& toolbar : toolbars()) {
            if (!toolbar || &toolbar->tab() != window->current_tab())
                continue;
            if (auto* button = toolbar->button_for(extension))
                button->open_popup();
        }
    };
}

ActionButton::ActionButton(QWidget* parent, Tab& tab, NonnullRefPtr<Extension> extension)
    : QToolButton(parent)
    , m_tab(tab)
    , m_extension(AK::move(extension))
{
    setObjectName("XenonExtensionActionButton");
    setAutoRaise(true);
    setIconSize({ 16, 16 });
    update_appearance();

    QObject::connect(this, &QToolButton::clicked, this, [this] { clicked(); });
}

void ActionButton::update_appearance()
{
    auto appearance = Browser::action_appearance(*m_extension, &m_tab);

    if (appearance.icon_path.has_value()) {
        auto file = LexicalPath::join(m_extension->root(), appearance.icon_path->bytes_as_string_view()).string();
        setIcon(QIcon(qstring_from_ak_string(file)));
    } else {
        setIcon(QIcon::fromTheme("application-x-addon"));
    }

    auto title = qstring_from_ak_string(appearance.title);
    setToolTip(title);
    setAccessibleName(title);
    setEnabled(appearance.enabled);

    m_badge_text = qstring_from_ak_string(appearance.badge_text);
    m_badge_background_color = appearance.badge_background_color;
    m_badge_text_color = appearance.badge_text_color;
    update();
}

void ActionButton::clicked()
{
    if (m_popup) {
        m_popup->close();
        return;
    }

    auto appearance = Browser::action_appearance(*m_extension, &m_tab);
    if (!appearance.popup.has_value()) {
        Browser::action_clicked(*m_extension, &m_tab);
        return;
    }
    open_popup();
}

void ActionButton::open_popup()
{
    auto appearance = Browser::action_appearance(*m_extension, &m_tab);
    if (m_popup || !appearance.popup.has_value() || !isVisible())
        return;

    m_popup = new ActionPopup(m_tab, *this, m_extension, *appearance.popup);
    m_popup->on_visibility_change = [button = QPointer(this)](bool visible) {
        if (button)
            button->setDown(visible);
    };
    m_popup->show_popup();
}

void ActionButton::paintEvent(QPaintEvent* event)
{
    QToolButton::paintEvent(event);
    if (m_badge_text.isEmpty())
        return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    auto font = painter.font();
    font.setPixelSize(9);
    font.setBold(true);
    painter.setFont(font);

    auto text = m_badge_text.left(4);
    QFontMetrics metrics(font);
    auto width = max(metrics.horizontalAdvance(text) + 6, 14);
    auto height = 13;
    QRectF badge(rect().right() - width - 2, rect().bottom() - height - 3, width, height);

    QPainterPath path;
    path.addRoundedRect(badge, height / 2.0, height / 2.0);
    painter.fillPath(path, m_badge_background_color);

    auto text_color = m_badge_text_color.value_or(m_badge_background_color.lightnessF() > 0.6 ? Qt::black : Qt::white);
    painter.setPen(text_color);
    painter.drawText(badge, Qt::AlignCenter, text);
}

ActionToolbar::ActionToolbar(QWidget* parent, Tab& tab)
    : QWidget(parent)
    , m_tab(tab)
{
    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);

    install_open_popup_handler();
    toolbars().remove_all_matching([](auto const& toolbar) { return !toolbar; });
    toolbars().append(this);

    Registry::the().on_extensions_change([toolbar = QPointer(this)] {
        if (!toolbar)
            return false;
        toolbar->rebuild();
        return true;
    });
    Browser::on_action_change([toolbar = QPointer(this)] {
        if (!toolbar)
            return false;
        for (int i = 0; i < toolbar->m_layout->count(); ++i) {
            if (auto* button = dynamic_cast<ActionButton*>(toolbar->m_layout->itemAt(i)->widget()))
                button->update_appearance();
        }
        return true;
    });

    rebuild();
}

ActionButton* ActionToolbar::button_for(Extension const& extension) const
{
    for (int i = 0; i < m_layout->count(); ++i) {
        auto* button = dynamic_cast<ActionButton*>(m_layout->itemAt(i)->widget());
        if (button && button->extension().id() == extension.id())
            return button;
    }
    return nullptr;
}

void ActionToolbar::rebuild()
{
    while (auto* item = m_layout->takeAt(0)) {
        if (auto* widget = item->widget())
            widget->deleteLater();
        delete item;
    }

    for (auto const& extension : Registry::the().extensions()) {
        if (extension->manifest().action.has_value())
            m_layout->addWidget(new ActionButton(this, m_tab, extension));
    }
    setVisible(m_layout->count() > 0);
}

}
