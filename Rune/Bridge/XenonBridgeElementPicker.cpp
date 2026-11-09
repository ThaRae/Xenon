/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWebView/Application.h>
#include <LibWebView/Settings.h>
#include <Rune/Bridge/ElementPicker.h>
#include <Rune/Bridge/Engine.h>
#include <Rune/Bridge/XenonBridgeElementPicker.h>

namespace Xenon::ElementPicker {

static XenonElementPickerCallbacks s_callbacks {};
static u64 s_next_request { 1 };
static u64 s_pending_request { 0 };

void confirm_filter(XenonViewId view, String const& filter)
{
    s_pending_request = s_next_request++;
    if (auto function = s_callbacks.filter_confirmation_requested) {
        auto bytes = filter.bytes_as_string_view();
        function(s_callbacks.context, view, s_pending_request, { bytes.characters_without_null_termination(), bytes.length() });
    }
}

static void answer(u64 request, bool accepted, ByteString const& filter)
{
    if (request == 0 || request != s_pending_request)
        return;
    s_pending_request = 0;
    if (accepted)
        WebView::Application::settings().add_custom_content_blocker_filter(filter);
}

}

void xenon_engine_set_element_picker_callbacks(XenonElementPickerCallbacks const* callbacks)
{
    Xenon::ElementPicker::s_callbacks = callbacks ? *callbacks : XenonElementPickerCallbacks {};
}

void xenon_engine_confirm_element_filter(uint64_t request, bool accepted, XenonStr filter)
{
    ByteString text;
    if (filter.data && filter.length > 0)
        text = ByteString { filter.data, filter.length };
    Xenon::Engine::the().post([request, accepted, text = move(text)] {
        Xenon::ElementPicker::answer(request, accepted, text);
    });
}
