/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/String.h>
#include <AK/Utf16String.h>
#include <LibWebCommon/Clipboard/SystemClipboard.h>

namespace Xenon::Clipboard {

Utf16String text();
void set_text(String const&);

Web::Clipboard::SystemClipboardItem item();
void set_item(Web::Clipboard::SystemClipboardItem const&);

}
