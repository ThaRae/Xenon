/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/String.h>

class QWidget;

namespace Ladybird::ElementPicker {

void confirm_filter(QWidget& parent, String const& filter);

}
