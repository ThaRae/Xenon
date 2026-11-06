/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteString.h>
#include <AK/Function.h>
#include <AK/JsonObject.h>
#include <AK/JsonValue.h>
#include <AK/Optional.h>
#include <AK/Span.h>
#include <AK/String.h>
#include <AK/StringView.h>
#include <AK/Vector.h>

namespace Ladybird::XenonSettings {

namespace Keys {

inline constexpr auto compact_toolbar = "layout/compactToolbar"sv;
inline constexpr auto cycle_tabs_in_recently_used_order = "tabs/cycleInRecentlyUsedOrder"sv;
inline constexpr auto tab_hibernation = "tabs/hibernation"sv;
inline constexpr auto tab_hibernation_timeout = "tabs/hibernationTimeout"sv;
inline constexpr auto hibernate_on_memory_pressure = "tabs/hibernateOnMemoryPressure"sv;
inline constexpr auto split_view_drag_and_drop = "splitView/dragAndDrop"sv;
inline constexpr auto new_tab_page_recently_closed = "newTab/recentlyClosed"sv;
inline constexpr auto bangs = "search/bangs"sv;
inline constexpr auto automatic_updates = "updates/automatic"sv;
inline constexpr auto disabled_filter_lists = "blocking/disabledFilterLists"sv;
inline constexpr auto show_blocked_request_count = "blocking/showBlockedRequestCount"sv;
inline constexpr auto blocked_request_total = "blocking/blockedRequestTotal"sv;

}

enum class ValueType : u8 {
    Boolean,
    Integer,
    StringList,
};

struct KnownKey {
    StringView name;
    ValueType type;
};

ReadonlySpan<KnownKey> known_keys();

class Store {
public:
    static Store open(ByteString path, Function<Optional<JsonObject>()> const& import_legacy_values = {});

    static JsonObject import_qsettings_ini(StringView contents);
    static Optional<JsonObject> import_qsettings_ini_file(ByteString const& path);
    static Optional<JsonObject> import_qsettings_preferences(StringView application_id);

    static ByteString default_path();
    static ByteString legacy_ini_path();
    static StringView legacy_preferences_application_id();

    ByteString const& path() const { return m_path; }

    Optional<bool> get_bool(StringView key) const;
    Optional<i64> get_i64(StringView key) const;
    Optional<u64> get_u64(StringView key) const;
    Vector<String> get_strings(StringView key) const;

    void set(StringView key, JsonValue);
    ErrorOr<void> save() const;

private:
    Store(ByteString path, JsonObject values);

    ByteString m_path;
    JsonObject m_values;
};

}
