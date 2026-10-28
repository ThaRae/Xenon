/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/JsonObject.h>
#include <AK/Span.h>
#include <LibURL/URL.h>

namespace Ladybird {

// What the certificate warning (UI/CertificateErrors.h) says about a certificate that failed verification for a request
// to the URL, as JSON for its page:
//
//   problems             what's wrong with it, most serious first: "selfSigned", "host" (it's for other names),
//                        "expired", "notYetValid", or else "untrusted" (no authority the computer trusts issued it)
//   subject, issuer      the common names it's issued to and by, and subjectOrganization and issuerOrganization
//   validFrom, validUntil
//                        in milliseconds since the epoch
//   names                the host names and addresses it's for
//   fingerprint          its SHA-256 fingerprint
//
// RequestServer refused the certificate, so something is wrong with it even when none of the specific problems is found.
JsonObject describe_rejected_certificate(ReadonlyBytes der_certificate, URL::URL const&);

}
