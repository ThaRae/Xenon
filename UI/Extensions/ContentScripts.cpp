/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/AnyOf.h>
#include <AK/HashMap.h>
#include <AK/JsonValue.h>
#include <AK/NeverDestroyed.h>
#include <LibCore/File.h>
#include <LibWebView/Application.h>
#include <UI/Extensions/ContentScripts.h>
#include <UI/Extensions/Registry.h>
#include <UI/Extensions/Router.h>

namespace Ladybird::Extensions {

// Each extension file a content script needs, read once while the extensions stay as they are.
static HashMap<ByteString, String>& file_cache()
{
    static NeverDestroyed<HashMap<ByteString, String>> cache;
    return *cache;
}

Optional<String> ContentScripts::read_file(Extension const& extension, StringView extension_path)
{
    auto url = extension.url_for(extension_path);
    auto path = extension.resolve(url);
    if (path.is_error())
        return {};
    if (auto cached = file_cache().get(path.value()); cached.has_value())
        return *cached;

    auto file = Core::File::open(path.value(), Core::File::OpenMode::Read);
    if (file.is_error()) {
        warnln("Extensions: unable to read {}: {}", url, file.error());
        return {};
    }
    auto contents = file.value()->read_until_eof();
    if (contents.is_error()) {
        warnln("Extensions: unable to read {}: {}", url, contents.error());
        return {};
    }
    // Chrome reads content scripts and their stylesheets as UTF-8.
    auto text = String::from_utf8(contents.value());
    if (text.is_error()) {
        warnln("Extensions: {} isn't UTF-8", url);
        return {};
    }
    file_cache().set(path.release_value(), text.value());
    return text.release_value();
}

static Web::HTML::UserScriptInjectionTime injection_time(RunAt run_at)
{
    switch (run_at) {
    case RunAt::DocumentStart:
        return Web::HTML::UserScriptInjectionTime::DocumentStart;
    case RunAt::DocumentEnd:
        return Web::HTML::UserScriptInjectionTime::DocumentEnd;
    case RunAt::DocumentIdle:
        return Web::HTML::UserScriptInjectionTime::DocumentIdle;
    }
    VERIFY_NOT_REACHED();
}

void ContentScripts::update()
{
    file_cache().clear();

    auto has_content_scripts = any_of(Registry::the().extensions(), [](auto const& extension) {
        return !extension->manifest().content_scripts.is_empty() || !Registry::the().registered_content_scripts(extension->id()).is_empty();
    });

    auto& application = WebView::Application::the();
    if (!has_content_scripts) {
        application.set_user_script_handler(nullptr);
        return;
    }
    if (application.user_script_handler())
        return;

    auto handler = make<WebView::UserScriptHandler>();
    handler->scripts_for_document = [](WebView::ScriptBridgeEndpoint const& document) {
        return scripts_for_document(document);
    };
    handler->on_message = [](WebView::ScriptBridgeEndpoint const& world, String message) {
        Router::the().on_message(world, message);
    };
    application.set_user_script_handler(move(handler));
}

Vector<Web::HTML::UserScript> ContentScripts::scripts_for_document(WebView::ScriptBridgeEndpoint const& document)
{
    Vector<Web::HTML::UserScript> scripts;
    auto const& url = document.url();
    auto is_top_level_frame = !document.navigable().parent_id.has_value();

    for (auto const& extension : Registry::the().extensions()) {
        // The manifest's content scripts come first, then those the extension registered, as in Chrome.
        Vector<ContentScript const&> matching;
        for (auto const& content_script : extension->manifest().content_scripts) {
            if (content_script.matches_document(url, is_top_level_frame))
                matching.append(content_script);
        }
        for (auto const& registered : Registry::the().registered_content_scripts(extension->id())) {
            if (registered.script.matches_document(url, is_top_level_frame))
                matching.append(registered.script);
        }
        if (matching.is_empty())
            continue;

        auto world = extension->id();
        auto world_for = [&](ContentScript const& content_script) {
            return content_script.world == World::Main ? String {} : world;
        };

        // chrome.* comes first in the extension's world, before any of its scripts, whenever they run.
        auto has_isolated_scripts = any_of(matching, [](auto const& content_script) {
            return content_script.world == World::Isolated && !content_script.js.is_empty();
        });
        if (has_isolated_scripts) {
            scripts.append({
                .world = world,
                .source = Router::the().content_script_bootstrap(extension, document.in_world(world)),
                .url = extension->url_for("_generated_content_script_api.js"sv),
                .injection_time = Web::HTML::UserScriptInjectionTime::DocumentStart,
                .takes_bridge = true,
            });
        }

        // Stylesheets go in before the page shows, as in Chrome. The page sees them among its adopted stylesheets.
        // FIXME: Apply them as user stylesheets the page can't see or remove.
        for (auto const& content_script : matching) {
            for (auto const& path : content_script.css) {
                auto css = read_file(*extension, path);
                if (!css.has_value())
                    continue;
                auto source = MUST(String::formatted(
                    "(() => {{ const sheet = new CSSStyleSheet(); sheet.replaceSync({}); document.adoptedStyleSheets = [...document.adoptedStyleSheets, sheet]; }})();",
                    JsonValue { css.release_value() }.serialized()));
                scripts.append({
                    .world = world,
                    .source = move(source),
                    .url = extension->url_for(path),
                    .injection_time = Web::HTML::UserScriptInjectionTime::DocumentStart,
                    .takes_bridge = false,
                });
            }
        }

        for (auto const& content_script : matching) {
            for (auto const& path : content_script.js) {
                auto source = read_file(*extension, path);
                if (!source.has_value())
                    continue;
                scripts.append({
                    .world = world_for(content_script),
                    .source = source.release_value(),
                    .url = extension->url_for(path),
                    .injection_time = injection_time(content_script.run_at),
                    .takes_bridge = false,
                });
            }
        }
    }
    return scripts;
}

}
