/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Function.h>
#include <AK/Optional.h>
#include <AK/String.h>
#include <AK/Vector.h>
#include <LibWebView/ContentBlocking.h>
#include <UI/Popover.h>

class QHideEvent;
class QIcon;
class QKeyEvent;
class QLabel;
class QMouseEvent;
class QScrollArea;
class QShowEvent;
class QToolButton;
class QVBoxLayout;

namespace Ladybird {

class BlockingPowerButton;

// uBlock Origin's per-site switches that Xenon has: the ones a filter can stand for (see UI/Blocking.cpp). uBO's others,
// for large media, cosmetic filtering and JavaScript, need more than Ladybird's content blocker does.
enum class BlockingSiteSwitch : u8 {
    Popups,
    RemoteFonts,
};

// A host a page loaded something from, as uBlock Origin lists the domains a page connected to.
struct BlockingDomain {
    String host;
    size_t requests { 0 };
    bool first_party { false };
};

// The toolbar shield's popover, laid out like uBlock Origin's popup: a power switch for the site, what was blocked on
// the page and since Xenon was first run, the domains the page connected to, the site's switches, the element picker
// and zapper, and a way to the blocking dashboard. It replaces ContentBlockingPopover, and Tab drives it the same way.
class BlockingPopover final : public Popover {
    AK_ALLOC_WITH_KMALLOC;

public:
    explicit BlockingPopover(QWidget* parent);

    void set_snapshot(WebView::ContentBlockingSnapshot const&);
    void set_favicon(QIcon const&);
    void update_chrome_style(QPalette const&);
    void show_error(QString const&);
    void focus_switch();

    void set_blocked_request_total(u64);
    // The domains the page's documents loaded from, or none while they aren't known.
    void set_domains(Optional<Vector<BlockingDomain>>);
    void set_site_switch(BlockingSiteSwitch, bool enabled);

    Function<void(bool)> on_toggle;
    Function<void()> on_keyboard_dismiss;
    Function<void(BlockingSiteSwitch, bool)> on_site_switch_toggle;
    Function<void()> on_pick_element;
    Function<void()> on_zap_element;
    Function<void()> on_open_dashboard;
    // Asks for what the snapshot doesn't carry, when the popover shows, and when its page or blocked count changes.
    Function<void()> on_refresh;
    Function<void()> on_hide;

private:
    virtual void keyPressEvent(QKeyEvent*) override;
    virtual void mousePressEvent(QMouseEvent*) override;
    virtual void showEvent(QShowEvent*) override;
    virtual void hideEvent(QHideEvent*) override;

    void update_counts();
    void update_domains();
    void update_size();

    WebView::ContentBlockingSnapshot m_snapshot;
    u64 m_blocked_request_total { 0 };
    Optional<Vector<BlockingDomain>> m_domains;

    QLabel* m_favicon { nullptr };
    QLabel* m_site { nullptr };
    QToolButton* m_dashboard_button { nullptr };
    BlockingPowerButton* m_power { nullptr };
    QLabel* m_power_caption { nullptr };
    QLabel* m_blocked_on_page { nullptr };
    QLabel* m_blocked_on_page_caption { nullptr };
    QLabel* m_blocked_in_total { nullptr };
    QLabel* m_domain_count { nullptr };
    QToolButton* m_pick_button { nullptr };
    QToolButton* m_zap_button { nullptr };
    QToolButton* m_popups_switch { nullptr };
    QToolButton* m_fonts_switch { nullptr };
    QLabel* m_domains_heading { nullptr };
    QScrollArea* m_domains_scroll { nullptr };
    QVBoxLayout* m_domains_layout { nullptr };
    QLabel* m_error { nullptr };
    QLabel* m_note { nullptr };
};

}
