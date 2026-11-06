/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWebView/Application.h>
#include <UI/Extensions/Browser.h>

namespace Ladybird::Extensions::Browser {

ErrorOr<JsonValue, String> handle_call(Extension const&, Optional<u64>, StringView name, JsonArray const&)
{
    return MUST(String::formatted("{} isn't supported yet.", name));
}

Optional<TabAndWindowIds> tab_and_window_ids(u64)
{
    return {};
}

Optional<Vector<WebView::ScriptBridgeEndpoint>> frame_documents(i64)
{
    return {};
}

Optional<JsonObject> tab_json(u64, Extension const&)
{
    return {};
}

void open_tab(URL::URL const& url)
{
    WebView::Application::the().open_url_in_new_tab(url, Web::HTML::ActivateTab::Yes);
}

bool is_popup_view(u64)
{
    return false;
}

void prompt_for_permissions(Extension const&, Optional<u64>, Vector<String> const&, Function<void(bool)> on_answer)
{
    on_answer(false);
}

}
