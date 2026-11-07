/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibWebView/Application.h>
#include <UI/XenonArguments.h>

namespace Xenon {

class View;

class Application final : public WebView::Application {
    WEB_VIEW_APPLICATION(Application)

public:
    virtual ~Application() override;

    void did_create_view(View&);

private:
    Application();

    virtual void create_platform_arguments(Core::ArgsParser&) override;
    virtual void create_platform_options(WebView::BrowserOptions&, WebView::RequestServerOptions&, WebView::WebContentOptions&) override;
    virtual void create_platform_actions() override;

    virtual WebView::SessionLaunchPolicy launch_restore_policy() const override;
    virtual void on_devtools_enabled() const override;
    virtual void on_devtools_disabled() const override;

    virtual Optional<WebView::ViewImplementation&> active_web_view() const override;
    virtual Optional<WebView::ViewImplementation&> open_blank_new_tab(Web::HTML::ActivateTab) const override;
    virtual bool supports_private_browsing_windows() const override { return true; }
    virtual bool supports_picture_in_picture() const override;
    virtual NonnullRefPtr<Core::Promise<ByteString>> ask_user_for_download_path(ByteString const& file) const override;
    virtual Optional<String> ui_font_family() const override;
    virtual Optional<String> system_font_family() const override;
    virtual void resolve_external_url_handler(URL::URL const&, WebView::ExternalURLHandlerCallback) const override;
    virtual OwnPtr<WebView::PictureInPictureWindow> create_picture_in_picture_window(WebView::WebContentPage&, WebView::CanonicalTraversable&, Gfx::IntSize video_size) override;
    virtual void open_url_in_new_window(URL::URL const&, WebView::IsPrivate) override;
    virtual void open_navigation_in_new_window(Web::HTML::PreparedNavigationDescriptor, WebView::IsPrivate) override;
    virtual void open_download(WebView::FileDownloader::Download const&) const override;
    virtual void show_download_in_folder(WebView::FileDownloader::Download const&) const override;
    virtual void display_download_confirmation_dialog(StringView, LexicalPath const&) const override { }
    virtual NonnullRefPtr<BookmarkPromise> display_add_bookmark_dialog(Optional<String const&> target_folder_id = {}) const override;
    virtual NonnullRefPtr<BookmarkPromise> display_edit_bookmark_dialog(WebView::BookmarkItem const& current_bookmark, Optional<String const&> parent_folder_id) const override;
    virtual NonnullRefPtr<BookmarkPromise> display_add_bookmark_folder_dialog(Optional<String const&> default_title = {}, Optional<String const&> target_folder_id = {}) const override;
    virtual NonnullRefPtr<BookmarkPromise> display_edit_bookmark_folder_dialog(WebView::BookmarkItem const& current_folder, Optional<String const&> parent_folder_id) const override;
    virtual void show_bookmark_context_menu(Gfx::IntPoint, Optional<WebView::BookmarkItem const&>, Optional<String const&> target_folder_id, Optional<String const&> parent_folder_id) override;
    virtual Optional<BookmarkID> bookmark_item_id_for_context_menu() const override;

#ifdef AK_OS_MACOS
    virtual Utf16String clipboard_text(ClipboardType) const override;
    virtual void set_clipboard_text(String, ClipboardType = ClipboardType::Text) override;
    virtual Web::Clipboard::SystemClipboardItem clipboard_item() const override;
    virtual void insert_clipboard_item(Web::Clipboard::SystemClipboardItem) override;
#endif

    void start_xenon_services();

    Ladybird::XenonArguments m_xenon_arguments;
};

}
