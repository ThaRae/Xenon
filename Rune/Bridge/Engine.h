/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Atomic.h>
#include <AK/ByteString.h>
#include <AK/Function.h>
#include <AK/HashMap.h>
#include <AK/Mutex.h>
#include <AK/NonnullOwnPtr.h>
#include <AK/Optional.h>
#include <AK/RefPtr.h>
#include <AK/Vector.h>
#include <LibCore/AnonymousBuffer.h>
#include <LibCore/EventLoop.h>
#include <LibThreading/Thread.h>
#include <LibURL/Forward.h>
#include <LibWebCommon/HTML/ActivateTab.h>
#include <LibWebCommon/PixelUnits.h>
#include <LibWebView/Forward.h>
#include <Rune/Bridge/XenonBridge.h>

namespace Xenon {

class View;

class Engine {
public:
    static Engine& the();

    bool start(XenonUiCallbacks const&, int argc, char const* const* argv);
    void shutdown();

    XenonViewId allocate_view_id() { return m_next_view_id.fetch_add(1); }
    void post(Function<void()>);

    template<typename Callback, typename... Arguments>
    void notify(Callback XenonUiCallbacks::* callback, Arguments... arguments) const
    {
        if (auto function = m_callbacks.*callback)
            function(m_callbacks.context, arguments...);
    }

    Core::AnonymousBuffer const& theme() const { return m_theme; }

    View* view(XenonViewId);
    View& create_view(XenonViewId, Web::DevicePixelSize, double device_pixel_ratio, StringView input);
    View& open_view(XenonViewId opener, Web::HTML::ActivateTab, Optional<WebView::CanonicalTraversable&> = {});
    void close_view(XenonViewId);

    void set_focused_view(XenonViewId, bool focused);
    View* focused_view();

private:
    Engine() = default;

    intptr_t run();
    int run_application();
    void accept_commands();
    void stop_accepting_commands();
    View& insert_view(NonnullOwnPtr<View>);
    void open_urls_in_new_views(Vector<URL::URL> const&);

    enum class State : u8 {
        Idle,
        Starting,
        Running,
        Stopped,
    };

    XenonUiCallbacks m_callbacks {};
    Vector<ByteString> m_arguments;
    Vector<StringView> m_argument_views;
    Vector<char*> m_argv;

    Mutex m_mutex;
    State m_state { State::Idle };
    RefPtr<Core::WeakEventLoopReference> m_event_loop;
    Vector<Function<void()>> m_pending_commands;
    RefPtr<Threading::Thread> m_thread;

    Atomic<XenonViewId> m_next_view_id { 1 };

    Core::AnonymousBuffer m_theme;
    HashMap<XenonViewId, NonnullOwnPtr<View>> m_views;
    Optional<XenonViewId> m_focused_view;
};

}
