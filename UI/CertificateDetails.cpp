/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Array.h>
#include <AK/IPv6Address.h>
#include <AK/JsonArray.h>
#include <AK/ScopeGuard.h>
#include <AK/StringBuilder.h>
#include <AK/Time.h>
#include <UI/CertificateDetails.h>

#include <openssl/evp.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <string.h>
#include <time.h>

namespace Ladybird {

static String name_entry(X509_NAME const* name, int nid)
{
    auto index = X509_NAME_get_index_by_NID(name, nid, -1);
    if (index < 0)
        return {};

    unsigned char* text = nullptr;
    auto length = ASN1_STRING_to_UTF8(&text, X509_NAME_ENTRY_get_data(X509_NAME_get_entry(name, index)));
    if (length < 0)
        return {};
    ScopeGuard free_text = [&] { OPENSSL_free(text); };

    auto string = String::from_utf8(StringView { reinterpret_cast<char const*>(text), static_cast<size_t>(length) });
    if (string.is_error())
        return {};
    return string.release_value();
}

static Optional<i64> milliseconds_since_epoch(ASN1_TIME const* time)
{
    struct tm parts {};
    if (!time || ASN1_TIME_to_tm(time, &parts) != 1)
        return {};

    auto date_time = UnixDateTime::from_unix_time_parts(parts.tm_year + 1900, static_cast<u8>(parts.tm_mon + 1), static_cast<u8>(parts.tm_mday),
        static_cast<u8>(parts.tm_hour), static_cast<u8>(parts.tm_min), static_cast<u8>(parts.tm_sec), 0);
    return date_time.milliseconds_since_epoch();
}

static JsonArray subject_alternative_names(X509 const* certificate)
{
    JsonArray names;

    auto* general_names = static_cast<GENERAL_NAMES*>(X509_get_ext_d2i(certificate, NID_subject_alt_name, nullptr, nullptr));
    if (!general_names)
        return names;
    ScopeGuard free_general_names = [&] { GENERAL_NAMES_free(general_names); };

    for (int i = 0; i < sk_GENERAL_NAME_num(general_names); ++i) {
        auto const* general_name = sk_GENERAL_NAME_value(general_names, i);

        if (general_name->type == GEN_DNS) {
            auto const* host = general_name->d.dNSName;
            auto name = String::from_utf8(StringView { reinterpret_cast<char const*>(ASN1_STRING_get0_data(host)), static_cast<size_t>(ASN1_STRING_length(host)) });
            if (!name.is_error())
                names.must_append(name.release_value());
        } else if (general_name->type == GEN_IPADD) {
            auto const* address = general_name->d.iPAddress;
            auto const* bytes = ASN1_STRING_get0_data(address);
            if (ASN1_STRING_length(address) == 4) {
                names.must_append(MUST(String::formatted("{}.{}.{}.{}", static_cast<unsigned>(bytes[0]), static_cast<unsigned>(bytes[1]), static_cast<unsigned>(bytes[2]), static_cast<unsigned>(bytes[3]))));
            } else if (ASN1_STRING_length(address) == 16) {
                Array<u8, 16> ipv6_address {};
                memcpy(ipv6_address.data(), bytes, ipv6_address.size());
                names.must_append(MUST(IPv6Address { ipv6_address }.to_string()));
            }
        }
    }
    return names;
}

static bool is_for_host(X509* certificate, URL::URL const& url)
{
    auto host = url.serialized_host();
    if (url.host().has_value() && url.host()->is_domain())
        return X509_check_host(certificate, host.characters_without_null_termination(), host.length(), 0, nullptr) == 1;

    if (host.starts_with('[') && host.ends_with(']'))
        host = host.substring_view(1, host.length() - 2);
    return X509_check_ip_asc(certificate, ByteString { host }.characters(), 0) == 1;
}

static String fingerprint(ReadonlyBytes der_certificate)
{
    u8 digest[EVP_MAX_MD_SIZE];
    unsigned int digest_size = 0;
    if (EVP_Digest(der_certificate.data(), der_certificate.size(), digest, &digest_size, EVP_sha256(), nullptr) != 1)
        return {};

    StringBuilder builder;
    for (unsigned int i = 0; i < digest_size; ++i) {
        if (i > 0)
            builder.append(':');
        builder.appendff("{:02X}", digest[i]);
    }
    return MUST(builder.to_string());
}

JsonObject describe_rejected_certificate(ReadonlyBytes der_certificate, URL::URL const& url)
{
    JsonObject description;
    JsonArray problems;

    auto const* der = der_certificate.data();
    auto* certificate = d2i_X509(nullptr, &der, static_cast<long>(der_certificate.size()));
    if (!certificate) {
        problems.must_append("untrusted"_string);
        description.set("problems"sv, move(problems));
        return description;
    }
    ScopeGuard free_certificate = [&] { X509_free(certificate); };

    auto is_self_signed = X509_self_signed(certificate, 1) == 1;
    if (is_self_signed)
        problems.must_append("selfSigned"_string);
    if (!is_for_host(certificate, url))
        problems.must_append("host"_string);
    if (X509_cmp_current_time(X509_get0_notAfter(certificate)) < 0)
        problems.must_append("expired"_string);
    if (X509_cmp_current_time(X509_get0_notBefore(certificate)) > 0)
        problems.must_append("notYetValid"_string);
    if (problems.is_empty())
        problems.must_append("untrusted"_string);
    description.set("problems"sv, move(problems));

    auto const* subject = X509_get_subject_name(certificate);
    auto const* issuer = X509_get_issuer_name(certificate);
    description.set("subject"sv, name_entry(subject, NID_commonName));
    description.set("subjectOrganization"sv, name_entry(subject, NID_organizationName));
    description.set("issuer"sv, name_entry(issuer, NID_commonName));
    description.set("issuerOrganization"sv, name_entry(issuer, NID_organizationName));

    if (auto valid_from = milliseconds_since_epoch(X509_get0_notBefore(certificate)); valid_from.has_value())
        description.set("validFrom"sv, *valid_from);
    if (auto valid_until = milliseconds_since_epoch(X509_get0_notAfter(certificate)); valid_until.has_value())
        description.set("validUntil"sv, *valid_until);

    description.set("names"sv, subject_alternative_names(certificate));
    description.set("fingerprint"sv, fingerprint(der_certificate));
    return description;
}

}
