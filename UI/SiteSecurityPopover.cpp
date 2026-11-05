/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/JsonArray.h>
#include <LibWebView/ViewImplementation.h>
#include <UI/BrandColors.h>
#include <UI/ChromeStyle.h>
#include <UI/SiteSecurity.h>
#include <UI/SiteSecurityPopover.h>
#include <UI/StringUtils.h>

#include <QDateTime>
#include <QFontDatabase>
#include <QGridLayout>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QLocale>
#include <QMouseEvent>
#include <QScreen>
#include <QStringList>
#include <QStyle>
#include <QTimeZone>
#include <QToolButton>
#include <QVBoxLayout>

namespace Ladybird {

static constexpr int CARD_WIDTH = 340;
static constexpr int SHADOW_MARGIN = 24;
static constexpr int ANCHOR_GAP = 6;
static constexpr int ANCHOR_LEADING_OFFSET = 8;
static constexpr int FINGERPRINT_BYTES_PER_LINE = 8;
static constexpr auto OBJECT_NAME = "XenonSiteSecurity";

QString format_certificate_date(i64 milliseconds_since_epoch)
{
    auto date = QDateTime::fromMSecsSinceEpoch(milliseconds_since_epoch, QTimeZone::UTC).date();
    return QLocale().toString(date, "d MMMM yyyy");
}

QString fingerprint_in_lines(QString const& fingerprint)
{
    auto bytes = fingerprint.split(':');
    QStringList lines;
    for (qsizetype i = 0; i < bytes.size(); i += FINGERPRINT_BYTES_PER_LINE)
        lines.append(bytes.mid(i, FINGERPRINT_BYTES_PER_LINE).join(':'));
    return lines.join('\n');
}

static QLabel* make_label(QWidget* parent, char const* object_name)
{
    auto* label = new QLabel(parent);
    label->setObjectName(object_name);
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    return label;
}

static QString string(JsonObject const& object, StringView key)
{
    auto value = object.get_string(key);
    return value.has_value() ? qstring_from_ak_string(*value) : QString {};
}

static QString name_with_organization(JsonObject const& certificate, StringView name_key, StringView organization_key)
{
    auto name = string(certificate, name_key);
    auto organization = string(certificate, organization_key);
    if (name.isEmpty())
        return organization;
    if (organization.isEmpty() || organization == name)
        return name;
    return QString("%1\n%2").arg(name, organization);
}

SiteSecurityPopover::SiteSecurityPopover(QWidget* parent)
    : Popover(parent)
{
    setObjectName(OBJECT_NAME);
    setAttribute(Qt::WA_NoMouseReplay);
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName("Site information");
    card().setObjectName("XenonSiteSecurityPopover");
    card().setFixedWidth(CARD_WIDTH);

    auto* layout = new QVBoxLayout(&card());
    layout->setContentsMargins(16, 14, 16, 16);
    layout->setSpacing(6);

    m_host = make_label(&card(), "XenonSiteSecurityHost");
    m_host->setWordWrap(false);
    m_host->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    layout->addWidget(m_host);

    m_state = make_label(&card(), "XenonSiteSecurityState");
    layout->addWidget(m_state);

    m_summary = make_label(&card(), "XenonSiteSecuritySummary");
    layout->addWidget(m_summary);

    m_sections = new QVBoxLayout;
    m_sections->setContentsMargins(0, 6, 0, 0);
    m_sections->setSpacing(12);
    layout->addLayout(m_sections);
}

void SiteSecurityPopover::toggle(QWidget& parent, QToolButton& anchor, WebView::ViewImplementation& view)
{
    auto* popover = static_cast<SiteSecurityPopover*>(parent.findChild<QWidget*>(OBJECT_NAME, Qt::FindDirectChildrenOnly));
    if (popover && popover->isVisible()) {
        popover->close();
        return;
    }

    auto url = view.url();
    if (!SiteSecurity::applies_to(url))
        return;

    if (!popover)
        popover = new SiteSecurityPopover(&parent);
    popover->on_visibility_change = [anchor = QPointer(&anchor)](bool visible) {
        if (anchor)
            anchor->setDown(visible);
    };

    popover->update_chrome_style(parent.palette());
    popover->set_description(SiteSecurity::describe(url, {}, SiteSecurity::has_certificate_exception(view.session(), url)), Details::Loading);
    popover->place_below(anchor);
    popover->show();
    popover->place_below(anchor);
    popover->raise();
    popover->setFocus(Qt::PopupFocusReason);

    auto generation = ++popover->m_request_generation;
    SiteSecurity::request_description(view, [popover = QPointer(popover), generation](JsonObject description) {
        if (!popover || popover->m_request_generation != generation || !popover->isVisible())
            return;
        popover->set_description(description, Details::Final);
        if (popover->m_anchor)
            popover->place_below(*popover->m_anchor);
    });
}

void SiteSecurityPopover::clear_sections()
{
    while (auto* item = m_sections->takeAt(0)) {
        if (auto* widget = item->widget())
            delete widget;
        delete item;
    }
}

QVBoxLayout& SiteSecurityPopover::add_section(QString const& heading)
{
    auto* section = new QWidget(&card());
    section->setObjectName("XenonSiteSecuritySection");
    auto* layout = new QVBoxLayout(section);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    auto* heading_label = make_label(section, "XenonSiteSecurityHeading");
    heading_label->setText(heading);
    layout->addWidget(heading_label);

    m_sections->addWidget(section);
    return *layout;
}

void SiteSecurityPopover::add_row(QVBoxLayout& section, QString const& name, QString const& value, char const* value_object_name)
{
    if (value.isEmpty())
        return;

    auto* row = new QWidget(section.parentWidget());
    auto* layout = new QGridLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setHorizontalSpacing(12);
    layout->setColumnMinimumWidth(0, 92);
    layout->setColumnStretch(1, 1);

    auto* name_label = make_label(row, "XenonSiteSecurityName");
    name_label->setText(name);
    layout->addWidget(name_label, 0, 0, Qt::AlignTop);

    auto* value_label = make_label(row, value_object_name);
    value_label->setText(value);
    value_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    if (qstrcmp(value_object_name, "XenonSiteSecurityFingerprint") == 0) {
        value_label->setWordWrap(false);
        value_label->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    }
    layout->addWidget(value_label, 0, 1, Qt::AlignTop);

    section.addWidget(row);
}

void SiteSecurityPopover::set_description(JsonObject const& description, Details details)
{
    clear_sections();

    m_host->setText(string(description, "host"sv));
    m_host->setToolTip(m_host->text());

    auto state = description.get_string("state"sv).value_or({});
    m_state->setProperty("warning", state != "secure"sv);
    m_state->style()->unpolish(m_state);
    m_state->style()->polish(m_state);

    if (state == "notSecure"sv) {
        m_state->setText("Connection isn't secure");
        m_summary->setText("Don't enter passwords or payment details on this site. Others on the network can see and change what you send and receive here.");
    } else if (state == "acceptedCertificate"sv) {
        m_state->setText("Certificate not verified");
        m_summary->setText("You chose to proceed to this site although its certificate couldn't be verified, so Xenon can't tell that the connection is really to this site.");
    } else {
        m_state->setText("Connection is secure");
        m_summary->setText("Information you send to or receive from this site is private to it.");
    }

    if (state != "notSecure"sv) {
        if (!description.has("protocol"sv)) {
            auto& section = add_section("Connection");
            auto* note = make_label(section.parentWidget(), "XenonSiteSecurityNote");
            note->setText(details == Details::Loading ? "Looking up this connection…" : "Xenon has no details of this connection. The page may have come from the cache.");
            section.addWidget(note);
        } else {
            auto& connection = add_section("Connection");
            add_row(connection, "Protocol", string(description, "protocol"sv));
            add_row(connection, "Cipher", string(description, "cipher"sv));
        }
    }

    if (auto certificate = description.get_object("certificate"sv); certificate.has_value()) {
        auto& section = add_section("Certificate");
        add_row(section, "Issued to", name_with_organization(*certificate, "subject"sv, "subjectOrganization"sv));
        add_row(section, "Issued by", name_with_organization(*certificate, "issuer"sv, "issuerOrganization"sv));
        if (auto valid_from = certificate->get_i64("validFrom"sv); valid_from.has_value())
            add_row(section, "Valid from", format_certificate_date(*valid_from));
        if (auto valid_until = certificate->get_i64("validUntil"sv); valid_until.has_value())
            add_row(section, "Valid until", format_certificate_date(*valid_until));
        add_row(section, "SHA-256", fingerprint_in_lines(string(*certificate, "fingerprint"sv)), "XenonSiteSecurityFingerprint");
    }

    if (auto chain = description.get_array("chain"sv); chain.has_value() && !chain->is_empty()) {
        auto& section = add_section("Certificate chain");
        auto depth = 0;
        for (size_t i = chain->size(); i-- > 0;) {
            auto const& name = chain->at(i);
            if (!name.is_string())
                continue;
            auto* link = make_label(section.parentWidget(), "XenonSiteSecurityChainLink");
            link->setText(qstring_from_ak_string(name.as_string()));
            link->setContentsMargins(depth++ * 12, 0, 0, 0);
            section.addWidget(link);
        }
    }

    card().layout()->activate();
    layout()->activate();
    adjustSize();
}

void SiteSecurityPopover::update_chrome_style(QPalette const& palette)
{
    auto active_palette = palette;
    active_palette.setCurrentColorGroup(QPalette::Active);
    setPalette(active_palette);

    auto text_color = ChromeStyle::chrome_text(active_palette);
    auto dark = ChromeStyle::is_dark(active_palette);
    auto text = ChromeStyle::style_sheet_color(text_color);
    auto muted = ChromeStyle::style_sheet_color(ChromeStyle::mix(text_color, ChromeStyle::chrome_surface(active_palette), 0.45));
    auto surface = ChromeStyle::style_sheet_color(ChromeStyle::chrome_surface(active_palette));
    auto border = ChromeStyle::style_sheet_color(ChromeStyle::chrome_border(active_palette));
    auto warning = ChromeStyle::style_sheet_color(QColor(dark ? BrandColors::Rose60 : BrandColors::Rose500));

    setStyleSheet(qformatted(R"(
QFrame#XenonSiteSecurityPopover {{
    color: {0};
    background: {2};
    border: 1px solid {3};
    border-radius: 10px;
}}
QFrame#XenonSiteSecurityPopover QLabel {{
    color: {0};
}}
QFrame#XenonSiteSecurityPopover QLabel#XenonSiteSecurityHost {{
    font-weight: 600;
}}
QFrame#XenonSiteSecurityPopover QLabel#XenonSiteSecurityState {{
    font-weight: 600;
}}
QFrame#XenonSiteSecurityPopover QLabel#XenonSiteSecurityState[warning="true"] {{
    color: {4};
}}
QFrame#XenonSiteSecurityPopover QLabel#XenonSiteSecuritySummary,
QFrame#XenonSiteSecurityPopover QLabel#XenonSiteSecurityName,
QFrame#XenonSiteSecurityPopover QLabel#XenonSiteSecurityNote,
QFrame#XenonSiteSecurityPopover QLabel#XenonSiteSecurityHeading {{
    color: {1};
}}
QFrame#XenonSiteSecurityPopover QLabel#XenonSiteSecurityHeading {{
    font-size: 11px;
    font-weight: 600;
}}
QFrame#XenonSiteSecurityPopover QLabel#XenonSiteSecurityFingerprint {{
    font-size: 11px;
}}
)",
        text, muted, surface, border, warning));
}

void SiteSecurityPopover::place_below(QWidget& anchor)
{
    m_anchor = &anchor;
    auto card_size = size().shrunkBy({ SHADOW_MARGIN, SHADOW_MARGIN, SHADOW_MARGIN, SHADOW_MARGIN });
    auto anchor_position = anchor.mapToGlobal(anchor.rect().bottomLeft());
    auto card_position = QPoint(anchor_position.x() - ANCHOR_LEADING_OFFSET, anchor_position.y() + ANCHOR_GAP);

    if (auto* screen = QGuiApplication::screenAt(anchor_position)) {
        auto available_geometry = screen->availableGeometry();
        if (card_position.x() + card_size.width() > available_geometry.right())
            card_position.setX(available_geometry.right() - card_size.width() + 1);
        if (card_position.x() < available_geometry.left())
            card_position.setX(available_geometry.left());
        if (card_position.y() + card_size.height() > available_geometry.bottom())
            card_position.setY(available_geometry.bottom() - card_size.height() + 1);
    }

    move(card_position - QPoint(SHADOW_MARGIN, SHADOW_MARGIN));
}

void SiteSecurityPopover::mousePressEvent(QMouseEvent* event)
{
    if (!card().geometry().contains(event->position().toPoint())) {
        close();
        event->accept();
        return;
    }

    Popover::mousePressEvent(event);
}

void SiteSecurityPopover::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        close();
        event->accept();
        return;
    }

    Popover::keyPressEvent(event);
}

}
