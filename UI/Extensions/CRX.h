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

struct PackedExtension {
    String id;
    ByteBuffer public_key;
    ReadonlyBytes archive;
};

ReadonlyBytes chrome_web_store_publisher_key_hash();

ErrorOr<PackedExtension, String> verify_crx(ReadonlyBytes crx, StringView expected_id, ReadonlyBytes publisher_key_hash);

ErrorOr<void, String> unpack_zip(ReadonlyBytes archive, ByteString const& directory);

}
