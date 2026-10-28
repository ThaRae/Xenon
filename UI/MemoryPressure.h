/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Optional.h>
#include <AK/StringView.h>
#include <AK/Types.h>

// Whether the machine is short of memory, for TabHibernation: Chromium's Memory Saver discards background tabs on
// memory pressure as well as after a timeout. Xenon used to wait for the timeout alone.
namespace Ladybird::MemoryPressure {

enum class Level : u8 {
    Normal,
    // Hibernate the least recently used eligible tab.
    Low,
    // Hibernate every eligible background tab.
    Critical,
};

struct Sample {
    Optional<double> psi_some_avg10 {};
    Optional<u64> available_bytes {};
    Optional<u64> total_bytes {};
};

// PSI "some" avg10 of 5% (15% for critical), or MemAvailable below 20% (10%) of MemTotal. A sample that has neither
// figure is Normal.
Level level_for(Sample const&);

// Linux: /proc/meminfo and /proc/pressure/memory. macOS: host_statistics64. Nothing: an empty sample.
Sample sample();

Sample parse_meminfo(StringView);
Optional<double> parse_psi_some_avg10(StringView);

// How many of `eligible_count` tabs to hibernate, oldest first.
size_t tabs_to_hibernate(Level, size_t eligible_count);

}
