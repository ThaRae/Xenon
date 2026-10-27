/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteBuffer.h>
#include <AK/ByteString.h>
#include <AK/Function.h>
#include <AK/Optional.h>
#include <AK/String.h>
#include <LibURL/URL.h>

// Extensions from the Chrome Web Store, fetched the way Chrome fetches them: an update check that names the newest
// version of an extension and where its CRX file is, at the store's update URL (Omaha's protocol 2.0, which Chrome's
// extension updater speaks), then the CRX file itself. What's fetched is checked by verify_crx() before it's installed.
namespace Ladybird::Extensions::WebStore {

// A version of an extension the store offers.
struct Update {
    String version;
    URL::URL url;
    // The CRX file's size and SHA-256 hash (in hex), when the store gives them.
    Optional<u64> size;
    Optional<ByteString> sha256;
};

// What the store's update check says about an extension: the version it offers, or none if the version Xenon asked
// with is the newest.
ErrorOr<Optional<Update>, String> parse_update_check(StringView response, StringView id);

// Whether an extension version is newer than another, comparing their numbers in order, as Chrome does.
bool is_newer_version(StringView version, StringView than);

// The SHA-256 hash of the key every CRX file from the store has to be signed with, besides the developer's.
ReadonlyBytes publisher_key_hash();

struct Fetched {
    String version;
    ByteBuffer crx;
};
using Callback = Function<void(ErrorOr<Optional<Fetched>, String>)>;

// Fetches the newest version of an extension, if it's newer than the version given. Calls back once, with none if the
// version given is the newest.
void fetch(String id, Optional<String> current_version, Callback);

}
