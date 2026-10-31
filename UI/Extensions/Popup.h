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

class Tab;
class WebContentView;

}

namespace Ladybird::Extensions {

class ActionPopup final : public Popover {
public:
    AK_ALLOC_WITH_KMALLOC;

    ActionPopup(Tab&, QWidget& anchor, NonnullRefPtr<Extension>, String popup_path);
    virtual ~ActionPopup() override;

    Extension const& extension() const { return *m_extension; }

    void show_popup();

private:
    virtual void hideEvent(QHideEvent*) override;
    virtual void keyPressEvent(QKeyEvent*) override;

    void resize_to(int width, int height);

    QWidget& m_anchor;
    NonnullRefPtr<Extension> m_extension;
    String m_popup_path;
    WebContentView* m_view { nullptr };
};

}
