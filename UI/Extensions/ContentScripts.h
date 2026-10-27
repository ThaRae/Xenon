/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Vector.h>
#include <LibWebCommon/HTML/Scripting/UserScript.h>
#include <LibWebView/URLSchemeHandler.h>
#include <UI/Extensions/Extension.h>

namespace Ladybird::Extensions {

// Extensions' content scripts, which the engine runs as user scripts (see Web::HTML::UserScript). An extension's
// scripts in a document share an isolated world named by the extension's ID, where chrome.* has what Chrome gives
// content scripts; one that asks for the MAIN world runs with the page's own scripts, with no chrome.* at all.
// https://developer.chrome.com/docs/extensions/develop/concepts/content-scripts
class ContentScripts {
public:
    // Gives the engine a user script handler while any extension has content scripts, so that documents only ask the
    // UI process for scripts when there may be some. Call it whenever the set of extensions, or of the content scripts
    // they registered, changes.
    static void update();

    // The text of one of the extension's files, as content scripts and scripting.executeScript() run it, or none if it
    // can't be read as UTF-8.
    static Optional<String> read_file(Extension const&, StringView path);

    // The isolated world of a document that relays the messages of a web page the extension's externally_connectable
    // matches.
    static String externally_connectable_world(Extension const&);

private:
    static Vector<Web::HTML::UserScript> scripts_for_document(WebView::ScriptBridgeEndpoint const& document);
    static Vector<Web::HTML::UserScript> externally_connectable_scripts(WebView::ScriptBridgeEndpoint const& document);
};

}
