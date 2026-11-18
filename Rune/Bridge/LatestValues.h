/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/HashMap.h>
#include <AK/Mutex.h>
#include <AK/Optional.h>

namespace Xenon {

template<typename Key, typename Value>
class LatestValues {
public:
    [[nodiscard]] bool set(Key const& key, Value value)
    {
        MutexLocker locker(m_mutex);
        return m_values.set(key, move(value)) == HashSetResult::InsertedNewEntry;
    }

    Optional<Value> take(Key const& key)
    {
        MutexLocker locker(m_mutex);
        return m_values.take(key);
    }

private:
    Mutex m_mutex;
    HashMap<Key, Value> m_values;
};

}
