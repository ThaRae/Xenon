/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteBuffer.h>
#include <AK/ByteString.h>
#include <AK/Error.h>
#include <AK/String.h>

namespace Ladybird::Extensions {

// A packed extension, as the Chrome Web Store serves it: a CRX3 file, which is a signed header in front of a zip of the
// extension's files (components/crx_file/crx3.proto in Chromium).
struct PackedExtension {
    // The ID the developer's key gives the extension.
    String id;
    // The developer's public key, as a DER SubjectPublicKeyInfo, which Chrome keeps as the extension's "key".
    ByteBuffer public_key;
    // The zip, inside the CRX file it was verified from.
    ReadonlyBytes archive;
};

// The SHA-256 hash of the key the Chrome Web Store signs every extension it serves with (kPublisherKeyHash in
// components/crx_file/crx_verifier.cc).
ReadonlyBytes chrome_web_store_publisher_key_hash();

// Checks a CRX3 file as Chrome does before installing it from the Chrome Web Store: every signature in it has to be
// valid, one of them has to be by the key that gives the expected ID, and, if a publisher key hash is given, one by
// the publisher's key. The error says what's wrong with the file.
ErrorOr<PackedExtension, String> verify_crx(ReadonlyBytes crx, StringView expected_id, ReadonlyBytes publisher_key_hash);

// Unpacks a zip into a directory that doesn't exist yet, which it creates. Every entry has to be a plain file or
// directory with a relative path that stays inside it, stored or deflated, whose contents match its CRC. A failure can
// leave some files behind, in the new directory only.
ErrorOr<void, String> unpack_zip(ReadonlyBytes archive, ByteString const& directory);

}
