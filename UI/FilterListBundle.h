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

static constexpr u32 BUNDLE_FORMAT = 1;

static constexpr u64 MAXIMUM_FILE_SIZE = 64 * MiB;
static constexpr u64 MAXIMUM_BUNDLE_SIZE = 256 * MiB;

struct BundleFile {
    ByteString path;
    u64 size { 0 };
    ByteString sha256;

    bool operator==(BundleFile const&) const = default;
};

struct BundleManifest {
    Optional<u32> format;
    ByteString generated_at;
    Vector<BundleFile> files;

    Optional<BundleFile const&> file(StringView path) const;
};

ErrorOr<BundleManifest> parse_bundle_manifest(StringView json);

bool bundle_signature_is_valid(ReadonlyBytes manifest, StringView signature, ReadonlyBytes public_key);

ByteString sha256_hex(ReadonlyBytes);
bool file_matches(BundleFile const&, ReadonlyBytes contents);

}
