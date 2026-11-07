/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibURL/URL.h>

namespace Ladybird::BlockingDashboard {

URL::URL url();

void register_url_scheme();

}
