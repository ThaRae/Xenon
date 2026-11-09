/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteString.h>
#include <AK/HashMap.h>
#include <AK/Optional.h>
#include <AK/RefPtr.h>
#include <AK/Time.h>
#include <AK/Vector.h>
#include <LibCore/Forward.h>
#include <Rune/Bridge/XenonBridgeHibernation.h>
#include <UI/XenonSettings.h>

namespace Xenon {

void set_hibernation_callbacks(XenonHibernationCallbacks const&);

Vector<u8> hibernated_tab_icon(ReadonlyBytes rgba);

class TabHibernation final : public Ladybird::XenonSettings::Observer {
    AK_MAKE_NONCOPYABLE(TabHibernation);
    AK_MAKE_NONMOVABLE(TabHibernation);

public:
    AK_ALLOC_WITH_KMALLOC;

    enum class Request : u8 {
        Timeout,
        MemoryPressure,
        User,
    };

    TabHibernation();
    virtual ~TabHibernation() override;

    void start();

    bool may_hibernate(XenonViewId);
    void hibernate_when_ready(XenonViewId, Request);
    void change_settings(bool enabled, Optional<AK::Duration> timeout);

    void tab_inserted_unloaded(XenonViewId);
    void tab_loaded(XenonViewId);
    void tab_activated(Optional<XenonViewId> previous, XenonViewId);
    void tab_removed(XenonViewId);
    ByteString unloaded_tab_description(XenonViewId) const;

private:
    struct TabState {
        MonotonicTime last_used;
        Optional<MonotonicTime> last_audible {};
        bool is_preparing { false };
        Request request { Request::Timeout };
        Optional<u64> memory_freed {};
    };

    virtual void xenon_settings_changed() override;

    TabState& state_for(XenonViewId);
    bool is_in_use(XenonViewId) const;
    void settings_changed();
    void report_settings() const;
    void check_tabs();
    void hibernate_under_memory_pressure();
    void finish_hibernating(XenonViewId, bool has_unsubmitted_input);
    void report_load_state(XenonViewId, u32 state, XenonTabIcon const* = nullptr) const;

    HashMap<XenonViewId, TabState> m_tabs;
    RefPtr<Core::Timer> m_timer;
};

}
