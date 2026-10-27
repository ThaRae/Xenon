/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/AllOf.h>
#include <AK/Base64.h>
#include <AK/CharacterTypes.h>
#include <AK/JsonObject.h>
#include <AK/JsonValue.h>
#include <AK/LexicalPath.h>
#include <AK/StringBuilder.h>
#include <LibCore/DirIterator.h>
#include <LibCore/Directory.h>
#include <LibCore/Environment.h>
#include <LibCore/EventLoop.h>
#include <LibCore/File.h>
#include <LibCore/System.h>
#include <LibCore/Timer.h>
#include <LibFileSystem/FileSystem.h>
#include <LibHTTP/Cache/CacheMode.h>
#include <LibHTTP/Cookie/IncludeCredentials.h>
#include <LibHTTP/HeaderList.h>
#include <LibRequests/NetworkError.h>
#include <LibRequests/Request.h>
#include <LibRequests/RequestClient.h>
#include <LibURL/Parser.h>
#include <LibWebCommon/Fetch/Infrastructure/HTTP/Statuses.h>
#include <LibWebCommon/Loader/UserAgent.h>
#include <LibWebView/Application.h>
#include <UI/FilterListUpdates.h>
#include <UI/FilterLists.h>

namespace Ladybird::FilterLists {

// Where xenon-filter-lists publishes the bundle, and the public half of the key it signs manifest.json with.
static constexpr auto UPDATES_URL = "https://git.avx512.dev/tharae/xenon-filter-lists/raw/branch/main/"sv;
static constexpr auto PUBLIC_KEY = "n1G8auYT7+ppTD19FMr7o3oWbSaioHm38CWCr7FCuZ4="sv;

// For tests, which publish bundles of their own from a local server, signed with a key of their own.
static constexpr auto UPDATES_URL_VARIABLE = "XENON_FILTER_LIST_UPDATES_URL"sv;
static constexpr auto PUBLIC_KEY_VARIABLE = "XENON_FILTER_LIST_UPDATES_PUBLIC_KEY"sv;

static constexpr auto MANIFEST_NAME = "manifest.json"sv;
static constexpr auto SIGNATURE_NAME = "manifest.json.sig"sv;
static constexpr auto STATE_NAME = "state.json"sv;
static constexpr auto STAGING_NAME = "staging"sv;
static constexpr u64 MAXIMUM_MANIFEST_SIZE = 4 * MiB;
static constexpr u64 MAXIMUM_SIGNATURE_SIZE = 1 * KiB;

static constexpr auto CHECK_INTERVAL = AK::Duration::from_seconds(24 * 60 * 60);
static constexpr auto RETRY_INTERVAL = AK::Duration::from_seconds(60 * 60);
// How often Xenon looks at whether a check is due, which also catches up soon after the computer wakes.
static constexpr auto TICK_INTERVAL = AK::Duration::from_seconds(10 * 60);
// The first check waits a moment, so that its downloads don't compete with the pages the browser opens with.
static constexpr auto FIRST_CHECK_DELAY = AK::Duration::from_seconds(5);
static constexpr auto DOWNLOAD_TIMEOUT = AK::Duration::from_seconds(120);
static constexpr size_t MAXIMUM_REDIRECTS = 5;

static Updater* s_updater;

// Fetches one file of the bundle, up to the size it should be, without cookies or the HTTP cache. Calls back once,
// with the body of a successful response or an error.
class Download final : public RefCounted<Download> {
public:
    using Callback = Function<void(ErrorOr<ByteBuffer>)>;

    static void start(URL::URL url, u64 maximum_size, Callback callback)
    {
        auto download = adopt_ref(*new Download(maximum_size, move(callback)));
        download->m_timeout = Core::Timer::create_single_shot(DOWNLOAD_TIMEOUT, [download] {
            download->finish(Error::from_string_literal("the download timed out"));
        });
        download->m_timeout->start();
        download->fetch(move(url));
    }

private:
    Download(u64 maximum_size, Callback callback)
        : m_maximum_size(maximum_size)
        , m_callback(move(callback))
    {
    }

    void fetch(URL::URL url)
    {
        auto client = WebView::Application::request_server_client();
        if (client.is_error()) {
            finish(Error::from_string_literal("RequestServer isn't running"));
            return;
        }

        auto request_headers = HTTP::HeaderList::create();
        request_headers->set({ "User-Agent"sv, Web::default_user_agent });

        m_url = move(url);
        m_request = client.value()->start_request("GET"sv, m_url, *request_headers, {}, HTTP::CacheMode::NoStore, HTTP::Cookie::IncludeCredentials::No);
        if (!m_request) {
            finish(Error::from_string_literal("the request didn't start"));
            return;
        }

        // A redirect stops the request it answers, whose callbacks may still come, so each request's are its own.
        auto id = ++m_request_id;
        m_request->set_unbuffered_request_callbacks(
            [self = NonnullRefPtr(*this), id](NonnullRefPtr<HTTP::HeaderList> response_headers, Optional<u32> response_code, Optional<String> const&, Optional<Core::ImmutableBytes>, Optional<u64>, Requests::CacheState) {
                if (id == self->m_request_id)
                    self->did_receive_headers(*response_headers, response_code);
            },
            [self = NonnullRefPtr(*this), id](Requests::ResponseData data) {
                if (id == self->m_request_id)
                    self->did_receive_data(data.bytes());
            },
            [self = NonnullRefPtr(*this), id](Core::ImmutableBytes data) {
                if (id == self->m_request_id)
                    self->did_receive_data(data.bytes());
            },
            [self = NonnullRefPtr(*this), id](u64, Requests::RequestTimingInfo const&, Optional<Requests::NetworkError> network_error) {
                if (id != self->m_request_id)
                    return;
                self->m_request_finished = true;
                if (network_error.has_value()) {
                    warnln("Unable to download {}: {}", self->m_url, Requests::network_error_to_string(*network_error));
                    self->finish(Error::from_string_literal("the download failed"));
                    return;
                }
                self->finish(move(self->m_payload));
            });
    }

    void did_receive_headers(HTTP::HeaderList const& headers, Optional<u32> response_code)
    {
        if (response_code.has_value() && Web::Fetch::Infrastructure::is_redirect_status(*response_code)) {
            auto location = headers.get("Location"sv);
            auto url = location.has_value() ? URL::Parser::basic_parse(*location, m_url) : Optional<URL::URL> {};
            auto allowed = url.has_value() && (url->scheme() == "https"sv || (url->scheme() == "http"sv && m_url.scheme() == "http"sv));
            if (!allowed || ++m_redirect_count > MAXIMUM_REDIRECTS) {
                finish(Error::from_string_literal("the download was redirected somewhere it can't go"));
                return;
            }

            ++m_request_id;
            m_request->stop();
            Core::deferred_invoke([self = NonnullRefPtr(*this), url = url.release_value()]() mutable {
                if (!self->m_finished)
                    self->fetch(move(url));
            });
            return;
        }

        if (!response_code.has_value() || *response_code < 200 || *response_code >= 300) {
            warnln("Unable to download {}: received HTTP status {}", m_url, response_code.value_or(0));
            finish(Error::from_string_literal("the server didn't send the file"));
        }
    }

    void did_receive_data(ReadonlyBytes data)
    {
        if (m_finished)
            return;
        if (data.size() > m_maximum_size - m_payload.size()) {
            finish(Error::from_string_literal("the file is larger than it should be"));
            return;
        }
        if (m_payload.try_append(data).is_error())
            finish(Error::from_string_literal("there's no memory for the file"));
    }

    void finish(ErrorOr<ByteBuffer> result)
    {
        if (m_finished)
            return;
        m_finished = true;
        m_timeout->stop();

        auto request = move(m_request);
        if (request && !m_request_finished) {
            ++m_request_id;
            request->stop();
        }

        // NB: The request is let go of only after its own callback has returned.
        Core::deferred_invoke([self = NonnullRefPtr(*this), request = move(request), result = move(result)]() mutable {
            request = nullptr;
            self->m_timeout = nullptr;
            self->m_callback(move(result));
        });
    }

    u64 m_maximum_size { 0 };
    Callback m_callback;
    URL::URL m_url;
    RefPtr<Requests::Request> m_request;
    RefPtr<Core::Timer> m_timeout;
    ByteBuffer m_payload;
    size_t m_request_id { 0 };
    size_t m_redirect_count { 0 };
    bool m_request_finished { false };
    bool m_finished { false };
};

static ErrorOr<ByteBuffer> read_file(StringView path)
{
    auto file = TRY(Core::File::open(path, Core::File::OpenMode::Read));
    return file->read_until_eof();
}

// Writes a file in full or not at all, so that nothing reads half of one.
static ErrorOr<void> write_file(StringView path, ReadonlyBytes contents)
{
    auto temporary = ByteString::formatted("{}.tmp", path);
    {
        auto file = TRY(Core::File::open(temporary, Core::File::OpenMode::Write | Core::File::OpenMode::Truncate));
        TRY(file->write_until_depleted(contents));
    }
    TRY(Core::System::rename(temporary, path));
    return {};
}

void Updater::initialize()
{
    VERIFY(!s_updater);

    if (WebView::Application::web_content_options().is_test_mode == WebView::IsTestMode::Yes)
        return;
    if (WebView::Application::browser_options().enable_content_blocker == WebView::EnableContentBlocker::No)
        return;

    auto base_url = URL::Parser::basic_parse(Core::Environment::get(UPDATES_URL_VARIABLE).value_or(UPDATES_URL));
    auto public_key = decode_base64(Core::Environment::get(PUBLIC_KEY_VARIABLE).value_or(PUBLIC_KEY));
    if (!base_url.has_value() || public_key.is_error() || public_key.value().size() != 32) {
        warnln("Filter list updates are off, because their URL or key isn't valid");
        return;
    }

    auto directory = LexicalPath::join(WebView::Application::profile().paths().data, "ContentBlocking/Xenon"sv).string();
    s_updater = new Updater(move(directory), base_url.release_value(), public_key.release_value());
}

Updater::Updater(ByteString directory, URL::URL base_url, ByteBuffer public_key)
    : m_directory(move(directory))
    , m_base_url(move(base_url))
    , m_public_key(move(public_key))
{
    if (auto bundled = bundled_lists_directory(); bundled.has_value())
        m_bundled = load_bundle(bundled.release_value(), false);
    m_current = m_bundled;

    load_state();

    m_timer = Core::Timer::create_repeating(TICK_INTERVAL, [this] { check_if_due(); });
    m_timer->start();
    m_first_check = Core::Timer::create_single_shot(FIRST_CHECK_DELAY, [this] { check_if_due(); });
    m_first_check->start();
}

void Updater::background_networking_settings_changed()
{
    Core::deferred_invoke([this] { check_if_due(); });
}

void Updater::content_blocker_settings_changed()
{
    Core::deferred_invoke([this] { check_if_due(); });
}

// A bundle in a directory, if its manifest reads, and its files are all there at the sizes it says. A downloaded
// bundle's manifest must also be signed with Xenon's key, and in the format this build reads.
Optional<Updater::Bundle> Updater::load_bundle(ByteString directory, bool downloaded) const
{
    auto manifest_bytes = read_file(LexicalPath::join(directory, MANIFEST_NAME).string());
    if (manifest_bytes.is_error())
        return {};

    if (downloaded) {
        auto signature = read_file(LexicalPath::join(directory, SIGNATURE_NAME).string());
        if (signature.is_error() || !bundle_signature_is_valid(manifest_bytes.value(), StringView { signature.value() }, m_public_key))
            return {};
    }

    auto manifest = parse_bundle_manifest(StringView { manifest_bytes.value() });
    if (manifest.is_error()) {
        warnln("The filter lists in {} can't be used: {}", directory, manifest.error());
        return {};
    }
    if (downloaded && manifest.value().format != BUNDLE_FORMAT)
        return {};

    for (auto const& file : manifest.value().files) {
        auto size = FileSystem::size_from_stat(LexicalPath::join(directory, file.path).string());
        if (size.is_error() || static_cast<u64>(size.value()) != file.size)
            return {};
    }
    return Bundle { move(directory), manifest.release_value(), downloaded };
}

void Updater::use_bundle(Bundle bundle)
{
    auto content_blockers = content_blockers_in(bundle.directory);
    WebView::Application::the().set_explicit_content_blockers(move(content_blockers.lists), move(content_blockers.trusted_lists), move(content_blockers.resources));
    m_current = move(bundle);
}

static bool is_newer(BundleManifest const& manifest, Optional<Updater::Bundle> const& than)
{
    return !than.has_value() || manifest.generated_at > than->manifest.generated_at;
}

void Updater::load_state()
{
    auto contents = read_file(LexicalPath::join(m_directory, STATE_NAME).string());
    if (contents.is_error())
        return;
    auto state = JsonValue::from_string(StringView { contents.value() });
    if (state.is_error() || !state.value().is_object())
        return;
    auto const& object = state.value().as_object();

    if (auto checked_at = object.get_i64("checkedAt"sv); checked_at.has_value())
        m_checked_at = UnixDateTime::from_seconds_since_epoch(*checked_at);

    auto active = object.get_string("active"sv);
    if (!active.has_value() || active->is_empty() || !all_of(active->bytes_as_string_view(), is_ascii_digit))
        return;
    auto bundle = load_bundle(LexicalPath::join(m_directory, *active).string(), true);
    if (!bundle.has_value()) {
        warnln("The filter lists Xenon downloaded aren't intact, so it uses the ones it came with");
        return;
    }
    if (is_newer(bundle->manifest, m_bundled))
        use_bundle(bundle.release_value());
}

void Updater::save_state()
{
    JsonObject state;
    if (m_current.has_value() && m_current->downloaded)
        state.set("active"sv, LexicalPath::basename(m_current->directory).view());
    if (m_checked_at.has_value())
        state.set("checkedAt"sv, m_checked_at->seconds_since_epoch());

    auto result = [&]() -> ErrorOr<void> {
        TRY(Core::Directory::create(m_directory, Core::Directory::CreateDirectories::Yes));
        TRY(write_file(LexicalPath::join(m_directory, STATE_NAME).string(), state.serialized().bytes()));
        return {};
    }();
    if (result.is_error())
        warnln("Unable to save the state of filter list updates: {}", result.error());
}

void Updater::check_if_due()
{
    if (m_updating)
        return;

    auto const& settings = WebView::Application::settings();
    if (!settings.automatic_filter_list_updates_allowed() || !settings.content_blocker_enabled())
        return;

    // A check from the future means the clock was wrong then, or is now, so it doesn't count.
    auto now = UnixDateTime::now();
    if (m_checked_at.has_value() && *m_checked_at <= now && now - *m_checked_at < CHECK_INTERVAL)
        return;
    if (m_attempted_at.has_value() && *m_attempted_at <= now && now - *m_attempted_at < RETRY_INTERVAL)
        return;
    check();
}

void Updater::check()
{
    m_updating = true;
    m_attempted_at = UnixDateTime::now();

    auto manifest_url = m_base_url.complete_url(MANIFEST_NAME);
    auto signature_url = m_base_url.complete_url(SIGNATURE_NAME);
    if (!manifest_url.has_value() || !signature_url.has_value()) {
        fail("the manifest's URL isn't valid"sv);
        return;
    }

    Download::start(manifest_url.release_value(), MAXIMUM_MANIFEST_SIZE, [this, signature_url = signature_url.release_value()](ErrorOr<ByteBuffer> manifest) mutable {
        if (manifest.is_error()) {
            fail(manifest.error());
            return;
        }
        Download::start(move(signature_url), MAXIMUM_SIGNATURE_SIZE, [this, manifest = manifest.release_value()](ErrorOr<ByteBuffer> signature) mutable {
            if (signature.is_error()) {
                fail(signature.error());
                return;
            }
            did_download_manifest(move(manifest), signature.release_value());
        });
    });
}

void Updater::did_download_manifest(ByteBuffer manifest, ByteBuffer signature)
{
    if (!bundle_signature_is_valid(manifest, StringView { signature }, m_public_key)) {
        fail("the manifest isn't signed with Xenon's key"sv);
        return;
    }

    auto new_bundle = parse_bundle_manifest(StringView { manifest });
    if (new_bundle.is_error()) {
        fail(new_bundle.error());
        return;
    }

    // Lists for a newer Xenon, or ones no newer than these, leave these as they are until tomorrow.
    if (new_bundle.value().format != BUNDLE_FORMAT || !is_newer(new_bundle.value(), m_current)) {
        if (new_bundle.value().format != BUNDLE_FORMAT)
            dbgln("The latest filter lists are for a newer version of Xenon");
        m_checked_at = UnixDateTime::now();
        save_state();
        m_updating = false;
        return;
    }

    auto staging = LexicalPath::join(m_directory, STAGING_NAME).string();
    auto prepared = [&]() -> ErrorOr<void> {
        if (FileSystem::exists(staging))
            TRY(FileSystem::remove(staging, FileSystem::RecursionMode::Allowed));
        for (auto subdirectory : { "lists"sv, "trusted-lists"sv, "resources"sv })
            TRY(Core::Directory::create(LexicalPath::join(staging, subdirectory), Core::Directory::CreateDirectories::Yes));
        return {};
    }();
    if (prepared.is_error()) {
        fail(prepared.error());
        return;
    }

    m_new_manifest = move(manifest);
    m_new_signature = move(signature);
    m_new_bundle = new_bundle.release_value();
    m_next_file = 0;
    update_next_file();
}

void Updater::update_next_file()
{
    auto staging = LexicalPath::join(m_directory, STAGING_NAME).string();

    for (; m_next_file < m_new_bundle->files.size(); ++m_next_file) {
        auto const& file = m_new_bundle->files[m_next_file];
        auto destination = LexicalPath::join(staging, file.path).string();

        // A file that hasn't changed is copied from the lists Xenon has, rather than downloaded again.
        auto copied = [&](Optional<Bundle> const& bundle) {
            if (!bundle.has_value())
                return false;
            auto existing = bundle->manifest.file(file.path);
            if (!existing.has_value() || existing->sha256 != file.sha256)
                return false;
            auto contents = read_file(LexicalPath::join(bundle->directory, file.path).string());
            return !contents.is_error() && file_matches(file, contents.value()) && !write_file(destination, contents.value()).is_error();
        };
        if (copied(m_current) || copied(m_bundled))
            continue;

        auto url = m_base_url.complete_url(file.path);
        if (!url.has_value()) {
            fail("a file's URL isn't valid"sv);
            return;
        }
        Download::start(url.release_value(), file.size, [this, destination = move(destination)](ErrorOr<ByteBuffer> contents) {
            if (contents.is_error()) {
                fail(contents.error());
                return;
            }
            if (!file_matches(m_new_bundle->files[m_next_file], contents.value())) {
                fail("a file doesn't match the manifest's hash for it"sv);
                return;
            }
            if (auto result = write_file(destination, contents.value()); result.is_error()) {
                fail(result.error());
                return;
            }
            ++m_next_file;
            update_next_file();
        });
        return;
    }

    finish_update();
}

void Updater::finish_update()
{
    auto staging = LexicalPath::join(m_directory, STAGING_NAME).string();

    // The directory is named for when the lists were generated: "2026-10-27 05:17 UTC" is 202610270517.
    StringBuilder name;
    for (auto c : m_new_bundle->generated_at.view()) {
        if (is_ascii_digit(c))
            name.append(c);
    }
    auto destination = LexicalPath::join(m_directory, name.string_view()).string();

    auto installed = [&]() -> ErrorOr<void> {
        TRY(write_file(LexicalPath::join(staging, MANIFEST_NAME).string(), m_new_manifest));
        TRY(write_file(LexicalPath::join(staging, SIGNATURE_NAME).string(), m_new_signature));
        if (FileSystem::exists(destination))
            TRY(FileSystem::remove(destination, FileSystem::RecursionMode::Allowed));
        TRY(Core::System::rename(staging, destination));
        return {};
    }();
    if (installed.is_error()) {
        fail(installed.error());
        return;
    }

    auto bundle = load_bundle(destination, true);
    if (!bundle.has_value()) {
        fail("the downloaded lists don't load"sv);
        return;
    }

    auto generated_at = bundle->manifest.generated_at;
    use_bundle(bundle.release_value());
    m_checked_at = UnixDateTime::now();
    save_state();

    // Ladybird has read the new lists, so the older ones can go.
    Core::DirIterator iterator(m_directory, Core::DirIterator::SkipDots);
    while (iterator.has_next()) {
        auto path = iterator.next_full_path();
        if (path != destination && FileSystem::is_directory(path))
            (void)FileSystem::remove(path, FileSystem::RecursionMode::Allowed);
    }

    m_updating = false;
    m_new_manifest.clear();
    m_new_signature.clear();
    m_new_bundle.clear();
    dbgln("Updated the filter lists to those generated {}", generated_at);
}

void Updater::fail(Error const& error)
{
    fail(ByteString::formatted("{}", error));
}

void Updater::fail(StringView reason)
{
    warnln("Unable to update the filter lists: {}", reason);

    auto staging = LexicalPath::join(m_directory, STAGING_NAME).string();
    if (FileSystem::exists(staging))
        (void)FileSystem::remove(staging, FileSystem::RecursionMode::Allowed);

    m_updating = false;
    m_new_manifest.clear();
    m_new_signature.clear();
    m_new_bundle.clear();
}

}
