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

constexpr auto NATIVE_HOST_NOT_FOUND = "Specified native messaging host not found."sv;
constexpr auto NATIVE_HOST_FORBIDDEN = "Access to the specified native messaging host is forbidden."sv;
constexpr auto NATIVE_HOST_FAILED_TO_START = "Failed to start native messaging host."sv;
constexpr auto NATIVE_HOST_EXITED = "Native host has exited."sv;
constexpr auto NATIVE_HOST_COMMUNICATION_ERROR = "Error when communicating with the native messaging host."sv;

struct NativeHost {
    String name;
    String path;
    Vector<String> allowed_origins;
};

bool is_valid_native_host_name(StringView name);

Vector<ByteString> native_messaging_host_directories();

ErrorOr<NativeHost, String> find_native_host(StringView name, StringView extension_id);

ByteBuffer encode_native_message(JsonValue const&);
ErrorOr<Optional<JsonValue>, String> take_native_message(ByteBuffer&);

class NativeHostProcess : public RefCounted<NativeHostProcess> {
public:
    static ErrorOr<NonnullRefPtr<NativeHostProcess>, String> start(
        StringView application,
        StringView extension_id,
        Function<void(JsonValue)> on_message,
        Function<void(Optional<String>)> on_disconnect);

    ~NativeHostProcess();

    void post_message(JsonValue const&);
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
