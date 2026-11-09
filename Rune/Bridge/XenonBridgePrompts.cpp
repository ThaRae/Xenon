/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/ByteString.h>
#include <AK/Vector.h>
#include <LibGfx/Color.h>
#include <LibWebView/WebAuthn/Prompt.h>
#include <Rune/Bridge/Engine.h>
#include <Rune/Bridge/View.h>
#include <Rune/Bridge/XenonBridgePrompts.h>

namespace Xenon {

static XenonPromptCallbacks s_prompt_callbacks {};

void set_prompt_callbacks(XenonPromptCallbacks const& callbacks)
{
    s_prompt_callbacks = callbacks;
}

XenonPromptCallbacks const& prompt_callbacks()
{
    return s_prompt_callbacks;
}

static XenonStr xenon_str(StringView string)
{
    return { string.characters_without_null_termination(), string.length() };
}

static ByteString without_carriage_returns(StringView text)
{
    if (!text.contains('\r'))
        return text;
    return text.replace("\r\n"sv, "\n"sv, ReplaceMode::All).replace("\r"sv, "\n"sv, ReplaceMode::All);
}

static u32 rgb_from(Color color)
{
    return (static_cast<u32>(color.red()) << 16) | (static_cast<u32>(color.green()) << 8) | color.blue();
}

static u32 web_authn_state(WebView::WebAuthnPrompt::State state)
{
    using State = WebView::WebAuthnPrompt::State;
    switch (state) {
    case State::InsertKey:
        return XENON_WEB_AUTHN_INSERT_KEY;
    case State::EnterPin:
        return XENON_WEB_AUTHN_ENTER_PIN;
    case State::TouchKey:
        return XENON_WEB_AUTHN_TOUCH_KEY;
    case State::ChooseAccount:
        return XENON_WEB_AUTHN_CHOOSE_ACCOUNT;
    case State::AttestationConsent:
        return XENON_WEB_AUTHN_ATTESTATION_CONSENT;
    case State::KeyNotRegistered:
        return XENON_WEB_AUTHN_KEY_NOT_REGISTERED;
    case State::KeyAlreadyRegistered:
        return XENON_WEB_AUTHN_KEY_ALREADY_REGISTERED;
    case State::PinBlocked:
        return XENON_WEB_AUTHN_PIN_BLOCKED;
    case State::PinNotSet:
        return XENON_WEB_AUTHN_PIN_NOT_SET;
    case State::KeyNotSupported:
        return XENON_WEB_AUTHN_KEY_NOT_SUPPORTED;
    case State::NoPlatformAuthenticator:
        return XENON_WEB_AUTHN_NO_PLATFORM_AUTHENTICATOR;
    }
    VERIFY_NOT_REACHED();
}

static ByteString web_authn_account_label(WebView::WebAuthnPrompt::Account const& account)
{
    if (account.display_name.is_empty() || account.display_name == account.name)
        return account.name.is_empty() ? ByteString { "Unnamed account"sv } : account.name.to_byte_string();
    return ByteString::formatted("{} ({})", account.display_name, account.name);
}

void View::install_prompt_callbacks()
{
    on_request_tooltip_override = [this](Gfx::IntPoint position, ByteString const& tooltip) {
        auto text = without_carriage_returns(tooltip);
        notify_prompts(&XenonPromptCallbacks::tooltip_override, m_id, xenon_str(text), static_cast<float>(position.x()), static_cast<float>(position.y()));
    };

    on_stop_tooltip_override = [this] {
        notify_prompts(&XenonPromptCallbacks::tooltip_override, m_id, xenon_str({}), 0.f, 0.f);
    };

    on_request_color_picker = [this](Color current_color) {
        m_color_picker_id = next_prompt_id();
        notify_prompts(&XenonPromptCallbacks::color_picker_requested, m_id, m_color_picker_id, rgb_from(current_color));
    };

    on_request_date_picker = [this](Utf16String const& input_type, Utf16String const& value, Utf16String const& minimum, Utf16String const& maximum) {
        m_date_picker_id = next_prompt_id();
        auto type_text = input_type.to_utf8().to_byte_string();
        auto value_text = value.to_utf8().to_byte_string();
        auto minimum_text = minimum.to_utf8().to_byte_string();
        auto maximum_text = maximum.to_utf8().to_byte_string();
        XenonDatePicker picker {
            .id = m_date_picker_id,
            .input_type = xenon_str(type_text),
            .value = xenon_str(value_text),
            .minimum = xenon_str(minimum_text),
            .maximum = xenon_str(maximum_text),
        };
        notify_prompts(&XenonPromptCallbacks::date_picker_requested, m_id, static_cast<XenonDatePicker const*>(&picker));
    };

    on_web_authn_prompt = [this](WebView::WebAuthnPrompt const& prompt) {
        m_web_authn_prompt_shown = true;
        auto relying_party = prompt.rp_id.to_byte_string();
        auto user_name = prompt.user_name.to_byte_string();
        ByteString caller;
        if (prompt.top_level_origin.has_value())
            caller = ByteString::formatted("Asked by {}, in a frame on {}", prompt.origin.serialize(), prompt.top_level_origin->serialize());

        Vector<ByteString> account_labels;
        for (auto const& account : prompt.accounts)
            account_labels.append(web_authn_account_label(account));
        Vector<XenonStr> accounts;
        for (auto const& label : account_labels)
            accounts.append(xenon_str(label));

        XenonWebAuthnPrompt shown {
            .state = web_authn_state(prompt.state),
            .registering = prompt.operation == WebView::WebAuthnPrompt::Operation::Create,
            .relying_party = xenon_str(relying_party),
            .user_name = xenon_str(user_name),
            .caller = xenon_str(caller),
            .pin_was_wrong = prompt.pin_was_wrong,
            .pin_retries = prompt.pin_retries.has_value() ? static_cast<i32>(*prompt.pin_retries) : -1,
            .accounts = accounts.data(),
            .account_count = accounts.size(),
        };
        notify_prompts(&XenonPromptCallbacks::web_authn_prompt, m_id, static_cast<XenonWebAuthnPrompt const*>(&shown));
    };

    on_web_authn_prompt_closed = [this] {
        m_web_authn_prompt_shown = false;
        notify_prompts(&XenonPromptCallbacks::web_authn_prompt_closed, m_id);
    };
}

void View::reset_prompts()
{
    m_color_picker_id = 0;
    m_date_picker_id = 0;
    notify_prompts(&XenonPromptCallbacks::tooltip_override, m_id, xenon_str({}), 0.f, 0.f);
    if (exchange(m_web_authn_prompt_shown, false))
        notify_prompts(&XenonPromptCallbacks::web_authn_prompt_closed, m_id);
}

void View::request_find_selection(u64 request)
{
    selected_text()->when_resolved([weak_this = make_weak_ptr<View>(), request](ByteString const& text) {
        if (weak_this)
            notify_prompts(&XenonPromptCallbacks::find_selection, weak_this->id(), request, xenon_str(text));
    });
}

void View::update_color_picker(XenonPromptId id, Optional<Color> color, bool closed)
{
    if (id == 0 || id != m_color_picker_id)
        return;
    if (closed)
        m_color_picker_id = 0;
    color_picker_update(color, closed ? Web::HTML::ColorPickerUpdateState::Closed : Web::HTML::ColorPickerUpdateState::Update);
}

void View::close_date_picker(XenonPromptId id, Optional<StringView> value)
{
    if (id == 0 || id != m_date_picker_id)
        return;
    m_date_picker_id = 0;
    if (!value.has_value()) {
        date_picker_closed({});
        return;
    }
    auto utf8_value = String::from_utf8(*value);
    date_picker_closed(utf8_value.is_error() ? Optional<Utf16String> {} : Utf16String::from_utf8(utf8_value.value()));
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

void xenon_engine_set_prompt_callbacks(XenonPromptCallbacks const* callbacks)
{
    Xenon::set_prompt_callbacks(callbacks ? *callbacks : XenonPromptCallbacks {});
}

void xenon_engine_request_find_selection(XenonViewId id, uint64_t request)
{
    post_to_view(id, [request](View& view) {
        view.request_find_selection(request);
    });
}

void xenon_engine_color_picker_update(XenonViewId id, XenonPromptId picker, bool has_color, uint32_t rgb, bool closed)
{
    auto color = has_color ? Optional<Color> { Color((rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff) } : Optional<Color> {};
    post_to_view(id, [picker, color, closed](View& view) {
        view.update_color_picker(picker, color, closed);
    });
}

void xenon_engine_close_date_picker(XenonViewId id, XenonPromptId picker, bool has_value, XenonStr value)
{
    post_to_view(id, [picker, has_value, value = ByteString { string_view_from(value) }](View& view) {
        view.close_date_picker(picker, has_value ? Optional<StringView> { value.view() } : Optional<StringView> {});
    });
}

void xenon_engine_web_authn_provide_pin(XenonViewId id, XenonStr pin)
{
    auto pin_text = String::from_utf8(string_view_from(pin));
    if (pin_text.is_error() || pin_text.value().is_empty())
        return;
    post_to_view(id, [pin = pin_text.release_value()](View& view) {
        view.web_authn_provide_pin(pin);
    });
}

void xenon_engine_web_authn_choose_account(XenonViewId id, size_t index)
{
    post_to_view(id, [index](View& view) {
        view.web_authn_choose_account(index);
    });
}

void xenon_engine_web_authn_allow_attestation(XenonViewId id, bool allow)
{
    post_to_view(id, [allow](View& view) {
        view.web_authn_allow_attestation(allow);
    });
}

void xenon_engine_web_authn_cancel(XenonViewId id)
{
    post_to_view(id, [](View& view) {
        view.web_authn_cancel();
    });
}
