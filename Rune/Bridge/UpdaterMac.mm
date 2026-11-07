/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Vector.h>
#include <LibCore/Environment.h>
#include <AK/Mutex.h>
#include <LibWebView/Application.h>
#include <Rune/Bridge/Engine.h>
#include <Rune/Bridge/UpdaterBridge.h>
#include <UI/Updater.h>
#include <UI/XenonSettings.h>

#import <Cocoa/Cocoa.h>
#import <Sparkle/Sparkle.h>

@interface XenonRuneUpdaterDelegate : NSObject <SPUUpdaterDelegate>
@end

namespace Ladybird::Updater {

static NSString* const FEED_URL_KEY = @"SUFeedURL";
static NSString* const PUBLIC_KEY_KEY = @"SUPublicEDKey";
static NSString* const RELAUNCH_VERSION_KEY = @"XenonRelaunchToUpdateVersion";

class SettingsFollower;

static SPUStandardUpdaterController* s_controller;
static XenonRuneUpdaterDelegate* s_delegate;
static SettingsFollower* s_settings_follower;
static void (^s_install_now)(void);
static Mutex s_status_mutex;
static Status s_status;
static Vector<Function<bool()>> s_status_listeners;

static ByteString to_byte_string(NSString* string)
{
    if (!string)
        return {};
    return ByteString { [string UTF8String] };
}

static NSString* to_ns_string(ByteString const& string)
{
    return [NSString stringWithUTF8String:string.characters()];
}

static bool is_release_build()
{
    auto* info = [[NSBundle mainBundle] infoDictionary];
    return info[FEED_URL_KEY] && info[PUBLIC_KEY_KEY];
}

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

template<typename Change>
static void change_status(Change change)
{
    {
        MutexLocker locker(s_status_mutex);
        change(s_status);
    }
    Xenon::Engine::the().post([] {
        s_status_listeners.remove_all_matching([](auto& listener) { return !listener(); });
        Xenon::report_update_status(status());
    });
}

class SettingsFollower final
    : public XenonSettings::Observer
    , public WebView::SettingsObserver {
public:
    static void apply()
    {
        auto automatic = XenonSettings::automatic_updates_enabled() && WebView::Application::settings().background_networking_enabled();
        dispatch_async(dispatch_get_main_queue(), ^{
            if (!s_controller)
                return;
            auto* updater = s_controller.updater;
            if (updater.automaticallyChecksForUpdates != automatic)
                updater.automaticallyChecksForUpdates = automatic;
            if (updater.automaticallyDownloadsUpdates != automatic)
                updater.automaticallyDownloadsUpdates = automatic;
        });
    }

private:
    virtual void xenon_settings_changed() override { apply(); }

    virtual void background_networking_settings_changed() override { apply(); }
};

static void start_sparkle(bool automatic)
{
    s_delegate = [[XenonRuneUpdaterDelegate alloc] init];
    s_controller = [[SPUStandardUpdaterController alloc] initWithStartingUpdater:NO updaterDelegate:s_delegate userDriverDelegate:nil];
    s_controller.updater.automaticallyChecksForUpdates = automatic;
    s_controller.updater.automaticallyDownloadsUpdates = automatic;

    NSError* error = nil;
    if (![s_controller.updater startUpdater:&error]) {
        auto reason = to_byte_string(error.localizedDescription);
        warnln("Xenon can't update itself: {}", reason);
        change_status([&](Status& status) { status.error = reason; });
        return;
    }
    if (auto* checked_at = s_controller.updater.lastUpdateCheckDate) {
        auto seconds = static_cast<i64>(checked_at.timeIntervalSince1970);
        change_status([&](Status& status) { status.checked_at = UnixDateTime::from_seconds_since_epoch(seconds); });
    }
    if (s_controller.updater.automaticallyChecksForUpdates)
        [s_controller.updater checkForUpdatesInBackground];
}

void initialize()
{
    if (test_ready_version().has_value()) {
        Xenon::Engine::the().post([] { Xenon::report_update_status(status()); });
        return;
    }
    if (!is_release_build() || s_settings_follower)
        return;

    s_settings_follower = new SettingsFollower;
    auto automatic = XenonSettings::automatic_updates_enabled() && WebView::Application::settings().background_networking_enabled();
    dispatch_async(dispatch_get_main_queue(), ^{
        start_sparkle(automatic);
    });
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

    Status status;
    {
        MutexLocker locker(s_status_mutex);
        status = s_status;
    }
    status.availability = is_release_build() ? Availability::Available : Availability::DevelopmentBuild;
    return status;
}

void on_status_change(Function<bool()> listener)
{
    s_status_listeners.append(move(listener));
}

void check_for_updates()
{
    dispatch_async(dispatch_get_main_queue(), ^{
        if (s_controller && s_controller.updater.canCheckForUpdates)
            [s_controller checkForUpdates:nil];
    });
}

void install_and_relaunch()
{
    if (auto const& stand_in = test_ready_version(); stand_in.has_value()) {
        dbgln("Relaunching to update to Xenon {}, which XENON_TEST_UPDATE_READY stands in for, so Xenon only quits", *stand_in);
        Xenon::Engine::the().notify(&XenonUiCallbacks::ui_command, static_cast<u32>(XENON_UI_COMMAND_QUIT));
        return;
    }

    auto ready_version = status().ready_version;
    if (!ready_version.has_value())
        return;
    auto version_string = to_ns_string(*ready_version);
    dispatch_async(dispatch_get_main_queue(), ^{
        if (!s_install_now)
            return;
        [[NSUserDefaults standardUserDefaults] setObject:version_string forKey:RELAUNCH_VERSION_KEY];
        s_install_now();
    });
}

bool relaunched_to_update()
{
    static auto const relaunched = [] {
        auto* defaults = [NSUserDefaults standardUserDefaults];
        NSString* installing = [defaults stringForKey:RELAUNCH_VERSION_KEY];
        if (!installing)
            return false;
        [defaults removeObjectForKey:RELAUNCH_VERSION_KEY];
        return to_byte_string(installing) == version();
    }();
    return relaunched;
}

QAction* create_check_for_updates_action(QObject&)
{
    return nullptr;
}

static void did_start_check()
{
    change_status([](Status& status) { status.checking = true; });
}

static void did_find_update_ready(NSString* version, void (^install_now)(void))
{
    s_install_now = install_now;
    auto ready_version = to_byte_string(version);
    dbgln("Xenon {} is ready, and installs when Xenon quits", ready_version);
    change_status([&](Status& status) {
        status.ready_version = ready_version;
        status.checking = false;
        status.error.clear();
    });
}

static void did_finish_check(SPUUpdater* updater, NSError* error)
{
    Optional<UnixDateTime> checked_at;
    if (auto* date = updater.lastUpdateCheckDate)
        checked_at = UnixDateTime::from_seconds_since_epoch(static_cast<i64>(date.timeIntervalSince1970));

    Optional<ByteString> failure;
    auto failed = error && !([error.domain isEqualToString:SUSparkleErrorDomain] && (error.code == SUNoUpdateError || error.code == SUInstallationCanceledError));
    if (failed) {
        auto reason = to_byte_string(error.localizedDescription);
        if (NSError* underlying = error.userInfo[NSUnderlyingErrorKey])
            reason = ByteString::formatted("{} ({})", reason, to_byte_string(underlying.localizedDescription));
        warnln("Unable to update Xenon: {}", reason);
        failure = move(reason);
    }

    change_status([&](Status& status) {
        status.checking = false;
        if (checked_at.has_value())
            status.checked_at = checked_at;
        status.error = failure;
    });
}

}

@implementation XenonRuneUpdaterDelegate

- (BOOL)updaterShouldPromptForPermissionToCheckForUpdates:(SPUUpdater*)updater
{
    return NO;
}

- (BOOL)updater:(SPUUpdater*)updater mayPerformUpdateCheck:(SPUUpdateCheck)updateCheck error:(NSError* __autoreleasing*)error
{
    Ladybird::Updater::did_start_check();
    return YES;
}

- (BOOL)updater:(SPUUpdater*)updater willInstallUpdateOnQuit:(SUAppcastItem*)item immediateInstallationBlock:(void (^)(void))immediateInstallHandler
{
    Ladybird::Updater::did_find_update_ready(item.displayVersionString, immediateInstallHandler);
    return YES;
}

- (void)updater:(SPUUpdater*)updater didFinishUpdateCycleForUpdateCheck:(SPUUpdateCheck)updateCheck error:(NSError*)error
{
    Ladybird::Updater::did_finish_check(updater, error);
}

@end
