/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/kmalloc.h>

#include <QWidget>

class QFrame;
class QLabel;

namespace Ladybird {

class WebContentView;

class PageUnresponsivePrompt final : public QWidget {
public:
    AK_ALLOC_WITH_KMALLOC;

    static void attach(WebContentView&);

private:
    explicit PageUnresponsivePrompt(WebContentView&);

    void show_for_current_page();
    void place_over_view();
    void update_chrome_style();

    virtual bool event(QEvent*) override;
    virtual bool eventFilter(QObject*, QEvent*) override;

    WebContentView& m_view;
    QFrame* m_card { nullptr };
    QLabel* m_message { nullptr };
    bool m_is_updating_chrome_style { false };
};

}
