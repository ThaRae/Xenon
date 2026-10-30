/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Error.h>
#include <AK/JsonObject.h>
#include <AK/Optional.h>
#include <AK/String.h>
#include <AK/Vector.h>
#include <LibURL/URL.h>

namespace Ladybird::Extensions {

constexpr auto DEFAULT_EXTENSION_PAGES_CSP = "script-src 'self' 'wasm-unsafe-eval'; object-src 'self';"sv;
constexpr auto DEFAULT_SANDBOX_CSP = "sandbox allow-scripts allow-forms allow-popups allow-modals; script-src 'self' 'unsafe-inline' 'unsafe-eval'; child-src 'self';"sv;

struct Icon {
    u32 size { 0 };
    String path;
};

enum class RunAt : u8 {
    DocumentStart,
    DocumentEnd,
    DocumentIdle,
};

enum class World : u8 {
    Isolated,
    Main,
};

struct ContentScript {
    Vector<String> matches;
    Vector<String> exclude_matches;
    Vector<String> include_globs;
    Vector<String> exclude_globs;
    Vector<String> js;
    Vector<String> css;
    RunAt run_at { RunAt::DocumentIdle };
    bool all_frames { false };
    bool match_about_blank { false };
    bool match_origin_as_fallback { false };
    World world { World::Isolated };

    bool matches_document(URL::URL const&, bool is_top_level_frame) const;
};

struct RegisteredContentScript {
    String id;
    ContentScript script;
    bool persist_across_sessions { true };

    static ErrorOr<RegisteredContentScript, String> from_json(JsonObject const&);
    JsonObject to_json() const;
};

struct WebAccessibleResources {
    Vector<String> resources;
    Vector<String> matches;
    Vector<String> extension_ids;
    bool use_dynamic_url { false };
};

struct ExternallyConnectable {
    Vector<String> matches;

    bool matches_document(URL::URL const&) const;
};

struct Action {
    Optional<String> default_popup;
    Optional<String> default_title;
    Vector<Icon> default_icon;
};

struct Command {
    String name;
    Optional<String> description;
    Optional<String> suggested_key;
};

struct Manifest {
    JsonObject json;

    String name;
    Optional<String> short_name;
    String version;
    Optional<String> version_name;
    Optional<String> description;
    Optional<String> default_locale;
    Optional<String> key;
    Vector<Icon> icons;

    Optional<Action> action;
    Optional<String> background_service_worker;
    bool background_is_module { false };
    Vector<ContentScript> content_scripts;
    Vector<WebAccessibleResources> web_accessible_resources;
    Optional<ExternallyConnectable> externally_connectable;
    Optional<String> options_page;
    Vector<Command> commands;

    Vector<String> permissions;
    Vector<String> optional_permissions;
    Vector<String> host_permissions;
    Vector<String> optional_host_permissions;

    String extension_pages_csp;
    String sandbox_csp;
    Vector<String> sandbox_pages;

    Vector<String> warnings;

    static ErrorOr<Manifest, String> parse(JsonObject);

    bool has_permission(StringView) const;
};

Optional<String> best_icon(Vector<Icon> const&, u32 size);

}
