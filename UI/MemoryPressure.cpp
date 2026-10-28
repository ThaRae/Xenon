/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/ByteString.h>
#include <AK/Optional.h>
#include <AK/StringBuilder.h>
#include <AK/StringConversions.h>
#include <AK/StringView.h>
#include <UI/MemoryPressure.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#if defined(AK_OS_LINUX)
#    include <AK/ScopeGuard.h>
#    include <fcntl.h>
#    include <unistd.h>
#elif defined(AK_OS_MACOS)
#    include <mach/mach.h>
#    include <mach/vm_statistics.h>
#    include <sys/sysctl.h>
#endif

namespace Ladybird::MemoryPressure {

static constexpr double LOW_PSI_AVG10 = 5;
static constexpr double CRITICAL_PSI_AVG10 = 15;
static constexpr double LOW_AVAILABLE_FRACTION = 0.20;
static constexpr double CRITICAL_AVAILABLE_FRACTION = 0.10;

static Optional<double> available_fraction(Sample const& sample)
{
    if (!sample.available_bytes.has_value() || !sample.total_bytes.has_value() || *sample.total_bytes == 0)
        return {};
    return static_cast<double>(*sample.available_bytes) / static_cast<double>(*sample.total_bytes);
}

Level level_for(Sample const& sample)
{
    auto fraction = available_fraction(sample);
    if ((sample.psi_some_avg10.has_value() && *sample.psi_some_avg10 >= CRITICAL_PSI_AVG10)
        || (fraction.has_value() && *fraction < CRITICAL_AVAILABLE_FRACTION))
        return Level::Critical;
    if ((sample.psi_some_avg10.has_value() && *sample.psi_some_avg10 >= LOW_PSI_AVG10)
        || (fraction.has_value() && *fraction < LOW_AVAILABLE_FRACTION))
        return Level::Low;
    return Level::Normal;
}

size_t tabs_to_hibernate(Level level, size_t eligible_count)
{
    switch (level) {
    case Level::Normal:
        return 0;
    case Level::Low:
        return min(eligible_count, static_cast<size_t>(1));
    case Level::Critical:
        return eligible_count;
    }
    VERIFY_NOT_REACHED();
}

static Optional<u64> meminfo_bytes(StringView text, StringView name)
{
    auto line_start = text.find(name);
    if (!line_start.has_value())
        return {};
    auto rest = text.substring_view(*line_start + name.length());
    auto kib = AK::parse_first_number<u64>(rest);
    if (!kib.has_value())
        return {};
    return kib->value * 1024;
}

Sample parse_meminfo(StringView text)
{
    Sample sample;
    sample.total_bytes = meminfo_bytes(text, "MemTotal:"sv);
    sample.available_bytes = meminfo_bytes(text, "MemAvailable:"sv);
    return sample;
}

Optional<double> parse_psi_some_avg10(StringView text)
{
    auto line_end = text.find('\n').value_or(text.length());
    auto line = text.substring_view(0, line_end);
    if (!line.starts_with("some "sv))
        return {};
    auto key = line.find("avg10="sv);
    if (!key.has_value())
        return {};
    auto value = line.substring_view(*key + "avg10="sv.length());
    auto end = value.find_any_of(" \t"sv).value_or(value.length());
    char buffer[32] {};
    auto n = min(end, sizeof(buffer) - 1);
    memcpy(buffer, value.characters_without_null_termination(), n);
    errno = 0;
    char* parsed_end = nullptr;
    auto parsed = strtod(buffer, &parsed_end);
    if (errno != 0 || parsed_end == buffer)
        return {};
    return parsed;
}

#if defined(AK_OS_LINUX)

static Optional<ByteString> read_proc_file(char const* path)
{
    auto fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return {};
    ScopeGuard close_fd = [&] { close(fd); };
    StringBuilder builder;
    char buffer[4096];
    for (;;) {
        auto n = read(fd, buffer, sizeof(buffer));
        if (n < 0)
            return {};
        if (n == 0)
            break;
        builder.append(StringView { buffer, static_cast<size_t>(n) });
    }
    return builder.to_byte_string();
}

Sample sample()
{
    Sample sample;
    if (auto meminfo = read_proc_file("/proc/meminfo"); meminfo.has_value())
        sample = parse_meminfo(*meminfo);
    if (auto psi = read_proc_file("/proc/pressure/memory"); psi.has_value())
        sample.psi_some_avg10 = parse_psi_some_avg10(*psi);
    return sample;
}

#elif defined(AK_OS_MACOS)

Sample sample()
{
    Sample sample;
    vm_statistics64_data_t vm {};
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    if (host_statistics64(mach_host_self(), HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&vm), &count) != KERN_SUCCESS)
        return sample;

    u64 page_size = 0;
    size_t page_size_length = sizeof(page_size);
    if (sysctlbyname("hw.pagesize", &page_size, &page_size_length, nullptr, 0) != 0 || page_size == 0)
        page_size = 4096;

    u64 memsize = 0;
    size_t memsize_length = sizeof(memsize);
    if (sysctlbyname("hw.memsize", &memsize, &memsize_length, nullptr, 0) == 0)
        sample.total_bytes = memsize;
    else
        sample.total_bytes = (static_cast<u64>(vm.free_count) + vm.active_count + vm.inactive_count + vm.wire_count) * page_size;

    // Free + inactive + speculative: pages the kernel can reclaim without swapping, as MemAvailable is.
    sample.available_bytes = (static_cast<u64>(vm.free_count) + vm.inactive_count + vm.speculative_count) * page_size;
    return sample;
}

#else

Sample sample()
{
    return {};
}

#endif

}
