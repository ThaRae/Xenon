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
#include <LibWebView/URLSchemeHandler.h>
#include <UI/Extensions/Extension.h>

#include <QColor>

class QWidget;

namespace Ladybird {

class BrowserWindow;
class Tab;

}

namespace Ladybird::Extensions::Browser {

ErrorOr<JsonValue, String> handle_call(Extension const&, Optional<u64> caller_view_id, StringView name, JsonArray const& args);

struct TabAndWindowIds {
    i64 tab_id { -1 };
    i64 window_id { -1 };
};
Optional<TabAndWindowIds> tab_and_window_ids(u64 view_id);
Optional<Vector<WebView::ScriptBridgeEndpoint>> frame_documents(i64 tab_id);
Optional<JsonObject> tab_json(u64 view_id, Extension const&);

void open_tab(URL::URL const&);

void did_open_popup(u64 view_id, Tab&, QWidget& popup);
void did_close_popup(u64 view_id);
bool is_popup_view(u64 view_id);

void did_create_window(BrowserWindow&);
void did_create_tab(BrowserWindow&, Tab&);
void did_activate_tab(BrowserWindow&, Tab&);

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
void on_action_change(Function<bool()>);
Function<void(Extension const&)>& open_popup_handler();
void prompt_for_permissions(Extension const&, Optional<u64> caller_view_id, Vector<String> const& warnings, Function<void(bool granted)>);

void action_clicked(Extension const&, Tab*);

}
