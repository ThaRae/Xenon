/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/JsonArray.h>
#include <AK/JsonObject.h>
#include <AK/JsonValue.h>
#include <AK/LexicalPath.h>
#include <AK/Platform.h>
#include <AK/StringBuilder.h>
#include <LibCore/Directory.h>
#include <LibCore/Environment.h>
#include <LibCore/File.h>
#include <LibCore/System.h>
#include <LibCore/Timer.h>
#include <LibWebView/Application.h>
#include <LibWebView/WebUI.h>
#include <UI/Application.h>
#include <UI/Extensions/Registry.h>
#include <UI/Extensions/WebStore.h>
#include <UI/PasswordManagers.h>

namespace Ladybird {

// The Chrome Web Store IDs of the extensions, which their developers' keys give them.
static constexpr auto BITWARDEN_ID = "nngceckbapebfimnlniiiahkandclblb"sv;
static constexpr auto PROTON_PASS_ID = "ghmbeldphafepmbegfdlkpapadhbakde"sv;
static constexpr auto ONE_PASSWORD_ID = "aeblfdkhhhdcdjpifhhbdiojplfjncoa"sv;
static constexpr auto ICLOUD_PASSWORDS_ID = "pejdijmoenmkgeppbflobdenhhabjlaj"sv;

// For tests, whose extensions are signed with keys of their own:
// "bitwarden=<id>,proton-pass=<id>,1password=<id>,icloud-passwords=<id>".
static constexpr auto EXTENSION_IDS_VARIABLE = "XENON_PASSWORD_MANAGER_IDS"sv;

static constexpr auto STATE_NAME = "password-manager-updates.json"sv;

// As often as Chrome checks the store for updates to the extensions it installed from there.
static constexpr auto CHECK_INTERVAL = AK::Duration::from_seconds(24 * 60 * 60);
static constexpr auto RETRY_INTERVAL = AK::Duration::from_seconds(60 * 60);
// How often Xenon looks at whether a check is due, which also catches up soon after the computer wakes.
static constexpr auto TICK_INTERVAL = AK::Duration::from_seconds(10 * 60);
// The first check waits a moment, so that its downloads don't compete with the pages the browser opens with.
static constexpr auto FIRST_CHECK_DELAY = AK::Duration::from_seconds(5);

static String offered_ids(Vector<PasswordManagers::Choice> const& choices)
{
    StringBuilder builder;
    bool first = true;
    for (auto const& choice : choices) {
        if (!choice.available)
            continue;
        if (!first)
            builder.append(", "sv);
        first = false;
        builder.append(choice.id);
    }
    return MUST(builder.to_string());
}

PasswordManagers& PasswordManagers::the()
{
    static NeverDestroyed<PasswordManagers> password_managers;
    return *password_managers;
}

PasswordManagers::PasswordManagers()
{
    m_choices.append({ .id = "bitwarden"sv, .name = "Bitwarden"sv, .extension_id = MUST(String::from_utf8(BITWARDEN_ID)) });
    m_choices.append({ .id = "proton-pass"sv, .name = "Proton Pass"sv, .extension_id = MUST(String::from_utf8(PROTON_PASS_ID)) });
    m_choices.append({ .id = "1password"sv, .name = "1Password"sv, .extension_id = MUST(String::from_utf8(ONE_PASSWORD_ID)) });
    m_choices.append({
        .id = "icloud-passwords"sv,
        .name = "iCloud Passwords"sv,
        .extension_id = MUST(String::from_utf8(ICLOUD_PASSWORDS_ID)),
#if defined(AK_OS_MACOS)
        .available = true,
#else
        .available = false,
#endif
    });

    if (auto ids = Core::Environment::get(EXTENSION_IDS_VARIABLE); ids.has_value()) {
        ids->for_each_split_view(',', SplitBehavior::Nothing, [&](auto pair) {
            auto parts = pair.split_view('=');
            if (parts.size() != 2)
                return;
            for (auto& choice : m_choices) {
                if (choice.id == parts[0])
                    choice.extension_id = MUST(String::from_utf8(parts[1]));
            }
        });
    }
}

void PasswordManagers::initialize(Optional<ByteString> const& command_line_choice)
{
    m_state_file = LexicalPath::join(Application::profile().paths().data, "Extensions"sv, STATE_NAME).string();
    load_state();

    Extensions::Registry::the().on_extensions_change([this] {
        notify();
        return true;
    });

    if (command_line_choice.has_value()) {
        if (*command_line_choice == "none"sv)
            choose({});
        else if (choice(*command_line_choice).has_value())
            choose(command_line_choice->view());
        else
            warnln("There's no password manager called {}; Xenon offers {}", *command_line_choice, offered_ids(m_choices));
    }

    // The chosen password manager stays installed: one whose files are gone is installed again.
    if (auto current = this->current(); current.has_value() && !m_installing.has_value() && !Extensions::Registry::the().extension(current->extension_id)) {
        warnln("{} didn't load, so Xenon installs it again", current->name);
        install(*current);
    }

    m_timer = Core::Timer::create_repeating(TICK_INTERVAL, [this] { check_for_updates_if_due(); });
    m_timer->start();
    m_first_check = Core::Timer::create_single_shot(FIRST_CHECK_DELAY, [this] { check_for_updates_if_due(); });
    m_first_check->start();
}

Optional<PasswordManagers::Choice const&> PasswordManagers::choice(StringView id) const
{
    for (auto const& choice : m_choices) {
        if (choice.id == id)
            return choice;
    }
    return {};
}

Optional<PasswordManagers::Choice const&> PasswordManagers::current() const
{
    for (auto const& choice : m_choices) {
        if (Extensions::Registry::the().installed_version(choice.extension_id).has_value())
            return choice;
    }
    return {};
}

Optional<PasswordManagers::Choice const&> PasswordManagers::installing() const
{
    if (!m_installing.has_value())
        return {};
    return m_choices[*m_installing];
}

void PasswordManagers::choose(Optional<StringView> id)
{
    if (m_installing.has_value())
        return;
    m_error.clear();

    auto current = this->current();
    if (!id.has_value()) {
        for (auto const& choice : m_choices) {
            if (Extensions::Registry::the().installed_version(choice.extension_id).has_value())
                Extensions::Registry::the().remove(choice.extension_id);
        }
        notify();
        return;
    }

    auto chosen = choice(*id);
    if (!chosen.has_value()) {
        m_error = MUST(String::formatted("Xenon doesn't offer a password manager called {}.", *id));
        notify();
        return;
    }
    if (!chosen->available) {
        m_error = MUST(String::formatted(
            "{} is available on macOS, where it talks to Apple's Password Manager.",
            chosen->name));
        warnln("{}", *m_error);
        notify();
        return;
    }
    if (current.has_value() && current->id == chosen->id && Extensions::Registry::the().extension(chosen->extension_id)) {
        notify();
        return;
    }
    install(*chosen);
}

void PasswordManagers::install(Choice const& chosen)
{
    auto index = static_cast<size_t>(&chosen - m_choices.data());
    m_installing = index;
    m_error.clear();
    notify();

    Extensions::WebStore::fetch(chosen.extension_id, {}, [this, index](ErrorOr<Optional<Extensions::WebStore::Fetched>, String> result) {
        m_installing.clear();
        auto const& chosen = m_choices[index];

        auto install = [&]() -> ErrorOr<void, String> {
            auto fetched = TRY(move(result));
            if (!fetched.has_value())
                return "The Chrome Web Store has no version of it."_string;
            TRY(Extensions::Registry::the().install(fetched->crx, chosen.extension_id, Extensions::WebStore::publisher_key_hash()));
            return {};
        };
        if (auto installed = install(); installed.is_error()) {
            warnln("Could not install {}: {}", chosen.name, installed.error());
            m_error = MUST(String::formatted("{} couldn't be installed. {}", chosen.name, installed.error()));
            notify();
            return;
        }

        // One password manager at a time: the one chosen before gives way.
        for (auto const& other : m_choices) {
            if (other.id != chosen.id && Extensions::Registry::the().installed_version(other.extension_id).has_value())
                Extensions::Registry::the().remove(other.extension_id);
        }
        m_checked_at = UnixDateTime::now();
        save_state();
        notify();
    });
}

void PasswordManagers::register_web_ui_interfaces(WebView::WebUI& web_ui)
{
    web_ui.register_interface("loadPasswordManagers"sv, [this, &web_ui](auto const&) {
        send_status(web_ui);
    });
    web_ui.register_interface("setPasswordManager"sv, [this](JsonValue const& data) {
        if (data.is_null())
            choose({});
        else if (data.is_string())
            choose(data.as_string().bytes_as_string_view());
    });

    m_web_uis.remove_all_matching([](auto const& page) { return page.is_null(); });
    m_web_uis.append(web_ui.make_weak_ptr<WebView::WebUI>());
}

void PasswordManagers::send_status(WebView::WebUI& web_ui) const
{
    JsonArray choices;
    auto current = this->current();
    for (auto const& choice : m_choices) {
        if (!choice.available && (!current.has_value() || current->id != choice.id))
            continue;
        JsonObject json;
        json.set("id"sv, choice.id);
        json.set("name"sv, choice.name);
        choices.must_append(move(json));
    }

    auto id_or_null = [](Optional<Choice const&> choice) -> JsonValue {
        if (!choice.has_value())
            return JsonValue {};
        return MUST(String::from_utf8(choice->id));
    };

    JsonObject status;
    status.set("choices"sv, move(choices));
    status.set("current"sv, id_or_null(current));
    status.set("installing"sv, id_or_null(installing()));
    status.set("error"sv, m_error.has_value() ? JsonValue { *m_error } : JsonValue {});
    web_ui.async_send_message("passwordManagers"sv, move(status));
}

void PasswordManagers::on_change(Function<bool()> listener)
{
    m_change_listeners.append(move(listener));
}

void PasswordManagers::notify()
{
    for (auto const& page : m_web_uis) {
        if (auto* web_ui = page.ptr())
            send_status(*web_ui);
    }

    // NB: A listener can add another, so this goes through the ones there are now.
    auto listeners = move(m_change_listeners);
    for (auto& listener : listeners) {
        if (listener())
            m_change_listeners.append(move(listener));
    }
}

void PasswordManagers::load_state()
{
    auto file = Core::File::open(m_state_file, Core::File::OpenMode::Read);
    if (file.is_error())
        return;
    auto contents = file.value()->read_until_eof();
    if (contents.is_error())
        return;
    auto state = JsonValue::from_string(StringView { contents.value() });
    if (state.is_error() || !state.value().is_object())
        return;

    if (auto checked_at = state.value().as_object().get_i64("checkedAt"sv); checked_at.has_value())
        m_checked_at = UnixDateTime::from_seconds_since_epoch(*checked_at);
}

void PasswordManagers::save_state() const
{
    JsonObject state;
    if (m_checked_at.has_value())
        state.set("checkedAt"sv, m_checked_at->seconds_since_epoch());

    auto write = [&]() -> ErrorOr<void> {
        TRY(Core::Directory::create(LexicalPath { m_state_file }.parent(), Core::Directory::CreateDirectories::Yes));
        auto temporary = ByteString::formatted("{}.tmp", m_state_file);
        auto file = TRY(Core::File::open(temporary, Core::File::OpenMode::Write | Core::File::OpenMode::Truncate));
        auto serialized = state.serialized();
        TRY(file->write_until_depleted(serialized.bytes()));
        file->close();
        TRY(Core::System::rename(temporary, m_state_file));
        return {};
    };
    if (auto result = write(); result.is_error())
        warnln("Unable to save {}: {}", m_state_file, result.error());
}

void PasswordManagers::check_for_updates_if_due()
{
    if (m_installing.has_value() || m_updating || !WebView::Application::settings().background_networking_enabled())
        return;
    auto current = this->current();
    if (!current.has_value())
        return;
    auto version = Extensions::Registry::the().installed_version(current->extension_id);

    // A check from the future means the clock was wrong then, or is now, so it doesn't count.
    auto now = UnixDateTime::now();
    if (m_checked_at.has_value() && *m_checked_at <= now && now - *m_checked_at < CHECK_INTERVAL)
        return;
    if (m_attempted_at.has_value() && *m_attempted_at <= now && now - *m_attempted_at < RETRY_INTERVAL)
        return;

    m_updating = true;
    m_attempted_at = now;
    Extensions::WebStore::fetch(current->extension_id, *version, [this, extension_id = current->extension_id, name = current->name](auto result) {
        m_updating = false;
        if (result.is_error()) {
            warnln("Could not check for a new version of {}: {}", name, result.error());
            return;
        }
        // NB: The password manager may have changed while the store answered.
        if (result.value().has_value() && Extensions::Registry::the().installed_version(extension_id).has_value()) {
            if (auto installed = Extensions::Registry::the().install(result.value()->crx, extension_id, Extensions::WebStore::publisher_key_hash()); installed.is_error()) {
                warnln("Could not update {}: {}", name, installed.error());
                return;
            }
            dbgln("Updated {} to {}, from the next start", name, result.value()->version);
        }
        m_checked_at = UnixDateTime::now();
        save_state();
    });
}

}
