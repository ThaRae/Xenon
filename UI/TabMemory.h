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

// How much memory a tab uses, for its hover card, as Chromium's hover card says. A tab's memory is the footprint of the
// processes that serve it: the WebContent processes that host its pages and frames, the workers only it runs, and the
// image decoders those processes started. A process that serves several tabs, as a page and the popup it opened share
// one, is split evenly between them.
namespace Ladybird::TabMemory {

struct Usage {
    u64 bytes { 0 };
    // Whether part of it is a share of a process the tab shares with other tabs.
    bool is_estimate { false };
};

// A tab that hasn't loaded, or that hibernated, has no processes, and so no usage.
Optional<Usage> usage(Tab&);

// The hover card's line about the tab's memory.
QString description(Tab&);

}
