/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/HashMap.h>
#include <AK/JsonValue.h>
#include <AK/RefCounted.h>
#include <AK/TypeCasts.h>
#include <LibURL/Parser.h>
#include <LibWebView/Application.h>
#include <LibWebView/FarblingSeedStore.h>
#include <LibWebView/UserScriptHandler.h>
#include <LibWebView/ViewImplementation.h>
#include <UI/Blocking.h>
#include <UI/BrowserWindow.h>
#include <UI/Tab.h>
#include <UI/XenonSettings.h>

#include <QPointer>
#include <QTimer>

namespace Ladybird::Blocking {

static constexpr auto DASHBOARD_URL = "xenon://blocking/"sv;

// The popover reads what a page loaded from the page's performance timeline, in an isolated world of its own, so the
// page's scripts can neither see the question nor change the answer.
static constexpr auto DOMAINS_WORLD = "xenon-blocking"sv;
static constexpr auto DOMAINS_SCRIPT = R"~~~([location.href, ...performance.getEntriesByType("resource").map(entry => entry.name)])~~~"sv;

// How long the popover waits after a page's blocked count last changed before asking it for its domains again.
static constexpr int DOMAINS_REFRESH_DELAY_MS = 250;

URL::URL dashboard_url()
{
    return URL::Parser::basic_parse(DASHBOARD_URL).release_value();
}

void count_blocked_requests(WebView::ViewImplementation const& view, WebView::ContentBlockingSnapshot const& snapshot)
{
    // What each view's page had blocked when it last reported. A count lower than that is a new page's.
    static HashMap<u64, u64> s_counted;
    auto& counted = s_counted.ensure(view.view_id(), [] { return 0; });
    auto count = snapshot.blocked_request_count;
    XenonSettings::add_to_blocked_request_total(count >= counted ? count - counted : count);
    counted = count;
}

String site_switch_filter(BlockingSiteSwitch site_switch, StringView site)
{
    switch (site_switch) {
    case BlockingSiteSwitch::Popups:
        return MUST(String::formatted("*$popup,domain={}", site));
    case BlockingSiteSwitch::RemoteFonts:
        return MUST(String::formatted("*$font,domain={}", site));
    }
    VERIFY_NOT_REACHED();
}

bool site_switch_enabled(BlockingSiteSwitch site_switch, StringView site)
{
    auto filter = site_switch_filter(site_switch, site);
    auto const& filters = WebView::Application::settings().custom_content_blocker_filters();
    return filters.bytes_as_string_view().lines().contains_slow(filter.bytes_as_string_view());
}

void set_site_switch(BlockingSiteSwitch site_switch, StringView site, bool enabled)
{
    auto filter = site_switch_filter(site_switch, site);
    auto const& filters = WebView::Application::settings().custom_content_blocker_filters();

    Vector<StringView> lines;
    for (auto line : filters.bytes_as_string_view().lines()) {
        if (line != filter.bytes_as_string_view())
            lines.append(line);
    }
    while (!lines.is_empty() && lines.last().is_whitespace())
        lines.take_last();
    if (enabled)
        lines.append(filter);

    // The new filters are joined before the old ones they view are replaced.
    auto new_filters = MUST(String::join('\n', lines));
    WebView::Application::settings().set_custom_content_blocker_filters(move(new_filters));
}

void start_zapper(WebView::ViewImplementation& view)
{
    using Type = DevTools::DevToolsDelegate::NodePickerEvent::Type;

    view.start_node_picker([&view](DevTools::DevToolsDelegate::NodePickerEvent event) {
        switch (event.type) {
        case Type::Hovered:
            if (event.node_id.has_value())
                view.highlight_dom_node(*event.node_id, {});
            break;
        case Type::Previewed:
            break;
        case Type::Picked:
            view.stop_node_picker();
            view.clear_highlighted_dom_node();
            if (event.node_id.has_value())
                view.remove_dom_node(*event.node_id);
            break;
        case Type::Canceled:
            view.stop_node_picker();
            view.clear_highlighted_dom_node();
            break;
        }
    });
}

static bool is_network_url(URL::URL const& url)
{
    return url.scheme().is_one_of("http"sv, "https"sv, "ws"sv, "wss"sv) && url.host().has_value();
}

// Asks each of the view's documents what it loaded, and counts the requests by host.
class DomainQuery final : public RefCounted<DomainQuery> {
public:
    static void run(WebView::ViewImplementation& view, Function<void(Vector<BlockingDomain>)> on_done)
    {
        auto frames = view.frame_document_endpoints();
        auto query = adopt_ref(*new DomainQuery(view.url(), frames.size(), move(on_done)));
        if (frames.is_empty()) {
            query->finish();
            return;
        }

        for (auto const& frame : frames) {
            Web::HTML::UserScript script {
                .world = MUST(String::from_utf8(DOMAINS_WORLD)),
                .source = MUST(String::from_utf8(DOMAINS_SCRIPT)),
                .url = dashboard_url(),
                .injection_time = Web::HTML::UserScriptInjectionTime::DocumentStart,
                .takes_bridge = false,
            };
            frame.run_user_script(move(script), [query](WebView::UserScriptResult result) {
                if (result.ran && result.result.has_value())
                    query->add(*result.result);
                if (--query->m_remaining == 0)
                    query->finish();
            });
        }
    }

private:
    DomainQuery(URL::URL const& page_url, size_t frames, Function<void(Vector<BlockingDomain>)> on_done)
        : m_remaining(frames)
        , m_on_done(move(on_done))
    {
        if (is_network_url(page_url))
            m_page_site = site_of(*page_url.host());
    }

    // A host's registrable domain, or the host itself for an address or a name with no public suffix.
    static String site_of(URL::Host const& host)
    {
        if (auto registrable_domain = host.registrable_domain(); registrable_domain.has_value())
            return registrable_domain.release_value();
        return host.serialize();
    }

    void add(StringView result)
    {
        auto urls = JsonValue::from_string(result);
        if (urls.is_error() || !urls.value().is_array())
            return;
        for (auto const& value : urls.value().as_array().values()) {
            if (!value.is_string())
                continue;
            auto url = URL::Parser::basic_parse(value.as_string());
            if (!url.has_value() || !is_network_url(*url))
                continue;
            auto host = MUST(String::from_utf8(url->serialized_host()));
            m_requests.ensure(host, [&] { return Request { .site = site_of(*url->host()) }; }).count++;
        }
    }

    void finish()
    {
        Vector<BlockingDomain> domains;
        for (auto const& [host, request] : m_requests)
            domains.append({ .host = host, .requests = request.count, .first_party = m_page_site.has_value() && request.site == *m_page_site });
        m_on_done(move(domains));
    }

    struct Request {
        String site;
        size_t count { 0 };
    };

    size_t m_remaining { 0 };
    Function<void(Vector<BlockingDomain>)> m_on_done;
    Optional<String> m_page_site;
    HashMap<String, Request> m_requests;
};

void connect_popover(Tab& tab, BlockingPopover& popover)
{
    struct State : RefCounted<State> {
        // Whether a site switch changed while the popover was open: the page reloads to show it once it closes.
        bool switches_changed { false };
    };
    auto state = make_ref_counted<State>();

    auto* domains_timer = new QTimer(&popover);
    domains_timer->setSingleShot(true);
    domains_timer->setInterval(DOMAINS_REFRESH_DELAY_MS);
    QObject::connect(domains_timer, &QTimer::timeout, &popover, [&tab, popover = QPointer(&popover)] {
        DomainQuery::run(tab.view(), [popover](Vector<BlockingDomain> domains) {
            if (popover)
                popover->set_domains(move(domains));
        });
    });

    popover.on_refresh = [&tab, &popover, domains_timer] {
        popover.set_blocked_request_total(XenonSettings::blocked_request_total());
        if (auto site = tab.view().content_blocking_snapshot().site; site.has_value()) {
            for (auto site_switch : { BlockingSiteSwitch::Popups, BlockingSiteSwitch::RemoteFonts })
                popover.set_site_switch(site_switch, site_switch_enabled(site_switch, *site));
        }
        auto farbling = WebView::Application::farbling_seed_store().effective_seed(tab.view().url(), tab.view().is_private());
        popover.set_farbling_enabled(farbling.enabled());
        domains_timer->start();
    };

    popover.on_site_switch_toggle = [&tab, state](BlockingSiteSwitch site_switch, bool enabled) {
        auto site = tab.view().content_blocking_snapshot().site;
        if (!site.has_value())
            return;
        set_site_switch(site_switch, *site, enabled);
        state->switches_changed = true;
    };

    popover.on_hide = [&tab, state] {
        if (!exchange(state->switches_changed, false))
            return;
        tab.view().reload();
    };

    popover.on_pick_element = [&tab] {
        tab.view().start_element_picker([&tab](Optional<String> filter) {
            if (filter.has_value() && tab.view().on_element_picker_filter)
                tab.view().on_element_picker_filter(*filter);
        });
    };

    popover.on_zap_element = [&tab] {
        start_zapper(tab.view());
    };

    popover.on_open_dashboard = [&tab] {
        if (auto* window = as_if<BrowserWindow>(tab.window()))
            window->new_tab_from_url(dashboard_url(), Web::HTML::ActivateTab::Yes, BrowserWindow::TabLocation::after_current_tab());
    };
}

}
