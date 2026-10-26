/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/NonnullRefPtr.h>
#include <AK/kmalloc.h>
#include <UI/Extensions/Extension.h>

#include <QPointer>
#include <QToolButton>
#include <QWidget>

namespace Ladybird::Extensions {

class ActionPopup;

// An extension's toolbar button: its action's icon and title, opening its popup.
class ActionButton final : public QToolButton {
public:
    AK_ALLOC_WITH_KMALLOC;

    ActionButton(QWidget* parent, QWidget& popup_parent, NonnullRefPtr<Extension>);

    Extension const& extension() const { return *m_extension; }

private:
    virtual QSize sizeHint() const override { return { 34, 34 }; }

    void toggle_popup();

    QWidget& m_popup_parent;
    NonnullRefPtr<Extension> m_extension;
    QPointer<ActionPopup> m_popup;
};

// The buttons of every loaded extension that has an action, for a tab's toolbar.
class ActionToolbar final : public QWidget {
public:
    AK_ALLOC_WITH_KMALLOC;

    // Popups open as children of popup_parent (the tab), as the toolbar's other popovers do.
    ActionToolbar(QWidget* parent, QWidget& popup_parent);

private:
    QWidget& m_popup_parent;
};

}
