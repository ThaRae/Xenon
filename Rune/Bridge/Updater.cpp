/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <UI/Updater.h>

namespace Ladybird::Updater {

void initialize()
{
}

ByteString version()
{
    return XENON_VERSION;
}

Status status()
{
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
