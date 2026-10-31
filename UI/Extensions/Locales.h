/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteString.h>
#include <AK/Error.h>
#include <AK/HashMap.h>
#include <AK/JsonObject.h>
#include <AK/Optional.h>
#include <AK/Span.h>
#include <AK/String.h>

namespace Ladybird::Extensions {

class Locales {
public:
    static ErrorOr<Locales, String> load(ByteString const& extension_root, Optional<String> const& default_locale, StringView ui_language, StringView extension_id);

    Optional<String> message(StringView name, ReadonlySpan<String> substitutions = {}) const;

    String localize(StringView text) const;

    JsonObject const& messages() const { return m_messages; }

    String const& ui_language() const { return m_ui_language; }

private:
    JsonObject m_messages;
    String m_ui_language;
};

String substitute_message(StringView message, ReadonlySpan<String> substitutions);

}
