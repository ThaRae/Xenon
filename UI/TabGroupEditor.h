/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Vector.h>
#include <AK/kmalloc.h>
#include <UI/Popover.h>
#include <UI/TabGroups.h>

#include <QPointer>

class QLineEdit;
class QPushButton;

namespace Ladybird {

class TabGroupColorSwatch;

// What a group's header opens, as Chromium's tab group editor bubble: the group's title and color, which change as
// they're edited, and what can be done with the group's tabs.
class TabGroupEditor final : public Popover {
public:
    AK_ALLOC_WITH_KMALLOC;

    explicit TabGroupEditor(QWidget* parent);

    // Opens below the anchor, a rect in global coordinates, or beside it when it's a row of the vertical tabs.
    enum class Placement : u8 {
        Below,
        Beside,
    };
    void edit(TabGroups&, TabGroup&, QRect const& anchor, Placement, bool focus_title);

    TabGroup* group() const { return m_group; }

private:
    virtual void hideEvent(QHideEvent*) override;

    void update_from_group();
    void place(QRect const& anchor, Placement);

    QPointer<TabGroups> m_groups;
    QPointer<TabGroup> m_group;
    QLineEdit* m_title_field { nullptr };
    Vector<TabGroupColorSwatch*> m_swatches;
    QPushButton* m_move_to_new_window_button { nullptr };
};

}
