/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <UI/Updater.h>

// Xenon updates itself on macOS only (UI/Updater.mm). Elsewhere it's updated the way it was installed.
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
}

QAction* create_check_for_updates_action(QObject&)
{
    return nullptr;
}

}
