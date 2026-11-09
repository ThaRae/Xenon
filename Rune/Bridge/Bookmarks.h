/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/NonnullRefPtr.h>
#include <AK/Optional.h>
#include <AK/String.h>
#include <LibGfx/Point.h>
#include <LibURL/URL.h>
#include <LibWebView/Application.h>
#include <LibWebView/BookmarkStore.h>

namespace Xenon::Bookmarks {

enum class DialogKind : u8 {
    AddBookmark,
    EditBookmark,
    AddFolder,
    EditFolder,
};

struct DialogRequest {
    DialogKind kind { DialogKind::AddBookmark };
    Optional<URL::URL> url;
    Optional<String> title;
    Optional<String> favicon_hash;
    Optional<String> selected_folder_id;
    Optional<String> excluded_folder_id;
};

NonnullRefPtr<WebView::Application::BookmarkPromise> show_dialog(DialogRequest);

void show_context_menu_at_page_position(Gfx::IntPoint, Optional<WebView::BookmarkItem const&>, Optional<String const&> target_folder_id, Optional<String const&> parent_folder_id);
Optional<WebView::Application::BookmarkID> item_for_context_menu();

}
