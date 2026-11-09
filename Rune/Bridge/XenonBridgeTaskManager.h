/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <Rune/Bridge/XenonBridge.h>

#ifdef __cplusplus
extern "C" {
#endif

enum XenonProcessIcon {
    XENON_PROCESS_ICON_BROWSER,
    XENON_PROCESS_ICON_PAGE,
    XENON_PROCESS_ICON_SPARE,
    XENON_PROCESS_ICON_SERVICE,
};

typedef struct XenonProcess {
    uint64_t child_id;
    int64_t pid;
    uint32_t icon;
    bool is_browser;
    bool can_end;
    XenonStr name;
    double cpu_percent;
    uint64_t memory_bytes;
    XenonStr favicon_hash;
    uint8_t const* favicon_png;
    size_t favicon_png_length;
} XenonProcess;

typedef struct XenonTaskManagerCallbacks {
    void* context;
    void (*open_requested)(void* context);
    void (*processes_changed)(void* context, XenonProcess const*, size_t count);
    void (*end_process_failed)(void* context, XenonStr message);
} XenonTaskManagerCallbacks;

void xenon_engine_set_task_manager_callbacks(XenonTaskManagerCallbacks const*);

void xenon_engine_task_manager_set_visible(bool visible);
void xenon_engine_task_manager_end_process(uint64_t child_id);
void xenon_engine_task_manager_copy_pid(uint64_t child_id);

#ifdef __cplusplus
}
#endif
