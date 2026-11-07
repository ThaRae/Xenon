/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <Rune/Bridge/Engine.h>
#include <Rune/Bridge/UpdaterBridge.h>
#include <Rune/Bridge/XenonBridgeUpdater.h>

namespace Xenon {

static XenonUpdaterCallbacks s_updater_callbacks {};

void report_update_status(Ladybird::Updater::Status const& status)
{
    if (!s_updater_callbacks.status_changed)
        return;
    auto version = status.ready_version.value_or({});
    s_updater_callbacks.status_changed(s_updater_callbacks.context, { version.characters(), version.length() }, status.checking);
}

}

void xenon_engine_set_updater_callbacks(XenonUpdaterCallbacks const* callbacks)
{
    Xenon::s_updater_callbacks = callbacks ? *callbacks : XenonUpdaterCallbacks {};
}

void xenon_engine_install_update(void)
{
    Xenon::Engine::the().post([] {
        Ladybird::Updater::install_and_relaunch();
    });
}

void xenon_engine_check_for_updates(void)
{
    Xenon::Engine::the().post([] {
        Ladybird::Updater::check_for_updates();
    });
}
