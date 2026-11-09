/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/NonnullRefPtr.h>
#include <AK/OwnPtr.h>
#include <LibWebView/Forward.h>
#include <Rune/Bridge/SplitViews.h>
#include <Rune/Bridge/XenonBridgeSplitView.h>
#include <UI/XenonSettings.h>

namespace Xenon {

class SplitViewHost final
    : public SplitViews::Observer
    , public Ladybird::XenonSettings::Observer {
public:
    AK_ALLOC_WITH_KMALLOC;

    explicit SplitViewHost(BrowserModel&);
    virtual ~SplitViewHost() override;

    SplitViews& splits() { return m_splits; }

    bool contains(ViewId view) const { return m_splits.partner(view).has_value(); }
    Optional<ViewId> partner(ViewId view) const { return m_splits.partner(view); }

    void schedule_validation();
    NonnullRefPtr<WebView::Menu> context_menu_for(ViewId, WebView::Menu&);
    void open_new_split_view_in_active_window();
    void open_new_tab_beside(ViewId, SplitSide);
    void drop_tab(ViewId, SplitSide);

private:
    virtual void splits_changed() override;
    virtual void xenon_settings_changed() override;

    void report_drag_and_drop();
    void open_link_in_split_view(ViewId, WebView::Action& open_in_new_tab);

    BrowserModel& m_model;
    SplitViews m_splits;
    bool m_validation_scheduled { false };
    Optional<bool> m_reported_drag_and_drop;
};

}
