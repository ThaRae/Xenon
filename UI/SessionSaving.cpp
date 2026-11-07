/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibCore/Timer.h>
#include <LibWebView/Application.h>
#include <LibWebView/BrowsingSession.h>
#include <LibWebView/SessionStore.h>
#include <UI/SessionSaving.h>

namespace Ladybird::SessionSaving {

static constexpr auto SAVE_DELAY = AK::Duration::from_milliseconds(1000);

void save_soon()
{
    auto& session_store = *WebView::Application::default_session().session_store;
    if (!session_store.is_persistent())
        return;

    static auto& timer = Core::Timer::create_single_shot(SAVE_DELAY, [] {
        WebView::Application::default_session().session_store->flush_dirty_state();
    }).leak_ref();

    if (!timer.is_active())
        timer.start();
}

}
