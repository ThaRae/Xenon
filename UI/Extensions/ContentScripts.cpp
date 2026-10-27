/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/AnyOf.h>
#include <AK/HashMap.h>
#include <AK/JsonValue.h>
#include <AK/NeverDestroyed.h>
#include <AK/Random.h>
#include <LibCore/File.h>
#include <LibWebView/Application.h>
#include <UI/Extensions/ContentScripts.h>
#include <UI/Extensions/Registry.h>
#include <UI/Extensions/Router.h>

namespace Ladybird::Extensions {

// UI/Resources/extensions/externally-connectable.js, which the build embeds.
extern String const& externally_connectable_source;

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
        return !extension->manifest().content_scripts.is_empty() || !Registry::the().registered_content_scripts(extension->id()).is_empty()
            || extension->manifest().externally_connectable.has_value();
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

    scripts.extend(externally_connectable_scripts(document));
    return scripts;
}

// A web page that an extension's externally_connectable matches gets chrome.runtime in its own world, to message the
// extension with. The page's world can't have a bridge, so its chrome.runtime talks over DOM events to a relay in an
// isolated world of its own for each extension, which has one, and which the router only lets message that extension.
// https://developer.chrome.com/docs/extensions/develop/concepts/messaging#external-webpage
Vector<Web::HTML::UserScript> ContentScripts::externally_connectable_scripts(WebView::ScriptBridgeEndpoint const& document)
{
    auto const& url = document.url();
    if (!url.scheme().is_one_of("http"sv, "https"sv))
        return {};

    Vector<NonnullRefPtr<Extension>> extensions;
    for (auto const& extension : Registry::the().extensions()) {
        auto const& externally_connectable = extension->manifest().externally_connectable;
        if (externally_connectable.has_value() && externally_connectable->matches_document(url))
            extensions.append(extension);
    }
    if (extensions.is_empty())
        return {};

    // The events get a name of their own in each document, so that they don't meet the page's own events by chance.
    auto event_name = MUST(String::formatted("xenon-externally-connectable-{:016x}", get_random<u64>()));

    Vector<Web::HTML::UserScript> scripts;
    JsonArray extension_ids;
    for (auto const& extension : extensions) {
        auto world = externally_connectable_world(*extension);
        scripts.append({
            .world = world,
            .source = Router::the().externally_connectable_relay(extension, document.in_world(world), event_name),
            .url = extension->url_for("_generated_externally_connectable_relay.js"sv),
            .injection_time = Web::HTML::UserScriptInjectionTime::DocumentStart,
            .takes_bridge = true,
        });
        extension_ids.must_append(extension->id());
    }

    JsonObject config;
    config.set("extensionIds"sv, move(extension_ids));
    config.set("eventName"sv, event_name);
    scripts.append({
        .world = {},
        .source = MUST(String::formatted("(config => {{\n{}\n}})({});", externally_connectable_source, config.serialized())),
        // NB: The script is the browser's, not any one extension's, but its errors need a URL.
        .url = extensions.first()->url_for("_generated_externally_connectable.js"sv),
        .injection_time = Web::HTML::UserScriptInjectionTime::DocumentStart,
        .takes_bridge = false,
    });
    return scripts;
}

String ContentScripts::externally_connectable_world(Extension const& extension)
{
    return MUST(String::formatted("{}-externally-connectable", extension.id()));
}

}
