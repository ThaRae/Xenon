/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Types.h>

class QWidget;

namespace Ladybird::RelaunchToUpdate {

size_t private_window_count();

void relaunch(QWidget* parent);

}
