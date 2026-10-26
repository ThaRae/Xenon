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

// An extension's _locales/<locale>/messages.json files, for chrome.i18n and __MSG_name__ in its manifest and CSS.
// https://developer.chrome.com/docs/extensions/reference/api/i18n
class Locales {
public:
    // Loads the messages for the UI locale (a BCP 47 tag, like "en-US"), falling back to its language ("en") and then to
    // the extension's default locale, as Chrome does.
    static ErrorOr<Locales, String> load(ByteString const& extension_root, Optional<String> const& default_locale, StringView ui_language, StringView extension_id);

    // The message with the given name (in any case), with its named placeholders filled in and $1 to $9 replaced by the
    // substitutions. A name with no message gives nothing; chrome.i18n.getMessage() makes that an empty string.
    Optional<String> message(StringView name, ReadonlySpan<String> substitutions = {}) const;

    // Replaces each __MSG_name__ with its message, or with nothing if there's no such message.
    String localize(StringView text) const;

    // Every message by its lowercased name, with named placeholders filled in but $1 to $9 left for the renderer to
    // substitute, so that chrome.i18n.getMessage() can answer synchronously.
    JsonObject const& messages() const { return m_messages; }

    // "en-US", as chrome.i18n.getUILanguage() returns it.
    String const& ui_language() const { return m_ui_language; }

private:
    JsonObject m_messages;
    String m_ui_language;
};

String substitute_message(StringView message, ReadonlySpan<String> substitutions);

}
