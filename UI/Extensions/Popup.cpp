/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibCore/EventLoop.h>
#include <UI/ChromeStyle.h>
#include <UI/Extensions/Browser.h>
#include <UI/Extensions/Popup.h>
#include <UI/Extensions/PopupSize.h>
#include <UI/Extensions/Router.h>
#include <UI/StringUtils.h>
#include <UI/Tab.h>
#include <UI/WebAuthnPopover.h>
#include <UI/WebContentView.h>

#include <QFrame>
#include <QKeyEvent>
#include <QPointer>
#include <QScreen>
#include <QVBoxLayout>

namespace Ladybird::Extensions {

ActionPopup::ActionPopup(Tab& tab, QWidget& anchor, NonnullRefPtr<Extension> extension, String popup_path)
    : Popover(&tab)
    , m_anchor(&anchor)
    , m_extension(AK::move(extension))
    , m_popup_path(AK::move(popup_path))
{
    setAccessibleName(qstring_from_ak_string(m_extension->manifest().name));
    card().setObjectName("XenonExtensionPopup");

    auto palette = tab.palette();
    card().setStyleSheet(qformatted(R"(
QFrame#XenonExtensionPopup {{
    background: {0};
    border: 1px solid {1};
    border-radius: 8px;
}}
)",
        ChromeStyle::style_sheet_color(ChromeStyle::chrome_surface(palette)),
        ChromeStyle::style_sheet_color(ChromeStyle::chrome_border(palette))));

    auto* layout = new QVBoxLayout(&card());
    layout->setContentsMargins(1, 1, 1, 1);
    layout->setSpacing(0);

    WebContentViewInitialState initial_state;
    initial_state.enable_content_blocker = WebView::EnableContentBlocker::No;
    m_view = new WebContentView(&card(), {}, initial_state);
    layout->addWidget(m_view);
    Browser::did_open_popup(m_view->view_id(), tab, *this);

    m_view->on_close = [this] {
        close();
    };
    WebAuthnPopover::attach(*m_view);

    Router::the().set_popup_size_listener(m_extension->id(), [popup = QPointer(this)](int width, int height) {
        if (popup)
            popup->resize_to(width, height);
    });

    resize_to(INITIAL_POPUP_SIZE.width(), INITIAL_POPUP_SIZE.height());
}

ActionPopup::~ActionPopup()
{
    Router::the().set_popup_size_listener(m_extension->id(), {});
}

void ActionPopup::show_popup()
{
    if (!m_anchor) {
        close();
        return;
    }

    m_view->load(m_extension->url_for(m_popup_path));

    move_popover_below(*this, *m_anchor);
    show();
    raise();
    activateWindow();
    m_view->setFocus(Qt::PopupFocusReason);
}

void ActionPopup::resize_to(int width, int height)
{
    if (!m_anchor) {
        close();
        return;
    }

    auto limit = MAXIMUM_POPUP_SIZE;
    QScreen* popup_screen = screen();
    if (!popup_screen)
        popup_screen = m_anchor->screen();
    if (!popup_screen && parentWidget())
        popup_screen = parentWidget()->screen();
    if (popup_screen)
        limit = popup_screen->availableGeometry().size() - QSize(4 * POPOVER_SHADOW_MARGIN, 4 * POPOVER_SHADOW_MARGIN);

    auto size = popup_size_for({ width, height }, limit);
    if (!size.has_value() || *size == m_view->size())
        return;

    m_view->setFixedSize(*size);
    adjustSize();
    if (isVisible())
        move_popover_below(*this, *m_anchor);
}

void ActionPopup::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        close();
        return;
    }
    Popover::keyPressEvent(event);
}

void ActionPopup::hideEvent(QHideEvent* event)
{
    Popover::hideEvent(event);

    Browser::did_close_popup(m_view->view_id());
    m_view->prepare_for_immediate_close();
    deleteLater();
    Core::deferred_invoke([] { Router::the().prune_closed_contexts(); });
}

}
