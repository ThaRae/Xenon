/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/ByteBuffer.h>
#include <AK/Vector.h>
#include <AK/WeakPtr.h>
#include <LibWebView/Application.h>
#include <LibWebView/BookmarkStore.h>
#include <LibWebView/BrowsingSession.h>
#include <LibWebView/FaviconStore.h>
#include <LibWebView/Settings.h>
#include <LibWebView/URL.h>
#include <Rune/Bridge/Bookmarks.h>
#include <Rune/Bridge/Engine.h>
#include <Rune/Bridge/View.h>
#include <Rune/Bridge/XenonBridgeBookmarks.h>

namespace Xenon::Bookmarks {

static XenonBookmarksCallbacks s_callbacks {};

template<typename Callback, typename... Arguments>
static void notify(Callback XenonBookmarksCallbacks::* callback, Arguments... arguments)
{
    if (auto function = s_callbacks.*callback)
        function(s_callbacks.context, arguments...);
}

static XenonStr xenon_str(StringView string)
{
    return { string.characters_without_null_termination(), string.length() };
}

static StringView string_view_from(XenonStr string)
{
    if (!string.data || string.length == 0)
        return {};
    return { string.data, string.length };
}

static String string_from(XenonStr string)
{
    return String::from_utf8_with_replacement_character(string_view_from(string));
}

static Optional<String> non_empty(String string)
{
    if (string.is_empty())
        return {};
    return string;
}

struct BarEntry {
    XenonBookmarkItemKind kind { XENON_BOOKMARK_ITEM_BOOKMARK };
    String id;
    String title;
    String url;
    Optional<ByteBuffer> favicon_png;
};

static void collect_entries(ReadonlySpan<WebView::BookmarkItem> items, Vector<BarEntry>& entries)
{
    auto& favicons = *WebView::Application::default_session().favicon_store;
    for (auto const& item : items) {
        item.data.visit(
            [&](WebView::BookmarkItem::Bookmark const& bookmark) {
                entries.append({
                    .kind = XENON_BOOKMARK_ITEM_BOOKMARK,
                    .id = item.id,
                    .title = bookmark.title.value_or({}),
                    .url = bookmark.url.serialize(),
                    .favicon_png = bookmark.favicon_hash.has_value() ? favicons.favicon_png(*bookmark.favicon_hash) : OptionalNone {},
                });
            },
            [&](WebView::BookmarkItem::Folder const& folder) {
                entries.append({ .kind = XENON_BOOKMARK_ITEM_FOLDER_BEGIN, .id = item.id, .title = folder.title.value_or("(no title)"_string), .url = {}, .favicon_png = {} });
                collect_entries(folder.children, entries);
                entries.append({ .kind = XENON_BOOKMARK_ITEM_FOLDER_END, .id = item.id, .title = {}, .url = {}, .favicon_png = {} });
            });
    }
}

static void report_bookmarks()
{
    Vector<BarEntry> entries;
    collect_entries(WebView::Application::bookmark_store().root_items(), entries);

    Vector<XenonBookmarkItem> items;
    items.ensure_capacity(entries.size());
    for (auto const& entry : entries) {
        items.unchecked_append({
            .kind = static_cast<u32>(entry.kind),
            .id = xenon_str(entry.id),
            .title = xenon_str(entry.title),
            .url = xenon_str(entry.url),
            .favicon_png = entry.favicon_png.has_value() ? entry.favicon_png->data() : nullptr,
            .favicon_png_length = entry.favicon_png.has_value() ? entry.favicon_png->size() : 0,
        });
    }
    notify(&XenonBookmarksCallbacks::bookmarks_changed, static_cast<XenonBookmarkItem const*>(items.data()), items.size());
}

static XenonBookmarksBarVisibility bar_visibility(WebView::ShowBookmarksBar show_bookmarks_bar)
{
    switch (show_bookmarks_bar) {
    case WebView::ShowBookmarksBar::Always:
        return XENON_BOOKMARKS_BAR_ALWAYS;
    case WebView::ShowBookmarksBar::Never:
        return XENON_BOOKMARKS_BAR_NEVER;
    case WebView::ShowBookmarksBar::OnHover:
        return XENON_BOOKMARKS_BAR_ON_HOVER;
    }
    VERIFY_NOT_REACHED();
}

static u32 milliseconds_setting(WebView::ConfigVariableID id)
{
    return static_cast<u32>(max(0, WebView::Application::settings().config_variable_as_integer<int>(id)));
}

static void report_bar_settings()
{
    auto const& settings = WebView::Application::settings();
    XenonBookmarksBarSettings bar_settings {
        .visibility = static_cast<u32>(bar_visibility(settings.appearance().show_bookmarks_bar)),
        .hover_expand_delay_ms = milliseconds_setting(WebView::ConfigVariableID::VerticalTabsHoverExpandDelay),
        .hover_expand_animation_ms = milliseconds_setting(WebView::ConfigVariableID::VerticalTabsHoverExpandAnimationDuration),
    };
    notify(&XenonBookmarksCallbacks::bar_settings_changed, static_cast<XenonBookmarksBarSettings const*>(&bar_settings));
}

class StoreObserver final : public WebView::BookmarkStoreObserver {
public:
    AK_ALLOC_WITH_KMALLOC;

    virtual void bookmarks_changed() override { report_bookmarks(); }
};

class SettingsObserver final : public WebView::SettingsObserver {
public:
    AK_ALLOC_WITH_KMALLOC;

    virtual void appearance_changed() override { report_bar_settings(); }
    virtual void config_variable_changed(WebView::ConfigVariableID id) override
    {
        if (id == WebView::ConfigVariableID::VerticalTabsHoverExpandDelay || id == WebView::ConfigVariableID::VerticalTabsHoverExpandAnimationDuration)
            report_bar_settings();
    }
};

static void start()
{
    static bool started = false;
    if (!started) {
        started = true;
        new StoreObserver;
        new SettingsObserver;
    }
    report_bar_settings();
    report_bookmarks();
}

struct ContextMenuState {
    u64 next_menu_id { 1 };
    u64 shown_menu_id { 0 };
    Vector<WeakPtr<WebView::Action>> actions;
    Optional<WebView::Application::BookmarkID> selection;
};

static ContextMenuState& context_menu_state()
{
    static auto& state = *new ContextMenuState;
    return state;
}

static void show_context_menu(View& view, Optional<WebView::BookmarkItem const&> item, Optional<String> target_folder_id, Optional<String> parent_folder_id, Gfx::FloatPoint position, bool at_page_position)
{
    auto& state = context_menu_state();
    state.selection = WebView::Application::BookmarkID {
        .id = item.has_value() ? item->id : String {},
        .target_folder_id = move(target_folder_id),
        .parent_folder_id = move(parent_folder_id),
    };

    auto& menu = [&]() -> WebView::Menu& {
        if (item.has_value() && item->is_folder())
            return view.bookmark_folder_context_menu();
        if (item.has_value())
            return view.bookmark_context_menu();
        return view.bookmarks_bar_context_menu();
    }();

    state.actions.clear();
    Vector<XenonMenuItem> items;
    flatten_menu(menu, items, state.actions);
    drop_trailing_separator(items);
    if (items.is_empty())
        return;

    state.shown_menu_id = state.next_menu_id++;
    XenonMenu shown_menu {
        .id = state.shown_menu_id,
        .x = position.x(),
        .y = position.y(),
        .minimum_width = 0,
        .items = items.data(),
        .item_count = items.size(),
    };
    notify(&XenonBookmarksCallbacks::context_menu_requested, view.id(), static_cast<XenonMenu const*>(&shown_menu), at_page_position);
}

void show_context_menu_at_page_position(Gfx::IntPoint position, Optional<WebView::BookmarkItem const&> item, Optional<String const&> target_folder_id, Optional<String const&> parent_folder_id)
{
    auto* view = Engine::the().focused_view();
    if (!view)
        return;
    show_context_menu(*view, item, target_folder_id.copy(), parent_folder_id.copy(), position.to_type<float>(), true);
}

Optional<WebView::Application::BookmarkID> item_for_context_menu()
{
    if (!Engine::the().focused_view())
        return {};
    return context_menu_state().selection.value_or({});
}

static void activate_menu_item(u64 menu, u32 item)
{
    auto& state = context_menu_state();
    if (menu == 0 || menu != state.shown_menu_id || item >= state.actions.size())
        return;
    auto action = state.actions[item].strong_ref();
    if (!action || !action->enabled())
        return;
    if (action->is_checkable())
        action->set_checked(!action->checked());
    action->activate();
}

struct PendingDialog {
    u64 id { 0 };
    DialogKind kind { DialogKind::AddBookmark };
    Optional<String> favicon_hash;
    RefPtr<WebView::Application::BookmarkPromise> promise;
};

static Optional<PendingDialog>& pending_dialog()
{
    static auto& dialog = *new Optional<PendingDialog>;
    return dialog;
}

static bool is_bookmark_dialog(DialogKind kind)
{
    return kind == DialogKind::AddBookmark || kind == DialogKind::EditBookmark;
}

static XenonBookmarkDialogKind xenon_dialog_kind(DialogKind kind)
{
    switch (kind) {
    case DialogKind::AddBookmark:
        return XENON_BOOKMARK_DIALOG_ADD_BOOKMARK;
    case DialogKind::EditBookmark:
        return XENON_BOOKMARK_DIALOG_EDIT_BOOKMARK;
    case DialogKind::AddFolder:
        return XENON_BOOKMARK_DIALOG_ADD_FOLDER;
    case DialogKind::EditFolder:
        return XENON_BOOKMARK_DIALOG_EDIT_FOLDER;
    }
    VERIFY_NOT_REACHED();
}

struct FolderOption {
    String id;
    String path;
};

static void collect_folder_options(ReadonlySpan<WebView::BookmarkItem> items, StringView prefix, Optional<String> const& excluded_folder_id, Vector<FolderOption>& options)
{
    for (auto const& item : items) {
        if (!item.is_folder() || item.id == excluded_folder_id)
            continue;
        auto title = item.folder().title.value_or("(no title)"_string);
        auto path = prefix.is_empty() ? title : MUST(String::formatted("{} / {}", prefix, title));
        options.append({ .id = item.id, .path = path });
        collect_folder_options(item.folder().children, path, excluded_folder_id, options);
    }
}

static void cancel_pending_dialog()
{
    if (auto dialog = pending_dialog().release_value(); dialog.promise)
        dialog.promise->reject(Error::from_errno(ECANCELED));
}

NonnullRefPtr<WebView::Application::BookmarkPromise> show_dialog(DialogRequest request)
{
    static u64 next_dialog_id = 1;

    if (pending_dialog().has_value())
        cancel_pending_dialog();

    auto promise = WebView::Application::BookmarkPromise::construct();
    auto id = next_dialog_id++;
    pending_dialog() = PendingDialog { .id = id, .kind = request.kind, .favicon_hash = request.favicon_hash, .promise = promise };

    Vector<FolderOption> folder_options;
    collect_folder_options(WebView::Application::bookmark_store().root_items(), {}, request.excluded_folder_id, folder_options);
    Vector<XenonBookmarkFolderOption> folders;
    folders.ensure_capacity(folder_options.size());
    for (auto const& option : folder_options)
        folders.unchecked_append({ .id = xenon_str(option.id), .path = xenon_str(option.path) });

    auto url = request.url.has_value() ? request.url->serialize() : String {};
    auto title = request.title.value_or({});
    auto selected_folder_id = request.selected_folder_id.value_or({});
    XenonBookmarkDialog dialog {
        .id = id,
        .kind = static_cast<u32>(xenon_dialog_kind(request.kind)),
        .url = xenon_str(url),
        .title = xenon_str(title),
        .selected_folder_id = xenon_str(selected_folder_id),
        .folders = folders.data(),
        .folder_count = folders.size(),
    };
    notify(&XenonBookmarksCallbacks::dialog_opened, static_cast<XenonBookmarkDialog const*>(&dialog));
    return promise;
}

static void close_dialog(u64 id, bool accepted, String url_text, String title_text, String folder_id_text)
{
    auto& pending = pending_dialog();
    if (!pending.has_value() || pending->id != id)
        return;
    auto dialog = pending.release_value();
    auto& promise = *dialog.promise;

    if (!accepted) {
        promise.reject(Error::from_errno(ECANCELED));
        return;
    }

    auto title = non_empty(move(title_text));
    auto target_folder_id = non_empty(move(folder_id_text));

    if (!is_bookmark_dialog(dialog.kind)) {
        promise.resolve(WebView::Application::BookmarkDialogResult {
            .data = WebView::BookmarkItem::Folder { .title = move(title), .children = {} },
            .target_folder_id = move(target_folder_id),
        });
        return;
    }

    auto url = WebView::sanitize_url(url_text);
    if (!url.has_value()) {
        promise.reject(Error::from_errno(EINVAL));
        return;
    }
    promise.resolve(WebView::Application::BookmarkDialogResult {
        .data = WebView::BookmarkItem::Bookmark { .url = url.release_value(), .title = move(title), .favicon_hash = move(dialog.favicon_hash) },
        .target_folder_id = move(target_folder_id),
    });
}

static void open_bookmark(XenonViewId view_id, String const& id, u32 disposition)
{
    auto& application = WebView::Application::the();
    switch (disposition) {
    case XENON_BOOKMARK_OPEN_IN_NEW_TAB:
        application.open_bookmark_in_new_tab(id, Web::HTML::ActivateTab::Yes);
        return;
    case XENON_BOOKMARK_OPEN_IN_NEW_BACKGROUND_TAB:
        application.open_bookmark_in_new_tab(id, Web::HTML::ActivateTab::No);
        return;
    default:
        break;
    }

    auto item = WebView::Application::bookmark_store().find_item_by_id(id);
    if (!item.has_value() || !item->is_bookmark())
        return;
    auto url = item->bookmark().url;
    if (auto* view = Engine::the().view(view_id))
        view->load_from_user_input(url);
    else
        application.open_url_in_new_tab(url, Web::HTML::ActivateTab::Yes);
}

}

using namespace Xenon::Bookmarks;

void xenon_engine_set_bookmarks_callbacks(XenonBookmarksCallbacks const* callbacks)
{
    s_callbacks = callbacks ? *callbacks : XenonBookmarksCallbacks {};
}

void xenon_engine_bookmarks_start(void)
{
    Xenon::Engine::the().post([] { start(); });
}

void xenon_engine_open_bookmark(XenonViewId view, XenonStr id, uint32_t disposition)
{
    Xenon::Engine::the().post([view, id = string_from(id), disposition] { open_bookmark(view, id, disposition); });
}

void xenon_engine_open_bookmark_folder(XenonStr id)
{
    Xenon::Engine::the().post([id = string_from(id)] {
        WebView::Application::the().open_bookmark_folder_in_new_tabs(id);
    });
}

void xenon_engine_show_bookmark_context_menu(XenonViewId view_id, XenonStr id, XenonStr target_folder_id, XenonStr parent_folder_id, float x, float y)
{
    Xenon::Engine::the().post([view_id, id = string_from(id), target_folder_id = non_empty(string_from(target_folder_id)), parent_folder_id = non_empty(string_from(parent_folder_id)), x, y]() mutable {
        auto* view = Xenon::Engine::the().view(view_id);
        if (!view)
            return;
        Optional<WebView::BookmarkItem const&> item;
        if (!id.is_empty()) {
            item = WebView::Application::bookmark_store().find_item_by_id(id);
            if (!item.has_value())
                return;
        }
        show_context_menu(*view, item, move(target_folder_id), move(parent_folder_id), { x, y }, false);
    });
}

void xenon_engine_activate_bookmark_menu_item(uint64_t menu, uint32_t item)
{
    Xenon::Engine::the().post([menu, item] { activate_menu_item(menu, item); });
}

void xenon_engine_close_bookmark_dialog(uint64_t dialog, bool accepted, XenonStr url, XenonStr title, XenonStr folder_id)
{
    Xenon::Engine::the().post([dialog, accepted, url = string_from(url), title = string_from(title), folder_id = string_from(folder_id)]() mutable {
        close_dialog(dialog, accepted, move(url), move(title), move(folder_id));
    });
}
