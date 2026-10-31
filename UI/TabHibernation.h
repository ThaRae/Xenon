/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Optional.h>
#include <AK/Span.h>
#include <AK/Types.h>

class QAction;
class QIcon;
class QMenu;
class QWidget;

namespace Ladybird {

class Tab;

}

namespace Ladybird::TabHibernation {

void start();

ReadonlySpan<i64> timeout_choices_in_minutes();

QMenu* create_settings_menu(QWidget& parent);

QAction* create_hibernate_tab_action(Tab&, QMenu& context_menu);

Optional<u64> memory_freed(Tab&);

QIcon hibernated_tab_icon(QIcon const& favicon);

}
