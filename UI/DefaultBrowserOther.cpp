/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <UI/DefaultBrowser.h>

namespace Ladybird::DefaultBrowser {

Status status()
{
    return Status::Unsupported;
}

void request_default(Function<void()>) { }

}
