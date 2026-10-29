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

// The toolbar's Update button, like Chrome's and Helium's update chip. Once an update has downloaded and is ready to
// install (UI/Updater.h), the end of every window's toolbar shows it, in Xenon's accent, and it offers to relaunch Xenon
// to install the update. It takes no room before that. Later hides it in every window until Xenon starts again, or a
// newer update is ready; Xenon still installs the update when it quits, and Settings and the Xenon menu still offer it.
class UpdateButton final : public QToolButton {
    AK_ALLOC_WITH_KMALLOC;

public:
    UpdateButton(QWidget& toolbar, int height);
    virtual ~UpdateButton() override;

    // Shows the button, or hides it, for the update that's ready.
    void update_from_status();

    // The version that's ready, what relaunching does, and Later.
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
