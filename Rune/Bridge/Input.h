/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/StringView.h>
#include <LibGfx/Cursor.h>
#include <LibWebCommon/Page/InputEvent.h>
#include <Rune/Bridge/XenonBridge.h>

namespace Xenon {

Web::UIEvents::KeyModifier key_modifiers_from(u32 xenon_modifiers);
Web::UIEvents::MouseButton mouse_buttons_from(u32 xenon_buttons);
Web::KeyEvent key_event_from(XenonKeyEventType, StringView code, StringView key, u32 xenon_modifiers, bool repeat);
XenonCursor xenon_cursor_from(Gfx::Cursor const&);

}
