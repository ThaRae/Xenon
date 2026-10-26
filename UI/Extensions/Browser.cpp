/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/HashTable.h>
#include <AK/NeverDestroyed.h>
#include <AK/QuickSort.h>
#include <LibURL/InternalURLs.h>
#include <LibURL/Parser.h>
#include <UI/Application.h>
#include <UI/BrowserWindow.h>
#include <UI/ChromeStyle.h>
#include <UI/Extensions/Browser.h>
#include <UI/Extensions/MatchPattern.h>
#include <UI/Extensions/Registry.h>
#include <UI/Extensions/Router.h>
#include <UI/StringUtils.h>
#include <UI/Tab.h>
#include <UI/WebContentView.h>

#include <QApplication>
#include <QGuiApplication>
#include <QPointer>
#include <QWindow>

namespace Ladybird::Extensions::Browser {

static constexpr i64 WINDOW_ID_NONE = -1;
static constexpr i64 WINDOW_ID_CURRENT = -2;
static constexpr u32 TOOLBAR_ICON_PIXELS = 32;

namespace {

// What action.set*() changed, for every tab or for one.
struct ActionProperties {
    Optional<String> badge_text;
    Optional<QColor> badge_background_color;
    Optional<QColor> badge_text_color;
    Optional<String> title;
    Optional<String> icon_path;
    Optional<String> popup;
    Optional<bool> enabled;
};

struct ActionState {
    ActionProperties all_tabs;
    HashMap<i64, ActionProperties> tabs;
};

struct State {
    HashMap<Tab const*, i64> tab_ids;
    HashMap<Tab const*, i64> tab_window_ids;
    HashMap<BrowserWindow const*, i64> window_ids;
    i64 next_tab_id { 1 };
    i64 next_window_id { 1 };

    HashMap<u64, QPointer<BrowserWindow>> popup_views;
    HashTable<BrowserWindow const*> popup_windows;
    QPointer<BrowserWindow> last_focused_window;
    bool watches_focus { false };

    HashMap<String, ActionState> actions;
    Vector<Function<bool()>> action_listeners;
    Function<void(Extension const&)> open_popup;
};

}

static State& state()
{
    static NeverDestroyed<State> state;
    return *state;
}

// --- Windows and tabs --------------------------------------------------------------------------------------------------

static Vector<BrowserWindow*> all_windows()
{
    Vector<BrowserWindow*> windows;
    for (auto* widget : QApplication::topLevelWidgets()) {
        if (auto* window = qobject_cast<BrowserWindow*>(widget))
            windows.append(window);
    }
    return windows;
}

static void notify_action_listeners()
{
    state().action_listeners.remove_all_matching([](auto& listener) { return !listener(); });
}

static i64 window_id(BrowserWindow& window);

static void watch_focus()
{
    if (exchange(state().watches_focus, true))
        return;

    QObject::connect(qApp, &QGuiApplication::focusWindowChanged, qApp, [](QWindow* focus_window) {
        i64 focused_id = WINDOW_ID_NONE;
        for (auto* window : all_windows()) {
            if (window->windowHandle() == focus_window) {
                state().last_focused_window = window;
                focused_id = window_id(*window);
            }
        }

        JsonArray args;
        args.must_append(focused_id);
        Router::the().dispatch_event_to_all("windows.onFocusChanged"sv, move(args));
    });
}

static i64 window_id(BrowserWindow& window)
{
    if (auto id = state().window_ids.get(&window); id.has_value())
        return *id;

    watch_focus();
    auto id = state().next_window_id++;
    state().window_ids.set(&window, id);
    QObject::connect(&window, &QObject::destroyed, qApp, [id, pointer = &window] {
        state().window_ids.remove(pointer);
        state().popup_windows.remove(pointer);

        JsonArray args;
        args.must_append(id);
        Router::the().dispatch_event_to_all("windows.onRemoved"sv, move(args));
    });
    return id;
}

static BrowserWindow* window_with_tab(Tab& tab)
{
    for (auto* window : all_windows()) {
        if (window->tab_index(&tab) >= 0)
            return window;
    }
    return nullptr;
}

static i64 tab_id(Tab& tab)
{
    if (auto id = state().tab_ids.get(&tab); id.has_value())
        return *id;

    auto id = state().next_tab_id++;
    state().tab_ids.set(&tab, id);
    QObject::connect(&tab, &QObject::destroyed, qApp, [id, pointer = &tab] {
        state().tab_ids.remove(pointer);
        auto window_id = state().tab_window_ids.take(pointer).value_or(WINDOW_ID_NONE);
        for (auto& [extension_id, action] : state().actions)
            action.tabs.remove(id);

        JsonObject remove_info;
        remove_info.set("windowId"sv, window_id);
        remove_info.set("isWindowClosing"sv, false);
        JsonArray args;
        args.must_append(id);
        args.must_append(move(remove_info));
        Router::the().dispatch_event_to_all("tabs.onRemoved"sv, move(args));
    });
    return id;
}

struct TabInWindow {
    Tab* tab { nullptr };
    BrowserWindow* window { nullptr };
};

static Optional<TabInWindow> find_tab(i64 id)
{
    for (auto* window : all_windows()) {
        for (int i = 0; i < window->tab_count(); ++i) {
            auto* tab = window->tab_at(i);
            if (state().tab_ids.get(tab) == id)
                return TabInWindow { tab, window };
        }
    }
    return {};
}

static Optional<TabInWindow> find_tab_for_view(u64 view_id)
{
    for (auto* window : all_windows()) {
        for (int i = 0; i < window->tab_count(); ++i) {
            auto* tab = window->tab_at(i);
            if (tab->is_loaded() && tab->view().view_id() == view_id)
                return TabInWindow { tab, window };
        }
    }
    return {};
}

static BrowserWindow* find_window(i64 id)
{
    for (auto* window : all_windows()) {
        if (state().window_ids.get(window) == id)
            return window;
    }
    return nullptr;
}

// The window chrome.tabs and chrome.windows mean by "current": the caller's own, if it's in a tab or a popup, and the
// last focused one otherwise.
static BrowserWindow* current_window(Optional<u64> caller_view_id)
{
    if (caller_view_id.has_value()) {
        if (auto tab = find_tab_for_view(*caller_view_id); tab.has_value())
            return tab->window;
        if (auto popup = state().popup_views.get(*caller_view_id); popup.has_value() && *popup)
            return popup->data();
    }
    if (state().last_focused_window)
        return state().last_focused_window.data();
    return Application::the().active_window_if_any();
}

// Chrome shows an extension a tab's URL and title only with the "tabs" permission, or host permission for the URL.
static bool may_see_details(Extension const& extension, URL::URL const& url)
{
    auto const& manifest = extension.manifest();
    if (manifest.has_permission("tabs"sv))
        return true;
    if (url.scheme() == EXTENSION_SCHEME && url.serialized_host() == extension.id().bytes_as_string_view())
        return true;
    return any_of(manifest.host_permissions, [&](auto const& pattern) {
        auto parsed = MatchPattern::parse(pattern);
        return !parsed.is_error() && parsed.value().matches(url);
    });
}

static JsonObject tab_to_json(Tab& tab, BrowserWindow& window, Extension const& extension)
{
    auto active = window.current_tab() == &tab;
    auto window_id_value = window_id(window);
    state().tab_window_ids.set(&tab, window_id_value);

    JsonObject json;
    json.set("id"sv, tab_id(tab));
    json.set("index"sv, window.tab_index(&tab));
    json.set("windowId"sv, window_id_value);
    json.set("active"sv, active);
    json.set("highlighted"sv, active);
    json.set("selected"sv, active);
    json.set("pinned"sv, tab.is_pinned());
    json.set("audible"sv, false);
    json.set("discarded"sv, !tab.is_loaded());
    json.set("autoDiscardable"sv, true);
    json.set("frozen"sv, false);
    json.set("incognito"sv, window.is_private() == WebView::IsPrivate::Yes);
    json.set("status"sv, tab.is_loading() ? "loading"sv : "complete"sv);
    json.set("groupId"sv, -1);

    JsonObject muted;
    muted.set("muted"sv, false);
    json.set("mutedInfo"sv, move(muted));

    if (tab.is_loaded()) {
        json.set("width"sv, tab.view().width());
        json.set("height"sv, tab.view().height());
    }

    auto url = tab.url();
    if (may_see_details(extension, url)) {
        json.set("url"sv, url.serialize());
        json.set("title"sv, ak_string_from_qstring(tab.title()));
    }
    return json;
}

static JsonObject window_to_json(BrowserWindow& window, Extension const& extension, bool populate)
{
    auto geometry = window.geometry();

    JsonObject json;
    json.set("id"sv, window_id(window));
    json.set("focused"sv, window.isActiveWindow());
    json.set("top"sv, geometry.top());
    json.set("left"sv, geometry.left());
    json.set("width"sv, geometry.width());
    json.set("height"sv, geometry.height());
    json.set("incognito"sv, window.is_private() == WebView::IsPrivate::Yes);
    json.set("type"sv, state().popup_windows.contains(&window) ? "popup"sv : "normal"sv);
    json.set("alwaysOnTop"sv, false);

    StringView window_state = "normal"sv;
    if (window.isFullScreen())
        window_state = "fullscreen"sv;
    else if (window.isMinimized())
        window_state = "minimized"sv;
    else if (window.isMaximized())
        window_state = "maximized"sv;
    json.set("state"sv, window_state);

    if (populate) {
        JsonArray tabs;
        for (int i = 0; i < window.tab_count(); ++i)
            tabs.must_append(tab_to_json(*window.tab_at(i), window, extension));
        json.set("tabs"sv, move(tabs));
    }
    return json;
}

Optional<TabAndWindowIds> tab_and_window_ids(u64 view_id)
{
    auto tab = find_tab_for_view(view_id);
    if (!tab.has_value())
        return {};
    return TabAndWindowIds { .tab_id = tab_id(*tab->tab), .window_id = window_id(*tab->window) };
}

Optional<JsonObject> tab_json(u64 view_id, Extension const& extension)
{
    auto tab = find_tab_for_view(view_id);
    if (!tab.has_value())
        return {};
    return tab_to_json(*tab->tab, *tab->window, extension);
}

void open_tab(URL::URL const& url)
{
    if (auto* window = current_window({}))
        window->new_tab_from_url(url, Web::HTML::ActivateTab::Yes, BrowserWindow::TabLocation::end());
    else
        Application::the().new_window({ url });
}

void did_open_popup(u64 view_id, Tab& tab)
{
    state().popup_views.set(view_id, window_with_tab(tab));
}

void did_close_popup(u64 view_id)
{
    state().popup_views.remove(view_id);
}

bool is_popup_view(u64 view_id)
{
    return state().popup_views.contains(view_id);
}

// --- Events ------------------------------------------------------------------------------------------------------------

// Each extension sees the tab as its permissions let it.
static void dispatch_tab_event(StringView name, Tab& tab, BrowserWindow& window, Function<JsonArray(JsonObject tab)> const& make_args)
{
    for (auto const& extension : Registry::the().extensions())
        Router::the().dispatch_event(extension, name, make_args(tab_to_json(tab, window, extension)));
}

static void dispatch_tab_updated(Tab& tab, JsonObject const& change)
{
    auto* window = window_with_tab(tab);
    if (!window)
        return;

    for (auto const& extension : Registry::the().extensions()) {
        auto tab_json = tab_to_json(tab, *window, extension);

        // The change includes only what the extension may see.
        JsonObject visible_change;
        change.for_each_member([&](String const& key, JsonValue const& value) {
            if (!key.is_one_of("url"sv, "title"sv) || tab_json.has(key))
                visible_change.set(key, value);
        });
        if (visible_change.size() == 0)
            continue;

        JsonArray args;
        args.must_append(tab_id(tab));
        args.must_append(move(visible_change));
        args.must_append(move(tab_json));
        Router::the().dispatch_event(extension, "tabs.onUpdated"sv, move(args));
    }
}

void did_create_window(BrowserWindow& window)
{
    for (auto const& extension : Registry::the().extensions()) {
        JsonArray args;
        args.must_append(window_to_json(window, extension, false));
        Router::the().dispatch_event(extension, "windows.onCreated"sv, move(args));
    }
}

void did_create_tab(BrowserWindow& window, Tab& tab)
{
    // A tab moving to another window is set up again there; it's the same tab, which Chrome reports as attached.
    if (state().tab_ids.contains(&tab)) {
        JsonObject attach_info;
        attach_info.set("newWindowId"sv, window_id(window));
        attach_info.set("newPosition"sv, window.tab_index(&tab));
        JsonArray args;
        args.must_append(tab_id(tab));
        args.must_append(move(attach_info));
        Router::the().dispatch_event_to_all("tabs.onAttached"sv, move(args));
        state().tab_window_ids.set(&tab, window_id(window));
        return;
    }
    tab_id(tab);

    QObject::connect(&tab, &Tab::title_changed, &tab, [&tab](int, QString const& title) {
        JsonObject change;
        change.set("title"sv, ak_string_from_qstring(title));
        dispatch_tab_updated(tab, change);
    });
    QObject::connect(&tab, &Tab::loading_changed, &tab, [&tab] {
        JsonObject change;
        change.set("status"sv, tab.is_loading() ? "loading"sv : "complete"sv);
        if (tab.is_loading())
            change.set("url"sv, tab.url().serialize());
        dispatch_tab_updated(tab, change);
    });
    QObject::connect(&tab, &Tab::loading_finished, &tab, [&tab] {
        JsonObject change;
        change.set("status"sv, "complete"sv);
        dispatch_tab_updated(tab, change);
    });

    dispatch_tab_event("tabs.onCreated"sv, tab, window, [](JsonObject tab) {
        JsonArray args;
        args.must_append(move(tab));
        return args;
    });
}

void did_activate_tab(BrowserWindow& window, Tab& tab)
{
    JsonObject info;
    info.set("tabId"sv, tab_id(tab));
    info.set("windowId"sv, window_id(window));
    JsonArray args;
    args.must_append(move(info));
    Router::the().dispatch_event_to_all("tabs.onActivated"sv, move(args));

    // The toolbar shows the new tab's badges and titles.
    notify_action_listeners();
}

// --- chrome.tabs and chrome.windows ------------------------------------------------------------------------------------

static JsonObject object_argument(JsonArray const& args, size_t index)
{
    if (index < args.size() && args.at(index).is_object())
        return args.at(index).as_object();
    return {};
}

static Optional<i64> integer_argument(JsonArray const& args, size_t index)
{
    if (index < args.size() && args.at(index).is_number())
        return args.at(index).get_i64();
    return {};
}

static ErrorOr<TabInWindow, String> tab_argument(JsonArray const& args, size_t index, Optional<u64> caller_view_id)
{
    if (auto id = integer_argument(args, index); id.has_value()) {
        if (auto tab = find_tab(*id); tab.has_value())
            return tab.release_value();
        return MUST(String::formatted("No tab with id: {}.", *id));
    }

    // No tab means the current window's active tab.
    auto* window = current_window(caller_view_id);
    if (!window || !window->current_tab())
        return "No current tab."_string;
    return TabInWindow { window->current_tab(), window };
}

// A URL from an extension, which may be relative to the extension's base URL.
static Optional<URL::URL> extension_relative_url(Extension const& extension, StringView url)
{
    return URL::Parser::basic_parse(url, extension.base_url());
}

static bool tab_matches_query(Tab& tab, BrowserWindow& window, JsonObject const& query, Optional<u64> caller_view_id, Extension const& extension)
{
    auto active = window.current_tab() == &tab;

    auto matches_bool = [&](StringView key, bool value) {
        auto wanted = query.get_bool(key);
        return !wanted.has_value() || *wanted == value;
    };
    if (!matches_bool("active"sv, active) || !matches_bool("highlighted"sv, active) || !matches_bool("pinned"sv, tab.is_pinned()))
        return false;
    if (!matches_bool("audible"sv, false) || !matches_bool("muted"sv, false) || !matches_bool("discarded"sv, !tab.is_loaded()))
        return false;

    if (auto current = query.get_bool("currentWindow"sv); current.has_value() && (*current != (&window == current_window(caller_view_id))))
        return false;
    if (auto last_focused = query.get_bool("lastFocusedWindow"sv); last_focused.has_value()) {
        auto* focused = state().last_focused_window ? state().last_focused_window.data() : Application::the().active_window_if_any();
        if (*last_focused != (&window == focused))
            return false;
    }
    if (auto window_id_value = query.get_i64("windowId"sv); window_id_value.has_value()) {
        auto wanted = *window_id_value == WINDOW_ID_CURRENT ? current_window(caller_view_id) : find_window(*window_id_value);
        if (&window != wanted)
            return false;
    }
    if (auto index = query.get_i64("index"sv); index.has_value() && *index != window.tab_index(&tab))
        return false;
    if (auto status = query.get_string("status"sv); status.has_value() && *status != (tab.is_loading() ? "loading"sv : "complete"sv))
        return false;
    if (auto window_type = query.get_string("windowType"sv); window_type.has_value()) {
        auto type = state().popup_windows.contains(&window) ? "popup"sv : "normal"sv;
        if (*window_type != type)
            return false;
    }

    // URL and title queries match only what the extension may see.
    auto url = tab.url();
    auto sees_details = may_see_details(extension, url);
    if (auto title = query.get_string("title"sv); title.has_value()) {
        if (!sees_details || !glob_matches(*title, ak_string_from_qstring(tab.title()), true))
            return false;
    }
    if (auto url_query = query.get("url"sv); url_query.has_value() && !url_query->is_null()) {
        if (!sees_details)
            return false;
        Vector<String> patterns;
        if (url_query->is_string())
            patterns.append(url_query->as_string());
        else if (url_query->is_array()) {
            for (auto const& pattern : url_query->as_array().values()) {
                if (pattern.is_string())
                    patterns.append(pattern.as_string());
            }
        }
        if (!any_of(patterns, [&](auto const& pattern) {
                auto parsed = MatchPattern::parse(pattern);
                // Chrome matches tabs' own extension pages, which no match pattern scheme covers, by their URL.
                if (parsed.is_error())
                    return glob_matches(pattern, url.serialize());
                return parsed.value().matches(url);
            }))
            return false;
    }
    return true;
}

static ErrorOr<JsonValue, String> handle_tabs_call(Extension const& extension, Optional<u64> caller_view_id, StringView name, JsonArray const& args)
{
    if (name == "tabs.query"sv) {
        auto query = object_argument(args, 0);
        JsonArray tabs;
        for (auto* window : all_windows()) {
            for (int i = 0; i < window->tab_count(); ++i) {
                auto* tab = window->tab_at(i);
                if (tab_matches_query(*tab, *window, query, caller_view_id, extension))
                    tabs.must_append(tab_to_json(*tab, *window, extension));
            }
        }
        return JsonValue { move(tabs) };
    }

    if (name == "tabs.get"sv) {
        auto id = integer_argument(args, 0);
        if (!id.has_value())
            return "tabs.get() needs a tab ID."_string;
        auto tab = TRY(tab_argument(args, 0, caller_view_id));
        return JsonValue { tab_to_json(*tab.tab, *tab.window, extension) };
    }

    if (name == "tabs.getCurrent"sv) {
        if (!caller_view_id.has_value())
            return JsonValue {};
        auto tab = find_tab_for_view(*caller_view_id);
        if (!tab.has_value())
            return JsonValue {};
        return JsonValue { tab_to_json(*tab->tab, *tab->window, extension) };
    }

    if (name == "tabs.create"sv) {
        auto properties = object_argument(args, 0);
        auto* window = current_window(caller_view_id);
        if (auto id = properties.get_i64("windowId"sv); id.has_value() && *id != WINDOW_ID_CURRENT)
            window = find_window(*id);
        if (!window)
            return "No window to create the tab in."_string;

        auto url = URL::about_newtab();
        if (auto url_string = properties.get_string("url"sv); url_string.has_value()) {
            auto parsed = extension_relative_url(extension, *url_string);
            if (!parsed.has_value())
                return MUST(String::formatted("Invalid url: {}.", *url_string));
            url = parsed.release_value();
        }

        auto activate = properties.get_bool("active"sv).value_or(true) ? Web::HTML::ActivateTab::Yes : Web::HTML::ActivateTab::No;
        auto location = BrowserWindow::TabLocation::end();
        if (auto index = properties.get_i64("index"sv); index.has_value())
            location = BrowserWindow::TabLocation::at_index(static_cast<int>(*index));

        auto& tab = window->new_tab_from_url(url, activate, location);
        if (properties.get_bool("pinned"sv).value_or(false))
            window->pin_tab(tab);
        return JsonValue { tab_to_json(tab, *window, extension) };
    }

    if (name == "tabs.update"sv) {
        auto tab = TRY(tab_argument(args, 0, caller_view_id));
        auto properties = object_argument(args, 1);

        if (auto url_string = properties.get_string("url"sv); url_string.has_value()) {
            auto url = extension_relative_url(extension, *url_string);
            if (!url.has_value())
                return MUST(String::formatted("Invalid url: {}.", *url_string));
            tab.tab->navigate(*url);
        }
        if (properties.get_bool("active"sv).value_or(false) || properties.get_bool("highlighted"sv).value_or(false))
            tab.window->activate_tab(tab.window->tab_index(tab.tab));
        if (auto pinned = properties.get_bool("pinned"sv); pinned.has_value()) {
            if (*pinned && !tab.tab->is_pinned())
                tab.window->pin_tab(*tab.tab);
            else if (!*pinned && tab.tab->is_pinned())
                tab.window->unpin_tab(*tab.tab);
        }
        return JsonValue { tab_to_json(*tab.tab, *tab.window, extension) };
    }

    if (name == "tabs.remove"sv) {
        if (args.is_empty() || !args.at(0).is_array())
            return "tabs.remove() needs tab IDs."_string;
        for (auto const& id : args.at(0).as_array().values()) {
            auto tab = id.is_number() ? find_tab(id.get_i64().value_or(-1)) : Optional<TabInWindow> {};
            if (!tab.has_value())
                return "No tab with that id."_string;
            tab->window->request_to_close_tab(tab->window->tab_index(tab->tab));
        }
        return JsonValue {};
    }

    if (name == "tabs.reload"sv) {
        auto tab = TRY(tab_argument(args, 0, caller_view_id));
        if (tab.tab->is_loaded())
            tab.tab->view().reload();
        return JsonValue {};
    }

    if (name == "tabs.duplicate"sv) {
        auto tab = TRY(tab_argument(args, 0, caller_view_id));
        tab.window->duplicate_tab(*tab.tab);
        return JsonValue {};
    }

    return MUST(String::formatted("{} isn't supported yet.", name));
}

static ErrorOr<JsonValue, String> handle_windows_call(Extension const& extension, Optional<u64> caller_view_id, StringView name, JsonArray const& args)
{
    auto populate = [&](size_t index) { return object_argument(args, index).get_bool("populate"sv).value_or(false); };

    auto window_argument = [&](size_t index) -> ErrorOr<BrowserWindow*, String> {
        auto id = integer_argument(args, index);
        BrowserWindow* window = nullptr;
        if (!id.has_value() || *id == WINDOW_ID_CURRENT)
            window = current_window(caller_view_id);
        else
            window = find_window(*id);
        if (!window)
            return MUST(String::formatted("No window with id: {}.", id.value_or(WINDOW_ID_CURRENT)));
        return window;
    };

    if (name == "windows.get"sv)
        return JsonValue { window_to_json(*TRY(window_argument(0)), extension, populate(1)) };
    if (name == "windows.getCurrent"sv)
        return JsonValue { window_to_json(*TRY(window_argument(args.size())), extension, populate(0)) };
    if (name == "windows.getLastFocused"sv) {
        auto* window = state().last_focused_window ? state().last_focused_window.data() : Application::the().active_window_if_any();
        if (!window)
            return "No last-focused window."_string;
        return JsonValue { window_to_json(*window, extension, populate(0)) };
    }

    if (name == "windows.getAll"sv) {
        auto filter = object_argument(args, 0);
        auto types = filter.get_array("windowTypes"sv);
        JsonArray windows;
        for (auto* window : all_windows()) {
            auto type = state().popup_windows.contains(window) ? "popup"sv : "normal"sv;
            if (types.has_value() && !any_of(types->values(), [&](auto const& wanted) { return wanted.is_string() && wanted.as_string() == type; }))
                continue;
            windows.must_append(window_to_json(*window, extension, populate(0)));
        }
        return JsonValue { move(windows) };
    }

    if (name == "windows.create"sv) {
        auto data = object_argument(args, 0);

        Vector<URL::URL> urls;
        auto add_url = [&](String const& url_string) -> ErrorOr<void, String> {
            auto url = extension_relative_url(extension, url_string);
            if (!url.has_value())
                return MUST(String::formatted("Invalid url: {}.", url_string));
            urls.append(url.release_value());
            return {};
        };
        if (auto url = data.get("url"sv); url.has_value()) {
            if (url->is_string())
                TRY(add_url(url->as_string()));
            else if (url->is_array()) {
                for (auto const& value : url->as_array().values()) {
                    if (value.is_string())
                        TRY(add_url(value.as_string()));
                }
            }
        }

        WindowConfiguration configuration;
        if (auto left = data.get_i32("left"sv); left.has_value())
            configuration.x = *left;
        if (auto top = data.get_i32("top"sv); top.has_value())
            configuration.y = *top;
        if (auto width = data.get_i32("width"sv); width.has_value())
            configuration.width = *width;
        if (auto height = data.get_i32("height"sv); height.has_value())
            configuration.height = *height;
        configuration.maximized = data.get_string("state"sv) == "maximized"sv;

        auto is_popup = data.get_string("type"sv) == "popup"sv;
        auto is_private = data.get_bool("incognito"sv).value_or(false) ? WebView::IsPrivate::Yes : WebView::IsPrivate::No;
        auto& window = Application::the().new_window(urls, configuration, is_popup ? BrowserWindow::IsPopupWindow::Yes : BrowserWindow::IsPopupWindow::No, is_private);
        if (is_popup)
            state().popup_windows.set(&window);
        return JsonValue { window_to_json(window, extension, true) };
    }

    if (name == "windows.update"sv) {
        auto* window = TRY(window_argument(0));
        auto info = object_argument(args, 1);

        auto geometry = window->geometry();
        if (auto left = info.get_i32("left"sv); left.has_value())
            geometry.moveLeft(*left);
        if (auto top = info.get_i32("top"sv); top.has_value())
            geometry.moveTop(*top);
        if (auto width = info.get_i32("width"sv); width.has_value())
            geometry.setWidth(*width);
        if (auto height = info.get_i32("height"sv); height.has_value())
            geometry.setHeight(*height);
        if (geometry != window->geometry())
            window->setGeometry(geometry);

        if (auto window_state = info.get_string("state"sv); window_state.has_value()) {
            if (*window_state == "maximized"sv)
                window->showMaximized();
            else if (*window_state == "minimized"sv)
                window->showMinimized();
            else if (*window_state == "fullscreen"sv)
                window->showFullScreen();
            else
                window->showNormal();
        }
        if (info.get_bool("focused"sv).value_or(false) || info.get_bool("drawAttention"sv).value_or(false)) {
            window->show();
            window->raise();
            window->activateWindow();
        }
        return JsonValue { window_to_json(*window, extension, false) };
    }

    if (name == "windows.remove"sv) {
        auto* window = TRY(window_argument(0));
        window->close();
        return JsonValue {};
    }

    return MUST(String::formatted("{} isn't supported yet.", name));
}

// --- chrome.action -----------------------------------------------------------------------------------------------------

// Chrome's badge colors are CSS colors or [r, g, b, a] arrays.
static Optional<QColor> color_from_json(JsonValue const& value)
{
    if (value.is_string()) {
        auto string = value.as_string().bytes_as_string_view();
        // #rrggbbaa, which QColor reads as #aarrggbb.
        if (string.length() == 9 && string.starts_with('#')) {
            auto rgba = string.substring_view(1).to_number<u32>(TrimWhitespace::No, 16);
            if (!rgba.has_value())
                return {};
            return QColor((*rgba >> 24) & 0xff, (*rgba >> 16) & 0xff, (*rgba >> 8) & 0xff, *rgba & 0xff);
        }
        QColor color(qstring_from_ak_string(string));
        return color.isValid() ? Optional<QColor> { color } : Optional<QColor> {};
    }
    if (value.is_array() && value.as_array().size() == 4) {
        auto const& values = value.as_array();
        auto component = [&](size_t i) { return clamp(static_cast<int>(values.at(i).get_double_with_precision_loss().value_or(0)), 0, 255); };
        return QColor(component(0), component(1), component(2), component(3));
    }
    return {};
}

static JsonArray color_to_json(QColor const& color)
{
    JsonArray array;
    array.must_append(color.red());
    array.must_append(color.green());
    array.must_append(color.blue());
    array.must_append(color.alpha());
    return array;
}

static QColor default_badge_background_color()
{
    return ChromeStyle::chrome_accent();
}

ActionAppearance action_appearance(Extension const& extension, Tab* tab)
{
    auto const& manifest = extension.manifest();
    ActionAppearance appearance;
    appearance.title = manifest.action.has_value() ? manifest.action->default_title.value_or(manifest.name) : manifest.name;
    if (manifest.action.has_value()) {
        appearance.icon_path = best_icon(manifest.action->default_icon, TOOLBAR_ICON_PIXELS);
        appearance.popup = manifest.action->default_popup;
    }
    if (!appearance.icon_path.has_value())
        appearance.icon_path = best_icon(manifest.icons, TOOLBAR_ICON_PIXELS);
    appearance.badge_background_color = default_badge_background_color();

    auto apply = [&](ActionProperties const& properties) {
        if (properties.title.has_value())
            appearance.title = *properties.title;
        if (properties.icon_path.has_value())
            appearance.icon_path = *properties.icon_path;
        if (properties.badge_text.has_value())
            appearance.badge_text = *properties.badge_text;
        if (properties.badge_background_color.has_value())
            appearance.badge_background_color = *properties.badge_background_color;
        if (properties.badge_text_color.has_value())
            appearance.badge_text_color = *properties.badge_text_color;
        if (properties.popup.has_value())
            appearance.popup = properties.popup->is_empty() ? Optional<String> {} : properties.popup;
        if (properties.enabled.has_value())
            appearance.enabled = *properties.enabled;
    };

    auto action = state().actions.get(extension.id());
    if (!action.has_value())
        return appearance;
    apply(action->all_tabs);
    if (tab) {
        if (auto per_tab = action->tabs.get(tab_id(*tab)); per_tab.has_value())
            apply(*per_tab);
    }
    return appearance;
}

void on_action_change(Function<bool()> listener)
{
    state().action_listeners.append(move(listener));
}

Function<void(Extension const&)>& open_popup_handler()
{
    return state().open_popup;
}

void action_clicked(Extension const& extension, Tab* tab)
{
    JsonArray args;
    if (tab) {
        if (auto* window = window_with_tab(*tab))
            args.must_append(tab_to_json(*tab, *window, extension));
    }
    Router::the().dispatch_event(extension, "action.onClicked"sv, move(args));
}

static ErrorOr<JsonValue, String> handle_action_call(Extension const& extension, StringView name, JsonArray const& args)
{
    if (!extension.manifest().action.has_value())
        return "The extension has no action."_string;

    // Most calls take {tabId?, value}; enable(), disable(), and isEnabled() take the tab ID itself.
    auto details = object_argument(args, 0);
    Optional<i64> tab = details.get_i64("tabId"sv);
    if (name.is_one_of("action.enable"sv, "action.disable"sv, "action.isEnabled"sv))
        tab = integer_argument(args, 0);
    if (tab.has_value() && !find_tab(*tab).has_value())
        return MUST(String::formatted("No tab with id: {}.", *tab));

    auto& action = state().actions.ensure(extension.id());
    auto& properties = tab.has_value() ? action.tabs.ensure(*tab) : action.all_tabs;

    // Without a tab ID, a getter answers what all tabs show.
    auto appearance = action_appearance(extension, tab.has_value() ? find_tab(*tab)->tab : nullptr);

    auto changed = [&] {
        notify_action_listeners();
        return JsonValue {};
    };

    if (name == "action.setBadgeText"sv) {
        auto text = details.get_string("text"sv);
        properties.badge_text = text.has_value() ? *text : String {};
        return changed();
    }
    if (name == "action.getBadgeText"sv)
        return JsonValue { appearance.badge_text };

    if (name == "action.setBadgeBackgroundColor"sv || name == "action.setBadgeTextColor"sv) {
        auto color = color_from_json(details.get("color"sv).value_or(JsonValue {}));
        if (!color.has_value())
            return "Invalid color."_string;
        if (name == "action.setBadgeBackgroundColor"sv)
            properties.badge_background_color = *color;
        else
            properties.badge_text_color = *color;
        return changed();
    }
    if (name == "action.getBadgeBackgroundColor"sv)
        return JsonValue { color_to_json(appearance.badge_background_color) };
    if (name == "action.getBadgeTextColor"sv)
        return JsonValue { color_to_json(appearance.badge_text_color.value_or(Qt::white)) };

    if (name == "action.setTitle"sv) {
        properties.title = details.get_string("title"sv).copy();
        return changed();
    }
    if (name == "action.getTitle"sv)
        return JsonValue { appearance.title };

    if (name == "action.setIcon"sv) {
        auto path = details.get("path"sv);
        if (!path.has_value())
            return "action.setIcon() takes only a path for now."_string;
        if (path->is_string()) {
            auto icon = path->as_string().bytes_as_string_view();
            while (icon.starts_with('/'))
                icon = icon.substring_view(1);
            properties.icon_path = MUST(String::from_utf8(icon));
        } else if (path->is_object()) {
            Vector<Icon> icons;
            path->as_object().for_each_member([&](String const& size, JsonValue const& icon) {
                if (icon.is_string())
                    icons.append({ .size = size.to_number<u32>().value_or(0), .path = icon.as_string() });
            });
            quick_sort(icons, [](auto const& a, auto const& b) { return a.size < b.size; });
            properties.icon_path = best_icon(icons, TOOLBAR_ICON_PIXELS);
            if (properties.icon_path.has_value()) {
                auto icon = properties.icon_path->bytes_as_string_view();
                while (icon.starts_with('/'))
                    icon = icon.substring_view(1);
                properties.icon_path = MUST(String::from_utf8(icon));
            }
        }
        return changed();
    }

    if (name == "action.setPopup"sv) {
        auto popup = details.get_string("popup"sv).value_or({});
        auto view = popup.bytes_as_string_view();
        while (view.starts_with('/'))
            view = view.substring_view(1);
        properties.popup = MUST(String::from_utf8(view));
        return changed();
    }
    if (name == "action.getPopup"sv)
        return JsonValue { appearance.popup.has_value() ? extension.url_for(*appearance.popup).serialize() : String {} };

    if (name == "action.enable"sv || name == "action.disable"sv) {
        properties.enabled = name == "action.enable"sv;
        return changed();
    }
    if (name == "action.isEnabled"sv)
        return JsonValue { appearance.enabled };

    if (name == "action.openPopup"sv) {
        if (state().open_popup)
            state().open_popup(extension);
        return JsonValue {};
    }

    return MUST(String::formatted("{} isn't supported yet.", name));
}

ErrorOr<JsonValue, String> handle_call(Extension const& extension, Optional<u64> caller_view_id, StringView name, JsonArray const& args)
{
    if (name.starts_with("tabs."sv)) {
        if (name == "tabs.captureVisibleTab"sv)
            return "tabs.captureVisibleTab() isn't supported yet."_string;
        return handle_tabs_call(extension, caller_view_id, name, args);
    }
    if (name.starts_with("windows."sv))
        return handle_windows_call(extension, caller_view_id, name, args);
    if (name.starts_with("action."sv))
        return handle_action_call(extension, name, args);
    return MUST(String::formatted("{} isn't supported yet.", name));
}

}
