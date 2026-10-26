/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteString.h>
#include <AK/Error.h>
#include <AK/JsonObject.h>
#include <AK/NonnullOwnPtr.h>
#include <AK/Optional.h>
#include <AK/String.h>
#include <AK/Vector.h>

namespace Ladybird::Extensions {

// One of an extension's chrome.storage areas. Values are JSON, as Chrome stores them, and each change reports the old
// and new values of the keys it touched, for storage.onChanged.
// https://developer.chrome.com/docs/extensions/reference/api/storage
class StorageArea {
    AK_MAKE_NONCOPYABLE(StorageArea);

public:
    AK_ALLOC_WITH_KMALLOC;

    enum class Kind : u8 {
        Local,
        Session,
        Sync,
        Managed,
    };

    struct Quota {
        size_t bytes { 0 };
        Optional<size_t> bytes_per_item;
        Optional<size_t> max_items;
    };

    // Chrome's quotas. storage.sync is kept on this device, but within sync's quotas, so that an extension behaves as it
    // would in Chrome.
    static Quota quota_for(Kind, bool unlimited_storage);

    // An area kept in a JSON file, or in memory only if there is none (storage.session, storage.managed).
    static ErrorOr<NonnullOwnPtr<StorageArea>> create(Kind, Quota, Optional<ByteString> file);

    Kind kind() const { return m_kind; }

    // Every item, or those with the given keys.
    JsonObject get(Optional<Vector<String>> const& keys) const;
    Vector<String> keys() const;
    size_t bytes_in_use(Optional<Vector<String>> const& keys) const;

    // Each returns the changes it made, as storage.onChanged reports them: {key: {oldValue, newValue}}.
    ErrorOr<JsonObject, String> set(JsonObject const& items);
    JsonObject remove(Vector<String> const& keys);
    JsonObject clear();

private:
    StorageArea(Kind, Quota, Optional<ByteString> file, JsonObject items);

    void save() const;

    Kind m_kind;
    Quota m_quota;
    Optional<ByteString> m_file;
    JsonObject m_items;
};

StringView storage_area_name(StorageArea::Kind);

}
