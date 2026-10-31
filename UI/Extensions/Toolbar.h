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

class QHBoxLayout;

namespace Ladybird {

class Tab;

}

namespace Ladybird::Extensions {

class ActionPopup;

class ActionButton final : public QToolButton {
public:
    AK_ALLOC_WITH_KMALLOC;

    ActionButton(QWidget* parent, Tab&, NonnullRefPtr<Extension>);

    Extension const& extension() const { return *m_extension; }

    void update_appearance();
    void open_popup();

private:
    virtual QSize sizeHint() const override { return { 34, 34 }; }
    virtual void paintEvent(QPaintEvent*) override;

    void clicked();

    Tab& m_tab;
    NonnullRefPtr<Extension> m_extension;
    QPointer<ActionPopup> m_popup;
    QString m_badge_text;
    QColor m_badge_background_color;
    Optional<QColor> m_badge_text_color;
};

class ActionToolbar final : public QWidget {
public:
    AK_ALLOC_WITH_KMALLOC;

    ActionToolbar(QWidget* parent, Tab&);

    Tab& tab() const { return m_tab; }
    ActionButton* button_for(Extension const&) const;

private:
    void rebuild();

    Tab& m_tab;
    QHBoxLayout* m_layout { nullptr };
};

}
