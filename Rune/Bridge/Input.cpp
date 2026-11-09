/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Utf8View.h>
#include <Rune/Bridge/Input.h>

namespace Xenon {

using Web::UIEvents::KeyCode;

struct NamedKey {
    StringView name;
    KeyCode key_code;
    u32 code_point { 0 };
};

static constexpr NamedKey s_keys_by_name[] = {
    { "Backspace"sv, KeyCode::Key_Backspace, '\b' },
    { "Tab"sv, KeyCode::Key_Tab, '\t' },
    { "Enter"sv, KeyCode::Key_Return, '\n' },
    { "Escape"sv, KeyCode::Key_Escape, 0x1b },
    { "Delete"sv, KeyCode::Key_Delete, 0x7f },
    { "Insert"sv, KeyCode::Key_Insert },
    { "PageUp"sv, KeyCode::Key_PageUp },
    { "PageDown"sv, KeyCode::Key_PageDown },
    { "End"sv, KeyCode::Key_End },
    { "Home"sv, KeyCode::Key_Home },
    { "ArrowLeft"sv, KeyCode::Key_Left },
    { "ArrowUp"sv, KeyCode::Key_Up },
    { "ArrowRight"sv, KeyCode::Key_Right },
    { "ArrowDown"sv, KeyCode::Key_Down },
    { "PrintScreen"sv, KeyCode::Key_PrintScreen },
    { "Pause"sv, KeyCode::Key_PauseBreak },
    { "CapsLock"sv, KeyCode::Key_CapsLock },
    { "NumLock"sv, KeyCode::Key_NumLock },
    { "ScrollLock"sv, KeyCode::Key_ScrollLock },
    { "F1"sv, KeyCode::Key_F1 },
    { "F2"sv, KeyCode::Key_F2 },
    { "F3"sv, KeyCode::Key_F3 },
    { "F4"sv, KeyCode::Key_F4 },
    { "F5"sv, KeyCode::Key_F5 },
    { "F6"sv, KeyCode::Key_F6 },
    { "F7"sv, KeyCode::Key_F7 },
    { "F8"sv, KeyCode::Key_F8 },
    { "F9"sv, KeyCode::Key_F9 },
    { "F10"sv, KeyCode::Key_F10 },
    { "F11"sv, KeyCode::Key_F11 },
    { "F12"sv, KeyCode::Key_F12 },
    { "Shift"sv, KeyCode::Key_LeftShift },
    { "Control"sv, KeyCode::Key_LeftControl },
    { "Alt"sv, KeyCode::Key_LeftAlt },
    { "AltGraph"sv, KeyCode::Key_AltGr },
    { "Meta"sv, KeyCode::Key_LeftSuper },
    { "Super"sv, KeyCode::Key_LeftSuper },
    { "ContextMenu"sv, KeyCode::Key_Menu },
    { "BrowserBack"sv, KeyCode::Key_BrowserBack },
    { "BrowserForward"sv, KeyCode::Key_BrowserForward },
    { "BrowserRefresh"sv, KeyCode::Key_BrowserRefresh },
    { "BrowserStop"sv, KeyCode::Key_BrowserStop },
    { "BrowserSearch"sv, KeyCode::Key_BrowserSearch },
    { "BrowserFavorites"sv, KeyCode::Key_BrowserFavorites },
    { "BrowserHome"sv, KeyCode::Key_BrowserHome },
    { "AudioVolumeDown"sv, KeyCode::Key_VolumeDown },
    { "AudioVolumeUp"sv, KeyCode::Key_VolumeUp },
    { "AudioVolumeMute"sv, KeyCode::Key_Mute },
    { "MediaPlayPause"sv, KeyCode::Key_PlayPause },
    { "MediaTrackNext"sv, KeyCode::Key_NextTrack },
    { "MediaTrackPrevious"sv, KeyCode::Key_PreviousTrack },
    { "MediaStop"sv, KeyCode::Key_Stop },
};

static constexpr NamedKey s_side_specific_keys_by_code[] = {
    { "ShiftLeft"sv, KeyCode::Key_LeftShift },
    { "ShiftRight"sv, KeyCode::Key_RightShift },
    { "ControlLeft"sv, KeyCode::Key_LeftControl },
    { "ControlRight"sv, KeyCode::Key_RightControl },
    { "AltLeft"sv, KeyCode::Key_LeftAlt },
    { "AltRight"sv, KeyCode::Key_RightAlt },
    { "MetaLeft"sv, KeyCode::Key_LeftSuper },
    { "MetaRight"sv, KeyCode::Key_RightSuper },
    { "OSLeft"sv, KeyCode::Key_LeftSuper },
    { "OSRight"sv, KeyCode::Key_RightSuper },
};

Web::UIEvents::KeyModifier key_modifiers_from(u32 xenon_modifiers)
{
    auto modifiers = Web::UIEvents::KeyModifier::Mod_None;
    if (xenon_modifiers & XENON_MODIFIER_SHIFT)
        modifiers |= Web::UIEvents::KeyModifier::Mod_Shift;
    if (xenon_modifiers & XENON_MODIFIER_CTRL)
        modifiers |= Web::UIEvents::KeyModifier::Mod_Ctrl;
    if (xenon_modifiers & XENON_MODIFIER_ALT)
        modifiers |= Web::UIEvents::KeyModifier::Mod_Alt;
    if (xenon_modifiers & XENON_MODIFIER_SUPER)
        modifiers |= Web::UIEvents::KeyModifier::Mod_Super;
    return modifiers;
}

Web::UIEvents::MouseButton mouse_buttons_from(u32 xenon_buttons)
{
    static constexpr u32 all_buttons = XENON_MOUSE_BUTTON_PRIMARY | XENON_MOUSE_BUTTON_SECONDARY | XENON_MOUSE_BUTTON_MIDDLE | XENON_MOUSE_BUTTON_BACKWARD | XENON_MOUSE_BUTTON_FORWARD;
    static_assert(static_cast<u32>(XENON_MOUSE_BUTTON_PRIMARY) == static_cast<u32>(Web::UIEvents::MouseButton::Primary));
    static_assert(static_cast<u32>(XENON_MOUSE_BUTTON_SECONDARY) == static_cast<u32>(Web::UIEvents::MouseButton::Secondary));
    static_assert(static_cast<u32>(XENON_MOUSE_BUTTON_MIDDLE) == static_cast<u32>(Web::UIEvents::MouseButton::Middle));
    static_assert(static_cast<u32>(XENON_MOUSE_BUTTON_BACKWARD) == static_cast<u32>(Web::UIEvents::MouseButton::Backward));
    static_assert(static_cast<u32>(XENON_MOUSE_BUTTON_FORWARD) == static_cast<u32>(Web::UIEvents::MouseButton::Forward));
    return static_cast<Web::UIEvents::MouseButton>(xenon_buttons & all_buttons);
}

static Optional<NamedKey> find_key(ReadonlySpan<NamedKey> keys, StringView name)
{
    for (auto const& key : keys) {
        if (key.name == name)
            return key;
    }
    return {};
}

static Optional<u32> single_code_point(StringView text)
{
    Utf8View view { text };
    if (text.is_empty() || !view.validate())
        return {};
    auto iterator = view.begin();
    auto code_point = *iterator;
    if (++iterator != view.end())
        return {};
    return code_point;
}

static KeyCode key_code_from_physical_code(StringView code)
{
    if (code.length() == 4 && code.starts_with("Key"sv) && is_ascii_upper_alpha(code[3]))
        return static_cast<KeyCode>(KeyCode::Key_A + (code[3] - 'A'));
    if (code.length() == 6 && code.starts_with("Digit"sv) && is_ascii_digit(code[5]))
        return static_cast<KeyCode>(KeyCode::Key_0 + (code[5] - '0'));
    if (code == "Space"sv)
        return KeyCode::Key_Space;
    if (auto named_key = find_key(s_keys_by_name, code); named_key.has_value())
        return named_key->key_code;
    return KeyCode::Key_Invalid;
}

Web::KeyEvent key_event_from(XenonKeyEventType type, StringView code, StringView key, u32 xenon_modifiers, bool repeat)
{
    auto web_type = type == XENON_KEY_EVENT_UP ? Web::KeyEvent::Type::KeyUp : Web::KeyEvent::Type::KeyDown;
    auto modifiers = key_modifiers_from(xenon_modifiers);
    if (code.starts_with("Numpad"sv))
        modifiers |= Web::UIEvents::KeyModifier::Mod_Keypad;

    auto key_code = KeyCode::Key_Invalid;
    u32 code_point = 0;

    if (auto side_specific_key = find_key(s_side_specific_keys_by_code, code); side_specific_key.has_value()) {
        key_code = side_specific_key->key_code;
    } else if (auto named_key = find_key(s_keys_by_name, key); named_key.has_value()) {
        key_code = named_key->key_code;
        code_point = named_key->code_point;
    } else if (auto character = single_code_point(key); character.has_value()) {
        code_point = *character;
        key_code = Web::UIEvents::code_point_to_key_code(code_point);
        if (key_code == KeyCode::Key_Invalid)
            key_code = key_code_from_physical_code(code);
    } else {
        key_code = key_code_from_physical_code(code);
    }

    auto is_shift_tab = key_code == KeyCode::Key_Tab && (modifiers & Web::UIEvents::KeyModifier::Mod_Shift);
    auto should_insert_text = web_type == Web::KeyEvent::Type::KeyDown && code_point != 0 && !is_shift_tab;

    return Web::KeyEvent { web_type, key_code, modifiers, code_point, repeat, should_insert_text, nullptr };
}

XenonCursor xenon_cursor_from(Gfx::Cursor const& cursor)
{
    if (!cursor.has<Gfx::StandardCursor>())
        return XENON_CURSOR_DEFAULT;

    switch (cursor.get<Gfx::StandardCursor>()) {
    case Gfx::StandardCursor::None:
    case Gfx::StandardCursor::Arrow:
    case Gfx::StandardCursor::DragCopy:
        return XENON_CURSOR_DEFAULT;
    case Gfx::StandardCursor::Hidden:
        return XENON_CURSOR_HIDDEN;
    case Gfx::StandardCursor::Crosshair:
    case Gfx::StandardCursor::Eyedropper:
        return XENON_CURSOR_CROSSHAIR;
    case Gfx::StandardCursor::IBeam:
        return XENON_CURSOR_TEXT;
    case Gfx::StandardCursor::ResizeHorizontal:
        return XENON_CURSOR_RESIZE_EW;
    case Gfx::StandardCursor::ResizeVertical:
        return XENON_CURSOR_RESIZE_NS;
    case Gfx::StandardCursor::ResizeDiagonalTLBR:
        return XENON_CURSOR_RESIZE_NWSE;
    case Gfx::StandardCursor::ResizeDiagonalBLTR:
        return XENON_CURSOR_RESIZE_NESW;
    case Gfx::StandardCursor::ResizeColumn:
        return XENON_CURSOR_RESIZE_COLUMN;
    case Gfx::StandardCursor::ResizeRow:
        return XENON_CURSOR_RESIZE_ROW;
    case Gfx::StandardCursor::Hand:
        return XENON_CURSOR_POINTER;
    case Gfx::StandardCursor::Help:
        return XENON_CURSOR_HELP;
    case Gfx::StandardCursor::OpenHand:
        return XENON_CURSOR_GRAB;
    case Gfx::StandardCursor::Drag:
        return XENON_CURSOR_GRABBING;
    case Gfx::StandardCursor::Move:
        return XENON_CURSOR_MOVE;
    case Gfx::StandardCursor::Wait:
        return XENON_CURSOR_WAIT;
    case Gfx::StandardCursor::Disallowed:
        return XENON_CURSOR_NOT_ALLOWED;
    case Gfx::StandardCursor::Zoom:
        return XENON_CURSOR_ZOOM_IN;
    }
    VERIFY_NOT_REACHED();
}

}
