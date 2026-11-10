/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Optional.h>
#include <QSize>

namespace Ladybird::Extensions {

constexpr inline QSize MINIMUM_POPUP_SIZE { 25, 25 };
constexpr inline QSize MAXIMUM_POPUP_SIZE { 800, 600 };
constexpr inline QSize INITIAL_POPUP_SIZE { 360, 480 };

bool is_usable_popup_size(QSize reported);
Optional<QSize> popup_size_for(QSize reported, QSize limit);

}
