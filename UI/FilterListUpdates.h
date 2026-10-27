/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteBuffer.h>
#include <AK/ByteString.h>
#include <AK/Function.h>
#include <AK/Time.h>
#include <LibCore/Forward.h>
#include <LibURL/URL.h>
#include <LibWebView/Settings.h>
#include <UI/FilterListBundle.h>

namespace Ladybird::FilterLists {

// Keeps the filter lists up to date between releases. xenon-filter-lists, a public repository on git.avx512.dev,
// rebuilds the bundle every day with Meta/update-filter-lists.py and signs its manifest (Documentation/FilterListUpdates.md).
//
// Once a day, while content blocking and filter list updates (Settings, Services) are on, Xenon fetches that manifest,
// checks its signature, and downloads the files whose hash changed, checking each against it. The newest bundle that
// checks out is kept in the profile (ContentBlocking/Xenon), and replaces the bundled lists for as long as it's newer
// than them, starting the moment it's downloaded. Anything that fails leaves the lists in use as they were, and is
// tried again an hour later.
class Updater final : public WebView::SettingsObserver {
public:
    // Switches to the profile's downloaded lists if they're newer than the bundled ones, and starts the daily checks.
    // Call it once the application exists, before any page opens.
    static void initialize();

    struct Bundle {
        ByteString directory;
        BundleManifest manifest;
        bool downloaded { false };
    };

private:
    Updater(ByteString directory, URL::URL base_url, ByteBuffer public_key);

    // WebView::SettingsObserver
    virtual void background_networking_settings_changed() override;
    virtual void content_blocker_settings_changed() override;

    Optional<Bundle> load_bundle(ByteString directory, bool downloaded) const;
    void use_bundle(Bundle);

    void load_state();
    void save_state();

    void check_if_due();
    void check();
    void did_download_manifest(ByteBuffer manifest, ByteBuffer signature);
    void update_next_file();
    void finish_update();
    void fail(StringView reason);
    void fail(Error const&);

    ByteString m_directory;
    URL::URL m_base_url;
    ByteBuffer m_public_key;

    Optional<Bundle> m_bundled;
    // The bundle whose lists Ladybird has: the downloaded one, or the bundled one.
    Optional<Bundle> m_current;

    Optional<UnixDateTime> m_checked_at;
    Optional<UnixDateTime> m_attempted_at;
    RefPtr<Core::Timer> m_timer;
    RefPtr<Core::Timer> m_first_check;

    // The update in progress, written into the staging directory one file at a time.
    bool m_updating { false };
    ByteBuffer m_new_manifest;
    ByteBuffer m_new_signature;
    Optional<BundleManifest> m_new_bundle;
    size_t m_next_file { 0 };
};

}
