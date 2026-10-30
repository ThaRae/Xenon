/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/HashMap.h>
#include <AK/HashTable.h>
#include <AK/Time.h>
#include <LibWebView/Application.h>
#include <LibWebView/CanonicalTraversable.h>
#include <LibWebView/Process.h>
#include <LibWebView/ProcessManager.h>
#include <LibWebView/TabPerformanceStats.h>
#include <LibWebView/ViewImplementation.h>
#include <LibWebView/WebContentClient.h>
#include <LibWebView/WebContentPage.h>
#include <LibWebView/WorkerProcessManager.h>
#include <UI/StringUtils.h>
#include <UI/Tab.h>
#include <UI/TabHibernation.h>
#include <UI/TabMemory.h>

namespace Ladybird::TabMemory {

static constexpr auto USAGE_LIFETIME = AK::Duration::from_seconds(1);

struct SampledUsage {
    u64 view_id { 0 };
    MonotonicTime sampled_at;
    Usage usage;
};
static Optional<SampledUsage> s_last_usage;

static u64 tab_view_id(u64 view_id)
{
    if (auto view = WebView::ViewImplementation::find_view_by_id(view_id); view.has_value())
        return view->owner_view_id().value_or(view_id);
    return view_id;
}

static HashMap<pid_t, HashTable<u64>> tabs_by_process()
{
    HashMap<pid_t, HashTable<u64>> tabs;
    auto& process_manager = WebView::Application::process_manager();

    WebView::ViewImplementation::for_each_view([&](WebView::ViewImplementation& view) {
        if (!view.has_traversable())
            return IterationDecision::Continue;

        auto tab = view.owner_view_id().value_or(view.view_id());
        if (auto page = view.traversable().display_page())
            tabs.ensure(page->client().pid()).set(tab);
        view.traversable().for_each_in_subtree([&](auto const& navigable) {
            if (navigable.has_remote_host())
                tabs.ensure(navigable.remote_host().client().pid()).set(tab);
            return IterationDecision::Continue;
        });
        return IterationDecision::Continue;
    });

    HashMap<u64, pid_t> pids_by_child_id;
    process_manager.for_each_process([&](WebView::Process& process) {
        pids_by_child_id.set(process.child_id(), process.pid());
        if (process.type() == WebView::ProcessType::WebWorker) {
            if (auto owner = WebView::WorkerProcessManager::the().exclusive_performance_owner(process.pid()); owner.has_value())
                tabs.ensure(process.pid()).set(tab_view_id(*owner));
        }
    });

    process_manager.for_each_process([&](WebView::Process& process) {
        if (process.type() != WebView::ProcessType::ImageDecoder || !process.owner_child_id().has_value())
            return;
        auto owner_pid = pids_by_child_id.get(*process.owner_child_id());
        if (!owner_pid.has_value())
            return;
        if (auto owner_tabs = tabs.get(*owner_pid); owner_tabs.has_value()) {
            auto served_tabs = *owner_tabs;
            tabs.set(process.pid(), move(served_tabs));
        }
    });

    return tabs;
}

Optional<Usage> usage(Tab& tab)
{
    if (!tab.is_loaded())
        return {};

    auto view_id = tab.view().view_id();
    auto now = MonotonicTime::now();
    if (s_last_usage.has_value() && s_last_usage->view_id == view_id && now - s_last_usage->sampled_at < USAGE_LIFETIME)
        return s_last_usage->usage;

    Usage usage;
    auto& process_manager = WebView::Application::process_manager();
    for (auto const& it : tabs_by_process()) {
        if (!it.value.contains(view_id))
            continue;
        auto resource_usage = process_manager.resource_usage(it.key);
        if (!resource_usage.has_value())
            continue;
        usage.bytes += resource_usage->attributed_memory_bytes / it.value.size();
        if (it.value.size() > 1)
            usage.is_estimate = true;
    }

    s_last_usage = SampledUsage { .view_id = view_id, .sampled_at = now, .usage = usage };
    return usage;
}

static QString format_bytes(u64 bytes)
{
    return qstring_from_ak_string(WebView::format_performance_bytes(bytes));
}

QString description(Tab& tab)
{
    if (tab.is_hibernated()) {
        if (auto freed = TabHibernation::memory_freed(tab); freed.has_value())
            return QStringLiteral("Hibernated, using no memory (freed %1)").arg(format_bytes(*freed));
        return QStringLiteral("Hibernated, using no memory");
    }
    if (!tab.is_loaded())
        return QStringLiteral("Not loaded yet, using no memory");

    auto tab_usage = usage(tab);
    if (!tab_usage.has_value() || tab_usage->bytes == 0)
        return {};
    return QStringLiteral("Memory usage: %1%2").arg(tab_usage->is_estimate ? QStringLiteral("about ") : QString {}, format_bytes(tab_usage->bytes));
}

}
