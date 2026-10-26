/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Function.h>
#include <AK/HashMap.h>
#include <AK/JsonObject.h>
#include <AK/Optional.h>
#include <AK/String.h>
#include <AK/Vector.h>
#include <LibCore/Timer.h>

namespace Ladybird::Extensions {

// An extension's chrome.alarms, which fire while the browser runs.
// https://developer.chrome.com/docs/extensions/reference/api/alarms
class Alarms {
    AK_MAKE_NONCOPYABLE(Alarms);

public:
    AK_ALLOC_WITH_KMALLOC;

    // Called with each alarm as it fires, as alarms.onAlarm reports it.
    explicit Alarms(Function<void(JsonObject)> on_alarm);
    ~Alarms();

    // alarms.create(name, {when, delayInMinutes, periodInMinutes}). An alarm of the same name is replaced.
    ErrorOr<void, String> create(String const& name, JsonObject const& info);
    Optional<JsonObject> get(String const& name) const;
    Vector<JsonObject> all() const;
    bool clear(String const& name);
    bool clear_all();

    // Unpacked extensions may set alarms as often as they like, as in Chrome; others wait at least 30 seconds.
    void set_is_unpacked(bool is_unpacked) { m_is_unpacked = is_unpacked; }

private:
    struct Alarm {
        // Milliseconds since the epoch.
        double scheduled_time { 0 };
        Optional<double> period_in_minutes;
        RefPtr<Core::Timer> timer;
    };

    static JsonObject to_json(String const& name, Alarm const&);
    void schedule(String const& name);
    void fire(String const& name);

    Function<void(JsonObject)> m_on_alarm;
    HashMap<String, Alarm> m_alarms;
    bool m_is_unpacked { true };
};

}
