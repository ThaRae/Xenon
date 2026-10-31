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

class Updater final : public WebView::SettingsObserver {
public:
    static void initialize();

    static Optional<ByteString> current_directory();
    static void reapply_lists();

    struct Bundle {
        ByteString directory;
        BundleManifest manifest;
        bool downloaded { false };
    };

    struct Status {
        ByteString generated_at;
        size_t list_count { 0 };
        bool downloaded { false };
        Optional<UnixDateTime> checked_at;
        bool checking { false };
        Optional<ByteString> error;
    };
    static Optional<Status> status();

    static void check_now();

private:
    Updater(ByteString directory, URL::URL base_url, ByteBuffer public_key);

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
    Optional<Bundle> m_current;

    Optional<UnixDateTime> m_checked_at;
    Optional<UnixDateTime> m_attempted_at;
    Optional<ByteString> m_error;
    RefPtr<Core::Timer> m_timer;
    RefPtr<Core::Timer> m_first_check;

    bool m_updating { false };
    ByteBuffer m_new_manifest;
    ByteBuffer m_new_signature;
    Optional<BundleManifest> m_new_bundle;
    size_t m_next_file { 0 };
};

}
