/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/HashMap.h>
#include <LibCompositing/PictureInPictureControls.h>
#include <LibWebView/PictureInPictureWindow.h>
#include <Rune/Bridge/BrowserModel.h>
#include <Rune/Bridge/Engine.h>
#include <Rune/Bridge/PictureInPicture.h>
#include <Rune/Bridge/View.h>
#include <Rune/Bridge/XenonBridgeMedia.h>

namespace Xenon {

static XenonMediaCallbacks s_media_callbacks {};

template<typename Callback, typename... Arguments>
static void notify_media(Callback XenonMediaCallbacks::* callback, Arguments... arguments)
{
    if (auto function = s_media_callbacks.*callback)
        function(s_media_callbacks.context, arguments...);
}

class PictureInPictureWindow;

static HashMap<XenonViewId, PictureInPictureWindow*>& windows()
{
    static HashMap<XenonViewId, PictureInPictureWindow*> windows;
    return windows;
}

static Gfx::IntSize page_area_size_of(View const& view)
{
    auto ratio = max(view.device_pixel_ratio(), 0.01);
    auto size = view.viewport_size();
    return { max(1, static_cast<int>(size.width().value() / ratio)), max(1, static_cast<int>(size.height().value() / ratio)) };
}

class PictureInPictureWindow final : public WebView::PictureInPictureWindow {
public:
    AK_ALLOC_WITH_KMALLOC;

    PictureInPictureWindow(WebView::CanonicalTraversable& traversable, View const& owner_view, Gfx::IntSize video_size)
        : m_id(Engine::the().allocate_view_id())
        , m_owner_id(owner_view.id())
        , m_area_size(page_area_size_of(owner_view))
        , m_device_pixel_ratio(owner_view.device_pixel_ratio())
    {
        m_size = initial_size(video_size, m_area_size);
        m_view = View::create(m_id, device_size(m_size), m_device_pixel_ratio, owner_view.is_private(), Web::HTML::VisibilityState::Visible, traversable);
        m_view->adopt_owner(owner_view);
        report_page_close_of(*m_view);
        m_view->set_scales_frames_to_fit(true);
        m_view->enable_picture_in_picture_controls();
        m_view->on_picture_in_picture_control_activated = [this](Compositing::PictureInPictureControl control) {
            switch (control) {
            case Compositing::PictureInPictureControl::BackToTab:
                go_back_to_tab();
                break;
            case Compositing::PictureInPictureControl::Close:
                if (on_close)
                    on_close(VideoKeepsPlaying::No);
                break;
            }
        };
        m_view->on_finish_handling_mouse_event = [this](Web::MouseEvent const& event, Web::EventResult result) {
            if (event.type == Web::MouseEvent::Type::MouseDown)
                notify_media(&XenonMediaCallbacks::picture_in_picture_press_handled, m_id, result == Web::EventResult::Cancelled);
        };
        set_size_limits(video_size);
        windows().set(m_id, this);
        show();
    }

    virtual ~PictureInPictureWindow() override
    {
        windows().remove(m_id);
        notify_media(&XenonMediaCallbacks::picture_in_picture_hidden, m_id);
    }

    virtual Gfx::IntSize size() const override { return m_size; }
    virtual String handle() const override { return m_view->handle(); }

    virtual void hide() override
    {
        m_hidden = true;
        notify_media(&XenonMediaCallbacks::picture_in_picture_hidden, m_id);
    }

    virtual void set_video_size(Gfx::IntSize video_size) override
    {
        set_size_limits(video_size);
        auto size = size_for_video_size(m_size, video_size, m_area_size);
        if (size != m_size)
            resize(size, m_device_pixel_ratio);
        if (!m_hidden)
            show();
    }

    View& view() { return *m_view; }

    void resize(Gfx::IntSize size, double device_pixel_ratio)
    {
        auto minimum = minimum_size(m_aspect_ratio);
        auto maximum = maximum_size(m_aspect_ratio, m_area_size);
        size = { clamp(size.width(), minimum.width(), max(minimum.width(), maximum.width())), clamp(size.height(), minimum.height(), max(minimum.height(), maximum.height())) };
        auto changed = size != m_size;
        m_size = size;
        m_device_pixel_ratio = device_pixel_ratio;
        m_view->set_viewport(device_size(size), device_pixel_ratio);
        if (changed && on_resize)
            on_resize(size);
    }

private:
    Web::DevicePixelSize device_size(Gfx::IntSize size) const
    {
        return { static_cast<int>(size.width() * m_device_pixel_ratio), static_cast<int>(size.height() * m_device_pixel_ratio) };
    }

    void set_size_limits(Gfx::IntSize video_size)
    {
        m_aspect_ratio = aspect_ratio(video_size);
    }

    void show()
    {
        m_hidden = false;
        auto minimum = minimum_size(m_aspect_ratio);
        auto maximum = maximum_size(m_aspect_ratio, m_area_size);
        XenonPictureInPicture shown {
            .id = m_id,
            .owner = m_owner_id,
            .width = static_cast<float>(m_size.width()),
            .height = static_cast<float>(m_size.height()),
            .minimum_width = static_cast<float>(minimum.width()),
            .minimum_height = static_cast<float>(minimum.height()),
            .maximum_width = static_cast<float>(maximum.width()),
            .maximum_height = static_cast<float>(maximum.height()),
        };
        notify_media(&XenonMediaCallbacks::picture_in_picture_shown, static_cast<XenonPictureInPicture const*>(&shown));
    }

    void go_back_to_tab()
    {
        if (auto* model = Engine::the().model()) {
            if (auto const* window = model->window_of(m_owner_id)) {
                model->activate_window(window->id);
                model->activate_tab(m_owner_id);
            }
        }
        if (on_close)
            on_close(VideoKeepsPlaying::Yes);
    }

    XenonViewId m_id { 0 };
    XenonViewId m_owner_id { 0 };
    Gfx::IntSize m_area_size;
    double m_device_pixel_ratio { 1 };
    Gfx::IntSize m_size;
    Gfx::IntSize m_aspect_ratio;
    bool m_hidden { false };
    OwnPtr<View> m_view;
};

NonnullOwnPtr<WebView::PictureInPictureWindow> create_picture_in_picture_window_for(WebView::CanonicalTraversable& traversable, View const& owner_view, Gfx::IntSize video_size)
{
    return make<PictureInPictureWindow>(traversable, owner_view, video_size);
}

static PictureInPictureWindow* window_with_id(XenonViewId id)
{
    return windows().get(id).value_or(nullptr);
}

}

using Xenon::Engine;

static void post_to_window(XenonViewId id, Function<void(Xenon::PictureInPictureWindow&)> command)
{
    Engine::the().post([id, command = move(command)] {
        if (auto* window = Xenon::window_with_id(id))
            command(*window);
    });
}

void xenon_engine_set_media_callbacks(XenonMediaCallbacks const* callbacks)
{
    Xenon::s_media_callbacks = callbacks ? *callbacks : XenonMediaCallbacks {};
}

void xenon_engine_picture_in_picture_mouse_event(XenonViewId id, XenonMouseEvent const* event)
{
    if (!event)
        return;
    post_to_window(id, [event = *event](auto& window) {
        window.view().handle_mouse_event(event);
    });
}

void xenon_engine_picture_in_picture_cancel_press(XenonViewId id)
{
    post_to_window(id, [](auto& window) {
        window.view().cancel_mouse_press();
    });
}

void xenon_engine_picture_in_picture_resize(XenonViewId id, float width, float height, double device_pixel_ratio)
{
    post_to_window(id, [width, height, device_pixel_ratio](auto& window) {
        window.resize({ max(1, static_cast<int>(width)), max(1, static_cast<int>(height)) }, device_pixel_ratio > 0 ? device_pixel_ratio : 1.0);
    });
}
