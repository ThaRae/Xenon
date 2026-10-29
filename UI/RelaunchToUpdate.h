/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Types.h>

class QWidget;

// Relaunch to update, from the toolbar's Update button (UI/UpdateButton.h), the Xenon menu, or Settings. Every window
// reopens afterwards (Updater::relaunched_to_update()) except private ones, which Xenon says first. Pages with unsaved
// changes, and downloads that can't resume, ask as they do whenever Xenon quits.
namespace Ladybird::RelaunchToUpdate {

// The private windows that are open, which don't reopen after the relaunch.
size_t private_window_count();

// Relaunches Xenon to install the update that's ready (Updater::install_and_relaunch()), once the user agrees to close the
// private windows, if any are open. The Update button's popover says so itself, and relaunches at once.
void relaunch(QWidget* parent);

}
