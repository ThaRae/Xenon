/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibCore/Environment.h>
#include <UI/Application.h>
#include <UI/Updater.h>

namespace Ladybird::Updater {

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
