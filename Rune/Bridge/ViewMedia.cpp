/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <Rune/Bridge/View.h>

namespace Xenon {

void View::install_media_callbacks()
{
    on_screen_wake_lock_state_changed = [this](Web::ScreenWakeLockState state) {
        switch (state) {
        case Web::ScreenWakeLockState::Released:
            m_screen_wake_lock.clear();
            break;
        case Web::ScreenWakeLockState::Acquired:
            if (m_screen_wake_lock.has_value())
                break;
            if (auto inhibitor = WakeLock::DisplaySleepInhibitor::create("Xenon Content"sv); !inhibitor.is_error())
                m_screen_wake_lock = inhibitor.release_value();
            break;
        }
    };
}

void View::cancel_mouse_press()
{
    Web::DevicePixelPoint position { static_cast<int>(m_last_mouse_position.x()), static_cast<int>(m_last_mouse_position.y()) };
    enqueue_input_event(Web::MouseEvent {
        Web::MouseEvent::Type::MouseCancel,
        position,
        position,
        Web::UIEvents::MouseButton::None,
        Web::UIEvents::MouseButton::None,
        Web::UIEvents::KeyModifier::Mod_None,
        0,
        0,
        0,
        0,
        Web::WheelDeltaPrecision::Precise,
        Web::ScrollGesturePhase::None,
        0,
        nullptr,
    });
}

}
