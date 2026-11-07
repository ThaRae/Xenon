/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/NonnullOwnPtr.h>
#include <LibGfx/Size.h>
#include <LibWebView/Forward.h>

namespace Xenon {

class View;

NonnullOwnPtr<WebView::PictureInPictureWindow> create_picture_in_picture_window_for(WebView::CanonicalTraversable&, View const& owner_view, Gfx::IntSize video_size);

}
