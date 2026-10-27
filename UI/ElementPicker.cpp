/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWebView/Application.h>
#include <UI/ElementPicker.h>
#include <UI/StringUtils.h>

#include <QInputDialog>
#include <QLineEdit>

namespace Ladybird::ElementPicker {

void confirm_filter(QWidget& parent, String const& filter)
{
    bool accepted = false;
    auto text = QInputDialog::getText(&parent, "Block Element",
        "Hide this element on this site from now on. The filter uses uBlock Origin's syntax, and you can edit it, or remove it later in Settings > Content Blocking.",
        QLineEdit::Normal, qstring_from_ak_string(filter), &accepted);
    if (!accepted)
        return;

    auto edited_filter = ak_string_from_qstring(text);
    WebView::Application::settings().add_custom_content_blocker_filter(edited_filter);
}

}
