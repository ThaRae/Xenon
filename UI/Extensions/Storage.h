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

    static Quota quota_for(Kind, bool unlimited_storage);

    static ErrorOr<NonnullOwnPtr<StorageArea>> create(Kind, Quota, Optional<ByteString> file);

    Kind kind() const { return m_kind; }

    JsonObject get(Optional<Vector<String>> const& keys) const;
    Vector<String> keys() const;
    size_t bytes_in_use(Optional<Vector<String>> const& keys) const;

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
