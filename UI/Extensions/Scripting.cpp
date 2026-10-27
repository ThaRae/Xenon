/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/AnyOf.h>
#include <AK/Enumerate.h>
#include <AK/RefCounted.h>
#include <AK/StringBuilder.h>
#include <LibFileSystem/FileSystem.h>
#include <LibWebView/UserScriptHandler.h>
#include <UI/Extensions/Browser.h>
#include <UI/Extensions/ContentScripts.h>
#include <UI/Extensions/MatchPattern.h>
#include <UI/Extensions/Registry.h>
#include <UI/Extensions/Router.h>

// chrome.scripting: running an extension's scripts and stylesheets in a tab's frames when it asks, the content scripts
// it registers, and chrome.webNavigation's view of a tab's frames, by Chrome's frame IDs.
// https://developer.chrome.com/docs/extensions/reference/api/scripting

namespace Ladybird::Extensions {

static constexpr auto NO_HOST_ACCESS = "Cannot access contents of the page. Extension manifest must request permission to access the respective host."sv;

Optional<Vector<Router::Frame>> Router::frames_of_tab(i64 tab_id)
{
    auto documents = Browser::frame_documents(tab_id);
    if (!documents.has_value())
        return {};

    Optional<Web::HTML::CrossProcessId> top_level_navigable;
    for (auto const& document : *documents) {
        if (!document.navigable().parent_id.has_value())
            top_level_navigable = document.navigable().id;
    }

    Vector<Frame> frames;
    for (auto const& document : *documents) {
        auto const& navigable = document.navigable();
        Frame frame { .document = document, .frame_id = frame_id(document), .parent_frame_id = -1 };
        if (navigable.parent_id.has_value())
            frame.parent_frame_id = frame_id(*navigable.parent_id, navigable.parent_id == top_level_navigable);
        frames.append(move(frame));
    }
    return frames;
}

// What an extension may run scripts in: the hosts it has permission for, and those its manifest's content scripts
// match, which Chrome grants it along with them.
static bool may_access(Extension const& extension, URL::URL const& url)
{
    auto matches_any = [&](Vector<String> const& patterns) {
        return any_of(patterns, [&](auto const& pattern) {
            auto parsed = MatchPattern::parse(pattern);
            return !parsed.is_error() && parsed.value().matches(url);
        });
    };
    if (matches_any(extension.manifest().host_permissions))
        return true;
    return any_of(extension.manifest().content_scripts, [&](auto const& content_script) {
        return matches_any(content_script.matches) && !matches_any(content_script.exclude_matches);
    });
}

// https://developer.chrome.com/docs/extensions/reference/api/scripting#type-InjectionTarget
// The frames a target names: the top-level frame, some frames by ID or by document, or every frame the extension may
// access. A frame it names that the extension may not access is an error, as in Chrome.
ErrorOr<Vector<Router::Frame>, String> Router::injection_frames(Extension const& extension, Optional<JsonObject const&> target)
{
    if (!target.has_value())
        return "The injection needs a target."_string;
    auto tab_id = target->get_integer<i64>("tabId"sv);
    if (!tab_id.has_value())
        return "The injection's target needs a tabId."_string;
    auto frames = frames_of_tab(*tab_id);
    if (!frames.has_value() || frames->is_empty())
        return MUST(String::formatted("No tab with id: {}.", *tab_id));

    auto frame_ids = target->get_array("frameIds"sv);
    auto document_ids = target->get_array("documentIds"sv);
    auto all_frames = target->get_bool("allFrames"sv).value_or(false);
    if (all_frames && (frame_ids.has_value() || document_ids.has_value()))
        return "Cannot specify both 'allFrames' and 'frameIds' or 'documentIds'."_string;
    if (frame_ids.has_value() && document_ids.has_value())
        return "Cannot specify both 'frameIds' and 'documentIds'."_string;

    if (all_frames) {
        frames->remove_all_matching([&](auto const& frame) { return !may_access(extension, frame.document.url()); });
        if (frames->is_empty())
            return MUST(String::from_utf8(NO_HOST_ACCESS));
        return frames.release_value();
    }

    Vector<Frame> chosen;
    auto choose = [&](auto matches, String const& missing) -> ErrorOr<void, String> {
        auto frame = frames->find_if(matches);
        if (frame.is_end())
            return missing;
        if (!may_access(extension, frame->document.url()))
            return MUST(String::from_utf8(NO_HOST_ACCESS));
        if (!any_of(chosen, [&](auto const& other) { return other.frame_id == frame->frame_id; }))
            chosen.append(*frame);
        return {};
    };

    if (frame_ids.has_value()) {
        for (auto const& id : frame_ids->values()) {
            auto wanted = id.get_integer<i64>().value_or(-1);
            TRY(choose([&](auto const& frame) { return frame.frame_id == wanted; }, MUST(String::formatted("No frame with id {} in tab with id {}.", wanted, *tab_id))));
        }
    } else if (document_ids.has_value()) {
        for (auto const& id : document_ids->values()) {
            auto wanted = id.is_string() ? id.as_string() : String {};
            TRY(choose([&](auto const& frame) { return document_id_string(frame.document) == wanted; }, MUST(String::formatted("No document with id {} in tab with id {}.", wanted, *tab_id))));
        }
    } else {
        TRY(choose([](auto const& frame) { return frame.frame_id == 0; }, MUST(String::formatted("No frame with id 0 in tab with id {}.", *tab_id))));
    }
    return chosen;
}

void Router::handle_scripting_call(Context& context, u64 call_id, String const& name, JsonArray const& args)
{
    if (!context.extension->has_permission("scripting"sv)) {
        reply_with_error(context.endpoint, call_id, "The 'scripting' permission is required."sv);
        return;
    }

    auto details = !args.is_empty() && args.at(0).is_object() ? args.at(0).as_object() : JsonObject {};
    if (name == "scripting.executeScript"sv) {
        execute_script(context, call_id, details);
        return;
    }
    if (name == "scripting.insertCSS"sv || name == "scripting.removeCSS"sv) {
        change_css(context, call_id, details, name == "scripting.insertCSS"sv);
        return;
    }

    auto result = handle_registered_content_scripts_call(context, name, args);
    if (result.is_error())
        reply_with_error(context.endpoint, call_id, result.error());
    else
        reply(context.endpoint, call_id, result.release_value());
}

// Runs scripts one after another in a document, and gives back the last one's result, or that one didn't run.
class ScriptSequence : public RefCounted<ScriptSequence> {
public:
    static void run(WebView::ScriptBridgeEndpoint document, Vector<Web::HTML::UserScript> scripts, Function<void(WebView::UserScriptResult)> on_done)
    {
        auto sequence = adopt_ref(*new ScriptSequence(move(document), move(scripts), move(on_done)));
        sequence->run_next({ .ran = true, .result = {} });
    }

private:
    ScriptSequence(WebView::ScriptBridgeEndpoint document, Vector<Web::HTML::UserScript> scripts, Function<void(WebView::UserScriptResult)> on_done)
        : m_document(move(document))
        , m_scripts(move(scripts))
        , m_on_done(move(on_done))
    {
    }

    void run_next(WebView::UserScriptResult last)
    {
        if (!last.ran || m_next == m_scripts.size()) {
            m_on_done(move(last));
            return;
        }
        m_document.run_user_script(m_scripts[m_next++], [self = NonnullRefPtr { *this }](WebView::UserScriptResult result) {
            self->run_next(move(result));
        });
    }

    WebView::ScriptBridgeEndpoint m_document;
    Vector<Web::HTML::UserScript> m_scripts;
    Function<void(WebView::UserScriptResult)> m_on_done;
    size_t m_next { 0 };
};

// Answers once every frame has.
class FrameResults : public RefCounted<FrameResults> {
public:
    FrameResults(size_t count, Function<void(Vector<Optional<JsonObject>>)> on_done)
        : m_remaining(count)
        , m_on_done(move(on_done))
    {
        m_results.resize(count);
    }

    void set(size_t index, Optional<JsonObject> result)
    {
        m_results[index] = move(result);
        if (--m_remaining == 0)
            m_on_done(move(m_results));
    }

private:
    Vector<Optional<JsonObject>> m_results;
    size_t m_remaining { 0 };
    Function<void(Vector<Optional<JsonObject>>)> m_on_done;
};

// https://developer.chrome.com/docs/extensions/reference/api/scripting#method-executeScript
void Router::execute_script(Context& context, u64 call_id, JsonObject const& injection)
{
    auto extension = context.extension;
    auto caller = context.endpoint;

    auto frames = injection_frames(*extension, injection.get_object("target"sv));
    if (frames.is_error()) {
        reply_with_error(caller, call_id, frames.error());
        return;
    }

    // A function comes as its source, which runs with its arguments as JSON, as Chrome serializes both.
    auto files = injection.get_array("files"sv);
    auto function_source = injection.get_string("funcSource"sv);
    if (files.has_value() == function_source.has_value()) {
        reply_with_error(caller, call_id, "Exactly one of 'func' and 'files' must be specified."sv);
        return;
    }

    struct Source {
        String text;
        URL::URL url;
    };
    Vector<Source> sources;
    if (function_source.has_value()) {
        auto arguments = injection.get_array("args"sv);
        auto serialized_arguments = arguments.has_value() ? arguments->serialized() : "[]"_string;
        sources.append({ MUST(String::formatted("({})(...{});", *function_source, serialized_arguments)), extension->url_for("_generated_injected_function.js"sv) });
    } else {
        for (auto const& file : files->values()) {
            auto path = file.is_string() ? file.as_string() : String {};
            auto text = ContentScripts::read_file(*extension, path);
            if (!text.has_value()) {
                reply_with_error(caller, call_id, MUST(String::formatted("Could not load file: '{}'.", path)));
                return;
            }
            sources.append({ text.release_value(), extension->url_for(path) });
        }
    }

    auto is_main_world = injection.get_string("world"sv) == "MAIN"sv;
    auto world = is_main_world ? String {} : extension->id();
    // Chrome waits for document idle unless the extension asks it not to.
    auto injection_time = injection.get_bool("injectImmediately"sv).value_or(false) ? Web::HTML::UserScriptInjectionTime::DocumentStart : Web::HTML::UserScriptInjectionTime::DocumentIdle;

    auto results = make_ref_counted<FrameResults>(frames.value().size(), [caller, call_id](Vector<Optional<JsonObject>> results) {
        JsonArray array;
        for (auto& result : results) {
            if (result.has_value())
                array.must_append(result.release_value());
        }
        reply(caller, call_id, move(array));
    });

    for (auto const& [index, frame] : enumerate(frames.value())) {
        Vector<Web::HTML::UserScript> scripts;

        // An isolated world that doesn't have chrome.* yet gets it first, as content scripts' worlds do.
        if (!is_main_world) {
            auto world_endpoint = frame.document.in_world(world);
            if (!context_for(world_endpoint).has_value()) {
                scripts.append({
                    .world = world,
                    .source = content_script_bootstrap(extension, world_endpoint),
                    .url = extension->url_for("_generated_content_script_api.js"sv),
                    .injection_time = Web::HTML::UserScriptInjectionTime::DocumentStart,
                    .takes_bridge = true,
                });
            }
        }
        for (auto const& source : sources) {
            scripts.append({
                .world = world,
                .source = source.text,
                .url = source.url,
                .injection_time = injection_time,
                .takes_bridge = false,
            });
        }

        JsonObject frame_result;
        frame_result.set("frameId"sv, frame.frame_id);
        frame_result.set("documentId"sv, document_id_string(frame.document));
        ScriptSequence::run(frame.document, move(scripts), [results, index, frame_result = move(frame_result)](WebView::UserScriptResult result) mutable {
            if (!result.ran) {
                results->set(index, {});
                return;
            }
            if (result.result.has_value()) {
                if (auto value = JsonValue::from_string(*result.result); !value.is_error())
                    frame_result.set("result"sv, value.release_value());
            }
            results->set(index, move(frame_result));
        });
    }
}

// https://developer.chrome.com/docs/extensions/reference/api/scripting#method-insertCSS
// Stylesheets go into the extension's isolated world in each frame, as stylesheets the document adopts, as a content
// script's do. The world keeps them by their text, so that removeCSS() can take one out again.
void Router::change_css(Context& context, u64 call_id, JsonObject const& injection, bool insert)
{
    auto extension = context.extension;
    auto caller = context.endpoint;

    auto frames = injection_frames(*extension, injection.get_object("target"sv));
    if (frames.is_error()) {
        reply_with_error(caller, call_id, frames.error());
        return;
    }

    auto files = injection.get_array("files"sv);
    auto css = injection.get_string("css"sv);
    if (files.has_value() == css.has_value()) {
        reply_with_error(caller, call_id, "Exactly one of 'css' and 'files' must be specified."sv);
        return;
    }

    StringBuilder text;
    if (css.has_value()) {
        text.append(*css);
    } else {
        for (auto const& file : files->values()) {
            auto path = file.is_string() ? file.as_string() : String {};
            auto contents = ContentScripts::read_file(*extension, path);
            if (!contents.has_value()) {
                reply_with_error(caller, call_id, MUST(String::formatted("Could not load file: '{}'.", path)));
                return;
            }
            text.append(*contents);
            text.append('\n');
        }
    }

    auto quoted = JsonValue { text.to_string_without_validation() }.serialized();
    auto source = insert
        ? MUST(String::formatted(R"~~~((() => {{
    const sheets = (globalThis[Symbol.for("xenon.insertedCSS")] ??= new Map());
    const css = {};
    const sheet = new CSSStyleSheet();
    sheet.replaceSync(css);
    sheets.set(css, [...(sheets.get(css) ?? []), sheet]);
    document.adoptedStyleSheets = [...document.adoptedStyleSheets, sheet];
}})();)~~~",
              quoted))
        : MUST(String::formatted(R"~~~((() => {{
    const sheet = globalThis[Symbol.for("xenon.insertedCSS")]?.get({})?.pop();
    if (sheet) document.adoptedStyleSheets = document.adoptedStyleSheets.filter(other => other !== sheet);
}})();)~~~",
              quoted));

    auto results = make_ref_counted<FrameResults>(frames.value().size(), [caller, call_id](auto) {
        reply(caller, call_id, JsonValue {});
    });
    for (auto const& [index, frame] : enumerate(frames.value())) {
        Web::HTML::UserScript script {
            .world = extension->id(),
            .source = source,
            .url = extension->url_for("_generated_inserted_css.js"sv),
            .injection_time = Web::HTML::UserScriptInjectionTime::DocumentStart,
            .takes_bridge = false,
        };
        frame.document.run_user_script(move(script), [results, index](auto) { results->set(index, {}); });
    }
}

static Optional<Vector<String>> ids_filter(JsonArray const& args)
{
    if (args.is_empty() || !args.at(0).is_object())
        return {};
    auto ids = args.at(0).as_object().get_array("ids"sv);
    if (!ids.has_value())
        return {};
    Vector<String> list;
    for (auto const& id : ids->values()) {
        if (id.is_string())
            list.append(id.as_string());
    }
    return list;
}

// https://developer.chrome.com/docs/extensions/reference/api/scripting#method-registerContentScripts
ErrorOr<JsonValue, String> Router::handle_registered_content_scripts_call(Context& context, String const& name, JsonArray const& args)
{
    auto const& extension = *context.extension;
    auto scripts = Registry::the().registered_content_scripts(extension.id());
    auto find = [&](StringView id) {
        return scripts.find_if([&](auto const& script) { return script.id == id; });
    };

    // Chrome refuses a script whose files are missing when it's registered, rather than when it would run.
    auto check_files = [&](Vector<String> const& paths, StringView what) -> ErrorOr<void, String> {
        for (auto const& path : paths) {
            auto file = extension.resolve(extension.url_for(path));
            if (file.is_error() || !FileSystem::is_regular_file(file.value()))
                return MUST(String::formatted("Could not load {} '{}' for script.", what, path));
        }
        return {};
    };

    if (name == "scripting.getRegisteredContentScripts"sv) {
        auto ids = ids_filter(args);
        JsonArray list;
        for (auto const& script : scripts) {
            if (!ids.has_value() || ids->contains_slow(script.id))
                list.must_append(script.to_json());
        }
        return JsonValue { move(list) };
    }

    if (name == "scripting.unregisterContentScripts"sv) {
        if (auto ids = ids_filter(args); ids.has_value()) {
            for (auto const& id : *ids) {
                if (find(id).is_end())
                    return MUST(String::formatted("Nonexistent script ID '{}'", id));
            }
            scripts.remove_all_matching([&](auto const& script) { return ids->contains_slow(script.id); });
        } else {
            scripts.clear();
        }
        Registry::the().set_registered_content_scripts(extension.id(), move(scripts));
        return JsonValue {};
    }

    if (name != "scripting.registerContentScripts"sv && name != "scripting.updateContentScripts"sv)
        return MUST(String::formatted("{} isn't supported yet.", name));
    if (args.is_empty() || !args.at(0).is_array())
        return MUST(String::formatted("{}() takes a list of scripts.", name.bytes_as_string_view().substring_view("scripting."sv.length())));

    auto is_update = name == "scripting.updateContentScripts"sv;
    Vector<String> seen;
    for (auto const& value : args.at(0).as_array().values()) {
        if (!value.is_object())
            return "Each script must be an object."_string;
        auto id = value.as_object().get_string("id"sv).value_or({});
        if (seen.contains_slow(id))
            return MUST(String::formatted("Duplicate script ID '{}'", id));
        seen.append(id);

        // An update changes only the members it gives.
        auto json = value.as_object();
        auto existing = find(id);
        if (is_update) {
            if (existing.is_end())
                return MUST(String::formatted("Script with ID '{}' does not exist or is not fully registered", id));
            auto merged = existing->to_json();
            json.for_each_member([&](String const& member, JsonValue const& member_value) { merged.set(member, member_value); });
            json = move(merged);
        } else if (!existing.is_end()) {
            return MUST(String::formatted("Duplicate script ID '{}'", id));
        }

        auto registered = TRY(RegisteredContentScript::from_json(json));
        TRY(check_files(registered.script.js, "javascript"sv));
        TRY(check_files(registered.script.css, "css"sv));
        if (is_update)
            *find(id) = move(registered);
        else
            scripts.append(move(registered));
    }
    Registry::the().set_registered_content_scripts(extension.id(), move(scripts));
    return JsonValue {};
}

// --- chrome.webNavigation ---------------------------------------------------------------------------------------------

// FIXME: The navigation events. A frame's URL is the one its document was created with; Chrome follows same-document
//        navigations too.
ErrorOr<JsonValue, String> Router::handle_web_navigation_call(Context& context, String const& name, JsonArray const& args)
{
    if (!context.extension->has_permission("webNavigation"sv))
        return "The webNavigation permission is required."_string;

    auto details = !args.is_empty() && args.at(0).is_object() ? args.at(0).as_object() : JsonObject {};
    auto tab_id = details.get_integer<i64>("tabId"sv);
    if (!tab_id.has_value())
        return "tabId is required."_string;

    // As in Chrome, a tab or frame that doesn't exist gives null.
    auto frames = frames_of_tab(*tab_id);
    if (!frames.has_value())
        return JsonValue {};

    auto frame_json = [&](Frame const& frame) {
        JsonObject json;
        json.set("frameId"sv, frame.frame_id);
        json.set("parentFrameId"sv, frame.parent_frame_id);
        json.set("url"sv, frame.document.url().serialize());
        json.set("documentId"sv, document_id_string(frame.document));
        if (auto parent = frames->find_if([&](auto const& other) { return other.frame_id == frame.parent_frame_id; }); !parent.is_end())
            json.set("parentDocumentId"sv, document_id_string(parent->document));
        json.set("errorOccurred"sv, false);
        json.set("frameType"sv, frame.frame_id == 0 ? "outermost_frame"sv : "sub_frame"sv);
        json.set("documentLifecycle"sv, "active"sv);
        json.set("processId"sv, -1);
        return json;
    };

    if (name == "webNavigation.getAllFrames"sv) {
        JsonArray list;
        for (auto const& frame : *frames)
            list.must_append(frame_json(frame));
        return JsonValue { move(list) };
    }

    if (name == "webNavigation.getFrame"sv) {
        auto document_id = details.get_string("documentId"sv);
        auto wanted_frame_id = details.get_integer<i64>("frameId"sv);
        auto frame = frames->find_if([&](auto const& frame) {
            if (document_id.has_value())
                return document_id_string(frame.document) == *document_id;
            return frame.frame_id == wanted_frame_id.value_or(0);
        });
        if (frame.is_end())
            return JsonValue {};
        auto json = frame_json(*frame);
        json.remove("frameId"sv);
        json.remove("processId"sv);
        return JsonValue { move(json) };
    }

    return MUST(String::formatted("{} isn't supported yet.", name));
}

}
