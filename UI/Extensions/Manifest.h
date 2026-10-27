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

// Chrome's default for MV3 extension pages, which a manifest may tighten but not loosen.
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

    // Whether this runs in a document of the URL, in a tab's top-level frame or in a frame inside it.
    bool matches_document(URL::URL const&, bool is_top_level_frame) const;
};

// A content script an extension registers while it runs (scripting.registerContentScripts()), in Chrome's terms.
struct RegisteredContentScript {
    String id;
    ContentScript script;
    bool persist_across_sessions { true };

    // Chrome's RegisteredContentScript dictionary, as extensions give it and get it back.
    static ErrorOr<RegisteredContentScript, String> from_json(JsonObject const&);
    JsonObject to_json() const;
};

struct WebAccessibleResources {
    Vector<String> resources;
    Vector<String> matches;
    Vector<String> extension_ids;
    bool use_dynamic_url { false };
};

// Which web pages may message the extension (runtime.sendMessage() and runtime.connect() with its ID), as Chrome
// reads externally_connectable. Other extensions can't message one yet, so `ids` isn't kept.
// https://developer.chrome.com/docs/extensions/reference/manifest/externally-connectable
struct ExternallyConnectable {
    Vector<String> matches;

    // Whether a document of the URL, in any frame, may message the extension.
    bool matches_document(URL::URL const&) const;
};

struct Action {
    Optional<String> default_popup;
    Optional<String> default_title;
    // Smallest first.
    Vector<Icon> default_icon;
};

struct Command {
    String name;
    Optional<String> description;
    // The default key, for the current platform if the manifest gives one.
    Optional<String> suggested_key;
};

// A Manifest V3 manifest.json, validated as Chrome validates it when it loads an unpacked extension. Paths are relative
// to the extension's directory, without a leading slash.
struct Manifest {
    // As parsed, with its localizable strings localized, for chrome.runtime.getManifest().
    JsonObject json;

    String name;
    Optional<String> short_name;
    String version;
    Optional<String> version_name;
    Optional<String> description;
    Optional<String> default_locale;
    // The public key, base64 encoded, which fixes the extension's ID.
    Optional<String> key;
    // Smallest first.
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

    // Things Chrome would load the extension despite, such as keys it doesn't know.
    Vector<String> warnings;

    // Parses a manifest whose __MSG_ placeholders are already resolved. The error says what's wrong, as Chrome's
    // extensions page would.
    static ErrorOr<Manifest, String> parse(JsonObject);

    bool has_permission(StringView) const;
};

// The icon of the given size, or else the next larger one, or else the largest.
Optional<String> best_icon(Vector<Icon> const&, u32 size);

}
