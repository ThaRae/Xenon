/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteString.h>
#include <AK/Function.h>
#include <AK/NeverDestroyed.h>
#include <AK/Optional.h>
#include <AK/String.h>
#include <AK/Time.h>
#include <AK/Vector.h>
#include <AK/WeakPtr.h>
#include <LibCore/Forward.h>
#include <LibWebView/Forward.h>

namespace Ladybird {

// The password managers Xenon offers to set up, as Helium offers them during setup: the extension each one's developer
// publishes in the Chrome Web Store. Choosing one downloads and installs it (UI/Extensions/WebStore.h), and it then
// loads every session, and is checked for updates once a day, until it's removed or another is chosen. Xenon bundles
// neither, so it redistributes neither: each is fetched from its developer's own store listing, signed by them.
//
// iCloud Passwords is offered only on macOS. Its extension talks to Apple's Password Manager through native messaging
// (UI/Extensions/NativeMessaging.h), using the helper macOS registers for Chrome. Linux has no such helper.
//
// Which one is chosen isn't a setting of its own: it's whichever of them is installed in the profile. Removing it from
// the Extensions window chooses none.
//
// Pages talk to it through two WebUI messages, on about:welcome and about:settings:
//   loadPasswordManagers                       answered with a passwordManagers message
//   setPasswordManager <id or null>            installs that password manager, or removes the installed one
// and every change sends a passwordManagers message to every open page that has one:
//   { choices: [{ id, name }], current: id or null, installing: id or null, error: string or null }
class PasswordManagers {
    AK_MAKE_NONCOPYABLE(PasswordManagers);

public:
    struct Choice {
        // What pages and the command line call it.
        StringView id;
        StringView name;
        String extension_id;
        // False when this platform can't run it: iCloud Passwords needs Apple's helper, which exists on macOS.
        bool available { true };
    };

    static PasswordManagers& the();

    // Reinstalls the chosen password manager if it didn't load, applies the command line's choice if there is one, and
    // starts the daily update checks. Call it once the application exists and the extensions have loaded.
    void initialize(Optional<ByteString> const& command_line_choice);

    Vector<Choice> const& choices() const { return m_choices; }
    Optional<Choice const&> current() const;
    Optional<Choice const&> installing() const;
    Optional<String> const& error() const { return m_error; }

    // Installs a password manager in place of the one installed, which is removed once the new one is in. None removes
    // the one installed. Nothing happens while another is being installed.
    void choose(Optional<StringView> id);

    // Gives a WebUI page the messages above.
    void register_web_ui_interfaces(WebView::WebUI&);

    // Called whenever the choice or the installation's progress changes, until the listener returns false.
    void on_change(Function<bool()>);

private:
    friend class AK::NeverDestroyed<PasswordManagers>;
    PasswordManagers();

    Optional<Choice const&> choice(StringView id) const;
    void install(Choice const&);
    void notify();
    void send_status(WebView::WebUI&) const;

    void load_state();
    void save_state() const;
    void check_for_updates_if_due();

    Vector<Choice> m_choices;
    Optional<size_t> m_installing;
    Optional<String> m_error;

    Vector<WeakPtr<WebView::WebUI>> m_web_uis;
    Vector<Function<bool()>> m_change_listeners;

    ByteString m_state_file;
    Optional<UnixDateTime> m_checked_at;
    Optional<UnixDateTime> m_attempted_at;
    bool m_updating { false };
    RefPtr<Core::Timer> m_timer;
    RefPtr<Core::Timer> m_first_check;
};

}
