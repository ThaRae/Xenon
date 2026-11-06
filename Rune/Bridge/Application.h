/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibWebView/Application.h>

namespace Xenon {

class Application final : public WebView::Application {
    WEB_VIEW_APPLICATION(Application)

public:
    virtual ~Application() override;

private:
    Application();

    virtual void create_platform_options(WebView::BrowserOptions&, WebView::RequestServerOptions&, WebView::WebContentOptions&) override;

    virtual Optional<WebView::ViewImplementation&> active_web_view() const override;
    virtual Optional<WebView::ViewImplementation&> open_blank_new_tab(Web::HTML::ActivateTab) const override;
};

}
