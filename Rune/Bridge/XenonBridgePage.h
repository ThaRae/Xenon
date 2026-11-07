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

typedef uint64_t XenonPromptId;

enum XenonDialogKind {
    XENON_DIALOG_ALERT,
    XENON_DIALOG_CONFIRM,
    XENON_DIALOG_PROMPT,
    XENON_DIALOG_BEFORE_UNLOAD,
    XENON_DIALOG_EXTERNAL_URL,
};

typedef struct XenonDialog {
    XenonPromptId id;
    uint32_t kind;
    XenonStr title;
    XenonStr message;
    XenonStr default_text;
    XenonStr accept_label;
    XenonStr cancel_label;
} XenonDialog;

enum XenonMenuItemKind {
    XENON_MENU_ITEM_ACTION,
    XENON_MENU_ITEM_SEPARATOR,
    XENON_MENU_ITEM_HEADING,
    XENON_MENU_ITEM_SUBMENU_BEGIN,
    XENON_MENU_ITEM_SUBMENU_END,
};

typedef struct XenonMenuItem {
    uint32_t kind;
    uint32_t id;
    XenonStr title;
    bool enabled;
    bool checkable;
    bool checked;
    bool indented;
} XenonMenuItem;

typedef struct XenonMenu {
    XenonPromptId id;
    float x;
    float y;
    float minimum_width;
    XenonMenuItem const* items;
    size_t item_count;
} XenonMenu;

typedef struct XenonFileFilter {
    XenonStr name;
    XenonStr extensions;
} XenonFileFilter;

typedef struct XenonFilePicker {
    XenonPromptId id;
    bool allow_multiple;
    XenonFileFilter const* filters;
    size_t filter_count;
} XenonFilePicker;

enum XenonPermissionPromptKind {
    XENON_PERMISSION_PROMPT_QUESTION,
    XENON_PERMISSION_PROMPT_SYSTEM_DENIED_NOTICE,
};

enum XenonPermissionOutcome {
    XENON_PERMISSION_ALLOW_ALWAYS,
    XENON_PERMISSION_ALLOW_ONCE,
    XENON_PERMISSION_BLOCK,
    XENON_PERMISSION_DISMISSED,
};

typedef struct XenonPermissionRequest {
    XenonPromptId id;
    uint32_t kind;
    XenonStr permission;
    XenonStr requester;
} XenonPermissionRequest;

typedef struct XenonPageCallbacks {
    void* context;
    void (*dialog_opened)(void* context, XenonViewId, XenonDialog const*);
    void (*dialog_text_changed)(void* context, XenonViewId, XenonPromptId, XenonStr text);
    void (*dialog_closed)(void* context, XenonViewId, XenonPromptId);
    void (*context_menu_requested)(void* context, XenonViewId, XenonMenu const*);
    void (*select_dropdown_requested)(void* context, XenonViewId, XenonMenu const*);
    void (*file_picker_requested)(void* context, XenonViewId, XenonFilePicker const*);
    void (*find_result)(void* context, XenonViewId, size_t current_match, bool has_total, size_t total_matches);
    void (*tooltip_changed)(void* context, XenonViewId, XenonStr text, float x, float y);
    void (*fullscreen_requested)(void* context, XenonViewId, bool fullscreen);
    void (*permission_requested)(void* context, XenonViewId, XenonPermissionRequest const*);
    void (*permission_cancelled)(void* context, XenonViewId, XenonPromptId);
} XenonPageCallbacks;

void xenon_engine_set_page_callbacks(XenonPageCallbacks const*);

void xenon_engine_close_dialog(XenonViewId, XenonPromptId, bool accepted, XenonStr text);
void xenon_engine_activate_menu_item(XenonViewId, XenonPromptId menu, uint32_t item);
void xenon_engine_close_select_dropdown(XenonViewId, XenonPromptId, bool has_selection, uint32_t item);
void xenon_engine_close_file_picker(XenonViewId, XenonPromptId, XenonStr const* paths, size_t path_count);
void xenon_engine_find_in_page(XenonViewId, XenonStr query, bool case_sensitive, bool highlight_all);
void xenon_engine_find_next(XenonViewId);
void xenon_engine_find_previous(XenonViewId);
void xenon_engine_end_find(XenonViewId);
void xenon_engine_set_fullscreen(XenonViewId, bool fullscreen);
void xenon_engine_answer_permission(XenonViewId, XenonPromptId, uint32_t outcome);
void xenon_engine_request_close(XenonViewId);

#ifdef __cplusplus
}
#endif
