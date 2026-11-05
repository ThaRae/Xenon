/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/JsonObject.h>
#include <AK/Optional.h>
#include <AK/Span.h>
#include <LibURL/URL.h>

namespace Ladybird {

Optional<JsonObject> describe_certificate(ReadonlyBytes der_certificate);
JsonObject describe_rejected_certificate(ReadonlyBytes der_certificate, URL::URL const&);

}
