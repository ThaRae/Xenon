/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Vector.h>
#include <LibCore/Environment.h>
#include <LibWebView/Application.h>
#include <UI/RelaunchToUpdate.h>
#include <UI/StringUtils.h>
#include <UI/XenonSettings.h>

#include <QAction>
#include <QCoreApplication>
#include <QPointer>

#import <Cocoa/Cocoa.h>
#import <Sparkle/Sparkle.h>
#import <UI/Conversions.h>
#import <UI/Updater.h>

@interface XenonUpdaterDelegate : NSObject <SPUUpdaterDelegate>
@end

namespace Ladybird::Updater {

// What Meta/release.py writes into a release's Info.plist: where the appcast is, and the key its updates are signed with.
static NSString* const FEED_URL_KEY = @"SUFeedURL";
static NSString* const PUBLIC_KEY_KEY = @"SUPublicEDKey";
// Where install_and_relaunch() notes the version it installs, for the relaunch to find (relaunched_to_update()).
static NSString* const RELAUNCH_VERSION_KEY = @"XenonRelaunchToUpdateVersion";

class SettingsFollower;

static SPUStandardUpdaterController* s_controller;
static XenonUpdaterDelegate* s_delegate;
static SettingsFollower* s_settings_follower;
static Status s_status;
// Sparkle's way to install the update that's ready at once, rather than at quit, and relaunch.
static void (^s_install_now)(void);
static Vector<Function<bool()>> s_status_listeners;

static void notify_status_changed()
{
    s_status_listeners.remove_all_matching([](auto& listener) { return !listener(); });
}

// A release: Meta/release.py packaged it, and wrote where its updates come from into its Info.plist.
static bool is_release_build()
{
    auto* info = [[NSBundle mainBundle] infoDictionary];
    return info[FEED_URL_KEY] && info[PUBLIC_KEY_KEY];
}

static ByteString to_byte_string(NSString* string)
{
    return ns_string_to_string(string).to_byte_string();
}

// XENON_TEST_UPDATE_READY's stand-in for an update that's ready, which only development builds take (UI/Updater.h).
static Optional<ByteString> const& test_ready_version()
{
    static auto const version = []() -> Optional<ByteString> {
        auto value = Core::Environment::get("XENON_TEST_UPDATE_READY"sv);
        if (is_release_build() || !value.has_value() || value->is_empty())
            return {};
        return ByteString { *value };
    }();
    return version;
}

// Sparkle checks and downloads by itself only while both of Xenon's switches allow it. Checking from the menu still works
// with either off.
class SettingsFollower final
    : public XenonSettings::Observer
    , public WebView::SettingsObserver {
public:
    static void apply()
    {
        auto automatic = XenonSettings::automatic_updates_enabled() && WebView::Application::settings().background_networking_enabled();
        auto* updater = s_controller.updater;
        if (updater.automaticallyChecksForUpdates != automatic)
            updater.automaticallyChecksForUpdates = automatic;
        if (updater.automaticallyDownloadsUpdates != automatic)
            updater.automaticallyDownloadsUpdates = automatic;
    }

private:
    // XenonSettings::Observer
    virtual void xenon_settings_changed() override { apply(); }

    // WebView::SettingsObserver
    virtual void background_networking_settings_changed() override { apply(); }
};

void initialize()
{
    VERIFY(!s_controller);

    if (!is_release_build())
        return;

    s_delegate = [[XenonUpdaterDelegate alloc] init];
    s_controller = [[SPUStandardUpdaterController alloc] initWithStartingUpdater:NO updaterDelegate:s_delegate userDriverDelegate:nil];

    s_settings_follower = new SettingsFollower;
    SettingsFollower::apply();

    NSError* error = nil;
    if (![s_controller.updater startUpdater:&error]) {
        warnln("Xenon can't update itself: {}", to_byte_string(error.localizedDescription));
        s_status.error = to_byte_string(error.localizedDescription);
        return;
    }
    if (auto* checked_at = s_controller.updater.lastUpdateCheckDate)
        s_status.checked_at = UnixDateTime::from_seconds_since_epoch(checked_at.timeIntervalSince1970);

    // Sparkle checks once a day by itself. Xenon checks at launch too, so that it catches up as soon as it's opened.
    if (s_controller.updater.automaticallyChecksForUpdates)
        [s_controller.updater checkForUpdatesInBackground];
}

ByteString version()
{
    NSString* version = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"CFBundleShortVersionString"];
    if ([version isKindOfClass:[NSString class]])
        return to_byte_string(version);
    return XENON_VERSION;
}

Status status()
{
    if (auto const& stand_in = test_ready_version(); stand_in.has_value()) {
        Status status;
        status.availability = Availability::Available;
        status.ready_version = stand_in;
        return status;
    }

    auto status = s_status;
    status.availability = is_release_build() ? Availability::Available : Availability::DevelopmentBuild;
    return status;
}

void on_status_change(Function<bool()> listener)
{
    s_status_listeners.append(move(listener));
}

void check_for_updates()
{
    if (s_controller && s_controller.updater.canCheckForUpdates)
        [s_controller checkForUpdates:nil];
}

void install_and_relaunch()
{
    // The stand-in has nothing to install, so Xenon only quits, through the system as Sparkle has it do, which asks every
    // page first too.
    if (auto const& stand_in = test_ready_version(); stand_in.has_value()) {
        dbgln("Relaunching to update to Xenon {}, which XENON_TEST_UPDATE_READY stands in for, so Xenon only quits", *stand_in);
        QCoreApplication::quit();
        return;
    }

    // Sparkle starts the installer, then asks the application to quit, which asks every page first (Application::quit()).
    // A page that keeps Xenon open leaves the update to install at the next quit.
    if (!s_install_now || !s_status.ready_version.has_value())
        return;
    [[NSUserDefaults standardUserDefaults] setObject:string_to_ns_string(*s_status.ready_version) forKey:RELAUNCH_VERSION_KEY];
    s_install_now();
}

bool relaunched_to_update()
{
    // NB: The session store and main() both ask as Xenon starts, so the note is read once, then forgotten.
    static auto const relaunched = [] {
        auto* defaults = [NSUserDefaults standardUserDefaults];
        NSString* installing = [defaults stringForKey:RELAUNCH_VERSION_KEY];
        if (!installing)
            return false;
        [defaults removeObjectForKey:RELAUNCH_VERSION_KEY];
        // A relaunch that a page turned down leaves the update to install when Xenon next quits, and the windows reopen
        // after that one instead.
        return to_byte_string(installing) == version();
    }();
    return relaunched;
}

QAction* create_check_for_updates_action(QObject& parent)
{
    // NB: The menus exist before the updater starts.
    if (!is_release_build() && !test_ready_version().has_value())
        return nullptr;

    auto* action = new QAction(&parent);
    action->setMenuRole(QAction::ApplicationSpecificRole);

    // While an update is ready, the item installs it instead, as macOS apps' menus do.
    auto update_action = [action = QPointer<QAction> { action }] {
        if (!action)
            return false;
        if (auto ready_version = status().ready_version; ready_version.has_value())
            action->setText(qformatted("Relaunch to Update to {}", *ready_version));
        else
            action->setText("Check for &Updates…");
        return true;
    };
    update_action();
    on_status_change(move(update_action));

    QObject::connect(action, &QAction::triggered, &parent, [] {
        if (status().ready_version.has_value())
            RelaunchToUpdate::relaunch(nullptr);
        else
            check_for_updates();
    });
    return action;
}

static void did_start_check()
{
    s_status.checking = true;
    notify_status_changed();
}

// Sparkle holds the update cycle here until Xenon quits, so the check ends with it.
static void did_find_update_ready(NSString* version, void (^install_now)(void))
{
    s_install_now = install_now;
    s_status.ready_version = to_byte_string(version);
    s_status.checking = false;
    s_status.error.clear();
    dbgln("Xenon {} is ready, and installs when Xenon quits", *s_status.ready_version);
    notify_status_changed();
}

static void did_finish_check(SPUUpdater* updater, NSError* error)
{
    s_status.checking = false;
    if (auto* checked_at = updater.lastUpdateCheckDate)
        s_status.checked_at = UnixDateTime::from_seconds_since_epoch(checked_at.timeIntervalSince1970);

    // Finding nothing new isn't a failure, and neither is choosing not to install.
    auto failed = error && !([error.domain isEqualToString:SUSparkleErrorDomain] && (error.code == SUNoUpdateError || error.code == SUInstallationCanceledError));
    if (failed) {
        auto reason = to_byte_string(error.localizedDescription);
        if (NSError* underlying = error.userInfo[NSUnderlyingErrorKey])
            reason = ByteString::formatted("{} ({})", reason, to_byte_string(underlying.localizedDescription));
        warnln("Unable to update Xenon: {}", reason);
        s_status.error = move(reason);
    } else {
        s_status.error.clear();
    }
    notify_status_changed();
}

}

@implementation XenonUpdaterDelegate

// Xenon's own setting decides whether Sparkle checks by itself, so Sparkle never asks.
- (BOOL)updaterShouldPromptForPermissionToCheckForUpdates:(SPUUpdater*)updater
{
    return NO;
}

- (BOOL)updater:(SPUUpdater*)updater mayPerformUpdateCheck:(SPUUpdateCheck)updateCheck error:(NSError* __autoreleasing*)error
{
    Ladybird::Updater::did_start_check();
    return YES;
}

// A downloaded update that checked out. Xenon says so in its settings, and Sparkle installs it at quit.
- (BOOL)updater:(SPUUpdater*)updater willInstallUpdateOnQuit:(SUAppcastItem*)item immediateInstallationBlock:(void (^)(void))immediateInstallHandler
{
    Ladybird::Updater::did_find_update_ready(item.displayVersionString, immediateInstallHandler);
    return YES;
}

// A check that found nothing, or failed: the error says why, from the appcast to the archive's signature.
- (void)updater:(SPUUpdater*)updater didFinishUpdateCycleForUpdateCheck:(SPUUpdateCheck)updateCheck error:(NSError*)error
{
    Ladybird::Updater::did_finish_check(updater, error);
}

@end
