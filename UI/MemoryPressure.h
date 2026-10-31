/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Optional.h>
#include <AK/StringView.h>
#include <AK/Types.h>

namespace Ladybird::MemoryPressure {

enum class Level : u8 {
    Normal,
    Low,
    Critical,
};

struct Sample {
    Optional<double> psi_some_avg10 {};
    Optional<u64> available_bytes {};
    Optional<u64> total_bytes {};
};

Level level_for(Sample const&);

Sample sample();

Sample parse_meminfo(StringView);
Optional<double> parse_psi_some_avg10(StringView);

size_t tabs_to_hibernate(Level, size_t eligible_count);

}
