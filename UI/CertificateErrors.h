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

// The warning Xenon shows for a site whose certificate failed verification, at xenon://certificate-error/?url=<site>, as
// Chrome and Helium do: why the certificate was refused (self-signed, expired, for another host, or from an authority
// the computer doesn't trust), and behind Advanced, the certificate and a way to proceed to the site anyway.
//
// RequestServer tells Xenon about each certificate it refuses (engine patch 0031), and a tab whose page failed that way
// comes here in place of the engine's error page. Proceeding accepts that certificate, and only that one, for the
// site's host and port until Xenon quits, or the private windows close. A site that asked to be reached securely only
// (HSTS) can't be proceeded to, as in Chrome.
//
// Its pages are in UI/Resources/certificate-error, served from the application's resources under the xenon: scheme
// (UI/BlockingDashboard.cpp), with a script bridge to the UI process.
namespace Ladybird::CertificateErrors {

static constexpr auto PAGE_HOST = "certificate-error"sv;

// Keeps the certificates RequestServer refuses from now on. Call it once the application exists.
void initialize();

bool is_page(WebView::ScriptBridgeEndpoint const&);
ErrorOr<WebView::URLSchemeResponse> respond(WebView::URLSchemeRequest const&);
Optional<String> bootstrap_script(WebView::ScriptBridgeEndpoint const&);
void handle_message(WebView::ScriptBridgeEndpoint const&, String const& message);

}
