/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/LexicalPath.h>
#include <UI/Extensions/Popup.h>
#include <UI/Extensions/Registry.h>
#include <UI/Extensions/Toolbar.h>
#include <UI/StringUtils.h>

#include <QHBoxLayout>
#include <QIcon>

namespace Ladybird::Extensions {

// Toolbar icons are drawn at 16 points, so the 32-pixel icon serves both normal and high density screens.
static constexpr u32 TOOLBAR_ICON_PIXELS = 32;

static QIcon action_icon(Extension const& extension)
{
    auto const& action = *extension.manifest().action;
    auto path = best_icon(action.default_icon, TOOLBAR_ICON_PIXELS);
    if (!path.has_value())
        path = best_icon(extension.manifest().icons, TOOLBAR_ICON_PIXELS);
    if (!path.has_value())
        return QIcon::fromTheme("application-x-addon");

    auto file = LexicalPath::join(extension.root(), path->bytes_as_string_view()).string();
    return QIcon(qstring_from_ak_string(file));
}

ActionButton::ActionButton(QWidget* parent, QWidget& popup_parent, NonnullRefPtr<Extension> extension)
    : QToolButton(parent)
    , m_popup_parent(popup_parent)
    , m_extension(AK::move(extension))
{
    auto const& manifest = m_extension->manifest();
    auto title = manifest.action->default_title.value_or(manifest.name);

    setObjectName("XenonExtensionActionButton");
    setAutoRaise(true);
    setIcon(action_icon(*m_extension));
    setIconSize({ 16, 16 });
    setToolTip(qstring_from_ak_string(title));
    setAccessibleName(qstring_from_ak_string(title));

    QObject::connect(this, &QToolButton::clicked, this, [this] { toggle_popup(); });
}

void ActionButton::toggle_popup()
{
    if (m_popup) {
        m_popup->close();
        return;
    }

    // FIXME: Without a popup, a click fires action.onClicked in the background worker, which comes with milestone 2.
    if (!m_extension->manifest().action->default_popup.has_value())
        return;

    m_popup = new ActionPopup(&m_popup_parent, *this, m_extension);
    m_popup->on_visibility_change = [button = QPointer(this)](bool visible) {
        if (button)
            button->setDown(visible);
    };
    m_popup->show_popup();
}

ActionToolbar::ActionToolbar(QWidget* parent, QWidget& popup_parent)
    : QWidget(parent)
    , m_popup_parent(popup_parent)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // FIXME: Update the buttons when extensions are loaded or removed while the browser runs.
    for (auto const& extension : Registry::the().extensions()) {
        if (extension->manifest().action.has_value())
            layout->addWidget(new ActionButton(this, m_popup_parent, extension));
    }
    setVisible(layout->count() > 0);
}

}
