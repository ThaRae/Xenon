/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/HashTable.h>
#include <AK/NeverDestroyed.h>
#include <AK/QuickSort.h>
#include <LibCore/EventLoop.h>
#include <LibURL/InternalURLs.h>
#include <LibURL/Parser.h>
#include <LibWebView/Application.h>
#include <Rune/Bridge/Engine.h>
#include <Rune/Bridge/ExtensionsHost.h>
#include <Rune/Bridge/View.h>
#include <Rune/Bridge/WindowLifecycle.h>
#include <UI/Extensions/Browser.h>
#include <UI/Extensions/MatchPattern.h>
#include <UI/Extensions/Registry.h>
#include <UI/Extensions/Router.h>
#include <UI/PasswordManagers.h>

namespace Ladybird::Extensions::Browser {

using Xenon::BrowserModel;
using Xenon::Engine;

static constexpr i64 WINDOW_ID_CURRENT = -2;
static constexpr u32 TOOLBAR_ICON_PIXELS = 32;
static constexpr u32 DIALOG_ICON_PIXELS = 80;
static constexpr Gfx::Color DEFAULT_BADGE_BACKGROUND { 0xff, 0x00, 0x50 };

static constexpr int POPUP_MINIMUM_WIDTH = 25;
static constexpr int POPUP_MINIMUM_HEIGHT = 25;
static constexpr int POPUP_MAXIMUM_WIDTH = 800;
static constexpr int POPUP_MAXIMUM_HEIGHT = 600;
static constexpr int POPUP_INITIAL_WIDTH = 360;
static constexpr int POPUP_INITIAL_HEIGHT = 480;

namespace {

struct ActionProperties {
    Optional<String> badge_text;
    Optional<Gfx::Color> badge_background_color;
    Optional<Gfx::Color> badge_text_color;
    Optional<String> title;
    Optional<String> icon_path;
    Optional<String> popup;
    Optional<bool> enabled;
};

struct ActionState {
    ActionProperties all_tabs;
    HashMap<i64, ActionProperties> tabs;
};

struct Popup {
    XenonViewId tab { 0 };
    XenonWindowId window { XENON_WINDOW_NONE };
    String extension_id;
};

struct State {
    XenonExtensionCallbacks callbacks {};
    bool started { false };
    bool actions_report_pending { false };

    HashMap<XenonViewId, Popup> popups;
    HashTable<XenonWindowId> popup_windows;

    HashMap<String, ActionState> actions;

    HashMap<XenonPromptId, Function<void(bool)>> permission_prompts;
    XenonPromptId next_prompt_id { 1 };
};

}

static State& state()
{
    static NeverDestroyed<State> state;
    return *state;
}

template<typename Callback, typename... Arguments>
static void notify(Callback XenonExtensionCallbacks::* callback, Arguments... arguments)
{
    auto const& callbacks = state().callbacks;
    if (auto function = callbacks.*callback)
        function(callbacks.context, arguments...);
}

static XenonStr xenon_str(StringView string)
{
    return { string.characters_without_null_termination(), string.length() };
}

static u32 rgba(Gfx::Color color)
{
    return (static_cast<u32>(color.red()) << 24) | (static_cast<u32>(color.green()) << 16) | (static_cast<u32>(color.blue()) << 8) | color.alpha();
}

static BrowserModel* model()
{
    return Engine::the().model();
}

static Xenon::View* tab_view(XenonViewId id)
{
    auto* browser_model = model();
    if (!browser_model || !browser_model->tab(id))
        return nullptr;
    return Engine::the().view(id);
}

static BrowserModel::Window const* window_with_id(i64 id)
{
    auto* browser_model = model();
    if (!browser_model || id <= 0)
        return nullptr;
    return browser_model->window(static_cast<XenonWindowId>(id));
}

struct TabInWindow {
    BrowserModel::Tab const* tab { nullptr };
    BrowserModel::Window const* window { nullptr };
};

static Optional<TabInWindow> find_tab(i64 id)
{
    auto* browser_model = model();
    if (!browser_model || id <= 0)
        return {};
    auto const* tab = browser_model->tab(static_cast<XenonViewId>(id));
    if (!tab)
        return {};
    auto const* window = browser_model->window(tab->window);
    if (!window)
        return {};
    return TabInWindow { tab, window };
}

static BrowserModel::Window const* focused_window()
{
    auto* browser_model = model();
    if (!browser_model)
        return nullptr;
    auto active = browser_model->active_window();
    return active.has_value() ? browser_model->window(*active) : nullptr;
}

static BrowserModel::Window const* current_window(Optional<u64> caller_view_id)
{
    if (caller_view_id.has_value()) {
        if (auto tab = find_tab(static_cast<i64>(*caller_view_id)); tab.has_value())
            return tab->window;
        if (auto popup = state().popups.get(*caller_view_id); popup.has_value()) {
            if (auto const* window = window_with_id(static_cast<i64>(popup->window)))
                return window;
        }
    }
    return focused_window();
}

static Optional<XenonViewId> active_tab_of(BrowserModel::Window const* window)
{
    if (!window)
        return {};
    return window->active_tab;
}

static bool may_see_details(Extension const& extension, URL::URL const& url)
{
    auto const& manifest = extension.manifest();
    if (extension.has_permission("tabs"sv))
        return true;
    if (url.scheme() == EXTENSION_SCHEME && url.serialized_host() == extension.id().bytes_as_string_view())
        return true;
    return any_of(manifest.host_permissions, [&](auto const& pattern) {
        auto parsed = MatchPattern::parse(pattern);
        return !parsed.is_error() && parsed.value().matches(url);
    });
}

static URL::URL tab_url(BrowserModel::Tab const& tab)
{
    if (auto* view = tab_view(tab.view))
        return view->url();
    if (tab.has_initial_url())
        return tab.initial_url;
    return URL::about_blank();
}

static String tab_title(BrowserModel::Tab const& tab)
{
    if (auto* view = tab_view(tab.view))
        return view->title().to_utf8();
    return {};
}

static bool tab_is_loading(BrowserModel::Tab const& tab)
{
    auto* view = tab_view(tab.view);
    return view && view->is_loading();
}

static StringView window_type(BrowserModel::Window const& window)
{
    return state().popup_windows.contains(window.id) ? "popup"sv : "normal"sv;
}

static JsonObject tab_to_json(BrowserModel::Tab const& tab, BrowserModel::Window const& window, Extension const& extension)
{
    auto active = window.active_tab == tab.view;

    JsonObject json;
    json.set("id"sv, static_cast<i64>(tab.view));
    json.set("index"sv, static_cast<i64>(window.index_of(tab.view).value_or(0)));
    json.set("windowId"sv, static_cast<i64>(window.id));
    json.set("active"sv, active);
    json.set("highlighted"sv, active);
    json.set("selected"sv, active);
    json.set("pinned"sv, tab.pinned);
    json.set("audible"sv, false);
    json.set("discarded"sv, tab_view(tab.view) == nullptr);
    json.set("autoDiscardable"sv, true);
    json.set("frozen"sv, false);
    json.set("incognito"sv, window.is_private == WebView::IsPrivate::Yes);
    json.set("status"sv, tab_is_loading(tab) ? "loading"sv : "complete"sv);
    json.set("groupId"sv, -1);
    if (tab.opener.has_value() && model() && model()->tab(*tab.opener))
        json.set("openerTabId"sv, static_cast<i64>(*tab.opener));

    JsonObject muted;
    muted.set("muted"sv, false);
    json.set("mutedInfo"sv, move(muted));

    if (auto* view = tab_view(tab.view)) {
        auto size = view->viewport_size();
        auto ratio = view->device_pixel_ratio();
        json.set("width"sv, static_cast<i64>(size.width().value() / ratio));
        json.set("height"sv, static_cast<i64>(size.height().value() / ratio));
    }

    auto url = tab_url(tab);
    if (may_see_details(extension, url)) {
        json.set("url"sv, url.serialize());
        json.set("title"sv, tab_title(tab));
    }
    return json;
}

static WebView::SessionWindowGeometry window_geometry(BrowserModel::Window const& window)
{
    if (auto geometry = Xenon::WindowLifecycle::the().geometry_of(window.id); geometry.has_value())
        return *geometry;
    if (window.geometry.has_value())
        return *window.geometry;

    WebView::SessionWindowGeometry geometry;
    if (auto active = active_tab_of(&window); active.has_value()) {
        if (auto* view = tab_view(*active)) {
            auto size = view->viewport_size();
            auto ratio = view->device_pixel_ratio();
            geometry.width = static_cast<i32>(size.width().value() / ratio);
            geometry.height = static_cast<i32>(size.height().value() / ratio);
        }
    }
    return geometry;
}

static JsonObject window_to_json(BrowserModel::Window const& window, Extension const& extension, bool populate)
{
    auto geometry = window_geometry(window);

    JsonObject json;
    json.set("id"sv, static_cast<i64>(window.id));
    json.set("focused"sv, focused_window() == &window);
    json.set("top"sv, geometry.y);
    json.set("left"sv, geometry.x);
    json.set("width"sv, geometry.width);
    json.set("height"sv, geometry.height);
    json.set("incognito"sv, window.is_private == WebView::IsPrivate::Yes);
    json.set("type"sv, window_type(window));
    json.set("alwaysOnTop"sv, false);
    json.set("state"sv, geometry.maximized ? "maximized"sv : "normal"sv);

    if (populate) {
        JsonArray tabs;
        for (auto view : window.tabs) {
            if (auto const* tab = model()->tab(view))
                tabs.must_append(tab_to_json(*tab, window, extension));
        }
        json.set("tabs"sv, move(tabs));
    }
    return json;
}

static Vector<BrowserModel::Window const*> all_windows()
{
    Vector<BrowserModel::Window const*> windows;
    auto* browser_model = model();
    if (!browser_model)
        return windows;
    for (auto id : browser_model->windows_by_activation()) {
        if (auto const* window = browser_model->window(id))
            windows.append(window);
    }
    return windows;
}

static Optional<u64> xenon_view_id(Optional<u64> web_view_id)
{
    if (!web_view_id.has_value())
        return {};
    Optional<u64> found;
    WebView::ViewImplementation::for_each_view([&](WebView::ViewImplementation& view) {
        if (view.view_id() != *web_view_id)
            return IterationDecision::Continue;
        if (auto* xenon_view = dynamic_cast<Xenon::View*>(&view))
            found = xenon_view->id();
        return IterationDecision::Break;
    });
    return found;
}

Optional<TabAndWindowIds> tab_and_window_ids(u64 web_view_id)
{
    auto view_id = xenon_view_id(web_view_id);
    if (!view_id.has_value())
        return {};
    auto tab = find_tab(static_cast<i64>(*view_id));
    if (!tab.has_value())
        return {};
    return TabAndWindowIds { .tab_id = static_cast<i64>(tab->tab->view), .window_id = static_cast<i64>(tab->window->id) };
}

Optional<JsonObject> tab_json(u64 web_view_id, Extension const& extension)
{
    auto view_id = xenon_view_id(web_view_id);
    if (!view_id.has_value())
        return {};
    auto tab = find_tab(static_cast<i64>(*view_id));
    if (!tab.has_value())
        return {};
    return tab_to_json(*tab->tab, *tab->window, extension);
}

Optional<Vector<WebView::ScriptBridgeEndpoint>> frame_documents(i64 tab_id)
{
    if (tab_id <= 0)
        return {};
    auto* view = tab_view(static_cast<XenonViewId>(tab_id));
    if (!view)
        return {};
    return view->frame_document_endpoints();
}

void open_tab(URL::URL const& url)
{
    auto* browser_model = model();
    if (!browser_model)
        return;
    auto window = browser_model->active_window();
    if (!window.has_value())
        window = browser_model->open_window(WebView::IsPrivate::No);
    browser_model->open_tab(*window, { .url = url });
}

bool is_popup_view(u64 web_view_id)
{
    auto view_id = xenon_view_id(web_view_id);
    return view_id.has_value() && state().popups.contains(*view_id);
}

static void dispatch_tab_event(StringView name, BrowserModel::Tab const& tab, BrowserModel::Window const& window)
{
    for (auto const& extension : Registry::the().extensions()) {
        JsonArray args;
        args.must_append(tab_to_json(tab, window, extension));
        Router::the().dispatch_event(extension, name, move(args));
    }
}

static void dispatch_tab_updated(XenonViewId view, JsonObject const& change)
{
    auto tab = find_tab(static_cast<i64>(view));
    if (!tab.has_value())
        return;

    for (auto const& extension : Registry::the().extensions()) {
        auto json = tab_to_json(*tab->tab, *tab->window, extension);

        JsonObject visible_change;
        change.for_each_member([&](String const& key, JsonValue const& value) {
            if (!key.is_one_of("url"sv, "title"sv) || json.has(key))
                visible_change.set(key, value);
        });
        if (visible_change.size() == 0)
            continue;

        JsonArray args;
        args.must_append(static_cast<i64>(view));
        args.must_append(move(visible_change));
        args.must_append(move(json));
        Router::the().dispatch_event(extension, "tabs.onUpdated"sv, move(args));
    }
}

static String trimmed_path(StringView path)
{
    while (path.starts_with('/'))
        path = path.substring_view(1);
    return MUST(String::from_utf8(path));
}

static ActionAppearance appearance_for(Extension const& extension, Optional<XenonViewId> tab)
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
    appearance.badge_background_color = DEFAULT_BADGE_BACKGROUND;

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
    if (tab.has_value()) {
        if (auto per_tab = action->tabs.get(static_cast<i64>(*tab)); per_tab.has_value())
            apply(*per_tab);
    }
    return appearance;
}

static void report_actions(XenonViewId tab)
{
    struct Strings {
        String extension_id;
        String name;
        String title;
        ByteString icon_path;
        String badge_text;
    };
    Vector<Strings> strings;
    Vector<XenonExtensionAction> actions;
    auto const& extensions = Registry::the().extensions();
    strings.ensure_capacity(extensions.size());
    actions.ensure_capacity(extensions.size());

    for (auto const& extension : extensions) {
        if (!extension->manifest().action.has_value())
            continue;
        auto appearance = appearance_for(extension, tab);
        ByteString icon_path;
        if (appearance.icon_path.has_value()) {
            if (auto file = Extension::confined_file(extension->root(), appearance.icon_path->bytes_as_string_view()); !file.is_error())
                icon_path = file.release_value();
        }
        strings.append({ extension->id(), extension->manifest().name, appearance.title, move(icon_path), appearance.badge_text });
        auto const& owned = strings.last();
        actions.append({
            .extension_id = xenon_str(owned.extension_id),
            .name = xenon_str(owned.name),
            .title = xenon_str(owned.title),
            .icon_path = xenon_str(owned.icon_path),
            .badge_text = xenon_str(owned.badge_text),
            .badge_background = rgba(appearance.badge_background_color),
            .has_badge_text_color = appearance.badge_text_color.has_value(),
            .badge_text_color = rgba(appearance.badge_text_color.value_or(Gfx::Color::White)),
            .has_popup = appearance.popup.has_value(),
            .enabled = appearance.enabled,
            .has_options_page = extension->manifest().options_page.has_value(),
        });
    }
    notify(&XenonExtensionCallbacks::actions_changed, tab, static_cast<XenonExtensionAction const*>(actions.data()), actions.size());
}

static void report_actions_for_active_tabs()
{
    for (auto const* window : all_windows()) {
        if (window->active_tab.has_value())
            report_actions(*window->active_tab);
    }
}

static void notify_action_listeners()
{
    if (exchange(state().actions_report_pending, true))
        return;
    Core::deferred_invoke([] {
        state().actions_report_pending = false;
        report_actions_for_active_tabs();
    });
}

static void report_extensions()
{
    struct Strings {
        String details;
        String warnings;
        ByteString icon_path;
    };
    auto const& extensions = Registry::the().extensions();
    Vector<Strings> strings;
    Vector<XenonExtensionInfo> infos;
    strings.ensure_capacity(extensions.size());
    infos.ensure_capacity(extensions.size());

    for (auto const& extension : extensions) {
        auto const& manifest = extension->manifest();
        String details;
        if (Registry::the().is_installed(*extension))
            details = MUST(String::formatted("ID: {}\nInstalled from the Chrome Web Store", extension->id()));
        else
            details = MUST(String::formatted("ID: {}\n{}{}", extension->id(), extension->root(), Registry::the().is_persisted(*extension) ? ""sv : "\nLoaded for this session only"sv));

        ByteString icon_path;
        if (auto path = best_icon(manifest.icons, DIALOG_ICON_PIXELS); path.has_value()) {
            if (auto file = Extension::confined_file(extension->root(), path->bytes_as_string_view()); !file.is_error())
                icon_path = file.release_value();
        }
        strings.append({ move(details), MUST(String::join('\n', manifest.warnings)), move(icon_path) });
        auto const& owned = strings.last();
        infos.append({
            .id = xenon_str(extension->id()),
            .name = xenon_str(manifest.name),
            .version = xenon_str(manifest.version),
            .description = xenon_str(manifest.description.has_value() ? manifest.description->bytes_as_string_view() : StringView {}),
            .details = xenon_str(owned.details),
            .warnings = xenon_str(owned.warnings),
            .icon_path = xenon_str(owned.icon_path),
            .has_options_page = manifest.options_page.has_value(),
        });
    }
    notify(&XenonExtensionCallbacks::extensions_changed, static_cast<XenonExtensionInfo const*>(infos.data()), infos.size());
}

static void report_commands()
{
    Vector<XenonExtensionCommand> commands;
    for (auto const& extension : Registry::the().extensions()) {
        for (auto const& command : extension->manifest().commands) {
            if (!command.suggested_key.has_value())
                continue;
            commands.append({ xenon_str(extension->id()), xenon_str(command.name), xenon_str(*command.suggested_key) });
        }
    }
    notify(&XenonExtensionCallbacks::commands_changed, static_cast<XenonExtensionCommand const*>(commands.data()), commands.size());
}

static void report_password_managers()
{
    auto& password_managers = PasswordManagers::the();
    auto current = password_managers.current();
    auto installing = password_managers.installing();

    Vector<XenonPasswordManagerChoice> choices;
    for (auto const& choice : password_managers.choices()) {
        if (!choice.available && (!current.has_value() || current->id != choice.id))
            continue;
        choices.append({ xenon_str(choice.id), xenon_str(choice.name) });
    }

    String status;
    if (installing.has_value())
        status = MUST(String::formatted("Installing {} from the Chrome Web Store…", installing->name));
    else if (auto const& error = password_managers.error(); error.has_value())
        status = *error;
    else if (current.has_value())
        status = "From the Chrome Web Store, and kept up to date"_string;

    XenonPasswordManagers report {
        .choices = choices.data(),
        .choice_count = choices.size(),
        .current = xenon_str(current.has_value() ? current->id : StringView {}),
        .installing = xenon_str(installing.has_value() ? installing->id : StringView {}),
        .status = xenon_str(status),
    };
    notify(&XenonExtensionCallbacks::password_managers_changed, static_cast<XenonPasswordManagers const*>(&report));
}

static void report_error(StringView title, StringView message)
{
    notify(&XenonExtensionCallbacks::error, xenon_str(title), xenon_str(message));
}

static void close_popup(XenonViewId popup_view)
{
    auto popup = state().popups.take(popup_view);
    if (!popup.has_value())
        return;
    Router::the().set_popup_size_listener(popup->extension_id, {});
    Engine::the().close_detached_view(popup_view);
    notify(&XenonExtensionCallbacks::popup_closed, popup_view);
    Core::deferred_invoke([] { Router::the().prune_closed_contexts(); });
}

static void close_all_popups()
{
    auto views = state().popups.keys();
    for (auto view : views)
        close_popup(view);
}

static void open_popup(Extension const& extension, XenonViewId tab, String const& popup_path)
{
    close_all_popups();

    auto* browser_model = model();
    auto const* window = browser_model ? browser_model->window_of(tab) : nullptr;
    if (!window)
        return;

    auto& engine = Engine::the();
    auto id = engine.allocate_view_id();
    auto& view = engine.add_detached_view(Xenon::View::create(id, { POPUP_INITIAL_WIDTH, POPUP_INITIAL_HEIGHT }, 1.0, window->is_private,
        Web::HTML::VisibilityState::Visible, {}, WebView::EnableContentBlocker::No));
    state().popups.set(id, { .tab = tab, .window = window->id, .extension_id = extension.id() });

    view.on_close = [id] {
        Core::deferred_invoke([id] { close_popup(id); });
    };

    Router::the().set_popup_size_listener(extension.id(), [id](int width, int height) {
        if (!state().popups.contains(id))
            return;
        width = clamp(width, POPUP_MINIMUM_WIDTH, POPUP_MAXIMUM_WIDTH);
        height = clamp(height, POPUP_MINIMUM_HEIGHT, POPUP_MAXIMUM_HEIGHT);
        notify(&XenonExtensionCallbacks::popup_resized, id, static_cast<u32>(width), static_cast<u32>(height));
    });

    notify(&XenonExtensionCallbacks::popup_opened, tab, xenon_str(extension.id()), id, static_cast<u32>(POPUP_INITIAL_WIDTH), static_cast<u32>(POPUP_INITIAL_HEIGHT));
    view.load(extension.url_for(popup_path));
}

static void open_popup_in_focused_window(Extension const& extension)
{
    auto tab = active_tab_of(focused_window());
    if (!tab.has_value())
        return;
    auto appearance = appearance_for(extension, *tab);
    if (appearance.popup.has_value())
        open_popup(extension, *tab, *appearance.popup);
}

static void action_clicked_in_tab(Extension const& extension, Optional<XenonViewId> tab)
{
    JsonArray args;
    if (tab.has_value()) {
        if (auto found = find_tab(static_cast<i64>(*tab)); found.has_value())
            args.must_append(tab_to_json(*found->tab, *found->window, extension));
    }
    Router::the().dispatch_event(extension, "action.onClicked"sv, move(args));
}

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

    auto const* window = current_window(caller_view_id);
    auto active = active_tab_of(window);
    if (!active.has_value())
        return "No current tab."_string;
    auto tab = find_tab(static_cast<i64>(*active));
    if (!tab.has_value())
        return "No current tab."_string;
    return tab.release_value();
}

static Optional<URL::URL> extension_relative_url(Extension const& extension, StringView url)
{
    return URL::Parser::basic_parse(url, extension.base_url());
}

static bool tab_matches_query(BrowserModel::Tab const& tab, BrowserModel::Window const& window, JsonObject const& query, Optional<u64> caller_view_id, Extension const& extension)
{
    auto active = window.active_tab == tab.view;

    auto matches_bool = [&](StringView key, bool value) {
        auto wanted = query.get_bool(key);
        return !wanted.has_value() || *wanted == value;
    };
    if (!matches_bool("active"sv, active) || !matches_bool("highlighted"sv, active) || !matches_bool("pinned"sv, tab.pinned))
        return false;
    if (!matches_bool("audible"sv, false) || !matches_bool("muted"sv, false) || !matches_bool("discarded"sv, tab_view(tab.view) == nullptr))
        return false;

    if (auto current = query.get_bool("currentWindow"sv); current.has_value() && (*current != (&window == current_window(caller_view_id))))
        return false;
    if (auto last_focused = query.get_bool("lastFocusedWindow"sv); last_focused.has_value() && (*last_focused != (&window == focused_window())))
        return false;
    if (auto window_id_value = query.get_i64("windowId"sv); window_id_value.has_value()) {
        auto const* wanted = *window_id_value == WINDOW_ID_CURRENT ? current_window(caller_view_id) : window_with_id(*window_id_value);
        if (&window != wanted)
            return false;
    }
    if (auto index = query.get_i64("index"sv); index.has_value() && *index != static_cast<i64>(window.index_of(tab.view).value_or(0)))
        return false;
    if (auto status = query.get_string("status"sv); status.has_value() && *status != (tab_is_loading(tab) ? "loading"sv : "complete"sv))
        return false;
    if (auto type = query.get_string("windowType"sv); type.has_value() && *type != window_type(window))
        return false;
    if (auto opener = query.get_i64("openerTabId"sv); opener.has_value() && (!tab.opener.has_value() || static_cast<i64>(*tab.opener) != *opener))
        return false;

    auto url = tab_url(tab);
    auto sees_details = may_see_details(extension, url);
    if (auto title = query.get_string("title"sv); title.has_value()) {
        if (!sees_details || !glob_matches(*title, tab_title(tab), true))
            return false;
    }
    if (auto url_query = query.get("url"sv); url_query.has_value() && !url_query->is_null()) {
        if (!sees_details)
            return false;
        Vector<String> patterns;
        if (url_query->is_string()) {
            patterns.append(url_query->as_string());
        } else if (url_query->is_array()) {
            for (auto const& pattern : url_query->as_array().values()) {
                if (pattern.is_string())
                    patterns.append(pattern.as_string());
            }
        }
        if (!any_of(patterns, [&](auto const& pattern) {
                auto parsed = MatchPattern::parse(pattern);
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
    auto* browser_model = model();
    if (!browser_model)
        return "The browser isn't running."_string;

    if (name == "tabs.query"sv) {
        auto query = object_argument(args, 0);
        JsonArray tabs;
        for (auto const* window : all_windows()) {
            for (auto view : window->tabs) {
                auto const* tab = browser_model->tab(view);
                if (tab && tab_matches_query(*tab, *window, query, caller_view_id, extension))
                    tabs.must_append(tab_to_json(*tab, *window, extension));
            }
        }
        return JsonValue { move(tabs) };
    }

    if (name == "tabs.get"sv) {
        if (!integer_argument(args, 0).has_value())
            return "tabs.get() needs a tab ID."_string;
        auto tab = TRY(tab_argument(args, 0, caller_view_id));
        return JsonValue { tab_to_json(*tab.tab, *tab.window, extension) };
    }

    if (name == "tabs.getCurrent"sv) {
        if (!caller_view_id.has_value())
            return JsonValue {};
        auto tab = find_tab(static_cast<i64>(*caller_view_id));
        if (!tab.has_value())
            return JsonValue {};
        return JsonValue { tab_to_json(*tab->tab, *tab->window, extension) };
    }

    if (name == "tabs.create"sv) {
        auto properties = object_argument(args, 0);
        auto const* window = current_window(caller_view_id);
        if (auto id = properties.get_i64("windowId"sv); id.has_value() && *id != WINDOW_ID_CURRENT)
            window = window_with_id(*id);
        if (!window)
            return "No window to create the tab in."_string;

        auto url = URL::about_newtab();
        if (auto url_string = properties.get_string("url"sv); url_string.has_value()) {
            auto parsed = extension_relative_url(extension, *url_string);
            if (!parsed.has_value())
                return MUST(String::formatted("Invalid url: {}.", *url_string));
            url = parsed.release_value();
        }

        BrowserModel::NewTab new_tab {
            .url = url,
            .activate = properties.get_bool("active"sv).value_or(true) ? Web::HTML::ActivateTab::Yes : Web::HTML::ActivateTab::No,
        };
        if (auto index = properties.get_i64("index"sv); index.has_value() && *index >= 0)
            new_tab.index = static_cast<size_t>(*index);
        if (auto opener = properties.get_i64("openerTabId"sv); opener.has_value() && find_tab(*opener).has_value())
            new_tab.opener = static_cast<XenonViewId>(*opener);

        auto window_id = window->id;
        auto id = browser_model->open_tab(window_id, move(new_tab));
        if (properties.get_bool("pinned"sv).value_or(false))
            browser_model->set_tab_pinned(id, true);
        auto created = find_tab(static_cast<i64>(id));
        if (!created.has_value())
            return JsonValue {};
        return JsonValue { tab_to_json(*created->tab, *created->window, extension) };
    }

    if (name == "tabs.update"sv) {
        auto tab = TRY(tab_argument(args, 0, caller_view_id));
        auto properties = object_argument(args, integer_argument(args, 0).has_value() ? 1 : 0);
        auto view_id = tab.tab->view;

        if (auto url_string = properties.get_string("url"sv); url_string.has_value()) {
            auto url = extension_relative_url(extension, *url_string);
            if (!url.has_value())
                return MUST(String::formatted("Invalid url: {}.", *url_string));
            if (!tab_view(view_id))
                browser_model->activate_tab(view_id);
            if (auto* view = tab_view(view_id))
                view->load(*url);
        }
        if (properties.get_bool("active"sv).value_or(false) || properties.get_bool("highlighted"sv).value_or(false))
            browser_model->activate_tab(view_id);
        if (auto pinned = properties.get_bool("pinned"sv); pinned.has_value() && *pinned != tab.tab->pinned)
            browser_model->set_tab_pinned(view_id, *pinned);

        auto updated = find_tab(static_cast<i64>(view_id));
        if (!updated.has_value())
            return JsonValue {};
        return JsonValue { tab_to_json(*updated->tab, *updated->window, extension) };
    }

    if (name == "tabs.remove"sv) {
        Vector<i64> ids;
        if (auto id = integer_argument(args, 0); id.has_value()) {
            ids.append(*id);
        } else if (!args.is_empty() && args.at(0).is_array()) {
            for (auto const& id : args.at(0).as_array().values())
                ids.append(id.is_number() ? id.get_i64().value_or(-1) : -1);
        } else {
            return "tabs.remove() needs tab IDs."_string;
        }
        for (auto id : ids) {
            if (!find_tab(id).has_value())
                return MUST(String::formatted("No tab with id: {}.", id));
        }
        for (auto id : ids) {
            if (auto* view = tab_view(static_cast<XenonViewId>(id)))
                view->request_close_with_prompt();
            else
                browser_model->close_tab(static_cast<XenonViewId>(id));
        }
        return JsonValue {};
    }

    if (name == "tabs.reload"sv) {
        auto tab = TRY(tab_argument(args, 0, caller_view_id));
        auto properties = object_argument(args, integer_argument(args, 0).has_value() ? 1 : 0);
        if (auto* view = tab_view(tab.tab->view))
            view->reload(properties.get_bool("bypassCache"sv).value_or(false) ? WebView::BypassCache::Yes : WebView::BypassCache::No);
        return JsonValue {};
    }

    if (name == "tabs.duplicate"sv) {
        auto tab = TRY(tab_argument(args, 0, caller_view_id));
        auto& engine = Engine::the();
        auto duplicate = engine.allocate_view_id();
        engine.duplicate_tab(tab.tab->view, duplicate);
        auto created = find_tab(static_cast<i64>(duplicate));
        if (!created.has_value())
            return JsonValue {};
        return JsonValue { tab_to_json(*created->tab, *created->window, extension) };
    }

    return MUST(String::formatted("{} isn't supported yet.", name));
}

static ErrorOr<JsonValue, String> handle_windows_call(Extension const& extension, Optional<u64> caller_view_id, StringView name, JsonArray const& args)
{
    auto* browser_model = model();
    if (!browser_model)
        return "The browser isn't running."_string;

    auto populate = [&](size_t index) { return object_argument(args, index).get_bool("populate"sv).value_or(false); };

    auto window_argument = [&](size_t index) -> ErrorOr<BrowserModel::Window const*, String> {
        auto id = integer_argument(args, index);
        BrowserModel::Window const* window = nullptr;
        if (!id.has_value() || *id == WINDOW_ID_CURRENT)
            window = current_window(caller_view_id);
        else
            window = window_with_id(*id);
        if (!window)
            return MUST(String::formatted("No window with id: {}.", id.value_or(WINDOW_ID_CURRENT)));
        return window;
    };

    if (name == "windows.get"sv)
        return JsonValue { window_to_json(*TRY(window_argument(0)), extension, populate(1)) };
    if (name == "windows.getCurrent"sv)
        return JsonValue { window_to_json(*TRY(window_argument(args.size())), extension, populate(0)) };
    if (name == "windows.getLastFocused"sv) {
        auto const* window = focused_window();
        if (!window)
            return "No last-focused window."_string;
        return JsonValue { window_to_json(*window, extension, populate(0)) };
    }

    if (name == "windows.getAll"sv) {
        auto filter = object_argument(args, 0);
        auto types = filter.get_array("windowTypes"sv);
        JsonArray windows;
        for (auto const* window : all_windows()) {
            auto type = window_type(*window);
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
            if (url->is_string()) {
                TRY(add_url(url->as_string()));
            } else if (url->is_array()) {
                for (auto const& value : url->as_array().values()) {
                    if (value.is_string())
                        TRY(add_url(value.as_string()));
                }
            }
        }

        Optional<XenonViewId> moved_tab;
        if (auto tab_id = data.get_i64("tabId"sv); tab_id.has_value()) {
            if (!find_tab(*tab_id).has_value())
                return MUST(String::formatted("No tab with id: {}.", *tab_id));
            moved_tab = static_cast<XenonViewId>(*tab_id);
        }

        auto is_private = data.get_bool("incognito"sv).value_or(false) ? WebView::IsPrivate::Yes : WebView::IsPrivate::No;
        auto id = Engine::the().allocate_window_id();
        if (data.get_string("type"sv) == "popup"sv)
            state().popup_windows.set(id);
        browser_model->open_window(is_private, id);

        if (moved_tab.has_value())
            browser_model->move_tab(*moved_tab, id, 0);
        else if (urls.is_empty())
            urls.append(WebView::Application::settings().new_tab_page_url());
        for (size_t i = 0; i < urls.size(); ++i)
            browser_model->open_tab(id, { .url = urls[i], .activate = i == 0 && !moved_tab.has_value() ? Web::HTML::ActivateTab::Yes : Web::HTML::ActivateTab::No });

        if (data.get_bool("focused"sv).value_or(true))
            browser_model->activate_window(id);

        auto const* window = browser_model->window(id);
        if (!window)
            return JsonValue {};
        return JsonValue { window_to_json(*window, extension, true) };
    }

    if (name == "windows.update"sv) {
        auto const* window = TRY(window_argument(0));
        auto id = window->id;
        auto info = object_argument(args, 1);
        auto& lifecycle = Xenon::WindowLifecycle::the();
        auto geometry = window_geometry(*window);

        auto left = info.get_i32("left"sv);
        auto top = info.get_i32("top"sv);
        if (left.has_value() || top.has_value())
            lifecycle.request_window_frame(id, XENON_WINDOW_FRAME_MOVE, { left.value_or(geometry.x), top.value_or(geometry.y) });
        auto width = info.get_i32("width"sv);
        auto height = info.get_i32("height"sv);
        if (width.has_value() || height.has_value())
            lifecycle.request_window_frame(id, XENON_WINDOW_FRAME_RESIZE, {}, { width.value_or(geometry.width), height.value_or(geometry.height) });

        if (auto window_state = info.get_string("state"sv); window_state.has_value()) {
            if (*window_state == "maximized"sv)
                lifecycle.request_window_frame(id, XENON_WINDOW_FRAME_MAXIMIZE);
            else if (*window_state == "minimized"sv)
                lifecycle.request_window_frame(id, XENON_WINDOW_FRAME_MINIMIZE);
            else if (*window_state == "normal"sv)
                lifecycle.request_window_frame(id, XENON_WINDOW_FRAME_RESTORE);
        }
        if (info.get_bool("focused"sv).value_or(false) || info.get_bool("drawAttention"sv).value_or(false))
            browser_model->activate_window(id);
        window = browser_model->window(id);
        if (!window)
            return JsonValue {};
        return JsonValue { window_to_json(*window, extension, false) };
    }

    if (name == "windows.remove"sv) {
        auto const* window = TRY(window_argument(0));
        Xenon::WindowLifecycle::the().request_close(window->id);
        return JsonValue {};
    }

    return MUST(String::formatted("{} isn't supported yet.", name));
}

static Optional<Gfx::Color> color_from_json(JsonValue const& value)
{
    if (value.is_string()) {
        auto string = value.as_string().bytes_as_string_view();
        if (string.length() == 9 && string.starts_with('#')) {
            auto rgba = string.substring_view(1).to_number<u32>(TrimWhitespace::No, 16);
            if (!rgba.has_value())
                return {};
            return Gfx::Color((*rgba >> 24) & 0xff, (*rgba >> 16) & 0xff, (*rgba >> 8) & 0xff, *rgba & 0xff);
        }
        return Gfx::Color::from_string(string);
    }
    if (value.is_array() && value.as_array().size() == 4) {
        auto const& values = value.as_array();
        auto component = [&](size_t i) { return static_cast<u8>(clamp(static_cast<int>(values.at(i).get_double_with_precision_loss().value_or(0)), 0, 255)); };
        return Gfx::Color(component(0), component(1), component(2), component(3));
    }
    return {};
}

static JsonArray color_to_json(Gfx::Color color)
{
    JsonArray array;
    array.must_append(color.red());
    array.must_append(color.green());
    array.must_append(color.blue());
    array.must_append(color.alpha());
    return array;
}

static ErrorOr<JsonValue, String> handle_action_call(Extension const& extension, StringView name, JsonArray const& args)
{
    if (!extension.manifest().action.has_value())
        return "The extension has no action."_string;

    auto details = object_argument(args, 0);
    Optional<i64> tab = details.get_i64("tabId"sv);
    if (name.is_one_of("action.enable"sv, "action.disable"sv, "action.isEnabled"sv))
        tab = integer_argument(args, 0);
    if (tab.has_value() && !find_tab(*tab).has_value())
        return MUST(String::formatted("No tab with id: {}.", *tab));

    auto& action = state().actions.ensure(extension.id());
    auto& properties = tab.has_value() ? action.tabs.ensure(*tab) : action.all_tabs;

    auto appearance = appearance_for(extension, tab.map([](i64 id) { return static_cast<XenonViewId>(id); }));

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
        return JsonValue { color_to_json(appearance.badge_text_color.value_or(Gfx::Color::White)) };

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
            properties.icon_path = trimmed_path(path->as_string().bytes_as_string_view());
        } else if (path->is_object()) {
            Vector<Icon> icons;
            path->as_object().for_each_member([&](String const& size, JsonValue const& icon) {
                if (icon.is_string())
                    icons.append({ .size = size.to_number<u32>().value_or(0), .path = icon.as_string() });
            });
            quick_sort(icons, [](auto const& a, auto const& b) { return a.size < b.size; });
            auto best = best_icon(icons, TOOLBAR_ICON_PIXELS);
            properties.icon_path = best.has_value() ? Optional<String> { trimmed_path(best->bytes_as_string_view()) } : Optional<String> {};
        }
        return changed();
    }

    if (name == "action.setPopup"sv) {
        auto popup = details.get_string("popup"sv).value_or({});
        properties.popup = trimmed_path(popup.bytes_as_string_view());
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
        open_popup_in_focused_window(extension);
        return JsonValue {};
    }

    return MUST(String::formatted("{} isn't supported yet.", name));
}

ErrorOr<JsonValue, String> handle_call(Extension const& extension, Optional<u64> caller_web_view_id, StringView name, JsonArray const& args)
{
    auto caller_view_id = xenon_view_id(caller_web_view_id);
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

void prompt_for_permissions(Extension const& extension, Optional<u64> caller_web_view_id, Vector<String> const& warnings, Function<void(bool granted)> on_answer)
{
    auto caller_view_id = xenon_view_id(caller_web_view_id);
    if (WebView::Application::browser_options().headless_mode.has_value() || !state().callbacks.permission_prompt) {
        warnln("Allow “{}” to:", extension.manifest().name);
        for (auto const& warning : warnings)
            warnln("• {}", warning);
        on_answer(false);
        return;
    }

    auto id = state().next_prompt_id++;
    state().permission_prompts.set(id, move(on_answer));

    Vector<XenonStr> lines;
    lines.ensure_capacity(warnings.size());
    for (auto const& warning : warnings)
        lines.append(xenon_str(warning));

    XenonViewId popup = 0;
    XenonViewId tab = 0;
    if (caller_view_id.has_value()) {
        if (auto shown = state().popups.get(*caller_view_id); shown.has_value()) {
            popup = *caller_view_id;
            tab = shown->tab;
        } else if (find_tab(static_cast<i64>(*caller_view_id)).has_value()) {
            tab = *caller_view_id;
        }
    }
    if (tab == 0)
        tab = active_tab_of(focused_window()).value_or(0);

    XenonExtensionPermissionPrompt prompt {
        .id = id,
        .extension_name = xenon_str(extension.manifest().name),
        .warnings = lines.data(),
        .warning_count = lines.size(),
        .tab = tab,
        .popup = popup,
    };
    notify(&XenonExtensionCallbacks::permission_prompt, static_cast<XenonExtensionPermissionPrompt const*>(&prompt));
}

}

namespace Xenon::ExtensionsHost {

using namespace Ladybird::Extensions;
using namespace Ladybird::Extensions::Browser;

void set_callbacks(XenonExtensionCallbacks const& callbacks)
{
    state().callbacks = callbacks;
}

void start()
{
    if (exchange(state().started, true))
        return;

    Registry::the().on_extensions_change([] {
        if (!state().started)
            return false;
        report_extensions();
        report_commands();
        notify_action_listeners();
        return true;
    });
    Ladybird::PasswordManagers::the().on_change([] {
        if (!state().started)
            return false;
        report_password_managers();
        return true;
    });

    report_extensions();
    report_commands();
    report_password_managers();
}

void stop()
{
    state().popups.clear();
    state().popup_windows.clear();
    state().started = false;
}

void window_opened(BrowserModel::Window const& window)
{
    for (auto const& extension : Registry::the().extensions()) {
        JsonArray args;
        args.must_append(window_to_json(window, extension, false));
        Router::the().dispatch_event(extension, "windows.onCreated"sv, move(args));
    }
}

void window_closed(BrowserModel::Window const& window)
{
    for (auto const& [view, popup] : state().popups) {
        if (popup.window == window.id) {
            auto id = view;
            Core::deferred_invoke([id] { close_popup(id); });
        }
    }
    state().popup_windows.remove(window.id);
    JsonArray args;
    args.must_append(static_cast<i64>(window.id));
    Router::the().dispatch_event_to_all("windows.onRemoved"sv, move(args));
}

void window_activated(BrowserModel::Window const& window)
{
    JsonArray args;
    args.must_append(static_cast<i64>(window.id));
    Router::the().dispatch_event_to_all("windows.onFocusChanged"sv, move(args));
    notify_action_listeners();
}

void tab_inserted(BrowserModel::Window const& window, BrowserModel::Tab const& tab, size_t)
{
    dispatch_tab_event("tabs.onCreated"sv, tab, window);
}

void tab_removed(BrowserModel::Window const& window, BrowserModel::Tab const& tab)
{
    auto id = static_cast<i64>(tab.view);
    for (auto& [extension_id, action] : state().actions)
        action.tabs.remove(id);
    for (auto const& [view, popup] : state().popups) {
        if (popup.tab == tab.view) {
            auto popup_view = view;
            Core::deferred_invoke([popup_view] { close_popup(popup_view); });
        }
    }

    JsonObject remove_info;
    remove_info.set("windowId"sv, static_cast<i64>(window.id));
    remove_info.set("isWindowClosing"sv, window.tabs.size() <= 1);
    JsonArray args;
    args.must_append(id);
    args.must_append(move(remove_info));
    Router::the().dispatch_event_to_all("tabs.onRemoved"sv, move(args));
}

void tab_moved(BrowserModel::Tab const& tab, XenonWindowId from, size_t index)
{
    auto id = static_cast<i64>(tab.view);
    if (from == tab.window) {
        JsonObject move_info;
        move_info.set("windowId"sv, static_cast<i64>(tab.window));
        move_info.set("toIndex"sv, static_cast<i64>(index));
        JsonArray args;
        args.must_append(id);
        args.must_append(move(move_info));
        Router::the().dispatch_event_to_all("tabs.onMoved"sv, move(args));
        return;
    }

    JsonObject detach_info;
    detach_info.set("oldWindowId"sv, static_cast<i64>(from));
    JsonArray detach_args;
    detach_args.must_append(id);
    detach_args.must_append(move(detach_info));
    Router::the().dispatch_event_to_all("tabs.onDetached"sv, move(detach_args));

    JsonObject attach_info;
    attach_info.set("newWindowId"sv, static_cast<i64>(tab.window));
    attach_info.set("newPosition"sv, static_cast<i64>(index));
    JsonArray attach_args;
    attach_args.must_append(id);
    attach_args.must_append(move(attach_info));
    Router::the().dispatch_event_to_all("tabs.onAttached"sv, move(attach_args));
}

void tab_activated(BrowserModel::Window const& window, BrowserModel::Tab const& tab)
{
    JsonObject info;
    info.set("tabId"sv, static_cast<i64>(tab.view));
    info.set("windowId"sv, static_cast<i64>(window.id));
    JsonArray args;
    args.must_append(move(info));
    Router::the().dispatch_event_to_all("tabs.onActivated"sv, move(args));

    if (state().started)
        report_actions(tab.view);
}

void tab_pinned_changed(BrowserModel::Tab const& tab)
{
    JsonObject change;
    change.set("pinned"sv, tab.pinned);
    dispatch_tab_updated(tab.view, change);
}

void view_title_changed(View& view)
{
    if (!tab_view(view.id()))
        return;
    JsonObject change;
    change.set("title"sv, view.title().to_utf8());
    dispatch_tab_updated(view.id(), change);
}

void view_loading_changed(View& view, bool is_loading)
{
    if (!tab_view(view.id()))
        return;
    JsonObject change;
    change.set("status"sv, is_loading ? "loading"sv : "complete"sv);
    if (is_loading)
        change.set("url"sv, view.url().serialize());
    dispatch_tab_updated(view.id(), change);
}

void action_clicked(XenonViewId tab, StringView extension_id)
{
    auto extension = Registry::the().extension(extension_id);
    if (!extension || !extension->manifest().action.has_value())
        return;

    for (auto const& [view, popup] : state().popups) {
        if (popup.extension_id == extension_id) {
            close_popup(view);
            return;
        }
    }

    auto appearance = appearance_for(*extension, tab);
    if (!appearance.enabled)
        return;
    if (!appearance.popup.has_value()) {
        action_clicked_in_tab(*extension, tab);
        return;
    }
    open_popup(*extension, tab, *appearance.popup);
}

void close_popup(XenonViewId popup)
{
    Browser::close_popup(popup);
}

void answer_permissions(XenonPromptId id, bool granted)
{
    if (auto answer = state().permission_prompts.take(id); answer.has_value())
        (*answer)(granted);
}

void run_command(XenonViewId tab, StringView extension_id, StringView command)
{
    auto extension = Registry::the().extension(extension_id);
    if (!extension)
        return;

    if (command == "_execute_action"sv) {
        auto appearance = appearance_for(*extension, tab);
        if (appearance.popup.has_value())
            open_popup(*extension, tab, *appearance.popup);
        else
            action_clicked_in_tab(*extension, tab);
        return;
    }

    JsonArray args;
    args.must_append(MUST(String::from_utf8(command)));
    if (auto found = find_tab(static_cast<i64>(tab)); found.has_value())
        args.must_append(tab_to_json(*found->tab, *found->window, *extension));
    Router::the().dispatch_event(*extension, "commands.onCommand"sv, move(args));
}

void open_options(StringView extension_id)
{
    auto extension = Registry::the().extension(extension_id);
    if (!extension || !extension->manifest().options_page.has_value())
        return;
    open_tab(extension->url_for(*extension->manifest().options_page));
}

void reload(StringView extension_id)
{
    if (auto result = Registry::the().reload(extension_id); result.is_error())
        report_error("Couldn't reload the extension"sv, result.error());
}

void remove(StringView extension_id)
{
    for (auto const& [view, popup] : state().popups) {
        if (popup.extension_id == extension_id) {
            auto popup_view = view;
            Core::deferred_invoke([popup_view] { close_popup(popup_view); });
        }
    }
    state().actions.remove(MUST(String::from_utf8(extension_id)));
    Registry::the().remove(extension_id);
}

void load_unpacked(StringView directory)
{
    if (auto result = Registry::the().load_unpacked(directory, Registry::Persist::Yes); result.is_error())
        report_error("Couldn't load the extension"sv, result.error());
}

void choose_password_manager(StringView id)
{
    Ladybird::PasswordManagers::the().choose(id.is_empty() ? Optional<StringView> {} : Optional<StringView> { id });
}

void show_extensions()
{
    report_extensions();
    report_password_managers();
    notify(&XenonExtensionCallbacks::show_extensions);
}

}
