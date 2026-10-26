/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Time.h>
#include <LibCore/EventLoop.h>
#include <UI/Extensions/Alarms.h>

namespace Ladybird::Extensions {

static constexpr double MILLISECONDS_PER_MINUTE = 60'000;
static constexpr double MINIMUM_PACKED_DELAY_IN_MINUTES = 0.5;

static double now_in_milliseconds()
{
    return static_cast<double>(UnixDateTime::now().milliseconds_since_epoch());
}

Alarms::Alarms(Function<void(JsonObject)> on_alarm)
    : m_on_alarm(move(on_alarm))
{
}

Alarms::~Alarms()
{
    clear_all();
}

JsonObject Alarms::to_json(String const& name, Alarm const& alarm)
{
    JsonObject json;
    json.set("name"sv, name);
    json.set("scheduledTime"sv, alarm.scheduled_time);
    if (alarm.period_in_minutes.has_value())
        json.set("periodInMinutes"sv, *alarm.period_in_minutes);
    return json;
}

ErrorOr<void, String> Alarms::create(String const& name, JsonObject const& info)
{
    auto when = info.get_double_with_precision_loss("when"sv);
    auto delay = info.get_double_with_precision_loss("delayInMinutes"sv);
    auto period = info.get_double_with_precision_loss("periodInMinutes"sv);
    if (when.has_value() && delay.has_value())
        return "Cannot set both when and delayInMinutes."_string;

    if (!m_is_unpacked) {
        if (delay.has_value())
            delay = max(*delay, MINIMUM_PACKED_DELAY_IN_MINUTES);
        if (period.has_value())
            period = max(*period, MINIMUM_PACKED_DELAY_IN_MINUTES);
    }

    // An alarm with neither fires after its period, or at once.
    double scheduled_time = now_in_milliseconds();
    if (when.has_value())
        scheduled_time = *when;
    else if (delay.has_value())
        scheduled_time += *delay * MILLISECONDS_PER_MINUTE;
    else if (period.has_value())
        scheduled_time += *period * MILLISECONDS_PER_MINUTE;

    clear(name);
    m_alarms.set(name, { .scheduled_time = scheduled_time, .period_in_minutes = period, .timer = nullptr });
    schedule(name);
    return {};
}

void Alarms::schedule(String const& name)
{
    auto alarm = m_alarms.get(name);
    VERIFY(alarm.has_value());

    auto delay = AK::Duration::from_milliseconds(static_cast<i64>(max(0.0, alarm->scheduled_time - now_in_milliseconds())));
    if (alarm->timer) {
        alarm->timer->restart(delay);
        return;
    }
    alarm->timer = Core::Timer::create_single_shot(delay, [this, name] { fire(name); });
    alarm->timer->start();
}

void Alarms::fire(String const& name)
{
    auto alarm = m_alarms.get(name);
    if (!alarm.has_value())
        return;

    auto json = to_json(name, *alarm);
    if (alarm->period_in_minutes.has_value()) {
        // NB: A periodic alarm keeps its timer, which this runs in.
        alarm->scheduled_time = now_in_milliseconds() + *alarm->period_in_minutes * MILLISECONDS_PER_MINUTE;
        schedule(name);
    } else {
        clear(name);
    }
    m_on_alarm(move(json));
}

Optional<JsonObject> Alarms::get(String const& name) const
{
    auto alarm = m_alarms.get(name);
    if (!alarm.has_value())
        return {};
    return to_json(name, *alarm);
}

Vector<JsonObject> Alarms::all() const
{
    Vector<JsonObject> alarms;
    for (auto const& [name, alarm] : m_alarms)
        alarms.append(to_json(name, alarm));
    return alarms;
}

// NB: An alarm may be cleared from its own timer's callback, so its timer goes only once that has returned.
static void release_timer(RefPtr<Core::Timer> timer)
{
    if (!timer)
        return;
    timer->stop();
    Core::deferred_invoke([timer = move(timer)] { });
}

bool Alarms::clear(String const& name)
{
    auto alarm = m_alarms.take(name);
    if (!alarm.has_value())
        return false;
    release_timer(move(alarm->timer));
    return true;
}

bool Alarms::clear_all()
{
    auto had_alarms = !m_alarms.is_empty();
    for (auto& [name, alarm] : m_alarms)
        release_timer(move(alarm.timer));
    m_alarms.clear();
    return had_alarms;
}

}
