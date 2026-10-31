/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteString.h>
#include <AK/Function.h>
#include <AK/Optional.h>
#include <AK/Time.h>

class QAction;
class QObject;

namespace Ladybird::Updater {

void initialize();

ByteString version();

enum class Availability : u8 {
    Available,
    DevelopmentBuild,
    UnsupportedPlatform,
};

struct Status {
    Availability availability { Availability::UnsupportedPlatform };
    bool checking { false };
    Optional<ByteString> ready_version;
    Optional<UnixDateTime> checked_at;
    Optional<ByteString> error;
};
Status status();

void on_status_change(Function<bool()>);

void check_for_updates();

void install_and_relaunch();

bool relaunched_to_update();

QAction* create_check_for_updates_action(QObject& parent);

}
