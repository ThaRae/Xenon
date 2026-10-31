/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Vector.h>
#include <AK/kmalloc.h>
#include <LibTest/TestCase.h>

#include <string.h>

#if defined(AK_OS_LINUX)
#    include <stdio.h>
#    include <unistd.h>
#endif

#if defined(AK_OS_MACOS)
#    include <mach/mach.h>
#endif

#if defined(AK_OS_LINUX) && !defined(HAS_ADDRESS_SANITIZER)

static Optional<size_t> current_rss_bytes()
{
    auto* file = fopen("/proc/self/statm", "r");
    if (!file)
        return {};
    unsigned long size = 0;
    unsigned long resident = 0;
    auto fields = fscanf(file, "%lu %lu", &size, &resident);
    fclose(file);
    if (fields != 2)
        return {};
    return static_cast<size_t>(resident) * static_cast<size_t>(sysconf(_SC_PAGESIZE));
}

#endif

#if defined(AK_OS_MACOS) && !defined(HAS_ADDRESS_SANITIZER)

static Optional<size_t> current_footprint_bytes()
{
    task_vm_info_data_t info {};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS)
        return {};
    return static_cast<size_t>(info.phys_footprint);
}

#endif

TEST_CASE(collects_after_a_decommitting_collect_still_return_freed_pages)
{
#if defined(AK_OS_MACOS) && !defined(HAS_ADDRESS_SANITIZER)
    static constexpr size_t block_size = 32 * 1024;
    static constexpr size_t block_count = 4096;

    ak_kmalloc_decommit_free_pages();

    Vector<void*> blocks;
    blocks.ensure_capacity(block_count);
    for (size_t index = 0; index < block_count; ++index) {
        auto* block = kmalloc(block_size);
        EXPECT(block != nullptr);
        if (!block)
            break;
        memset(block, 0x5a, block_size);
        blocks.unchecked_append(block);
    }

    auto footprint_allocated = current_footprint_bytes();
    for (auto* block : blocks)
        kfree(block);
    ak_kmalloc_collect();
    auto footprint_released = current_footprint_bytes();

    EXPECT(footprint_allocated.has_value());
    EXPECT(footprint_released.has_value());
    if (!footprint_allocated.has_value() || !footprint_released.has_value())
        return;
    EXPECT(*footprint_released + 64 * 1024 * 1024 <= *footprint_allocated);
#endif
}

TEST_CASE(decommitting_collect_returns_freed_pages_to_the_os)
{
#if defined(AK_OS_LINUX) && !defined(HAS_ADDRESS_SANITIZER)
    static constexpr size_t block_size = 32 * 1024;
    static constexpr size_t block_count = 4096;

    auto rss_before = current_rss_bytes();
    EXPECT(rss_before.has_value());
    if (!rss_before.has_value())
        return;

    Vector<void*> blocks;
    for (size_t index = 0; index < block_count; ++index) {
        auto* block = kmalloc(block_size);
        EXPECT(block != nullptr);
        if (!block)
            break;
        memset(block, 0x5a, block_size);
        blocks.append(block);
    }

    auto rss_allocated = current_rss_bytes();
    EXPECT(rss_allocated.has_value());

    for (auto* block : blocks)
        kfree(block);
    ak_kmalloc_decommit_free_pages();

    auto rss_released = current_rss_bytes();
    EXPECT(rss_released.has_value());
    if (!rss_allocated.has_value() || !rss_released.has_value())
        return;

    EXPECT(*rss_released + 64 * 1024 * 1024 <= *rss_allocated);
    EXPECT(*rss_allocated > *rss_before);
#endif
}

TEST_CASE(allocations_work_after_a_decommitting_collect)
{
    ak_kmalloc_decommit_free_pages();

    static constexpr size_t size = 64 * 1024;
    auto* block = static_cast<unsigned char*>(kmalloc(size));
    EXPECT(block != nullptr);
    if (!block)
        return;

    memset(block, 0xa5, size);
    EXPECT_EQ(block[0], 0xa5);
    EXPECT_EQ(block[size - 1], 0xa5);
    kfree(block);
}
