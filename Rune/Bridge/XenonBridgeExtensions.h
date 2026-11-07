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

typedef struct XenonExtensionAction {
    XenonStr extension_id;
    XenonStr name;
    XenonStr title;
    XenonStr icon_path;
    XenonStr badge_text;
    uint32_t badge_background;
    bool has_badge_text_color;
    uint32_t badge_text_color;
    bool has_popup;
    bool enabled;
    bool has_options_page;
} XenonExtensionAction;

typedef struct XenonExtensionInfo {
    XenonStr id;
    XenonStr name;
    XenonStr version;
    XenonStr description;
    XenonStr details;
    XenonStr warnings;
    XenonStr icon_path;
    bool has_options_page;
} XenonExtensionInfo;

typedef struct XenonPasswordManagerChoice {
    XenonStr id;
    XenonStr name;
} XenonPasswordManagerChoice;

typedef struct XenonPasswordManagers {
    XenonPasswordManagerChoice const* choices;
    size_t choice_count;
    XenonStr current;
    XenonStr installing;
    XenonStr status;
} XenonPasswordManagers;

typedef struct XenonExtensionCommand {
    XenonStr extension_id;
    XenonStr command;
    XenonStr shortcut;
} XenonExtensionCommand;

typedef struct XenonExtensionPermissionPrompt {
    XenonPromptId id;
    XenonStr extension_name;
    XenonStr const* warnings;
    size_t warning_count;
    XenonViewId tab;
    XenonViewId popup;
} XenonExtensionPermissionPrompt;

typedef struct XenonExtensionCallbacks {
    void* context;
    void (*actions_changed)(void* context, XenonViewId tab, XenonExtensionAction const*, size_t count);
    void (*popup_opened)(void* context, XenonViewId tab, XenonStr extension_id, XenonViewId popup, uint32_t width, uint32_t height);
    void (*popup_resized)(void* context, XenonViewId popup, uint32_t width, uint32_t height);
    void (*popup_closed)(void* context, XenonViewId popup);
    void (*permission_prompt)(void* context, XenonExtensionPermissionPrompt const*);
    void (*extensions_changed)(void* context, XenonExtensionInfo const*, size_t count);
    void (*password_managers_changed)(void* context, XenonPasswordManagers const*);
    void (*commands_changed)(void* context, XenonExtensionCommand const*, size_t count);
    void (*show_extensions)(void* context);
    void (*error)(void* context, XenonStr title, XenonStr message);
} XenonExtensionCallbacks;

void xenon_engine_set_extension_callbacks(XenonExtensionCallbacks const*);

void xenon_engine_extension_action_clicked(XenonViewId tab, XenonStr extension_id);
void xenon_engine_close_extension_popup(XenonViewId popup);
void xenon_engine_answer_extension_permissions(XenonPromptId, bool granted);
void xenon_engine_run_extension_command(XenonViewId tab, XenonStr extension_id, XenonStr command);
void xenon_engine_open_extension_options(XenonStr extension_id);
void xenon_engine_reload_extension(XenonStr extension_id);
void xenon_engine_remove_extension(XenonStr extension_id);
void xenon_engine_load_unpacked_extension(XenonStr directory);
void xenon_engine_choose_password_manager(XenonStr id);
void xenon_engine_show_extensions(void);

#ifdef __cplusplus
}
#endif
