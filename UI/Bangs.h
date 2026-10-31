/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/EnumBits.h>
#include <AK/Optional.h>
#include <AK/StringView.h>
#include <LibURL/URL.h>

namespace Ladybird::Bangs {

enum class Format : u8 {
    None = 0,
    OpenBasePath = 1 << 0,
    OpenSnapDomain = 1 << 1,
    EncodeQuery = 1 << 2,
    SpaceToPlus = 1 << 3,
};
AK_ENUM_BITWISE_OPERATORS(Format);

struct Bang {
    StringView trigger;
    StringView name;
    StringView query_url;
    StringView snap_domain;
    Format format { Format::None };
};

struct BangNavigation {
    Bang bang;
    URL::URL url;
};

Optional<BangNavigation> resolve(StringView input);

Optional<URL::URL> url_for_query(Bang const&, StringView query);

Optional<Bang> bang_for_word(StringView word);

struct LeadingBang {
    Bang bang;
    size_t length;
};
Optional<LeadingBang> leading_bang(StringView input);

Optional<Bang> find_bang(StringView trigger);

size_t bang_count();
Bang bang_at(size_t index);

}
