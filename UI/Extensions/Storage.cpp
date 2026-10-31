/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/JsonValue.h>
#include <AK/LexicalPath.h>
#include <LibCore/Directory.h>
#include <LibCore/File.h>
#include <LibCore/System.h>
#include <LibFileSystem/FileSystem.h>
#include <UI/Extensions/Storage.h>

namespace Ladybird::Extensions {

static constexpr size_t MIB = 1024 * 1024;

StringView storage_area_name(StorageArea::Kind kind)
{
    switch (kind) {
    case StorageArea::Kind::Local:
        return "local"sv;
    case StorageArea::Kind::Session:
        return "session"sv;
    case StorageArea::Kind::Sync:
        return "sync"sv;
    case StorageArea::Kind::Managed:
        return "managed"sv;
    }
    VERIFY_NOT_REACHED();
}

StorageArea::Quota StorageArea::quota_for(Kind kind, bool unlimited_storage)
{
    switch (kind) {
    case Kind::Local:
        return { .bytes = unlimited_storage ? NumericLimits<size_t>::max() : 10 * MIB, .bytes_per_item = {}, .max_items = {} };
    case Kind::Session:
        return { .bytes = 10 * MIB, .bytes_per_item = {}, .max_items = {} };
    case Kind::Sync:
        return { .bytes = 102'400, .bytes_per_item = 8'192, .max_items = 512 };
    case Kind::Managed:
        return { .bytes = 0, .bytes_per_item = {}, .max_items = {} };
    }
    VERIFY_NOT_REACHED();
}

ErrorOr<NonnullOwnPtr<StorageArea>> StorageArea::create(Kind kind, Quota quota, Optional<ByteString> file)
{
    JsonObject items;
    if (file.has_value() && FileSystem::exists(*file)) {
        auto contents = TRY(TRY(Core::File::open(*file, Core::File::OpenMode::Read))->read_until_eof());
        auto json = JsonValue::from_string(contents);
        if (!json.is_error() && json.value().is_object())
            items = json.release_value().as_object();
        else
            warnln("Extensions: ignoring unreadable storage file {}", *file);
    }
    return adopt_own(*new StorageArea(kind, quota, move(file), move(items)));
}

StorageArea::StorageArea(Kind kind, Quota quota, Optional<ByteString> file, JsonObject items)
    : m_kind(kind)
    , m_quota(quota)
    , m_file(move(file))
    , m_items(move(items))
{
}

JsonObject StorageArea::get(Optional<Vector<String>> const& keys) const
{
    if (!keys.has_value())
        return m_items;

    JsonObject result;
    for (auto const& key : *keys) {
        if (auto value = m_items.get(key); value.has_value())
            result.set(key, *value);
    }
    return result;
}

Vector<String> StorageArea::keys() const
{
    Vector<String> keys;
    m_items.for_each_member([&](String const& key, JsonValue const&) { keys.append(key); });
    return keys;
}

static size_t item_size(String const& key, JsonValue const& value)
{
    auto serialized = value.serialized();
    return key.bytes().size() + serialized.bytes().size();
}

size_t StorageArea::bytes_in_use(Optional<Vector<String>> const& keys) const
{
    size_t total = 0;
    m_items.for_each_member([&](String const& key, JsonValue const& value) {
        if (!keys.has_value() || keys->contains_slow(key))
            total += item_size(key, value);
    });
    return total;
}

ErrorOr<JsonObject, String> StorageArea::set(JsonObject const& items)
{
    if (m_kind == Kind::Managed)
        return "This is a read-only store."_string;

    size_t new_total = bytes_in_use({});
    size_t new_count = m_items.size();
    Optional<String> error;
    items.for_each_member([&](String const& key, JsonValue const& value) {
        auto size = item_size(key, value);
        if (m_quota.bytes_per_item.has_value() && size > *m_quota.bytes_per_item)
            error = "QUOTA_BYTES_PER_ITEM quota exceeded"_string;
        if (auto existing = m_items.get(key); existing.has_value())
            new_total -= item_size(key, *existing);
        else
            ++new_count;
        new_total += size;
    });
    if (error.has_value())
        return error.release_value();
    if (new_total > m_quota.bytes)
        return "QUOTA_BYTES quota exceeded"_string;
    if (m_quota.max_items.has_value() && new_count > *m_quota.max_items)
        return "MAX_ITEMS quota exceeded"_string;

    JsonObject changes;
    items.for_each_member([&](String const& key, JsonValue const& value) {
        auto existing = m_items.get(key);
        if (existing.has_value() && existing->serialized() == value.serialized())
            return;

        JsonObject change;
        if (existing.has_value())
            change.set("oldValue"sv, *existing);
        change.set("newValue"sv, value);
        changes.set(key, move(change));
        m_items.set(key, value);
    });

    if (changes.size() > 0)
        save();
    return changes;
}

JsonObject StorageArea::remove(Vector<String> const& keys)
{
    JsonObject changes;
    for (auto const& key : keys) {
        auto existing = m_items.get(key);
        if (!existing.has_value())
            continue;

        JsonObject change;
        change.set("oldValue"sv, *existing);
        changes.set(key, move(change));
        m_items.remove(key);
    }

    if (changes.size() > 0)
        save();
    return changes;
}

JsonObject StorageArea::clear()
{
    return remove(keys());
}

void StorageArea::save() const
{
    if (!m_file.has_value())
        return;

    auto write = [&]() -> ErrorOr<void> {
        TRY(Core::Directory::create(LexicalPath { *m_file }.parent(), Core::Directory::CreateDirectories::Yes));
        auto temporary = ByteString::formatted("{}.tmp", *m_file);
        auto file = TRY(Core::File::open(temporary, Core::File::OpenMode::Write | Core::File::OpenMode::Truncate, 0600));
        auto serialized = m_items.serialized();
        TRY(file->write_until_depleted(serialized.bytes()));
        file->close();
        TRY(Core::System::rename(temporary, *m_file));
        return {};
    };
    if (auto result = write(); result.is_error())
        warnln("Extensions: unable to save {}: {}", *m_file, result.error());
}

}
