/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteBuffer.h>
#include <AK/Function.h>
#include <AK/RefCounted.h>
#include <AK/RefPtr.h>
#include <AK/Time.h>
#include <LibCore/Forward.h>
#include <LibHTTP/HeaderList.h>
#include <LibRequests/Forward.h>
#include <LibURL/URL.h>

namespace Ladybird {

class Download final : public RefCounted<Download> {
public:
    using Callback = Function<void(ErrorOr<ByteBuffer>)>;

    static constexpr auto DEFAULT_TIMEOUT = AK::Duration::from_seconds(120);

    static void start(URL::URL, u64 maximum_size, Callback, AK::Duration timeout = DEFAULT_TIMEOUT);

private:
    Download(u64 maximum_size, Callback);

    void fetch(URL::URL);
    void did_receive_headers(HTTP::HeaderList const&, Optional<u32> response_code);
    void did_receive_data(ReadonlyBytes);
    void finish(ErrorOr<ByteBuffer>);

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

}
