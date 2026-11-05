/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Format.h>
#include <AK/IterationDecision.h>
#include <LibCore/Timer.h>
#include <LibWebView/WebContentClient.h>
#include <UI/MemoryPressure.h>
#include <UI/MemoryPressureRelease.h>

namespace Ladybird::MemoryPressureRelease {

static constexpr auto SAMPLE_INTERVAL = AK::Duration::from_seconds(5);

static StringView level_name(MemoryPressure::Level level)
{
    switch (level) {
    case MemoryPressure::Level::Normal:
        return "normal"sv;
    case MemoryPressure::Level::Low:
        return "low"sv;
    case MemoryPressure::Level::Critical:
        return "critical"sv;
    }
    VERIFY_NOT_REACHED();
}

static void ask_web_content_processes_for_unused_memory(MemoryPressure::Level level)
{
    size_t process_count = 0;
    WebView::WebContentClient::for_each_client([&](WebView::WebContentClient& client) {
        client.async_release_unused_memory();
        ++process_count;
        return IterationDecision::Continue;
    });
    dbgln("Memory is {}: asked {} WebContent processes for the memory they don't use", level_name(level), process_count);
}

void start()
{
    static MemoryPressure::MemoryReleaseTrigger trigger;
    static auto& timer = Core::Timer::create_repeating(SAMPLE_INTERVAL, [] {
        auto level = MemoryPressure::level_for(MemoryPressure::sample());
        if (trigger.should_release_memory(level, MonotonicTime::now()))
            ask_web_content_processes_for_unused_memory(level);
    }).leak_ref();
    timer.start();
}

}
