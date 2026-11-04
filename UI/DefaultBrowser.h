/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Function.h>

namespace Ladybird::DefaultBrowser {

enum class Status {
    IsDefault,
    IsNotDefault,
    Unsupported,
};

Status status();
void request_default(Function<void()> on_done = {});

}
