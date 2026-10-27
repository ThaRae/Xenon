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

// Keeps Xenon itself up to date, on macOS, with Sparkle, as Helium does. Meta/release.py publishes each release to
// tharae/xenon-releases on git.avx512.dev, with an appcast that lists it (Documentation/Updates.md).
//
// Sparkle checks the appcast's signature, the archive's EdDSA signature against the key in Info.plist before it unpacks
// anything, and that the new app's code signature is intact, before it replaces anything. It installs the update when
// Xenon quits, by swapping the whole app at once, so an update that fails or is refused leaves Xenon as it was.
//
// Only release builds update themselves: Meta/release.py writes the appcast's URL and the key into their Info.plist.
// Development builds have neither, and Xenon doesn't update itself elsewhere than macOS yet.
namespace Ladybird::Updater {

// Starts the updater, if this build updates itself. While automatic updates (XenonSettings) and Ladybird's background
// connections are on, it checks at launch and once a day, downloads what it finds, and installs it at quit.
void initialize();

// This build's version, like 0.1.0 (VERSION.txt, at the top of the repository).
ByteString version();

enum class Availability : u8 {
    Available,
    // A build of Xenon that Meta/release.py didn't package.
    DevelopmentBuild,
    UnsupportedPlatform,
};

struct Status {
    Availability availability { Availability::UnsupportedPlatform };
    bool checking { false };
    // A new version that has been downloaded and checked, and installs when Xenon quits.
    Optional<ByteString> ready_version;
    Optional<UnixDateTime> checked_at;
    // Why the last check, download or install failed, until one succeeds.
    Optional<ByteString> error;
};
Status status();

// Called whenever the status changes, until the callback returns false.
void on_status_change(Function<bool()>);

// Checks now, in Sparkle's window, which offers whatever it finds. The menu's Check for Updates… and the settings page's
// Check now.
void check_for_updates();

// Installs the update that's ready, and relaunches Xenon once every page has agreed to close.
void install_and_relaunch();

// Check for Updates…, which macOS shows in the application menu. None where Xenon doesn't update itself.
QAction* create_check_for_updates_action(QObject& parent);

}
