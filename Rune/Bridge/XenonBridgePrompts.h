/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <Rune/Bridge/XenonBridgePage.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct XenonDatePicker {
    XenonPromptId id;
    XenonStr input_type;
    XenonStr value;
    XenonStr minimum;
    XenonStr maximum;
} XenonDatePicker;

enum XenonWebAuthnState {
    XENON_WEB_AUTHN_INSERT_KEY,
    XENON_WEB_AUTHN_ENTER_PIN,
    XENON_WEB_AUTHN_TOUCH_KEY,
    XENON_WEB_AUTHN_CHOOSE_ACCOUNT,
    XENON_WEB_AUTHN_ATTESTATION_CONSENT,
    XENON_WEB_AUTHN_KEY_NOT_REGISTERED,
    XENON_WEB_AUTHN_KEY_ALREADY_REGISTERED,
    XENON_WEB_AUTHN_PIN_BLOCKED,
    XENON_WEB_AUTHN_PIN_NOT_SET,
    XENON_WEB_AUTHN_KEY_NOT_SUPPORTED,
    XENON_WEB_AUTHN_NO_PLATFORM_AUTHENTICATOR,
};

typedef struct XenonWebAuthnPrompt {
    uint32_t state;
    bool registering;
    XenonStr relying_party;
    XenonStr user_name;
    XenonStr caller;
    bool pin_was_wrong;
    int32_t pin_retries;
    XenonStr const* accounts;
    size_t account_count;
} XenonWebAuthnPrompt;

typedef struct XenonPromptCallbacks {
    void* context;
    void (*find_selection)(void* context, XenonViewId, uint64_t request, XenonStr text);
    void (*tooltip_override)(void* context, XenonViewId, XenonStr text, float x, float y);
    void (*color_picker_requested)(void* context, XenonViewId, XenonPromptId, uint32_t rgb);
    void (*date_picker_requested)(void* context, XenonViewId, XenonDatePicker const*);
    void (*web_authn_prompt)(void* context, XenonViewId, XenonWebAuthnPrompt const*);
    void (*web_authn_prompt_closed)(void* context, XenonViewId);
} XenonPromptCallbacks;

void xenon_engine_set_prompt_callbacks(XenonPromptCallbacks const*);

void xenon_engine_request_find_selection(XenonViewId, uint64_t request);
void xenon_engine_color_picker_update(XenonViewId, XenonPromptId, bool has_color, uint32_t rgb, bool closed);
void xenon_engine_close_date_picker(XenonViewId, XenonPromptId, bool has_value, XenonStr value);
void xenon_engine_web_authn_provide_pin(XenonViewId, XenonStr pin);
void xenon_engine_web_authn_choose_account(XenonViewId, size_t index);
void xenon_engine_web_authn_allow_attestation(XenonViewId, bool allow);
void xenon_engine_web_authn_cancel(XenonViewId);

#ifdef __cplusplus
}
#endif
