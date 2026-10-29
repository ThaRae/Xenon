/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibCore/Environment.h>
#include <UI/Application.h>
#include <UI/Updater.h>

// Xenon updates itself on macOS only (UI/Updater.mm). Elsewhere it's updated the way it was installed.
namespace Ladybird::Updater {

// XENON_TEST_UPDATE_READY's stand-in for an update that's ready (UI/Updater.h).
static Optional<ByteString> const& test_ready_version()
{
    static auto const version = []() -> Optional<ByteString> {
        auto value = Core::Environment::get("XENON_TEST_UPDATE_READY"sv);
        if (!value.has_value() || value->is_empty())
            return {};
        return ByteString { *value };
    }();
    return version;
}

void initialize()
{
}

ByteString version()
{
    return XENON_VERSION;
}

Status status()
{
    if (auto const& stand_in = test_ready_version(); stand_in.has_value()) {
        Status status;
        status.availability = Availability::Available;
        status.ready_version = stand_in;
        return status;
    }

    // NB: A status is unsupported unless it says otherwise.
    return {};
}

void on_status_change(Function<bool()>)
{
}

void check_for_updates()
{
}

void install_and_relaunch()
{
    // The stand-in has nothing to install, so Xenon only quits.
    if (auto const& stand_in = test_ready_version(); stand_in.has_value()) {
        dbgln("Relaunching to update to Xenon {}, which XENON_TEST_UPDATE_READY stands in for, so Xenon only quits", *stand_in);
        Application::the().quit();
    }
}

bool relaunched_to_update()
{
    return false;
}

QAction* create_check_for_updates_action(QObject&)
{
    return nullptr;
}

}
