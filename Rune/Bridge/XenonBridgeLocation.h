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

enum XenonSiteSecurityState {
    XENON_SITE_SECURITY_NONE,
    XENON_SITE_SECURITY_SECURE,
    XENON_SITE_SECURITY_NOT_SECURE,
    XENON_SITE_SECURITY_ACCEPTED_CERTIFICATE,
};

typedef struct XenonLocationUrl {
    XenonStr url;
    XenonStr scheme;
    bool has_parts;
    XenonStr scheme_and_subdomain;
    XenonStr registrable_domain;
    XenonStr remainder;
    bool hidden;
    uint32_t security;
    XenonStr search_engine;
} XenonLocationUrl;

enum XenonLocationInputKind {
    XENON_LOCATION_INPUT_EMPTY,
    XENON_LOCATION_INPUT_URL,
    XENON_LOCATION_INPUT_SEARCH,
    XENON_LOCATION_INPUT_OTHER,
};

typedef struct XenonLocationInput {
    uint32_t kind;
    XenonStr bang_name;
    size_t bang_length;
} XenonLocationInput;

enum XenonSuggestionKind {
    XENON_SUGGESTION_URL,
    XENON_SUGGESTION_INTERNAL_PAGE,
    XENON_SUGGESTION_HISTORY,
    XENON_SUGGESTION_BOOKMARK,
    XENON_SUGGESTION_ADAPTIVE,
    XENON_SUGGESTION_SEARCH,
    XENON_SUGGESTION_CHOOSE_SEARCH_ENGINE,
};

typedef struct XenonTextRange {
    size_t start;
    size_t length;
} XenonTextRange;

typedef struct XenonLocationSuggestion {
    uint32_t kind;
    XenonStr text;
    XenonStr primary;
    XenonTextRange const* primary_matches;
    size_t primary_match_count;
    XenonStr secondary;
    XenonTextRange const* secondary_matches;
    size_t secondary_match_count;
    bool emphasize_origin;
    uint8_t const* favicon_png;
    size_t favicon_png_length;
} XenonLocationSuggestion;

typedef struct XenonSiteSecurity {
    uint32_t state;
    bool is_final;
    bool has_connection;
    XenonStr host;
    XenonStr protocol;
    XenonStr cipher;
    bool has_certificate;
    XenonStr issued_to;
    XenonStr issued_by;
    XenonStr valid_from;
    XenonStr valid_until;
    XenonStr fingerprint;
    XenonStr const* chain;
    size_t chain_length;
} XenonSiteSecurity;

enum XenonLocationCommand {
    XENON_LOCATION_SELECT_NEXT,
    XENON_LOCATION_SELECT_PREVIOUS,
    XENON_LOCATION_ACCEPT_COMPLETION,
    XENON_LOCATION_ESCAPE,
    XENON_LOCATION_DISMISS_POPUP,
    XENON_LOCATION_DISMISS_BANG,
    XENON_LOCATION_WILL_DELETE,
};

typedef struct XenonLocationCallbacks {
    void* context;
    void (*url_described)(void* context, XenonViewId, XenonLocationUrl const*);
    void (*display_changed)(void* context, XenonViewId, uint64_t generation, XenonStr text, int64_t selection_start);
    void (*input_changed)(void* context, XenonViewId, uint64_t generation, XenonLocationInput const*);
    void (*suggestions_changed)(void* context, XenonViewId, uint64_t generation, XenonLocationSuggestion const*, size_t count, bool visible, int64_t selected);
    void (*selection_changed)(void* context, XenonViewId, uint64_t generation, int64_t selected);
    void (*committed)(void* context, XenonViewId, XenonStr text);
    void (*site_security)(void* context, XenonViewId, XenonSiteSecurity const*);
} XenonLocationCallbacks;

void xenon_engine_set_location_callbacks(XenonLocationCallbacks const*);

void xenon_engine_location_describe_url(XenonViewId, XenonStr url);
void xenon_engine_location_begin_editing(XenonViewId, uint64_t generation, XenonStr text);
void xenon_engine_location_end_editing(XenonViewId, uint64_t generation);
void xenon_engine_location_text_edited(XenonViewId, uint64_t generation, XenonStr text, bool cursor_at_end);
void xenon_engine_location_cursor_moved(XenonViewId, uint64_t generation, bool cursor_at_end);
void xenon_engine_location_command(XenonViewId, uint64_t generation, uint32_t command);
void xenon_engine_location_submit(XenonViewId, uint64_t generation, uint32_t modifiers);
void xenon_engine_location_activate_suggestion(XenonViewId, uint64_t generation, size_t index);
void xenon_engine_location_request_site_security(XenonViewId);

#ifdef __cplusplus
}
#endif
