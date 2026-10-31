/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteString.h>
#include <AK/Optional.h>
#include <AK/kmalloc.h>

#include <QToolButton>

namespace Ladybird {

class UpdatePopover;

class UpdateButton final : public QToolButton {
    AK_ALLOC_WITH_KMALLOC;

public:
    UpdateButton(QWidget& toolbar, int height);
    virtual ~UpdateButton() override;

    void update_from_status();

    void show_popover();

private:
    void update_size();
    QRectF pill_rect() const;

    virtual bool eventFilter(QObject*, QEvent*) override;
    virtual void changeEvent(QEvent*) override;
    virtual void paintEvent(QPaintEvent*) override;

    int m_height { 0 };
    bool m_shows_label { true };
    Optional<ByteString> m_version;
    UpdatePopover* m_popover { nullptr };
};

}
