/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Error.h>
#include <AK/Optional.h>
#include <AK/String.h>
#include <AK/StringView.h>
#include <LibWebView/URLSchemeHandler.h>

namespace Ladybird::CertificateErrors {

static constexpr auto PAGE_HOST = "certificate-error"sv;

void initialize();

bool is_page(WebView::ScriptBridgeEndpoint const&);
ErrorOr<WebView::URLSchemeResponse> respond(WebView::URLSchemeRequest const&);
Optional<String> bootstrap_script(WebView::ScriptBridgeEndpoint const&);
void handle_message(WebView::ScriptBridgeEndpoint const&, String const& message);

}
