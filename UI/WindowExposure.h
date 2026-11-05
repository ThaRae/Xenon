/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

class QWidget;

namespace Ladybird::WindowExposure {

void watch(QWidget& window);

bool hides_pages_in(QWidget const* window);

}
