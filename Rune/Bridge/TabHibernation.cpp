/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/QuickSort.h>
#include <LibCore/EventLoop.h>
#include <LibCore/Timer.h>
#include <LibWebView/ViewImplementation.h>
#include <Rune/Bridge/Engine.h>
#include <Rune/Bridge/TabHibernation.h>
#include <Rune/Bridge/TabMemory.h>
#include <Rune/Bridge/View.h>
#include <UI/MemoryPressure.h>

namespace Xenon {

static constexpr auto AUDIO_PROTECTION_TIME = AK::Duration::from_seconds(60);

static XenonHibernationCallbacks s_callbacks {};

void set_hibernation_callbacks(XenonHibernationCallbacks const& callbacks)
{
    s_callbacks = callbacks;
}

template<typename Callback, typename... Arguments>
static void notify(Callback XenonHibernationCallbacks::* callback, Arguments... arguments)
{
    if (auto function = s_callbacks.*callback)
        function(s_callbacks.context, arguments...);
}

Vector<u8> hibernated_tab_icon(ReadonlyBytes rgba)
{
    Vector<u8> greyscale;
    greyscale.resize(rgba.size());
    for (size_t offset = 0; offset + 3 < rgba.size(); offset += 4) {
        auto grey = static_cast<u8>((rgba[offset] * 11 + rgba[offset + 1] * 16 + rgba[offset + 2] * 5) / 32);
        greyscale[offset] = grey;
        greyscale[offset + 1] = grey;
        greyscale[offset + 2] = grey;
        greyscale[offset + 3] = rgba[offset + 3];
    }
    return greyscale;
}

static bool has_picture_in_picture_window(WebView::ViewImplementation& view)
{
    auto has_window = false;
    WebView::ViewImplementation::for_each_view([&](auto& other_view) {
        has_window = other_view.owner_view_id() == view.view_id();
        return has_window ? IterationDecision::Break : IterationDecision::Continue;
    });
    return has_window;
}

TabHibernation::TabHibernation()
{
    m_timer = Core::Timer::create_repeating(AK::Duration::from_seconds(30), [this] { check_tabs(); });
}

TabHibernation::~TabHibernation() = default;

void TabHibernation::start()
{
    settings_changed();
    report_settings();
}

void TabHibernation::xenon_settings_changed()
{
    settings_changed();
    report_settings();
}

void TabHibernation::settings_changed()
{
    if (!Ladybird::XenonSettings::tab_hibernation_enabled()) {
        m_timer->stop();
        return;
    }

    auto timeout = Ladybird::XenonSettings::tab_hibernation_timeout().to_milliseconds();
    auto interval = clamp<i64>(timeout / 4, 1000, 30 * 1000);
    if (Ladybird::XenonSettings::hibernate_on_memory_pressure())
        interval = min(interval, static_cast<i64>(5000));
    m_timer->restart(AK::Duration::from_milliseconds(interval));
}

void TabHibernation::report_settings() const
{
    Vector<u32> choices;
    for (auto minutes : Ladybird::XenonSettings::tab_hibernation_timeout_choices_in_minutes())
        choices.append(static_cast<u32>(minutes));
    XenonHibernationSettings settings {
        .enabled = Ladybird::XenonSettings::tab_hibernation_enabled(),
        .timeout_seconds = static_cast<u64>(Ladybird::XenonSettings::tab_hibernation_timeout().to_seconds()),
        .timeout_choices_in_minutes = choices.data(),
        .timeout_choice_count = choices.size(),
        .hibernate_on_memory_pressure = Ladybird::XenonSettings::hibernate_on_memory_pressure(),
    };
    notify(&XenonHibernationCallbacks::settings_changed, static_cast<XenonHibernationSettings const*>(&settings));
}

void TabHibernation::change_settings(bool enabled, Optional<AK::Duration> timeout)
{
    Ladybird::XenonSettings::set_tab_hibernation_enabled(enabled);
    if (enabled && timeout.has_value())
        Ladybird::XenonSettings::set_tab_hibernation_timeout(*timeout);
}

TabHibernation::TabState& TabHibernation::state_for(XenonViewId id)
{
    return m_tabs.ensure(id, [] { return TabState { .last_used = MonotonicTime::now() }; });
}

bool TabHibernation::is_in_use(XenonViewId id) const
{
    auto const* window = Engine::the().model() ? Engine::the().model()->window_of(id) : nullptr;
    return !window || window->active_tab == id;
}

bool TabHibernation::may_hibernate(XenonViewId id)
{
    auto* model = Engine::the().model();
    auto const* tab = model ? model->tab(id) : nullptr;
    auto* view = Engine::the().view(id);
    if (!tab || tab->deferred || !view || is_in_use(id))
        return false;

    if (tab->pinned || view->is_loading())
        return false;

    if (view->tab_indicator_visible(WebView::TabIndicator::PlayingAudio) || view->has_devtools_client() || has_picture_in_picture_window(*view))
        return false;
    if (auto state = m_tabs.get(id); state.has_value() && state->last_audible.has_value() && MonotonicTime::now() - *state->last_audible < AUDIO_PROTECTION_TIME)
        return false;

    return view->can_hibernate();
}

void TabHibernation::hibernate_when_ready(XenonViewId id, Request request)
{
    auto* view = Engine::the().view(id);
    if (!view)
        return;
    auto& state = state_for(id);
    if (state.is_preparing)
        return;
    state.is_preparing = true;
    state.request = request;

    auto preparation = view->prepare_for_discard();
    preparation->when_resolved([id](auto& result) {
        Core::deferred_invoke([id, has_unsubmitted_input = result.has_unsubmitted_input] {
            if (auto* hibernation = Engine::the().tab_hibernation())
                hibernation->finish_hibernating(id, has_unsubmitted_input);
        });
    });
    preparation->when_rejected([id](auto&) {
        if (auto* hibernation = Engine::the().tab_hibernation()) {
            if (auto state = hibernation->m_tabs.get(id); state.has_value())
                state->is_preparing = false;
        }
    });
}

void TabHibernation::check_tabs()
{
    auto* model = Engine::the().model();
    if (!model)
        return;

    auto timeout = Ladybird::XenonSettings::tab_hibernation_timeout();
    auto now = MonotonicTime::now();

    for (auto window_id : model->windows_by_activation()) {
        auto const* window = model->window(window_id);
        if (!window)
            continue;
        for (auto id : window->tabs) {
            auto& state = state_for(id);
            auto* view = Engine::the().view(id);
            auto const* tab = model->tab(id);
            if (!view || !tab || tab->deferred || state.is_preparing)
                continue;

            if (is_in_use(id)) {
                state.last_used = now;
                continue;
            }
            if (view->tab_indicator_visible(WebView::TabIndicator::PlayingAudio))
                state.last_audible = now;

            if (now - state.last_used >= timeout && may_hibernate(id))
                hibernate_when_ready(id, Request::Timeout);
        }
    }

    hibernate_under_memory_pressure();
}

void TabHibernation::hibernate_under_memory_pressure()
{
    if (!Ladybird::XenonSettings::hibernate_on_memory_pressure())
        return;
    auto* model = Engine::the().model();
    if (!model)
        return;

    struct Candidate {
        XenonViewId view { 0 };
        MonotonicTime last_used;
    };
    Vector<Candidate> eligible;
    for (auto window_id : model->windows_by_activation()) {
        auto const* window = model->window(window_id);
        if (!window)
            continue;
        for (auto id : window->tabs) {
            auto& state = state_for(id);
            if (state.is_preparing || !may_hibernate(id))
                continue;
            eligible.append({ id, state.last_used });
        }
    }
    quick_sort(eligible, [](Candidate const& a, Candidate const& b) { return a.last_used < b.last_used; });

    auto wanted = Ladybird::MemoryPressure::tabs_to_hibernate(Ladybird::MemoryPressure::level_for(Ladybird::MemoryPressure::sample()), eligible.size());
    for (size_t i = 0; i < wanted; ++i)
        hibernate_when_ready(eligible[i].view, Request::MemoryPressure);
}

void TabHibernation::finish_hibernating(XenonViewId id, bool has_unsubmitted_input)
{
    auto state = m_tabs.get(id);
    if (!state.has_value())
        return;
    state->is_preparing = false;

    if (has_unsubmitted_input) {
        state->last_used = MonotonicTime::now();
        return;
    }

    if (state->request == Request::Timeout) {
        if (!Ladybird::XenonSettings::tab_hibernation_enabled() || MonotonicTime::now() - state->last_used < Ladybird::XenonSettings::tab_hibernation_timeout())
            return;
    } else if (state->request == Request::MemoryPressure) {
        if (!Ladybird::XenonSettings::tab_hibernation_enabled() || !Ladybird::XenonSettings::hibernate_on_memory_pressure())
            return;
    }
    if (!may_hibernate(id))
        return;

    auto* view = Engine::the().view(id);
    auto* model = Engine::the().model();
    VERIFY(view && model);

    auto usage = TabMemory::usage(*view);
    state->memory_freed = usage.bytes > 0 && !usage.is_estimate ? Optional<u64> { usage.bytes } : Optional<u64> {};

    Optional<View::Favicon> icon;
    if (auto const& favicon = view->favicon(); favicon.has_value())
        icon = View::Favicon { .width = favicon->width, .height = favicon->height, .rgba = hibernated_tab_icon(favicon->rgba.span()) };

    if (!model->hibernate_tab(id, view->url(), view->session_history_snapshot())) {
        state->memory_freed.clear();
        return;
    }

    if (icon.has_value()) {
        XenonTabIcon tab_icon { .width = icon->width, .height = icon->height, .rgba = icon->rgba.data() };
        report_load_state(id, XENON_TAB_HIBERNATED, &tab_icon);
    } else {
        report_load_state(id, XENON_TAB_HIBERNATED);
    }
}

void TabHibernation::report_load_state(XenonViewId id, u32 state, XenonTabIcon const* icon) const
{
    notify(&XenonHibernationCallbacks::tab_load_state_changed, id, state, icon);
}

void TabHibernation::tab_inserted_unloaded(XenonViewId id)
{
    report_load_state(id, XENON_TAB_NOT_LOADED);
}

void TabHibernation::tab_loaded(XenonViewId id)
{
    if (auto state = m_tabs.get(id); state.has_value()) {
        state->memory_freed.clear();
        state->last_used = MonotonicTime::now();
    }
    report_load_state(id, XENON_TAB_LOADED);
}

void TabHibernation::tab_activated(Optional<XenonViewId> previous, XenonViewId current)
{
    auto now = MonotonicTime::now();
    if (previous.has_value())
        state_for(*previous).last_used = now;
    state_for(current).last_used = now;
}

void TabHibernation::tab_removed(XenonViewId id)
{
    m_tabs.remove(id);
}

ByteString TabHibernation::unloaded_tab_description(XenonViewId id) const
{
    auto const* tab = Engine::the().model() ? Engine::the().model()->tab(id) : nullptr;
    if (!tab || !tab->hibernated)
        return TabMemory::description(nullptr);
    auto state = m_tabs.get(id);
    return TabMemory::hibernated_description(state.has_value() ? state->memory_freed : Optional<u64> {});
}

}
