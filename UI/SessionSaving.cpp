/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWebView/Application.h>
#include <LibWebView/BrowsingSession.h>
#include <LibWebView/SessionStore.h>
#include <UI/SessionSaving.h>

#include <QApplication>
#include <QTimer>

namespace Ladybird::SessionSaving {

static constexpr int SAVE_DELAY_MILLISECONDS = 1000;

void save_soon()
{
    auto& session_store = *WebView::Application::default_session().session_store;
    if (!session_store.is_persistent())
        return;

    static auto* timer = [] {
        auto* save_timer = new QTimer(QApplication::instance());
        save_timer->setSingleShot(true);
        save_timer->setInterval(SAVE_DELAY_MILLISECONDS);
        QObject::connect(save_timer, &QTimer::timeout, save_timer, [] {
            WebView::Application::default_session().session_store->flush_dirty_state();
        });
        return save_timer;
    }();

    if (!timer->isActive())
        timer->start();
}

}
