/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/kmalloc.h>
#include <LibWebView/WebAuthn/Prompt.h>
#include <UI/Popover.h>

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

namespace Ladybird {

class WebContentView;

class WebAuthnPopover final : public Popover {
public:
    AK_ALLOC_WITH_KMALLOC;

    static void attach(WebContentView&);

private:
    explicit WebAuthnPopover(WebContentView&);

    void update(WebView::WebAuthnPrompt const&);
    void show_over_view();
    void close_without_cancelling();
    void submit();

    virtual void hideEvent(QHideEvent*) override;

    WebContentView& m_view;
    WebView::WebAuthnPrompt::State m_state { WebView::WebAuthnPrompt::State::InsertKey };
    bool m_is_closing_without_cancelling { false };

    QLabel* m_title { nullptr };
    QLabel* m_caller { nullptr };
    QLabel* m_body { nullptr };
    QLineEdit* m_pin { nullptr };
    QListWidget* m_accounts { nullptr };
    QPushButton* m_dismiss_button { nullptr };
    QPushButton* m_accept_button { nullptr };
};

}
