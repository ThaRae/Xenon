/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Function.h>
#include <AK/JsonObject.h>
#include <AK/Optional.h>
#include <AK/String.h>
#include <AK/StringView.h>
#include <LibRequests/SecureConnection.h>
#include <LibURL/URL.h>
#include <LibWebView/Forward.h>

namespace Ladybird::SiteSecurity {

bool applies_to(URL::URL const&);
String display_protocol(StringView);
JsonObject describe(URL::URL const&, Optional<Requests::SecureConnection> const&, bool has_certificate_exception);

bool has_certificate_exception(WebView::BrowsingSession const&, URL::URL const&);
void request_description(WebView::ViewImplementation&, Function<void(JsonObject)>);

void initialize();

}
