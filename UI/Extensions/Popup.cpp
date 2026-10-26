/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibCore/EventLoop.h>
#include <UI/ChromeStyle.h>
#include <UI/Extensions/Browser.h>
#include <UI/Extensions/Popup.h>
#include <UI/Extensions/Router.h>
#include <UI/StringUtils.h>
#include <UI/Tab.h>
#include <UI/WebContentView.h>

#include <QFrame>
#include <QKeyEvent>
#include <QPointer>
#include <QVBoxLayout>

namespace Ladybird::Extensions {

// Chrome's limits for popups.
static constexpr int MINIMUM_WIDTH = 25;
static constexpr int MINIMUM_HEIGHT = 25;
static constexpr int MAXIMUM_WIDTH = 800;
static constexpr int MAXIMUM_HEIGHT = 600;

// Until the page reports its size.
static constexpr int INITIAL_WIDTH = 360;
static constexpr int INITIAL_HEIGHT = 480;

ActionPopup::ActionPopup(Tab& tab, QWidget& anchor, NonnullRefPtr<Extension> extension, String popup_path)
    : Popover(&tab)
    , m_anchor(anchor)
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
    // Inside the card's border and rounded corners.
    layout->setContentsMargins(1, 1, 1, 1);
    layout->setSpacing(0);

    m_view = new WebContentView(&card());
    layout->addWidget(m_view);
    // NB: Before the page loads, so that its chrome.* knows it's a popup.
    Browser::did_open_popup(m_view->view_id(), tab, *this);

    m_view->on_close = [this] {
        close();
    };

    Router::the().set_popup_size_listener(m_extension->id(), [popup = QPointer(this)](int width, int height) {
        if (popup)
            popup->resize_to(width, height);
    });

    resize_to(INITIAL_WIDTH, INITIAL_HEIGHT);
}

ActionPopup::~ActionPopup()
{
    Router::the().set_popup_size_listener(m_extension->id(), {});
}

void ActionPopup::show_popup()
{
    m_view->load(m_extension->url_for(m_popup_path));

    move_popover_below(*this, m_anchor);
    show();
    raise();
    activateWindow();
    m_view->setFocus(Qt::PopupFocusReason);
}

void ActionPopup::resize_to(int width, int height)
{
    width = clamp(width, MINIMUM_WIDTH, MAXIMUM_WIDTH);
    height = clamp(height, MINIMUM_HEIGHT, MAXIMUM_HEIGHT);
    m_view->setFixedSize(width, height);
    adjustSize();
    if (isVisible())
        move_popover_below(*this, m_anchor);
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

    // A closed popup's page goes away, as in Chrome; the next click opens it afresh. Its ports close then too, which is
    // how an extension's background learns its popup closed.
    // NB: The page is asked to close before its view goes. Once the view is gone, the page is closed on this side, and
    //     the request would have no connection to go out on, leaving WebContent to time out instead.
    Browser::did_close_popup(m_view->view_id());
    m_view->prepare_for_immediate_close()();
    deleteLater();
    Core::deferred_invoke([] { Router::the().prune_closed_contexts(); });
}

}
