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

class PasswordManagers {
    AK_MAKE_NONCOPYABLE(PasswordManagers);

public:
    struct Choice {
        StringView id;
        StringView name;
        String extension_id;
        bool available { true };
    };

    static PasswordManagers& the();

    void initialize(Optional<ByteString> const& command_line_choice);

    Vector<Choice> const& choices() const { return m_choices; }
    Optional<Choice const&> current() const;
    Optional<Choice const&> installing() const;
    Optional<String> const& error() const { return m_error; }

    void choose(Optional<StringView> id);

    void register_web_ui_interfaces(WebView::WebUI&);

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
