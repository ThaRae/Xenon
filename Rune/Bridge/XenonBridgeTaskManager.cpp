/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/ByteString.h>
#include <AK/HashMap.h>
#include <AK/HashTable.h>
#include <AK/StringBuilder.h>
#include <LibCore/Timer.h>
#include <LibURL/InternalURLs.h>
#include <LibURL/URL.h>
#include <LibWebView/Application.h>
#include <LibWebView/Menu.h>
#include <LibWebView/ProcessManager.h>
#include <LibWebView/ProcessSnapshot.h>
#include <Rune/Bridge/Engine.h>
#include <Rune/Bridge/TaskManager.h>
#include <Rune/Bridge/XenonBridgeTaskManager.h>

#include <errno.h>

namespace Xenon::TaskManager {

static constexpr auto REFRESH_INTERVAL = AK::Duration::from_seconds(1);

static XenonTaskManagerCallbacks s_callbacks {};
static HashTable<String> s_sent_favicons;

template<typename Callback, typename... Arguments>
static void notify(Callback XenonTaskManagerCallbacks::* callback, Arguments... arguments)
{
    if (auto function = s_callbacks.*callback)
        function(s_callbacks.context, arguments...);
}

static XenonStr xenon_str(StringView string)
{
    return { string.characters_without_null_termination(), string.length() };
}

String site_label(URL::URL const& url)
{
    if (url == URL::about_blank())
        return {};
    if (auto host = url.host(); host.has_value()) {
        if (auto registrable_domain = host->registrable_domain(); registrable_domain.has_value())
            return *registrable_domain;
        if (auto serialized_host = url.serialized_host(); !serialized_host.is_empty())
            return MUST(String::from_utf8(serialized_host));
    }
    if (url.scheme() == "file"sv && !url.basename().is_empty())
        return MUST(String::from_byte_string(url.basename()));
    return MUST(String::formatted("{}:", url.scheme()));
}

struct Row {
    WebView::ProcessSnapshot const* process { nullptr };
    ByteString name {};
    u32 icon { XENON_PROCESS_ICON_SERVICE };
    bool can_end { false };
    String favicon_hash {};
    Optional<ByteBuffer> favicon_png {};
};

static void refresh()
{
    auto& process_manager = WebView::Application::process_manager();
    auto processes = WebView::process_snapshots(process_manager);

    HashMap<u64, WebView::ProcessSnapshot const*> processes_by_child_id;
    for (auto const& process : processes)
        processes_by_child_id.set(process.child_id, &process);

    Vector<Row> rows;
    rows.ensure_capacity(processes.size());
    for (auto const& process : processes) {
        Row row { .process = &process };
        StringBuilder name;
        name.append(WebView::process_name_from_type(process.type));

        if (first_is_one_of(process.type, WebView::ProcessType::WebContent, WebView::ProcessType::ImageDecoder, WebView::ProcessType::RequestServer, WebView::ProcessType::WebWorker) || process.owner_child_id.has_value()) {
            auto const* label_process = &process;
            for (size_t depth = 0; depth < processes.size() && !label_process->site_url.has_value() && label_process->owner_child_id.has_value(); ++depth) {
                auto owner = processes_by_child_id.get(*label_process->owner_child_id);
                if (!owner.has_value())
                    break;
                label_process = *owner;
            }

            auto const& page = label_process->page;
            auto const& site_url = label_process->site_url;
            auto is_spare = label_process->title.has_value() && *label_process->title == "(spare)"_utf16;
            if (site_url.has_value()) {
                if (auto site = site_label(*site_url); !site.is_empty())
                    name.appendff(" — {}", site);
            } else if (label_process->title.has_value()) {
                name.appendff(" — {}", label_process->title->to_utf8());
            }

            if (process.type == WebView::ProcessType::WebContent) {
                row.icon = is_spare ? XENON_PROCESS_ICON_SPARE : XENON_PROCESS_ICON_PAGE;
                if (page.has_value() && page->url != URL::about_blank() && page->favicon_hash.has_value()) {
                    row.favicon_hash = *page->favicon_hash;
                    if (!s_sent_favicons.contains(row.favicon_hash)) {
                        row.favicon_png = WebView::favicon_png_for_page(*page);
                        if (row.favicon_png.has_value())
                            s_sent_favicons.set(row.favicon_hash);
                    }
                }
            }
        } else {
            if (process.title.has_value())
                name.appendff(" — {}", process.title->to_utf8());
            if (process.type == WebView::ProcessType::Browser)
                row.icon = XENON_PROCESS_ICON_BROWSER;
        }

        row.name = name.to_byte_string();
        row.can_end = process.type != WebView::ProcessType::Browser && process_manager.find_child(process.child_id).has_value();
        rows.append(move(row));
    }

    Vector<XenonProcess> reported;
    reported.ensure_capacity(rows.size());
    for (auto const& row : rows) {
        auto const& process = *row.process;
        reported.append({
            .child_id = process.child_id,
            .pid = static_cast<int64_t>(process.pid),
            .icon = row.icon,
            .is_browser = process.type == WebView::ProcessType::Browser,
            .can_end = row.can_end,
            .name = xenon_str(row.name),
            .cpu_percent = static_cast<double>(process.cpu_percent),
            .memory_bytes = process.memory_usage_bytes,
            .favicon_hash = xenon_str(row.favicon_hash),
            .favicon_png = row.favicon_png.has_value() ? row.favicon_png->data() : nullptr,
            .favicon_png_length = row.favicon_png.has_value() ? row.favicon_png->size() : 0,
        });
    }
    notify(&XenonTaskManagerCallbacks::processes_changed, static_cast<XenonProcess const*>(reported.data()), reported.size());
}

static Core::Timer& refresh_timer()
{
    static auto& timer = Core::Timer::create_repeating(REFRESH_INTERVAL, [] { refresh(); }).leak_ref();
    return timer;
}

static void set_visible(bool visible)
{
    if (!visible) {
        refresh_timer().stop();
        return;
    }
    s_sent_favicons.clear();
    refresh();
    refresh_timer().start();
}

static void end_process(u64 child_id)
{
    auto& process_manager = WebView::Application::process_manager();
    auto process = process_manager.find_child(child_id);
    if (!process.has_value() || process->type() == WebView::ProcessType::Browser)
        return;
    auto pid = process->pid();
    if (auto result = process_manager.terminate_child(child_id, Core::Process::TerminationMode::Forceful); result.is_error()) {
        if (!result.error().is_errno() || result.error().code() != ESRCH) {
            auto message = ByteString::formatted("Could not end process {}: {}", pid, result.error());
            notify(&XenonTaskManagerCallbacks::end_process_failed, xenon_str(message));
        }
    }
    if (refresh_timer().is_active())
        refresh();
}

static void copy_pid(u64 child_id)
{
    auto process = WebView::Application::process_manager().find_child(child_id);
    if (!process.has_value())
        return;
    WebView::Application::the().set_clipboard_text(String::number(process->pid()));
}

void add_open_action()
{
    WebView::Application::the().inspect_menu().add_action(WebView::Action::create("Open Task Manager"sv, WebView::ActionID::OpenTaskManager, [] {
        notify(&XenonTaskManagerCallbacks::open_requested);
    }));
}

}

void xenon_engine_set_task_manager_callbacks(XenonTaskManagerCallbacks const* callbacks)
{
    Xenon::TaskManager::s_callbacks = callbacks ? *callbacks : XenonTaskManagerCallbacks {};
}

void xenon_engine_task_manager_set_visible(bool visible)
{
    Xenon::Engine::the().post([visible] { Xenon::TaskManager::set_visible(visible); });
}

void xenon_engine_task_manager_end_process(uint64_t child_id)
{
    Xenon::Engine::the().post([child_id] { Xenon::TaskManager::end_process(child_id); });
}

void xenon_engine_task_manager_copy_pid(uint64_t child_id)
{
    Xenon::Engine::the().post([child_id] { Xenon::TaskManager::copy_pid(child_id); });
}
