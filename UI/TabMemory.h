/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Optional.h>
#include <AK/Types.h>

#include <QString>

namespace Ladybird {

class Tab;

}

namespace Ladybird::TabMemory {

struct Usage {
    u64 bytes { 0 };
    bool is_estimate { false };
};

Optional<Usage> usage(Tab&);

QString description(Tab&);

}
