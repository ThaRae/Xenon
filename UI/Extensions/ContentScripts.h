/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Vector.h>
#include <LibWebCommon/HTML/Scripting/UserScript.h>
#include <LibWebView/URLSchemeHandler.h>

namespace Ladybird::Extensions {

// Extensions' content scripts, which the engine runs as user scripts (see Web::HTML::UserScript). An extension's
// scripts in a document share an isolated world named by the extension's ID, where chrome.* has what Chrome gives
// content scripts; one that asks for the MAIN world runs with the page's own scripts, with no chrome.* at all.
// https://developer.chrome.com/docs/extensions/develop/concepts/content-scripts
class ContentScripts {
public:
    // Gives the engine a user script handler while any extension has content scripts, so that documents only ask the
    // UI process for scripts when there may be some. Call it whenever the set of extensions changes.
    static void update();

private:
    static Vector<Web::HTML::UserScript> scripts_for_document(WebView::ScriptBridgeEndpoint const& document);
};

}
