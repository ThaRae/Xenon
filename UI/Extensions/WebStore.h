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

namespace Ladybird::Extensions::WebStore {

struct Update {
    String version;
    URL::URL url;
    Optional<u64> size;
    Optional<ByteString> sha256;
};

ErrorOr<Optional<Update>, String> parse_update_check(StringView response, StringView id);

bool is_newer_version(StringView version, StringView than);

ReadonlyBytes publisher_key_hash();

struct Fetched {
    String version;
    ByteBuffer crx;
};
using Callback = Function<void(ErrorOr<Optional<Fetched>, String>)>;

void fetch(String id, Optional<String> current_version, Callback);

}
