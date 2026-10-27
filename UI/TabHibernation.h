/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

class QAction;
class QIcon;
class QMenu;
class QWidget;

namespace Ladybird {

class Tab;

}

// Tab hibernation, Xenon's take on Chromium's Memory Saver: a background tab left unused for the timeout in XenonSettings
// gives up its view, and with it its WebContent process. It keeps its title, favicon, URL, and session history, its
// scroll position included, and loads again when it's selected.
namespace Ladybird::TabHibernation {

// Starts checking every window's tabs, as XenonSettings says to.
void start();

// The window frame's Tab Hibernation menu, which turns hibernation off or picks its timeout.
QMenu* create_settings_menu(QWidget& parent);

// The tab's context menu entry that hibernates it right away, if nothing keeps it awake.
QAction* create_hibernate_tab_action(Tab&, QMenu& context_menu);

// A hibernated tab's favicon turns grey in the tab strip, so that it's clear which tabs are asleep.
QIcon hibernated_tab_icon(QIcon const& favicon);

}
