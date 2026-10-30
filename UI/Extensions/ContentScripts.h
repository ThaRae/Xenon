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

class ContentScripts {
public:
    static void update();

    static Optional<String> read_file(Extension const&, StringView path);

    static String externally_connectable_world(Extension const&);

private:
    static Vector<Web::HTML::UserScript> scripts_for_document(WebView::ScriptBridgeEndpoint const& document);
    static Vector<Web::HTML::UserScript> externally_connectable_scripts(WebView::ScriptBridgeEndpoint const& document);
};

}
