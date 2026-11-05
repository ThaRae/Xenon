/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/ByteBuffer.h>
#include <AK/ByteString.h>
#include <AK/JsonArray.h>
#include <AK/JsonObject.h>
#include <AK/NeverDestroyed.h>
#include <LibCore/Environment.h>
#include <LibCore/File.h>
#include <LibTest/TestCase.h>
#include <LibURL/Parser.h>
#include <UI/Icon.h>
#include <UI/SiteSecurity.h>
#include <UI/SiteSecurityPopover.h>

#include <QApplication>
#include <QFrame>
#include <QLabel>
#include <QPixmap>
#include <QStyleHints>

using namespace Ladybird;

static QApplication& application()
{
    static int argc = 1;
    static char program[] = "TestXenonSiteSecurity";
    static char* argv[] = { program, nullptr };
#if !defined(AK_OS_MACOS) && !defined(AK_OS_WINDOWS)
    if (!Core::Environment::has("DISPLAY"sv) && !Core::Environment::has("WAYLAND_DISPLAY"sv))
        qputenv("QT_QPA_PLATFORM", "offscreen");
#endif
    static NeverDestroyed<QApplication> application(argc, argv);
    return *application;
}

static ByteBuffer certificate(StringView name)
{
    auto path = ByteString::formatted("{}/Tests/CertificateErrors/{}.der", XENON_SOURCE_DIR, name);
    auto file = MUST(Core::File::open(path, Core::File::OpenMode::Read));
    return MUST(file->read_until_eof());
}

static URL::URL url(StringView string)
{
    return URL::Parser::basic_parse(string).release_value();
}

static Requests::SecureConnection connection(bool uses_certificate_exception)
{
    Requests::SecureConnection connection;
    connection.protocol = "TLSv1.3";
    connection.cipher = "TLS_AES_128_GCM_SHA256";
    connection.uses_certificate_exception = uses_certificate_exception;
    connection.certificate_chain.append(certificate("untrusted"sv));
    connection.certificate_chain.append(certificate("self-signed"sv));
    return connection;
}

static ByteString string(JsonObject const& object, StringView key)
{
    auto value = object.get_string(key);
    return value.has_value() ? value->to_byte_string() : ByteString {};
}

static Vector<ByteString> chain(JsonObject const& description)
{
    Vector<ByteString> names;
    if (auto array = description.get_array("chain"sv); array.has_value()) {
        for (auto const& value : array->values())
            names.append(value.as_string().to_byte_string());
    }
    return names;
}

static Vector<QString> labels_named(QWidget& parent, char const* name)
{
    Vector<QString> texts;
    for (auto* label : parent.findChildren<QLabel*>(name))
        texts.append(label->text());
    return texts;
}

static void save_screenshot(SiteSecurityPopover& popover, StringView name)
{
    auto directory = Core::Environment::get("XENON_SCREENSHOT_DIR"sv);
    if (!directory.has_value())
        return;
    popover.adjustSize();
    auto path = ByteString::formatted("{}/{}.png", *directory, name);
    EXPECT(popover.card().grab().save(QString::fromUtf8(path.characters())));
}

static void use_color_scheme(SiteSecurityPopover& popover, Qt::ColorScheme scheme)
{
    application().styleHints()->setColorScheme(scheme);
    popover.update_chrome_style(QApplication::palette());
}

TEST_CASE(protocol_names)
{
    EXPECT_EQ(SiteSecurity::display_protocol("TLSv1.3"sv), "TLS 1.3"sv);
    EXPECT_EQ(SiteSecurity::display_protocol("TLSv1.2"sv), "TLS 1.2"sv);
    EXPECT_EQ(SiteSecurity::display_protocol("TLSv1"sv), "TLS 1.0"sv);
    EXPECT_EQ(SiteSecurity::display_protocol("QUIC"sv), "QUIC"sv);
}

TEST_CASE(which_pages_have_site_information)
{
    EXPECT(SiteSecurity::applies_to(url("https://example.com/"sv)));
    EXPECT(SiteSecurity::applies_to(url("http://example.com/"sv)));
    EXPECT(!SiteSecurity::applies_to(url("about:newtab"sv)));
    EXPECT(!SiteSecurity::applies_to(url("file:///tmp/page.html"sv)));
    EXPECT(!SiteSecurity::applies_to(url("xenon://certificate-error/?url=https%3A%2F%2Fexample.com%2F"sv)));
}

TEST_CASE(a_plain_http_page_is_not_secure)
{
    auto description = SiteSecurity::describe(url("http://example.com:8080/"sv), {}, false);
    EXPECT_EQ(string(description, "state"sv), "notSecure"sv);
    EXPECT_EQ(string(description, "host"sv), "example.com:8080"sv);
    EXPECT(!description.has("protocol"sv));
}

TEST_CASE(a_verified_connection)
{
    auto description = SiteSecurity::describe(url("https://127.0.0.1/"sv), connection(false), false);
    EXPECT_EQ(string(description, "state"sv), "secure"sv);
    EXPECT_EQ(string(description, "host"sv), "127.0.0.1"sv);
    EXPECT_EQ(string(description, "protocol"sv), "TLS 1.3"sv);
    EXPECT_EQ(string(description, "cipher"sv), "TLS_AES_128_GCM_SHA256"sv);
    EXPECT_EQ(chain(description), (Vector<ByteString> { "127.0.0.1", "127.0.0.1" }));

    auto certificate = description.get_object("certificate"sv);
    VERIFY(certificate.has_value());
    EXPECT_EQ(string(*certificate, "issuer"sv), "Xenon Test Authority"sv);
    EXPECT_EQ(string(*certificate, "issuerOrganization"sv), "Xenon tests"sv);
    EXPECT_EQ(string(*certificate, "fingerprint"sv).length(), 95u);
}

TEST_CASE(a_connection_over_an_accepted_certificate)
{
    auto description = SiteSecurity::describe(url("https://127.0.0.1/"sv), connection(true), false);
    EXPECT_EQ(string(description, "state"sv), "acceptedCertificate"sv);
    EXPECT_EQ(string(description, "protocol"sv), "TLS 1.3"sv);
}

TEST_CASE(a_connection_with_no_details)
{
    EXPECT_EQ(string(SiteSecurity::describe(url("https://example.com/"sv), {}, false), "state"sv), "secure"sv);
    EXPECT_EQ(string(SiteSecurity::describe(url("https://example.com/"sv), {}, true), "state"sv), "acceptedCertificate"sv);
    EXPECT(!SiteSecurity::describe(url("https://example.com/"sv), {}, false).has("certificate"sv));
}

TEST_CASE(a_chain_with_a_certificate_that_does_not_parse)
{
    auto broken = connection(false);
    broken.certificate_chain.prepend(MUST(ByteBuffer::copy("not a certificate"sv.bytes())));
    auto description = SiteSecurity::describe(url("https://127.0.0.1/"sv), broken, false);
    EXPECT(!description.has("certificate"sv));
    EXPECT_EQ(chain(description).size(), 2u);
}

TEST_CASE(fingerprints_split_into_lines)
{
    auto fingerprint = QString("00:01:02:03:04:05:06:07:08:09:0A:0B:0C:0D:0E:0F:10:11");
    EXPECT(fingerprint_in_lines(fingerprint) == QString("00:01:02:03:04:05:06:07\n08:09:0A:0B:0C:0D:0E:0F\n10:11"));
    EXPECT(fingerprint_in_lines("AB:CD") == QString("AB:CD"));
}

TEST_CASE(certificate_dates_are_days_in_utc)
{
    application();
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
    EXPECT(format_certificate_date(1767225600000) == QString("1 January 2026"));
    EXPECT(format_certificate_date(1767225599000) == QString("31 December 2025"));
}

TEST_CASE(the_popover_shows_the_connection_and_its_certificate)
{
    application();
    SiteSecurityPopover popover(nullptr);
    use_color_scheme(popover, Qt::ColorScheme::Light);
    popover.set_description(SiteSecurity::describe(url("https://127.0.0.1:8443/"sv), connection(false), false), SiteSecurityPopover::Details::Final);

    EXPECT(labels_named(popover, "XenonSiteSecurityHost") == Vector<QString> { "127.0.0.1:8443" });
    EXPECT(labels_named(popover, "XenonSiteSecurityState") == Vector<QString> { "Connection is secure" });
    EXPECT(labels_named(popover, "XenonSiteSecurityHeading") == (Vector<QString> { "Connection", "Certificate", "Certificate chain" }));
    auto names = labels_named(popover, "XenonSiteSecurityName");
    EXPECT(names == (Vector<QString> { "Protocol", "Cipher", "Issued to", "Issued by", "Valid from", "Valid until", "SHA-256" }));
    auto values = labels_named(popover, "XenonSiteSecurityValue");
    EXPECT(values.first() == QString("TLS 1.3"));
    EXPECT(values.contains_slow(QString("Xenon Test Authority\nXenon tests")));
    EXPECT_EQ(labels_named(popover, "XenonSiteSecurityFingerprint").size(), 1u);
    EXPECT_EQ(labels_named(popover, "XenonSiteSecurityChainLink").size(), 2u);

    save_screenshot(popover, "site-security-popover-light"sv);
    use_color_scheme(popover, Qt::ColorScheme::Dark);
    save_screenshot(popover, "site-security-popover-dark"sv);
    use_color_scheme(popover, Qt::ColorScheme::Light);
}

TEST_CASE(the_popover_for_an_accepted_certificate)
{
    application();
    SiteSecurityPopover popover(nullptr);
    use_color_scheme(popover, Qt::ColorScheme::Light);
    popover.set_description(SiteSecurity::describe(url("https://127.0.0.1/"sv), connection(true), false), SiteSecurityPopover::Details::Final);
    EXPECT(labels_named(popover, "XenonSiteSecurityState") == Vector<QString> { "Certificate not verified" });
    save_screenshot(popover, "site-security-popover-accepted"sv);
}

TEST_CASE(the_popover_for_a_plain_http_page)
{
    application();
    SiteSecurityPopover popover(nullptr);
    use_color_scheme(popover, Qt::ColorScheme::Light);
    popover.set_description(SiteSecurity::describe(url("http://example.com/"sv), {}, false), SiteSecurityPopover::Details::Final);
    EXPECT(labels_named(popover, "XenonSiteSecurityState") == Vector<QString> { "Connection isn't secure" });
    EXPECT(labels_named(popover, "XenonSiteSecurityHeading").is_empty());
    save_screenshot(popover, "site-security-popover-not-secure"sv);
}

TEST_CASE(the_popover_while_it_looks_the_connection_up)
{
    application();
    SiteSecurityPopover popover(nullptr);
    popover.set_description(SiteSecurity::describe(url("https://example.com/"sv), {}, false), SiteSecurityPopover::Details::Loading);
    EXPECT(labels_named(popover, "XenonSiteSecurityNote") == Vector<QString> { "Looking up this connection…" });
    popover.set_description(SiteSecurity::describe(url("https://example.com/"sv), {}, false), SiteSecurityPopover::Details::Final);
    EXPECT_EQ(labels_named(popover, "XenonSiteSecurityNote").size(), 1u);
    EXPECT(labels_named(popover, "XenonSiteSecurityNote").first().contains("cache"));
}

TEST_CASE(the_address_bar_padlock)
{
    application();
    auto pixmap = create_chrome_icon_pixmap(ChromeIcon::Lock, Qt::black, 2);
    EXPECT(!pixmap.isNull());
    auto image = pixmap.toImage();
    auto painted = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(image.pixel(x, y)) > 0)
                ++painted;
        }
    }
    EXPECT(painted > 0);

    if (auto directory = Core::Environment::get("XENON_SCREENSHOT_DIR"sv); directory.has_value())
        EXPECT(pixmap.save(QString::fromUtf8(ByteString::formatted("{}/site-security-padlock.png", *directory).characters())));
}
