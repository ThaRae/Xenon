/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/JsonArray.h>
#include <AK/LexicalPath.h>
#include <LibCore/File.h>
#include <LibFileSystem/FileSystem.h>
#include <LibTest/TestCase.h>
#include <UI/XenonSettingsStore.h>

#include <stdlib.h>
#include <string.h>

using namespace Ladybird::XenonSettings;

class TemporaryDirectory {
public:
    TemporaryDirectory()
    {
        char path[] = "/tmp/xenon-settings-test-XXXXXX";
        VERIFY(mkdtemp(path));
        m_path = MUST(FileSystem::real_path(StringView { path, strlen(path) }));
    }

    ~TemporaryDirectory()
    {
        (void)FileSystem::remove(m_path, FileSystem::RecursionMode::Allowed);
    }

    ByteString join(StringView relative_path) const { return LexicalPath::join(m_path, relative_path).string(); }

    ByteString write(StringView relative_path, StringView contents) const
    {
        auto path = join(relative_path);
        auto file = MUST(Core::File::open(path, Core::File::OpenMode::Write));
        MUST(file->write_until_depleted(contents.bytes()));
        return path;
    }

private:
    ByteString m_path;
};

static constexpr auto QSETTINGS_INI = R"~~~([General]
unrelated=1

[blocking]
blockedRequestTotal=4242
disabledFilterLists=easylist, "annoyances, cookies", @@at-list, \xfc-list, ü
showBlockedRequestCount=true

[layout]
compactToolbar=true

[search]
bangs=false

[splitView]
dragAndDrop=0

[tabs]
cycleInRecentlyUsedOrder=false
hibernation=true
hibernationTimeout=1800
hibernateOnMemoryPressure=false

[updates]
automatic=false
)~~~"sv;

TEST_CASE(missing_file_starts_empty)
{
    TemporaryDirectory directory;
    auto store = Store::open(directory.join("XenonSettings.json"sv));

    EXPECT(!store.get_bool(Keys::bangs).has_value());
    EXPECT(store.get_strings(Keys::disabled_filter_lists).is_empty());
    EXPECT(!FileSystem::exists(store.path()));
}

TEST_CASE(values_survive_a_reload)
{
    TemporaryDirectory directory;
    auto path = directory.join("nested/XenonSettings.json"sv);

    {
        auto store = Store::open(path);
        store.set(Keys::bangs, false);
        store.set(Keys::tab_hibernation_timeout, static_cast<i64>(900));
        store.set(Keys::blocked_request_total, static_cast<u64>(1234567890123));
        JsonArray lists;
        lists.must_append("easylist"sv);
        lists.must_append("annoyances"sv);
        store.set(Keys::disabled_filter_lists, move(lists));
        MUST(store.save());
    }

    auto store = Store::open(path);
    EXPECT_EQ(store.get_bool(Keys::bangs), false);
    EXPECT_EQ(store.get_i64(Keys::tab_hibernation_timeout), 900);
    EXPECT_EQ(store.get_u64(Keys::blocked_request_total), 1234567890123u);
    EXPECT_EQ(store.get_strings(Keys::disabled_filter_lists), (Vector<String> { "easylist"_string, "annoyances"_string }));
    EXPECT(!FileSystem::exists(ByteString::formatted("{}.tmp", path)));
}

TEST_CASE(unknown_keys_are_kept)
{
    TemporaryDirectory directory;
    auto path = directory.write("XenonSettings.json"sv, R"~~~({"future/setting":"kept","search/bangs":true})~~~"sv);

    {
        auto store = Store::open(path);
        store.set(Keys::bangs, false);
        MUST(store.save());
    }

    auto file = MUST(Core::File::open(path, Core::File::OpenMode::Read));
    auto contents = MUST(file->read_until_eof());
    EXPECT(StringView { contents }.contains("\"future/setting\":\"kept\""sv));
    EXPECT(StringView { contents }.contains("\"search/bangs\":false"sv));
}

TEST_CASE(qsettings_ini_values_import_with_their_types)
{
    auto values = Store::import_qsettings_ini(QSETTINGS_INI);

    EXPECT_EQ(values.get_bool(Keys::compact_toolbar), true);
    EXPECT_EQ(values.get_bool(Keys::cycle_tabs_in_recently_used_order), false);
    EXPECT_EQ(values.get_bool(Keys::tab_hibernation), true);
    EXPECT_EQ(values.get_i64(Keys::tab_hibernation_timeout), 1800);
    EXPECT_EQ(values.get_bool(Keys::hibernate_on_memory_pressure), false);
    EXPECT_EQ(values.get_bool(Keys::split_view_drag_and_drop), false);
    EXPECT_EQ(values.get_bool(Keys::bangs), false);
    EXPECT_EQ(values.get_bool(Keys::automatic_updates), false);
    EXPECT_EQ(values.get_bool(Keys::show_blocked_request_count), true);
    EXPECT_EQ(values.get_u64(Keys::blocked_request_total), 4242u);
    EXPECT(!values.has("unrelated"sv));
    EXPECT(!values.has("General/unrelated"sv));

    auto lists = values.get_array(Keys::disabled_filter_lists);
    EXPECT(lists.has_value());
    EXPECT_EQ(lists->size(), 5u);
    EXPECT_EQ(lists->at(0).as_string(), "easylist"sv);
    EXPECT_EQ(lists->at(1).as_string(), "annoyances, cookies"sv);
    EXPECT_EQ(lists->at(2).as_string(), "@at-list"sv);
    EXPECT_EQ(lists->at(3).as_string(), "\u00fc-list"sv);
    EXPECT_EQ(lists->at(4).as_string(), "\u00fc"sv);
}

TEST_CASE(qsettings_ini_lists_handle_single_and_empty_values)
{
    auto single = Store::import_qsettings_ini("[blocking]\ndisabledFilterLists=easylist\n"sv);
    EXPECT_EQ(single.get_array(Keys::disabled_filter_lists)->size(), 1u);

    auto empty = Store::import_qsettings_ini("[blocking]\ndisabledFilterLists=@Invalid()\n"sv);
    EXPECT_EQ(empty.get_array(Keys::disabled_filter_lists)->size(), 0u);
}

TEST_CASE(first_open_imports_the_legacy_store_once)
{
    TemporaryDirectory directory;
    auto ini_path = directory.write("Xenon.conf"sv, QSETTINGS_INI);
    auto json_path = directory.join("XenonSettings.json"sv);

    size_t imports = 0;
    auto import_legacy = [&] {
        return Function<Optional<JsonObject>()> { [&]() -> Optional<JsonObject> {
            ++imports;
            return Store::import_qsettings_ini_file(ini_path);
        } };
    };

    {
        auto store = Store::open(json_path, import_legacy());
        EXPECT_EQ(store.get_bool(Keys::bangs), false);
        EXPECT(FileSystem::exists(json_path));
        store.set(Keys::bangs, true);
        MUST(store.save());
    }

    auto store = Store::open(json_path, import_legacy());
    EXPECT_EQ(imports, 1u);
    EXPECT_EQ(store.get_bool(Keys::bangs), true);
    EXPECT_EQ(store.get_i64(Keys::tab_hibernation_timeout), 1800);
}

TEST_CASE(missing_legacy_ini_imports_nothing)
{
    TemporaryDirectory directory;
    EXPECT(!Store::import_qsettings_ini_file(directory.join("Xenon.conf"sv)).has_value());

    auto store = Store::open(directory.join("XenonSettings.json"sv), [&] { return Store::import_qsettings_ini_file(directory.join("Xenon.conf"sv)); });
    EXPECT(!FileSystem::exists(store.path()));
}
