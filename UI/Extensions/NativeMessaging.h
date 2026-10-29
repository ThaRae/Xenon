/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteBuffer.h>
#include <AK/ByteString.h>
#include <AK/Function.h>
#include <AK/JsonValue.h>
#include <AK/Optional.h>
#include <AK/RefCounted.h>
#include <AK/RefPtr.h>
#include <AK/String.h>
#include <AK/Vector.h>
#include <LibCore/Forward.h>

namespace Ladybird::Extensions {

// Chrome's native messaging errors, which connectNative's port.onDisconnect reports through runtime.lastError.
constexpr auto NATIVE_HOST_NOT_FOUND = "Specified native messaging host not found."sv;
constexpr auto NATIVE_HOST_FORBIDDEN = "Access to the specified native messaging host is forbidden."sv;
constexpr auto NATIVE_HOST_FAILED_TO_START = "Failed to start native messaging host."sv;
constexpr auto NATIVE_HOST_EXITED = "Native host has exited."sv;
constexpr auto NATIVE_HOST_COMMUNICATION_ERROR = "Error when communicating with the native messaging host."sv;

// A native messaging host's manifest, as Chrome reads it from NativeMessagingHosts/<name>.json.
// https://developer.chrome.com/docs/extensions/develop/concepts/native-messaging
struct NativeHost {
    String name;
    String path;
    Vector<String> allowed_origins;
};

// Lowercase letters, digits, dots, and underscores; no leading, trailing, or consecutive dots. Chrome's host names.
bool is_valid_native_host_name(StringView name);

// Directories Xenon searches for host manifests, user then system. XENON_NATIVE_MESSAGING_HOSTS, a colon-separated
// list, replaces them, so tests don't see the machine's Chrome hosts. On macOS the list includes Chrome's locations,
// which is where Apple registers com.apple.passwordmanager for iCloud Passwords.
Vector<ByteString> native_messaging_host_directories();

// The host named `name` whose allowed_origins lists this extension, or Chrome's not-found / forbidden error.
ErrorOr<NativeHost, String> find_native_host(StringView name, StringView extension_id);

// Chrome's native messaging framing: a 32-bit native-endian length, then that many bytes of UTF-8 JSON. Hosts may send
// at most 1 MiB; the browser may send at most 1 GiB.
ByteBuffer encode_native_message(JsonValue const&);
// One complete message from the front of the buffer, which it consumes, or nothing if a full message isn't there yet.
ErrorOr<Optional<JsonValue>, String> take_native_message(ByteBuffer&);

// A native messaging host process, talking over stdin/stdout. Not QProcess: LibWebView's ProcessMonitor waitpid(-1)
// would reap that first.
class NativeHostProcess : public RefCounted<NativeHostProcess> {
public:
    static ErrorOr<NonnullRefPtr<NativeHostProcess>, String> start(
        StringView application,
        StringView extension_id,
        Function<void(JsonValue)> on_message,
        Function<void(Optional<String>)> on_disconnect);

    ~NativeHostProcess();

    void post_message(JsonValue const&);
    // Closes the pipes without telling the opener; the opener is closing the port itself.
    void disconnect();

private:
    NativeHostProcess() = default;

    void flush_outgoing();
    void read_from_host();
    bool host_holds_stdout() const;
    void closed_by_host(Optional<String> error);
    void close_pipes();

    pid_t m_pid { -1 };
    int m_stdin_fd { -1 };
    int m_stdout_fd { -1 };
    RefPtr<Core::Notifier> m_stdout_notifier;
    RefPtr<Core::Notifier> m_stdin_notifier;
    ByteBuffer m_incoming;
    ByteBuffer m_outgoing;
    Function<void(JsonValue)> m_on_message;
    Function<void(Optional<String>)> m_on_disconnect;
    bool m_closed { false };
};

}
