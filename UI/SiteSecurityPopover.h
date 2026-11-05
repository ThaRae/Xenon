/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/JsonObject.h>
#include <AK/Types.h>
#include <LibWebView/Forward.h>
#include <UI/Popover.h>

#include <QPointer>

class QKeyEvent;
class QLabel;
class QMouseEvent;
class QToolButton;
class QVBoxLayout;

namespace Ladybird {

QString format_certificate_date(i64 milliseconds_since_epoch);
QString fingerprint_in_lines(QString const& fingerprint);

class SiteSecurityPopover final : public Popover {
    AK_ALLOC_WITH_KMALLOC;

public:
    enum class Details : u8 {
        Loading,
        Final,
    };

    explicit SiteSecurityPopover(QWidget* parent);

    static void toggle(QWidget& parent, QToolButton& anchor, WebView::ViewImplementation&);

    void set_description(JsonObject const&, Details);
    void update_chrome_style(QPalette const&);
    void place_below(QWidget& anchor);

private:
    virtual void keyPressEvent(QKeyEvent*) override;
    virtual void mousePressEvent(QMouseEvent*) override;

    void clear_sections();
    QVBoxLayout& add_section(QString const& heading);
    void add_row(QVBoxLayout&, QString const& name, QString const& value, char const* value_object_name = "XenonSiteSecurityValue");

    QLabel* m_host { nullptr };
    QLabel* m_state { nullptr };
    QLabel* m_summary { nullptr };
    QVBoxLayout* m_sections { nullptr };
    QPointer<QWidget> m_anchor;
    u64 m_request_generation { 0 };
};

}
