/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/StringView.h>
#include <Rune/Bridge/BrowserModel.h>
#include <Rune/Bridge/XenonBridgeExtensions.h>

namespace Xenon {

class View;

}

namespace Xenon::ExtensionsHost {

void set_callbacks(XenonExtensionCallbacks const&);

void start();
void stop();

void window_opened(BrowserModel::Window const&);
void window_closed(BrowserModel::Window const&);
void window_activated(BrowserModel::Window const&);
void tab_inserted(BrowserModel::Window const&, BrowserModel::Tab const&, size_t index);
void tab_removed(BrowserModel::Window const&, BrowserModel::Tab const&);
void tab_moved(BrowserModel::Tab const&, XenonWindowId from, size_t index);
void tab_activated(BrowserModel::Window const&, BrowserModel::Tab const&);
void tab_pinned_changed(BrowserModel::Tab const&);
void view_title_changed(View&);
void view_loading_changed(View&, bool is_loading);

void action_clicked(XenonViewId tab, StringView extension_id);
void close_popup(XenonViewId popup);
void answer_permissions(XenonPromptId, bool granted);
void run_command(XenonViewId tab, StringView extension_id, StringView command);
void open_options(StringView extension_id);
void reload(StringView extension_id);
void remove(StringView extension_id);
void load_unpacked(StringView directory);
void choose_password_manager(StringView id);
void show_extensions();

}
