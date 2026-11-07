/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <Rune/Bridge/XenonBridge.h>
#include <Rune/Bridge/XenonBridgePage.h>

#ifdef __cplusplus
extern "C" {
#endif

enum XenonBookmarkItemKind {
    XENON_BOOKMARK_ITEM_BOOKMARK,
    XENON_BOOKMARK_ITEM_FOLDER_BEGIN,
    XENON_BOOKMARK_ITEM_FOLDER_END,
};

typedef struct XenonBookmarkItem {
    uint32_t kind;
    XenonStr id;
    XenonStr title;
    XenonStr url;
    uint8_t const* favicon_png;
    size_t favicon_png_length;
} XenonBookmarkItem;

enum XenonBookmarksBarVisibility {
    XENON_BOOKMARKS_BAR_ALWAYS,
    XENON_BOOKMARKS_BAR_NEVER,
    XENON_BOOKMARKS_BAR_ON_HOVER,
};

typedef struct XenonBookmarksBarSettings {
    uint32_t visibility;
    uint32_t hover_expand_delay_ms;
    uint32_t hover_expand_animation_ms;
} XenonBookmarksBarSettings;

enum XenonBookmarkOpenDisposition {
    XENON_BOOKMARK_OPEN_IN_CURRENT_TAB,
    XENON_BOOKMARK_OPEN_IN_NEW_TAB,
    XENON_BOOKMARK_OPEN_IN_NEW_BACKGROUND_TAB,
};

enum XenonBookmarkDialogKind {
    XENON_BOOKMARK_DIALOG_ADD_BOOKMARK,
    XENON_BOOKMARK_DIALOG_EDIT_BOOKMARK,
    XENON_BOOKMARK_DIALOG_ADD_FOLDER,
    XENON_BOOKMARK_DIALOG_EDIT_FOLDER,
};

typedef struct XenonBookmarkFolderOption {
    XenonStr id;
    XenonStr path;
} XenonBookmarkFolderOption;

typedef struct XenonBookmarkDialog {
    uint64_t id;
    uint32_t kind;
    XenonStr url;
    XenonStr title;
    XenonStr selected_folder_id;
    XenonBookmarkFolderOption const* folders;
    size_t folder_count;
} XenonBookmarkDialog;

typedef struct XenonBookmarksCallbacks {
    void* context;
    void (*bookmarks_changed)(void* context, XenonBookmarkItem const*, size_t count);
    void (*bar_settings_changed)(void* context, XenonBookmarksBarSettings const*);
    void (*context_menu_requested)(void* context, XenonViewId, XenonMenu const*, bool at_page_position);
    void (*dialog_opened)(void* context, XenonBookmarkDialog const*);
} XenonBookmarksCallbacks;

void xenon_engine_set_bookmarks_callbacks(XenonBookmarksCallbacks const*);

void xenon_engine_bookmarks_start(void);
void xenon_engine_open_bookmark(XenonViewId, XenonStr id, uint32_t disposition);
void xenon_engine_open_bookmark_folder(XenonStr id);
void xenon_engine_show_bookmark_context_menu(XenonViewId, XenonStr id, XenonStr target_folder_id, XenonStr parent_folder_id, float x, float y);
void xenon_engine_activate_bookmark_menu_item(uint64_t menu, uint32_t item);
void xenon_engine_close_bookmark_dialog(uint64_t dialog, bool accepted, XenonStr url, XenonStr title, XenonStr folder_id);

#ifdef __cplusplus
}
#endif
