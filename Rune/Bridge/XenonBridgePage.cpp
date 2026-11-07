/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/ByteString.h>
#include <AK/Vector.h>
#include <Rune/Bridge/Engine.h>
#include <Rune/Bridge/View.h>
#include <Rune/Bridge/XenonBridgePage.h>

namespace Xenon {

static XenonPageCallbacks s_page_callbacks {};

void set_page_callbacks(XenonPageCallbacks const& callbacks)
{
    s_page_callbacks = callbacks;
}

XenonPageCallbacks const& page_callbacks()
{
    return s_page_callbacks;
}

}

using Xenon::Engine;
using Xenon::View;

static StringView string_view_from(XenonStr string)
{
    if (!string.data || string.length == 0)
        return {};
    return { string.data, string.length };
}

static void post_to_view(XenonViewId id, Function<void(View&)> command)
{
    Engine::the().post([id, command = move(command)] {
        if (auto* view = Engine::the().view(id))
            command(*view);
    });
}

void xenon_engine_set_page_callbacks(XenonPageCallbacks const* callbacks)
{
    Xenon::set_page_callbacks(callbacks ? *callbacks : XenonPageCallbacks {});
}

void xenon_engine_close_dialog(XenonViewId id, XenonPromptId dialog, bool accepted, XenonStr text)
{
    post_to_view(id, [dialog, accepted, text = ByteString { string_view_from(text) }](View& view) {
        view.close_page_dialog(dialog, accepted, text);
    });
}

void xenon_engine_activate_menu_item(XenonViewId id, XenonPromptId menu, uint32_t item)
{
    post_to_view(id, [menu, item](View& view) {
        view.activate_context_menu_item(menu, item);
    });
}

void xenon_engine_close_select_dropdown(XenonViewId id, XenonPromptId dropdown, bool has_selection, uint32_t item)
{
    post_to_view(id, [dropdown, item = has_selection ? Optional<u32> { item } : Optional<u32> {}](View& view) {
        view.close_select_dropdown(dropdown, item);
    });
}

void xenon_engine_close_file_picker(XenonViewId id, XenonPromptId picker, XenonStr const* paths, size_t path_count)
{
    Vector<ByteString> selected_paths;
    for (size_t i = 0; paths && i < path_count; ++i) {
        if (auto path = string_view_from(paths[i]); !path.is_empty())
            selected_paths.append(path);
    }
    post_to_view(id, [picker, selected_paths = move(selected_paths)](View& view) {
        view.close_file_picker(picker, selected_paths);
    });
}

void xenon_engine_find_in_page(XenonViewId id, XenonStr query, bool case_sensitive, bool highlight_all)
{
    post_to_view(id, [query = ByteString { string_view_from(query) }, case_sensitive, highlight_all](View& view) {
        view.find(query, case_sensitive, highlight_all);
    });
}

void xenon_engine_find_next(XenonViewId id)
{
    post_to_view(id, [](View& view) {
        view.find_in_page_next_match();
    });
}

void xenon_engine_find_previous(XenonViewId id)
{
    post_to_view(id, [](View& view) {
        view.find_in_page_previous_match();
    });
}

void xenon_engine_end_find(XenonViewId id)
{
    post_to_view(id, [](View& view) {
        view.find_in_page_end();
    });
}

void xenon_engine_set_fullscreen(XenonViewId id, bool fullscreen)
{
    post_to_view(id, [fullscreen](View& view) {
        view.set_fullscreen(fullscreen);
    });
}

void xenon_engine_answer_permission(XenonViewId id, XenonPromptId request, uint32_t outcome)
{
    post_to_view(id, [request, outcome](View& view) {
        view.answer_permission(request, outcome);
    });
}

void xenon_engine_request_close(XenonViewId id)
{
    Engine::the().post([id] {
        if (auto* view = Engine::the().view(id)) {
            view->request_close_with_prompt();
            return;
        }
        Engine::the().close_tab(id);
    });
}
