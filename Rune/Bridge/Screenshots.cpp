/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/HashMap.h>
#include <AK/LexicalPath.h>
#include <AK/ScopeGuard.h>
#include <LibGfx/Bitmap.h>
#include <LibWebView/Application.h>
#include <Rune/Bridge/Engine.h>
#include <Rune/Bridge/SaveLocation.h>
#include <Rune/Bridge/View.h>
#include <Rune/Bridge/XenonBridgeScreenshots.h>

namespace Xenon {

static XenonScreenshotCallbacks s_screenshot_callbacks {};

static HashMap<u64, NonnullRefPtr<Core::Promise<ByteString>>>& save_location_requests()
{
    static HashMap<u64, NonnullRefPtr<Core::Promise<ByteString>>> requests;
    return requests;
}

static XenonStr xenon_str(StringView string)
{
    return { string.characters_without_null_termination(), string.length() };
}

NonnullRefPtr<Core::Promise<ByteString>> ask_ui_for_save_location(ByteString const& file_name)
{
    auto promise = Core::Promise<ByteString>::construct();
    if (!s_screenshot_callbacks.save_location_requested) {
        promise->reject(Error::from_errno(ECANCELED));
        return promise;
    }

    ByteString directory;
    ByteString suggested_name = file_name;
    if (auto default_path = WebView::Application::the().default_path_for_downloaded_file(file_name); !default_path.is_error()) {
        directory = default_path.value().dirname();
        suggested_name = default_path.value().basename();
    }

    static u64 next_request = 1;
    auto request = next_request++;
    save_location_requests().set(request, promise);
    s_screenshot_callbacks.save_location_requested(s_screenshot_callbacks.context, request, xenon_str(directory), xenon_str(suggested_name));
    return promise;
}

void View::install_screenshot_callbacks()
{
    on_request_screenshot = [this] {
        start_screenshot();
    };
}

RefPtr<Gfx::Bitmap> View::copy_current_frame()
{
    Gfx::SharedImageBuffer const* shared_image_buffer = nullptr;
    Web::DevicePixelSize size;
    if (m_has_usable_bitmap && m_front_bitmap.shared_image_buffer) {
        shared_image_buffer = m_front_bitmap.shared_image_buffer.ptr();
        size = m_front_bitmap.last_painted_size;
    } else if (m_backup_shared_image_buffer) {
        shared_image_buffer = m_backup_shared_image_buffer.ptr();
        size = m_backup_bitmap_size;
    }
    if (!shared_image_buffer)
        return {};
    auto bitmap = shared_image_buffer->bitmap_if_present();
    if (!bitmap)
        return {};
    auto width = min(size.width().value(), bitmap->width());
    auto height = min(size.height().value(), bitmap->height());
    if (width <= 0 || height <= 0)
        return {};

#ifdef AK_OS_MACOS
    auto const& iosurface = shared_image_buffer->iosurface_handle();
    if (!iosurface.lock_read_only())
        return {};
    ScopeGuard unlock_iosurface = [&] { iosurface.unlock_read_only(); };
#endif

    auto copy = bitmap->cropped({ 0, 0, width, height });
    if (copy.is_error())
        return {};
    return copy.release_value();
}

void View::send_screenshot_frame()
{
    if (!m_screenshot_frame || m_screenshot_surface == 0)
        return;
    XenonFrame frame {
        .width = static_cast<u32>(m_screenshot_frame->width()),
        .height = static_cast<u32>(m_screenshot_frame->height()),
        .stride = m_screenshot_frame->pitch(),
        .bgra = m_screenshot_frame->scanline_u8(0),
        .device_pixel_ratio = m_device_pixel_ratio,
    };
    Engine::the().notify(&XenonUiCallbacks::view_frame, m_screenshot_surface, static_cast<XenonFrame const*>(&frame));
}

void View::start_screenshot()
{
    if (m_screenshot_frame)
        return;
    m_screenshot_frame = copy_current_frame();
    if (!m_screenshot_frame || !s_screenshot_callbacks.selection_requested) {
        m_screenshot_frame = nullptr;
        take_and_save_screenshot(ScreenshotType::Visible);
        return;
    }
    m_screenshot_surface = Engine::the().allocate_view_id();
    send_screenshot_frame();
    s_screenshot_callbacks.selection_requested(s_screenshot_callbacks.context, m_id, m_screenshot_surface);
}

void View::refresh_screenshot_frame()
{
    if (!m_screenshot_frame)
        return;
    if (auto frame = copy_current_frame())
        m_screenshot_frame = move(frame);
    send_screenshot_frame();
}

void View::finish_screenshot(u32 answer, Gfx::IntRect selection)
{
    auto frame = move(m_screenshot_frame);
    m_screenshot_surface = 0;
    if (!frame)
        return;
    switch (answer) {
    case XENON_SCREENSHOT_SELECTION: {
        auto area = selection.intersected(frame->rect());
        if (area.is_empty())
            return;
        if (auto cropped = frame->cropped(area); !cropped.is_error())
            save_screenshot(*cropped.value());
        return;
    }
    case XENON_SCREENSHOT_VISIBLE_PAGE:
        save_screenshot(*frame);
        return;
    case XENON_SCREENSHOT_FULL_PAGE:
        take_and_save_screenshot(ScreenshotType::Full);
        return;
    default:
        return;
    }
}

}

void xenon_engine_set_screenshot_callbacks(XenonScreenshotCallbacks const* callbacks)
{
    Xenon::s_screenshot_callbacks = callbacks ? *callbacks : XenonScreenshotCallbacks {};
}

void xenon_engine_answer_save_location(uint64_t request, bool chosen, XenonStr path)
{
    auto chosen_path = chosen && path.data && path.length ? ByteString { path.data, path.length } : ByteString {};
    Xenon::Engine::the().post([request, chosen_path = move(chosen_path)] {
        auto promise = Xenon::save_location_requests().take(request);
        if (!promise.has_value())
            return;
        if (chosen_path.is_empty())
            promise.value()->reject(Error::from_errno(ECANCELED));
        else
            promise.value()->resolve(chosen_path);
    });
}

void xenon_engine_refresh_screenshot_frame(XenonViewId id)
{
    Xenon::Engine::the().post([id] {
        if (auto* view = Xenon::Engine::the().view(id))
            view->refresh_screenshot_frame();
    });
}

void xenon_engine_finish_screenshot(XenonViewId id, uint32_t answer, float x, float y, float width, float height)
{
    Gfx::IntRect selection { static_cast<int>(x), static_cast<int>(y), static_cast<int>(width), static_cast<int>(height) };
    Xenon::Engine::the().post([id, answer, selection] {
        if (auto* view = Xenon::Engine::the().view(id))
            view->finish_screenshot(answer, selection);
    });
}
