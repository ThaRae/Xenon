/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/String.h>
#include <LibURL/Forward.h>

namespace Xenon::TaskManager {

String site_label(URL::URL const&);

void add_open_action();

}
