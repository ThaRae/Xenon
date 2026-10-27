/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteString.h>
#include <AK/Error.h>
#include <AK/Optional.h>
#include <AK/Vector.h>

namespace Ladybird::FilterLists {

// A bundle is the directory Meta/update-filter-lists.py writes: lists/*.txt, trusted-lists/*.txt,
// resources/ublock-resources.json, and a manifest.json that describes them. Xenon ships one in its resources, and
// xenon-filter-lists publishes a new one every day (UI/FilterListUpdates.h), with manifest.json.sig beside it.

// The bundle layout, and the engine features its lists rely on, that this build reads: the "format" in manifest.json.
// Meta/update-filter-lists.py writes it. Bump both together whenever a Xenon built before the change couldn't use the
// new output, such as a new kind of file, or lists that need engine patches it lacks.
static constexpr u32 BUNDLE_FORMAT = 1;

// What Ladybird loads at most (LibWebView/Application.cpp).
static constexpr u64 MAXIMUM_FILE_SIZE = 64 * MiB;
static constexpr u64 MAXIMUM_BUNDLE_SIZE = 256 * MiB;

struct BundleFile {
    // Relative to the bundle's directory.
    ByteString path;
    u64 size { 0 };
    // In lowercase hex.
    ByteString sha256;

    bool operator==(BundleFile const&) const = default;
};

struct BundleManifest {
    // Absent in bundles from before formats were numbered, which only Xenon's own resources have.
    Optional<u32> format;
    // "YYYY-MM-DD HH:MM UTC", which sorts as the times do.
    ByteString generated_at;
    Vector<BundleFile> files;

    Optional<BundleFile const&> file(StringView path) const;
};

// Reads a manifest.json, and checks that the files it describes are ones Xenon loads: lists in lists/ or
// trusted-lists/ (as each list's "trusted" says), and resources/ublock-resources.json, each with a size and a hash
// and within Ladybird's limits.
ErrorOr<BundleManifest> parse_bundle_manifest(StringView json);

// Whether manifest.json.sig, the base64 Ed25519 signature of manifest.json's bytes, is the given key's.
bool bundle_signature_is_valid(ReadonlyBytes manifest, StringView signature, ReadonlyBytes public_key);

ByteString sha256_hex(ReadonlyBytes);
bool file_matches(BundleFile const&, ReadonlyBytes contents);

}
