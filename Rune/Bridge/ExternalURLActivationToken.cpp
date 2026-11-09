/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <UI/ExternalURLActivationToken.h>

namespace Ladybird {

void request_external_url_activation_token(Function<void(Optional<String>)> callback)
{
    callback({});
}

}
