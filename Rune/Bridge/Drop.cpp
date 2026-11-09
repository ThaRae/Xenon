/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Vector.h>
#include <LibURL/URL.h>
#include <LibWebCommon/Page/DragEvent.h>
#include <LibWebView/URL.h>
#include <LibWebView/Utilities.h>
#include <Rune/Bridge/BrowserModel.h>
#include <Rune/Bridge/Engine.h>
#include <Rune/Bridge/Input.h>
#include <Rune/Bridge/View.h>
#include <Rune/Bridge/XenonBridgeDrop.h>

namespace Xenon {

struct DroppedFiles : Web::BrowserInputData {
    explicit DroppedFiles(Vector<ByteString> paths)
        : paths(move(paths))
    {
    }

    Vector<ByteString> paths;
};

static Web::DragEvent::Type drag_event_type_from(u32 type)
{
    switch (type) {
    case XENON_DRAG_ENTER:
        return Web::DragEvent::Type::DragStart;
    case XENON_DRAG_MOVE:
        return Web::DragEvent::Type::DragMove;
    case XENON_DRAG_DROP:
        return Web::DragEvent::Type::Drop;
    default:
        return Web::DragEvent::Type::DragEnd;
    }
}

void View::install_drop_callbacks()
{
    on_finish_handling_drag_event = [this](Web::DragEvent const& event) {
        if (event.type != Web::DragEvent::Type::Drop || !event.browser_data)
            return;
        auto const& dropped = static_cast<DroppedFiles const&>(*event.browser_data);
        open_dropped_files(dropped.paths);
    };
}

void View::open_dropped_files(Vector<ByteString> const& paths)
{
    Vector<URL::URL> urls;
    for (auto const& path : paths) {
        if (auto url = URL::create_with_file_scheme(path); url.has_value())
            urls.append(url.release_value());
    }
    if (urls.is_empty())
        return;
    load(urls.first());

    XenonWindowId window = XENON_WINDOW_NONE;
    if (auto const* model = Engine::the().model(); model && model->window_of(m_id))
        window = model->window_of(m_id)->id;
    for (size_t i = 1; i < urls.size(); ++i)
        Engine::the().new_tab(window, Engine::the().allocate_view_id(), urls[i].serialize().bytes_as_string_view(), {}, Web::HTML::ActivateTab::No);
}

void View::handle_drag_event(u32 type, Gfx::FloatPoint position, u32 modifiers, Vector<ByteString> paths)
{
    Web::DevicePixelPoint point { static_cast<int>(position.x()), static_cast<int>(position.y()) };
    auto event_type = drag_event_type_from(type);

    Vector<Web::HTML::SelectedFile> files;
    OwnPtr<Web::BrowserInputData> browser_data;
    if (event_type == Web::DragEvent::Type::DragStart) {
        for (auto const& path : paths) {
            if (auto file = WebView::create_selected_file(path); file.is_error())
                warnln("Xenon: Unable to open file {}: {}", path, file.error());
            else
                files.append(file.release_value());
        }
    } else if (event_type == Web::DragEvent::Type::Drop) {
        browser_data = make<DroppedFiles>(move(paths));
    }

    if (event_type == Web::DragEvent::Type::DragEnd) {
        Web::DragEvent event {};
        event.type = Web::DragEvent::Type::DragEnd;
        enqueue_input_event(move(event));
        return;
    }

    enqueue_input_event(Web::DragEvent {
        event_type,
        point,
        point,
        Web::UIEvents::MouseButton::Primary,
        event_type == Web::DragEvent::Type::Drop ? Web::UIEvents::MouseButton::None : Web::UIEvents::MouseButton::Primary,
        key_modifiers_from(modifiers),
        move(files),
        move(browser_data),
    });
}

}

void xenon_engine_drag_event(XenonViewId id, uint32_t type, float x, float y, uint32_t modifiers, XenonStr const* paths, size_t path_count)
{
    Vector<ByteString> dropped_paths;
    for (size_t i = 0; paths && i < path_count; ++i) {
        if (paths[i].data && paths[i].length)
            dropped_paths.append(StringView { paths[i].data, paths[i].length });
    }
    Xenon::Engine::the().post([id, type, x, y, modifiers, dropped_paths = move(dropped_paths)]() mutable {
        if (auto* view = Xenon::Engine::the().view(id))
            view->handle_drag_event(type, { x, y }, modifiers, move(dropped_paths));
    });
}
