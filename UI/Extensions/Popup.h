/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/NonnullRefPtr.h>
#include <UI/Extensions/Extension.h>
#include <UI/Popover.h>

namespace Ladybird {

class WebContentView;

}

namespace Ladybird::Extensions {

// An extension's action popup: its default_popup page, in a popover below its toolbar button. Like Chrome's, it sizes
// itself to the page, between 25x25 and 800x600, and closes when it loses focus, on Escape, or when the page calls
// window.close().
class ActionPopup final : public Popover {
public:
    AK_ALLOC_WITH_KMALLOC;

    ActionPopup(QWidget* parent, QWidget& anchor, NonnullRefPtr<Extension>);
    virtual ~ActionPopup() override;

    Extension const& extension() const { return *m_extension; }

    void show_popup();

private:
    virtual void hideEvent(QHideEvent*) override;
    virtual void keyPressEvent(QKeyEvent*) override;

    void resize_to(int width, int height);

    QWidget& m_anchor;
    NonnullRefPtr<Extension> m_extension;
    WebContentView* m_view { nullptr };
};

}
