/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/CharacterTypes.h>
#include <AK/JsonArray.h>
#include <AK/JsonObject.h>
#include <AK/LexicalPath.h>
#include <AK/Platform.h>
#include <LibCore/Environment.h>
#include <LibCore/File.h>
#include <LibCore/Notifier.h>
#include <LibCore/StandardPaths.h>
#include <UI/Extensions/NativeMessaging.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace Ladybird::Extensions {

static constexpr size_t MAX_HOST_MESSAGE_SIZE = 1024 * 1024;
static constexpr size_t MAX_BROWSER_MESSAGE_SIZE = 1024 * 1024 * 1024;
// A host's stdout is read this much at a time, and at most a message's worth each time it's readable, so that a host
// that never stops writing can't hold up the event loop: the notifier fires again for the rest.
static constexpr size_t READ_CHUNK_SIZE = 16 * 1024;
static constexpr size_t MAX_READ_PER_ACTIVATION = sizeof(u32) + MAX_HOST_MESSAGE_SIZE;
static constexpr auto HOSTS_VARIABLE = "XENON_NATIVE_MESSAGING_HOSTS"sv;

static String host_error(StringView message)
{
    return MUST(String::from_utf8(message));
}

bool is_valid_native_host_name(StringView name)
{
    if (name.is_empty() || name.starts_with('.') || name.ends_with('.'))
        return false;

    bool previous_was_dot = false;
    for (auto character : name) {
        if (character == '.') {
            if (previous_was_dot)
                return false;
            previous_was_dot = true;
            continue;
        }
        previous_was_dot = false;
        if (!is_ascii_lower_alpha(character) && !is_ascii_digit(character) && character != '_')
            return false;
    }
    return true;
}

Vector<ByteString> native_messaging_host_directories()
{
    Vector<ByteString> directories;

    if (auto extra = Core::Environment::get(HOSTS_VARIABLE); extra.has_value()) {
        extra->for_each_split_view(':', SplitBehavior::Nothing, [&](auto path) {
            if (!path.is_empty())
                directories.append(path);
        });
        return directories;
    }

    auto home = Core::StandardPaths::home_directory();
#if defined(AK_OS_MACOS)
    directories.append(LexicalPath::join(home, "Library/Application Support/Xenon/NativeMessagingHosts"sv).string());
    directories.append("/Library/Application Support/Xenon/NativeMessagingHosts");
    // Apple registers iCloud Passwords' helper for Chrome. Xenon is not Chrome, but the helper's allowed_origins
    // lists the store extension by its Chrome ID, which is the ID Xenon keeps when it installs that extension.
    directories.append(LexicalPath::join(home, "Library/Application Support/Google/Chrome/NativeMessagingHosts"sv).string());
    directories.append("/Library/Google/Chrome/NativeMessagingHosts");
    directories.append(LexicalPath::join(home, "Library/Application Support/Chromium/NativeMessagingHosts"sv).string());
    directories.append("/Library/Application Support/Chromium/NativeMessagingHosts");
#else
    auto config = Core::StandardPaths::config_directory();
    directories.append(LexicalPath::join(config, "xenon/NativeMessagingHosts"sv).string());
    directories.append("/etc/xenon/native-messaging-hosts");
    directories.append(LexicalPath::join(config, "google-chrome/NativeMessagingHosts"sv).string());
    directories.append("/etc/opt/chrome/native-messaging-hosts");
    directories.append(LexicalPath::join(config, "chromium/NativeMessagingHosts"sv).string());
    directories.append("/etc/chromium/native-messaging-hosts");
#endif
    return directories;
}

static bool origin_is_allowed(Vector<String> const& allowed_origins, StringView extension_id)
{
    auto with_slash = MUST(String::formatted("chrome-extension://{}/", extension_id));
    auto without_slash = MUST(String::formatted("chrome-extension://{}", extension_id));
    return allowed_origins.contains_slow(with_slash) || allowed_origins.contains_slow(without_slash);
}

static ErrorOr<NativeHost, String> parse_native_host_manifest(ByteString const& path, StringView expected_name)
{
    auto file = Core::File::open(path, Core::File::OpenMode::Read);
    if (file.is_error())
        return host_error(NATIVE_HOST_NOT_FOUND);

    auto contents = file.value()->read_until_eof();
    if (contents.is_error())
        return host_error(NATIVE_HOST_NOT_FOUND);

    auto json = JsonValue::from_string(StringView { contents.value() });
    if (json.is_error() || !json.value().is_object())
        return host_error(NATIVE_HOST_NOT_FOUND);

    auto const& object = json.value().as_object();
    auto name = object.get_string("name"sv);
    auto host_path = object.get_string("path"sv);
    auto type = object.get_string("type"sv);
    auto origins = object.get_array("allowed_origins"sv);
    if (!name.has_value() || *name != expected_name)
        return host_error(NATIVE_HOST_NOT_FOUND);
    if (!host_path.has_value() || !type.has_value() || *type != "stdio"sv || !origins.has_value())
        return host_error(NATIVE_HOST_NOT_FOUND);
    if (!LexicalPath { host_path->to_byte_string() }.is_absolute())
        return host_error(NATIVE_HOST_NOT_FOUND);

    NativeHost host;
    host.name = *name;
    host.path = *host_path;
    for (auto const& origin : origins->values()) {
        if (origin.is_string())
            host.allowed_origins.append(origin.as_string());
    }
    return host;
}

ErrorOr<NativeHost, String> find_native_host(StringView name, StringView extension_id)
{
    if (!is_valid_native_host_name(name))
        return host_error(NATIVE_HOST_NOT_FOUND);

    auto filename = ByteString::formatted("{}.json", name);
    Optional<NativeHost> found;
    for (auto const& directory : native_messaging_host_directories()) {
        auto path = LexicalPath::join(directory, filename).string();
        auto parsed = parse_native_host_manifest(path, name);
        if (parsed.is_error())
            continue;
        found = parsed.release_value();
        break;
    }
    if (!found.has_value())
        return host_error(NATIVE_HOST_NOT_FOUND);
    if (!origin_is_allowed(found->allowed_origins, extension_id))
        return host_error(NATIVE_HOST_FORBIDDEN);
    return found.release_value();
}

ByteBuffer encode_native_message(JsonValue const& message)
{
    auto serialized = message.serialized();
    u32 size = serialized.bytes().size();
    ByteBuffer encoded;
    MUST(encoded.try_ensure_capacity(sizeof(size) + serialized.bytes().size()));
    MUST(encoded.try_append(ReadonlyBytes { &size, sizeof(size) }));
    MUST(encoded.try_append(serialized.bytes()));
    return encoded;
}

ErrorOr<Optional<JsonValue>, String> take_native_message(ByteBuffer& buffer)
{
    if (buffer.size() < sizeof(u32))
        return Optional<JsonValue> {};

    u32 size = 0;
    memcpy(&size, buffer.data(), sizeof(size));
    if (size > MAX_HOST_MESSAGE_SIZE)
        return host_error(NATIVE_HOST_COMMUNICATION_ERROR);
    if (buffer.size() < sizeof(u32) + size)
        return Optional<JsonValue> {};

    auto payload = StringView { buffer.bytes().slice(sizeof(u32), size) };
    auto json = JsonValue::from_string(payload);
    buffer = MUST(buffer.slice(sizeof(u32) + size, buffer.size() - sizeof(u32) - size));
    if (json.is_error())
        return host_error(NATIVE_HOST_COMMUNICATION_ERROR);
    return json.release_value();
}

static ErrorOr<void> set_cloexec(int fd)
{
    auto flags = fcntl(fd, F_GETFD);
    if (flags < 0)
        return Error::from_errno(errno);
    if (fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0)
        return Error::from_errno(errno);
    return {};
}

static ErrorOr<void> set_nonblocking(int fd)
{
    auto flags = fcntl(fd, F_GETFL);
    if (flags < 0)
        return Error::from_errno(errno);
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
        return Error::from_errno(errno);
    return {};
}

static void close_fd(int& fd)
{
    if (fd < 0)
        return;
    ::close(fd);
    fd = -1;
}

// Writes to a host's stdin. Writing to a host that has gone must fail with EPIPE instead of raising SIGPIPE, which would
// end the browser: macOS turns SIGPIPE off for the pipe itself (F_SETNOSIGPIPE, when the host starts), and elsewhere it's
// blocked for the write, and the one the write raised is taken before it's unblocked.
static ssize_t write_to_host(int fd, ReadonlyBytes bytes)
{
#if defined(F_SETNOSIGPIPE)
    return ::write(fd, bytes.data(), bytes.size());
#else
    sigset_t sigpipe;
    sigemptyset(&sigpipe);
    sigaddset(&sigpipe, SIGPIPE);

    sigset_t pending;
    sigpending(&pending);
    bool was_pending = sigismember(&pending, SIGPIPE);

    sigset_t previous_mask;
    pthread_sigmask(SIG_BLOCK, &sigpipe, &previous_mask);
    auto written = ::write(fd, bytes.data(), bytes.size());
    auto write_errno = errno;
    if (written < 0 && write_errno == EPIPE && !was_pending) {
        timespec no_wait {};
        while (sigtimedwait(&sigpipe, nullptr, &no_wait) < 0 && errno == EINTR) { }
    }
    pthread_sigmask(SIG_SETMASK, &previous_mask, nullptr);
    errno = write_errno;
    return written;
#endif
}

ErrorOr<NonnullRefPtr<NativeHostProcess>, String> NativeHostProcess::start(
    StringView application,
    StringView extension_id,
    Function<void(JsonValue)> on_message,
    Function<void(Optional<String>)> on_disconnect)
{
    auto host = TRY(find_native_host(application, extension_id));

    int stdin_pipe[2];
    int stdout_pipe[2];
    if (pipe(stdin_pipe) < 0)
        return host_error(NATIVE_HOST_FAILED_TO_START);
    if (pipe(stdout_pipe) < 0) {
        ::close(stdin_pipe[0]);
        ::close(stdin_pipe[1]);
        return host_error(NATIVE_HOST_FAILED_TO_START);
    }

    auto fail_pipes = [&] {
        ::close(stdin_pipe[0]);
        ::close(stdin_pipe[1]);
        ::close(stdout_pipe[0]);
        ::close(stdout_pipe[1]);
        return host_error(NATIVE_HOST_FAILED_TO_START);
    };

    if (set_cloexec(stdin_pipe[0]).is_error() || set_cloexec(stdin_pipe[1]).is_error()
        || set_cloexec(stdout_pipe[0]).is_error() || set_cloexec(stdout_pipe[1]).is_error())
        return fail_pipes();
#if defined(F_SETNOSIGPIPE)
    if (fcntl(stdin_pipe[1], F_SETNOSIGPIPE, 1) < 0)
        return fail_pipes();
#endif

    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) != 0)
        return fail_pipes();
    posix_spawnattr_t attributes;
    if (posix_spawnattr_init(&attributes) != 0) {
        posix_spawn_file_actions_destroy(&actions);
        return fail_pipes();
    }

    auto host_path = host.path.to_byte_string();
    auto origin = MUST(String::formatted("chrome-extension://{}/", extension_id)).to_byte_string();
    auto working_directory = LexicalPath::dirname(host_path);

    posix_spawn_file_actions_adddup2(&actions, stdin_pipe[0], STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&actions, stdout_pipe[1], STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&actions, stdin_pipe[0]);
    posix_spawn_file_actions_addclose(&actions, stdin_pipe[1]);
    posix_spawn_file_actions_addclose(&actions, stdout_pipe[0]);
    posix_spawn_file_actions_addclose(&actions, stdout_pipe[1]);
#if defined(AK_OS_MACOS) || defined(__GLIBC__)
    posix_spawn_file_actions_addchdir_np(&actions, working_directory.characters());
#endif

    // The host gets stdin, stdout, and stderr, and none of the browser's other files and sockets, as in Chrome.
#if defined(AK_OS_MACOS)
    posix_spawn_file_actions_addinherit_np(&actions, STDERR_FILENO);
    posix_spawnattr_setflags(&attributes, POSIX_SPAWN_CLOEXEC_DEFAULT);
#elif defined(__GLIBC__)
#    if __GLIBC_PREREQ(2, 34)
    posix_spawn_file_actions_addclosefrom_np(&actions, STDERR_FILENO + 1);
#    endif
#endif

    char* argv[] = { const_cast<char*>(host_path.characters()), const_cast<char*>(origin.characters()), nullptr };

    pid_t pid = -1;
    auto spawned = posix_spawn(&pid, host_path.characters(), &actions, &attributes, argv, environ);
    posix_spawnattr_destroy(&attributes);
    posix_spawn_file_actions_destroy(&actions);
    ::close(stdin_pipe[0]);
    ::close(stdout_pipe[1]);
    if (spawned != 0) {
        ::close(stdin_pipe[1]);
        ::close(stdout_pipe[0]);
        return host_error(NATIVE_HOST_FAILED_TO_START);
    }

    if (set_nonblocking(stdin_pipe[1]).is_error() || set_nonblocking(stdout_pipe[0]).is_error()) {
        ::close(stdin_pipe[1]);
        ::close(stdout_pipe[0]);
        ::kill(pid, SIGTERM);
        return host_error(NATIVE_HOST_FAILED_TO_START);
    }

    auto process = adopt_ref(*new NativeHostProcess);
    process->m_pid = pid;
    process->m_stdin_fd = stdin_pipe[1];
    process->m_stdout_fd = stdout_pipe[0];
    process->m_on_message = move(on_message);
    process->m_on_disconnect = move(on_disconnect);

    process->m_stdout_notifier = Core::Notifier::construct(process->m_stdout_fd, Core::Notifier::Type::Read);
    process->m_stdout_notifier->on_activation = [process = process.ptr()] {
        process->read_from_host();
    };

    process->m_stdin_notifier = Core::Notifier::construct(process->m_stdin_fd, Core::Notifier::Type::Write);
    process->m_stdin_notifier->set_enabled(false);
    process->m_stdin_notifier->on_activation = [process = process.ptr()] {
        process->flush_outgoing();
    };

    return process;
}

NativeHostProcess::~NativeHostProcess()
{
    disconnect();
}

void NativeHostProcess::post_message(JsonValue const& message)
{
    NonnullRefPtr protect = *this;
    if (m_closed || m_stdin_fd < 0)
        return;
    auto encoded = encode_native_message(message);
    if (encoded.size() - sizeof(u32) > MAX_BROWSER_MESSAGE_SIZE) {
        closed_by_host(host_error(NATIVE_HOST_COMMUNICATION_ERROR));
        return;
    }
    MUST(m_outgoing.try_append(encoded.bytes()));
    flush_outgoing();
}

void NativeHostProcess::disconnect()
{
    m_on_message = nullptr;
    m_on_disconnect = nullptr;
    close_pipes();
}

void NativeHostProcess::flush_outgoing()
{
    NonnullRefPtr protect = *this;
    while (!m_outgoing.is_empty() && m_stdin_fd >= 0) {
        auto written = write_to_host(m_stdin_fd, m_outgoing.bytes());
        if (written < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            closed_by_host(host_error(NATIVE_HOST_COMMUNICATION_ERROR));
            return;
        }
        if (written == 0)
            break;
        m_outgoing = MUST(m_outgoing.slice(written, m_outgoing.size() - written));
    }
    if (m_stdin_notifier)
        m_stdin_notifier->set_enabled(!m_outgoing.is_empty());
}

void NativeHostProcess::read_from_host()
{
    NonnullRefPtr protect = *this;

    // Each message goes to the opener as soon as it's whole, so that a reply arrives even when the host exits right after
    // sending it, and one that's too large is refused before the rest of it is read.
    u8 chunk[READ_CHUNK_SIZE];
    for (size_t read_so_far = 0; read_so_far < MAX_READ_PER_ACTIVATION && !m_closed;) {
        auto nread = ::read(m_stdout_fd, chunk, sizeof(chunk));
        if (nread < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return;
            closed_by_host(host_error(NATIVE_HOST_COMMUNICATION_ERROR));
            return;
        }
        if (nread == 0) {
            closed_by_host(host_error(NATIVE_HOST_EXITED));
            return;
        }
        read_so_far += nread;
        MUST(m_incoming.try_append(ReadonlyBytes { chunk, static_cast<size_t>(nread) }));

        while (!m_closed) {
            auto message = take_native_message(m_incoming);
            if (message.is_error()) {
                closed_by_host(message.release_error());
                return;
            }
            if (!message.value().has_value())
                break;
            if (m_on_message)
                m_on_message(*message.value());
        }
    }
}

// Whether the host still has its end of stdout open: reading what's left in the pipe gets to the end of the file once
// it's closed, and to EAGAIN while it's open.
bool NativeHostProcess::host_holds_stdout() const
{
    u8 chunk[READ_CHUNK_SIZE];
    for (size_t read_so_far = 0; read_so_far < MAX_READ_PER_ACTIVATION;) {
        auto nread = ::read(m_stdout_fd, chunk, sizeof(chunk));
        if (nread == 0)
            return false;
        if (nread < 0)
            return errno == EAGAIN || errno == EWOULDBLOCK;
        read_so_far += nread;
    }
    return true;
}

void NativeHostProcess::closed_by_host(Optional<String> error)
{
    NonnullRefPtr protect = *this;
    if (m_closed)
        return;
    auto on_disconnect = move(m_on_disconnect);
    m_on_message = nullptr;
    m_on_disconnect = nullptr;
    close_pipes();
    if (on_disconnect)
        on_disconnect(move(error));
}

void NativeHostProcess::close_pipes()
{
    m_closed = true;
    m_stdout_notifier = nullptr;
    m_stdin_notifier = nullptr;
    close_fd(m_stdin_fd);
    // A host should exit once its stdin closes, but one that doesn't would run on with no port, so Xenon terminates it, as
    // Chrome does. Only while it still holds stdout, though: once that's closed it has exited, and LibWebView's
    // ProcessMonitor, which reaps every child, may have reaped it and freed its pid for another process.
    if (m_pid > 0 && m_stdout_fd >= 0 && host_holds_stdout())
        ::kill(m_pid, SIGTERM);
    close_fd(m_stdout_fd);
    if (m_pid > 0) {
        ::waitpid(m_pid, nullptr, WNOHANG);
        m_pid = -1;
    }
}

}
