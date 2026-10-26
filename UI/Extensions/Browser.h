/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Error.h>
#include <AK/Function.h>
#include <AK/JsonArray.h>
#include <AK/JsonObject.h>
#include <AK/JsonValue.h>
#include <AK/Optional.h>
#include <AK/String.h>
#include <LibURL/URL.h>
#include <UI/Extensions/Extension.h>

#include <QColor>

class QWidget;

namespace Ladybird {

class BrowserWindow;
class Tab;

}

// chrome.tabs, chrome.windows, chrome.action, and chrome.webNavigation: what extensions see of the browser's windows and
// tabs, by Chrome's numeric IDs, and the state of their toolbar buttons.
namespace Ladybird::Extensions::Browser {

ErrorOr<JsonValue, String> handle_call(Extension const&, Optional<u64> caller_view_id, StringView name, JsonArray const& args);

struct TabAndWindowIds {
    i64 tab_id { -1 };
    i64 window_id { -1 };
};
Optional<TabAndWindowIds> tab_and_window_ids(u64 view_id);
// The tab showing a view, as chrome.tabs describes it to the extension.
Optional<JsonObject> tab_json(u64 view_id, Extension const&);

void open_tab(URL::URL const&);

// Popups are views of their own, whose window is the one their button's tab is in.
void did_open_popup(u64 view_id, Tab&, QWidget& popup);
void did_close_popup(u64 view_id);
bool is_popup_view(u64 view_id);

// Hooks in BrowserWindow and Application, from which tabs and windows events come.
void did_create_window(BrowserWindow&);
void did_create_tab(BrowserWindow&, Tab&);
void did_activate_tab(BrowserWindow&, Tab&);

// What an extension's toolbar button shows in a tab, after action.set*() calls for that tab or for all tabs.
struct ActionAppearance {
    String title;
    Optional<String> icon_path;
    String badge_text;
    QColor badge_background_color;
    Optional<QColor> badge_text_color;
    Optional<String> popup;
    bool enabled { true };
};
ActionAppearance action_appearance(Extension const&, Tab*);
// Called when any extension's action changes, for the toolbar to redraw, until the listener returns false.
void on_action_change(Function<bool()>);
// The current tab's window shows the extension's popup, as action.openPopup() and its keyboard shortcut do.
Function<void(Extension const&)>& open_popup_handler();
// Asks the user whether to let an extension do what the warnings say, as Chrome's permission prompt does: over the
// extension's popup when it asks from there, and in a dialog otherwise.
void prompt_for_permissions(Extension const&, Optional<u64> caller_view_id, Vector<String> const& warnings, Function<void(bool granted)>);

// A click on a button without a popup.
void action_clicked(Extension const&, Tab*);

}
