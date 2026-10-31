/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/LexicalPath.h>
#include <AK/ScopeGuard.h>
#include <AK/StringBuilder.h>
#include <AK/Utf8View.h>
#include <AK/Vector.h>
#include <LibCore/Directory.h>
#include <LibCore/File.h>
#include <LibCrypto/Hash/SHA2.h>
#include <LibFileSystem/FileSystem.h>
#include <UI/Extensions/CRX.h>
#include <UI/Extensions/Extension.h>

#include <openssl/evp.h>
#include <openssl/x509.h>
#include <sys/stat.h>
#include <zlib.h>

namespace Ladybird::Extensions {

static constexpr u8 PUBLISHER_KEY_HASH[] = {
    0x61, 0xf7, 0xf2, 0xa6, 0xbf, 0xcf, 0x74, 0xcd, 0x0b, 0xc1, 0xfe, 0x24, 0x97, 0xcc, 0x9b, 0x04,
    0x25, 0x4c, 0x65, 0x8f, 0x79, 0xf2, 0x14, 0x53, 0x92, 0x86, 0x7e, 0xa8, 0x36, 0x63, 0x67, 0xcf
};

static constexpr auto CRX_MAGIC = "Cr24"sv;
static constexpr u32 CRX_VERSION = 3;
static constexpr size_t MAXIMUM_HEADER_SIZE = 16 * MiB;
static constexpr auto SIGNATURE_CONTEXT = "CRX3 SignedData\0"sv;
static constexpr size_t CRX_ID_SIZE = 16;

static constexpr size_t MAXIMUM_ENTRIES = 50'000;
static constexpr u64 MAXIMUM_UNPACKED_SIZE = 512 * MiB;

ReadonlyBytes chrome_web_store_publisher_key_hash()
{
    return { PUBLISHER_KEY_HASH, sizeof(PUBLISHER_KEY_HASH) };
}

static u32 read_u16(ReadonlyBytes bytes, size_t offset)
{
    return bytes[offset] | (bytes[offset + 1] << 8);
}

static u32 read_u32(ReadonlyBytes bytes, size_t offset)
{
    return bytes[offset] | (bytes[offset + 1] << 8) | (bytes[offset + 2] << 16) | (static_cast<u32>(bytes[offset + 3]) << 24);
}

class ProtobufReader {
public:
    explicit ProtobufReader(ReadonlyBytes bytes)
        : m_bytes(bytes)
    {
    }

    struct Field {
        u64 number { 0 };
        ReadonlyBytes bytes;
    };

    ErrorOr<Optional<Field>, String> next()
    {
        if (m_offset == m_bytes.size())
            return Optional<Field> {};

        auto key = TRY(read_varint());
        Field field { .number = key >> 3, .bytes = {} };
        if (field.number == 0)
            return "a field has no number"_string;

        switch (key & 7) {
        case 0:
            TRY(read_varint());
            break;
        case 1:
            TRY(skip(8));
            break;
        case 2: {
            auto length = TRY(read_varint());
            if (length > m_bytes.size() - m_offset)
                return "a field runs past the end"_string;
            field.bytes = m_bytes.slice(m_offset, length);
            m_offset += length;
            break;
        }
        case 5:
            TRY(skip(4));
            break;
        default:
            return "a field has a type CRX headers don't use"_string;
        }
        return field;
    }

private:
    ErrorOr<u64, String> read_varint()
    {
        u64 value = 0;
        for (size_t shift = 0; shift < 64; shift += 7) {
            if (m_offset == m_bytes.size())
                return "a number runs past the end"_string;
            auto byte = m_bytes[m_offset++];
            value |= static_cast<u64>(byte & 0x7f) << shift;
            if (!(byte & 0x80))
                return value;
        }
        return "a number is too long"_string;
    }

    ErrorOr<void, String> skip(size_t count)
    {
        if (count > m_bytes.size() - m_offset)
            return "a field runs past the end"_string;
        m_offset += count;
        return {};
    }

    ReadonlyBytes m_bytes;
    size_t m_offset { 0 };
};

enum class SignatureAlgorithm : u8 {
    RSA_PKCS1_SHA256,
    ECDSA_SHA256,
};

struct Proof {
    SignatureAlgorithm algorithm;
    ReadonlyBytes public_key;
    ReadonlyBytes signature;
};

static ErrorOr<Proof, String> parse_proof(SignatureAlgorithm algorithm, ReadonlyBytes bytes)
{
    Proof proof { .algorithm = algorithm, .public_key = {}, .signature = {} };
    ProtobufReader reader { bytes };
    while (true) {
        auto field = TRY(reader.next());
        if (!field.has_value())
            return proof;
        if (field->number == 1)
            proof.public_key = field->bytes;
        else if (field->number == 2)
            proof.signature = field->bytes;
    }
}

static bool signature_is_valid(Proof const& proof, ReadonlyBytes signed_header_data, ReadonlyBytes archive)
{
    auto const* key_data = proof.public_key.data();
    auto* key = d2i_PUBKEY(nullptr, &key_data, static_cast<long>(proof.public_key.size()));
    if (!key)
        return false;
    ScopeGuard free_key = [&] { EVP_PKEY_free(key); };
    if (key_data != proof.public_key.data() + proof.public_key.size())
        return false;

    auto expected_type = proof.algorithm == SignatureAlgorithm::RSA_PKCS1_SHA256 ? EVP_PKEY_RSA : EVP_PKEY_EC;
    if (EVP_PKEY_get_base_id(key) != expected_type)
        return false;

    auto* context = EVP_MD_CTX_new();
    if (!context)
        return false;
    ScopeGuard free_context = [&] { EVP_MD_CTX_free(context); };

    u8 const signed_header_size[4] = {
        static_cast<u8>(signed_header_data.size()),
        static_cast<u8>(signed_header_data.size() >> 8),
        static_cast<u8>(signed_header_data.size() >> 16),
        static_cast<u8>(signed_header_data.size() >> 24),
    };

    return EVP_DigestVerifyInit(context, nullptr, EVP_sha256(), nullptr, key) == 1
        && EVP_DigestVerifyUpdate(context, SIGNATURE_CONTEXT.characters_without_null_termination(), SIGNATURE_CONTEXT.length()) == 1
        && EVP_DigestVerifyUpdate(context, signed_header_size, sizeof(signed_header_size)) == 1
        && EVP_DigestVerifyUpdate(context, signed_header_data.data(), signed_header_data.size()) == 1
        && EVP_DigestVerifyUpdate(context, archive.data(), archive.size()) == 1
        && EVP_DigestVerifyFinal(context, proof.signature.data(), proof.signature.size()) == 1;
}

static String extension_id_from_crx_id(ReadonlyBytes crx_id)
{
    StringBuilder builder;
    for (auto byte : crx_id) {
        builder.append(static_cast<char>('a' + (byte >> 4)));
        builder.append(static_cast<char>('a' + (byte & 0xf)));
    }
    return builder.to_string_without_validation();
}

ErrorOr<PackedExtension, String> verify_crx(ReadonlyBytes crx, StringView expected_id, ReadonlyBytes publisher_key_hash)
{
    if (crx.size() < 12 || crx.slice(0, 4) != CRX_MAGIC.bytes())
        return "It isn't a CRX file."_string;
    if (read_u32(crx, 4) != CRX_VERSION)
        return "It isn't a CRX3 file."_string;

    auto header_size = read_u32(crx, 8);
    if (header_size > MAXIMUM_HEADER_SIZE || header_size > crx.size() - 12)
        return "Its header is too large."_string;
    auto header = crx.slice(12, header_size);
    auto archive = crx.slice(12 + header_size);

    auto header_view = StringView { header };
    if (header_view.contains("PK\x05\x06"sv) || header_view.contains("PK\x06\x06"sv))
        return "Its header could be taken for part of the archive."_string;

    Vector<Proof> proofs;
    Optional<ReadonlyBytes> signed_header_data;
    ProtobufReader reader { header };
    while (true) {
        auto field = reader.next();
        if (field.is_error())
            return MUST(String::formatted("Its header can't be read: {}.", field.error()));
        if (!field.value().has_value())
            break;

        auto const& [number, bytes] = *field.value();
        if (number == 2 || number == 3) {
            auto proof = parse_proof(number == 2 ? SignatureAlgorithm::RSA_PKCS1_SHA256 : SignatureAlgorithm::ECDSA_SHA256, bytes);
            if (proof.is_error())
                return MUST(String::formatted("Its header can't be read: {}.", proof.error()));
            proofs.append(proof.release_value());
        } else if (number == 10000) {
            signed_header_data = bytes;
        }
    }

    Optional<ReadonlyBytes> crx_id;
    if (signed_header_data.has_value()) {
        ProtobufReader signed_data { *signed_header_data };
        while (true) {
            auto field = signed_data.next();
            if (field.is_error())
                return MUST(String::formatted("Its signed header can't be read: {}.", field.error()));
            if (!field.value().has_value())
                break;
            if (field.value()->number == 1)
                crx_id = field.value()->bytes;
        }
    }
    if (!crx_id.has_value() || crx_id->size() != CRX_ID_SIZE)
        return "It doesn't say which extension it is."_string;

    auto id = extension_id_from_crx_id(*crx_id);
    if (id != expected_id)
        return MUST(String::formatted("It's another extension, {}.", id));

    Optional<ReadonlyBytes> developer_key;
    auto signed_by_publisher = false;
    for (auto const& proof : proofs) {
        if (!signature_is_valid(proof, *signed_header_data, archive))
            return "One of its signatures isn't valid."_string;

        if (extension_id_from_bytes(proof.public_key) == id)
            developer_key = proof.public_key;
        if (!publisher_key_hash.is_empty() && Crypto::Hash::SHA256::hash(proof.public_key).bytes() == publisher_key_hash)
            signed_by_publisher = true;
    }
    if (!developer_key.has_value())
        return "It isn't signed by its developer."_string;
    if (!publisher_key_hash.is_empty() && !signed_by_publisher)
        return "It isn't signed by the Chrome Web Store."_string;

    auto public_key = ByteBuffer::copy(*developer_key);
    if (public_key.is_error())
        return "There's no memory for it."_string;
    return PackedExtension { .id = move(id), .public_key = public_key.release_value(), .archive = archive };
}

static bool is_safe_entry_path(StringView path)
{
    if (path.is_empty() || !Utf8View { path }.validate())
        return false;
    for (auto c : path) {
        if (static_cast<u8>(c) < 0x20 || c == '\\' || c == ':')
            return false;
    }
    auto safe = true;
    path.for_each_split_view('/', SplitBehavior::KeepEmpty, [&](auto part) {
        if (part.is_empty() || part == "."sv || part == ".."sv)
            safe = false;
    });
    return safe;
}

static ErrorOr<ByteBuffer, String> inflate_entry(ReadonlyBytes compressed, u32 size)
{
    auto output = ByteBuffer::create_uninitialized(max(size, 1u));
    if (output.is_error())
        return "there's no memory for it"_string;

    z_stream stream {};
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK)
        return "it can't be decompressed"_string;
    ScopeGuard end_stream = [&] { inflateEnd(&stream); };

    stream.next_in = const_cast<Bytef*>(compressed.data());
    stream.avail_in = static_cast<uInt>(compressed.size());
    stream.next_out = output.value().data();
    stream.avail_out = size;
    if (inflate(&stream, Z_FINISH) != Z_STREAM_END || stream.total_out != size)
        return "it isn't the size the archive says"_string;

    output.value().resize(size);
    return output.release_value();
}

ErrorOr<void, String> unpack_zip(ReadonlyBytes archive, ByteString const& directory)
{
    static constexpr size_t END_RECORD_SIZE = 22;
    if (archive.size() < END_RECORD_SIZE)
        return "The archive is empty."_string;

    Optional<size_t> end_record;
    for (size_t offset = archive.size() - END_RECORD_SIZE;; --offset) {
        if (read_u32(archive, offset) == 0x06054b50 && offset + END_RECORD_SIZE + read_u16(archive, offset + 20) == archive.size()) {
            end_record = offset;
            break;
        }
        if (offset == 0 || archive.size() - offset >= END_RECORD_SIZE + 0xffff)
            break;
    }
    if (!end_record.has_value())
        return "The archive has no table of contents."_string;

    auto disk = read_u16(archive, *end_record + 4);
    auto directory_disk = read_u16(archive, *end_record + 6);
    auto entries_on_disk = read_u16(archive, *end_record + 8);
    auto entry_count = read_u16(archive, *end_record + 10);
    auto directory_size = read_u32(archive, *end_record + 12);
    auto directory_offset = read_u32(archive, *end_record + 16);
    if (disk != 0 || directory_disk != 0 || entries_on_disk != entry_count)
        return "The archive is split into parts."_string;
    if (entry_count == 0xffff || directory_size == 0xffffffff || directory_offset == 0xffffffff)
        return "The archive is a ZIP64 archive, which extensions never need."_string;
    if (entry_count > MAXIMUM_ENTRIES)
        return "The archive has too many files."_string;
    if (static_cast<u64>(directory_offset) + directory_size > *end_record)
        return "The archive's table of contents is out of place."_string;

    if (FileSystem::exists(directory))
        return MUST(String::formatted("{} already exists.", directory));
    if (auto result = Core::Directory::create(directory, Core::Directory::CreateDirectories::Yes); result.is_error())
        return MUST(String::formatted("{} can't be created: {}", directory, result.error()));

    u64 unpacked_size = 0;
    size_t offset = directory_offset;
    for (size_t i = 0; i < entry_count; ++i) {
        if (offset + 46 > *end_record || read_u32(archive, offset) != 0x02014b50)
            return "The archive's table of contents is damaged."_string;

        auto made_by = read_u16(archive, offset + 4);
        auto flags = read_u16(archive, offset + 8);
        auto method = read_u16(archive, offset + 10);
        auto crc = read_u32(archive, offset + 16);
        auto compressed_size = read_u32(archive, offset + 20);
        auto size = read_u32(archive, offset + 24);
        auto name_length = read_u16(archive, offset + 28);
        auto extra_length = read_u16(archive, offset + 30);
        auto comment_length = read_u16(archive, offset + 32);
        auto external_attributes = read_u32(archive, offset + 38);
        auto local_offset = read_u32(archive, offset + 42);
        if (offset + 46 + name_length + extra_length + comment_length > *end_record)
            return "The archive's table of contents is damaged."_string;
        auto name = StringView { archive.slice(offset + 46, name_length) };
        offset += 46 + name_length + extra_length + comment_length;

        auto is_directory = name.ends_with('/');
        auto path = is_directory ? name.substring_view(0, name.length() - 1) : name;
        if (!is_safe_entry_path(path))
            return MUST(String::formatted("The archive has a file named \"{}\", which isn't allowed.", name));
        if (flags & 1)
            return MUST(String::formatted("{} is encrypted.", name));
        if ((made_by >> 8) == 3 && S_ISLNK(external_attributes >> 16))
            return MUST(String::formatted("{} is a symbolic link.", name));

        auto destination = LexicalPath::join(directory, path).string();
        if (is_directory) {
            if (auto result = Core::Directory::create(destination, Core::Directory::CreateDirectories::Yes); result.is_error())
                return MUST(String::formatted("{} can't be created: {}", destination, result.error()));
            continue;
        }

        if (method != 0 && method != 8)
            return MUST(String::formatted("{} is compressed in a way extensions don't use.", name));
        if (method == 0 && compressed_size != size)
            return MUST(String::formatted("{} isn't the size the archive says.", name));
        unpacked_size += size;
        if (unpacked_size > MAXIMUM_UNPACKED_SIZE)
            return "The archive is too large unpacked."_string;

        if (static_cast<u64>(local_offset) + 30 > directory_offset || read_u32(archive, local_offset) != 0x04034b50)
            return MUST(String::formatted("{} is missing from the archive.", name));
        auto local_name_length = read_u16(archive, local_offset + 26);
        auto local_extra_length = read_u16(archive, local_offset + 28);
        auto data_offset = static_cast<u64>(local_offset) + 30 + local_name_length + local_extra_length;
        if (data_offset + compressed_size > directory_offset)
            return MUST(String::formatted("{} is missing from the archive.", name));
        if (StringView { archive.slice(local_offset + 30, local_name_length) } != name)
            return MUST(String::formatted("{} has another name in its own header.", name));
        auto data = archive.slice(data_offset, compressed_size);

        ByteBuffer inflated;
        if (method == 8) {
            auto result = inflate_entry(data, size);
            if (result.is_error())
                return MUST(String::formatted("{} can't be unpacked: {}.", name, result.error()));
            inflated = result.release_value();
            data = inflated.bytes();
        }
        if (crc32(0, data.data(), static_cast<uInt>(data.size())) != crc)
            return MUST(String::formatted("{} is damaged.", name));

        auto write = [&]() -> ErrorOr<void> {
            TRY(Core::Directory::create(LexicalPath { destination }.parent(), Core::Directory::CreateDirectories::Yes));
            auto file = TRY(Core::File::open(destination, Core::File::OpenMode::Write | Core::File::OpenMode::MustBeNew));
            TRY(file->write_until_depleted(data));
            return {};
        };
        if (auto result = write(); result.is_error())
            return MUST(String::formatted("{} can't be written: {}", destination, result.error()));
    }

    return {};
}

}
