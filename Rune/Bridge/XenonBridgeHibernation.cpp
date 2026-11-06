/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <Rune/Bridge/Engine.h>
#include <Rune/Bridge/TabHibernation.h>
#include <Rune/Bridge/XenonBridgeHibernation.h>

using Xenon::Engine;
using Xenon::TabHibernation;

void xenon_engine_set_hibernation_callbacks(XenonHibernationCallbacks const* callbacks)
{
    Xenon::set_hibernation_callbacks(callbacks ? *callbacks : XenonHibernationCallbacks {});
}

void xenon_engine_hibernate_tab(XenonViewId id)
{
    Engine::the().post([id] {
        auto* hibernation = Engine::the().tab_hibernation();
        if (hibernation && hibernation->may_hibernate(id))
            hibernation->hibernate_when_ready(id, TabHibernation::Request::User);
    });
}

void xenon_engine_set_tab_hibernation(bool enabled, uint64_t timeout_seconds)
{
    Engine::the().post([enabled, timeout_seconds] {
        auto* hibernation = Engine::the().tab_hibernation();
        if (!hibernation)
            return;
        Optional<AK::Duration> timeout;
        if (timeout_seconds > 0)
            timeout = AK::Duration::from_seconds(static_cast<i64>(timeout_seconds));
        hibernation->change_settings(enabled, timeout);
    });
}
