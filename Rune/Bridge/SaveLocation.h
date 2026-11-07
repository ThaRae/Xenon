/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteString.h>
#include <LibCore/Promise.h>

namespace Xenon {

NonnullRefPtr<Core::Promise<ByteString>> ask_ui_for_save_location(ByteString const& file_name);

}
