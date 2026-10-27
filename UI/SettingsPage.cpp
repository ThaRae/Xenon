/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Base64.h>
#include <AK/JsonArray.h>
#include <AK/JsonObject.h>
#include <AK/LexicalPath.h>
#include <AK/QuickSort.h>
#include <AK/RefCounted.h>
#include <AK/Weakable.h>
#include <LibCore/File.h>
#include <LibURL/Parser.h>
#include <LibWebView/Application.h>
#include <LibWebView/CrashReportStore.h>
#include <LibWebView/WebUI.h>
#include <UI/Bangs.h>
#include <UI/Blocking.h>
#include <UI/Extensions/Dialog.h>
#include <UI/Extensions/Registry.h>
#include <UI/FilterListUpdates.h>
#include <UI/SettingsPage.h>
#include <UI/StringUtils.h>
#include <UI/TabHibernation.h>
#include <UI/XenonSettings.h>

#include <QApplication>
#include <QDesktopServices>
#include <QProcess>
#include <QUrl>

namespace Ladybird::SettingsPage {

static constexpr size_t MAXIMUM_BANG_RESULTS = 8;
static constexpr size_t EXTENSION_ICON_PIXELS = 64;
// Longer than any timeout the menus offer, and short enough that a mistyped one still comes round.
static constexpr i64 MAXIMUM_TAB_HIBERNATION_TIMEOUT_SECONDS = 7 * 24 * 60 * 60;

static StringView layout_name(XenonSettings::ChromeLayout layout)
{
    switch (layout) {
    case XenonSettings::ChromeLayout::Classic:
        return "classic"sv;
    case XenonSettings::ChromeLayout::Compact:
        return "compact"sv;
    case XenonSettings::ChromeLayout::Vertical:
        return "vertical"sv;
    }
    VERIFY_NOT_REACHED();
}

static Optional<XenonSettings::ChromeLayout> layout_from_name(StringView name)
{
    for (auto layout : { XenonSettings::ChromeLayout::Classic, XenonSettings::ChromeLayout::Compact, XenonSettings::ChromeLayout::Vertical }) {
        if (layout_name(layout) == name)
            return layout;
    }
    return {};
}

// The extension's icon as a data: URL, since the page can't load chrome-extension:// URLs.
static Optional<String> extension_icon(Extensions::Extension const& extension)
{
    auto path = Extensions::best_icon(extension.manifest().icons, EXTENSION_ICON_PIXELS);
    if (!path.has_value())
        return {};

    auto file = Core::File::open(LexicalPath::join(extension.root(), *path).string(), Core::File::OpenMode::Read);
    if (file.is_error())
        return {};
    auto contents = file.value()->read_until_eof();
    if (contents.is_error())
        return {};

    auto type = path->ends_with_bytes(".svg"sv) ? "image/svg+xml"sv : "image/png"sv;
    auto encoded = encode_base64(contents.value());
    if (encoded.is_error())
        return {};
    return MUST(String::formatted("data:{};base64,{}", type, encoded.value()));
}

static JsonArray extensions()
{
    JsonArray extensions;
    for (auto const& extension : Extensions::Registry::the().extensions()) {
        auto const& manifest = extension->manifest();

        JsonObject object;
        object.set("id"sv, extension->id());
        object.set("name"sv, manifest.name);
        object.set("version"sv, manifest.version);
        if (manifest.description.has_value())
            object.set("description"sv, *manifest.description);
        if (auto icon = extension_icon(*extension); icon.has_value())
            object.set("icon"sv, icon.release_value());
        object.set("persisted"sv, Extensions::Registry::the().is_persisted(*extension));
        extensions.must_append(move(object));
    }
    return extensions;
}

// The directories the page can show, by the names it asks for them by. It never names a path itself.
static Optional<ByteString> location(StringView name)
{
    auto const& paths = WebView::Application::profile().paths();
    if (name == "config"sv)
        return paths.config;
    if (name == "data"sv)
        return paths.data;
    if (name == "cache"sv)
        return paths.cache;
    if (name == "crashReports"sv)
        return WebView::CrashReportStore::the().directory();
    if (name == "xenonSettings"sv)
        return XenonSettings::location();
    return {};
}

static constexpr StringView LOCATION_NAMES[] = { "config"sv, "data"sv, "cache"sv, "crashReports"sv, "xenonSettings"sv };

static JsonObject profile()
{
    auto const& profile = WebView::Application::profile();

    JsonObject locations;
    for (auto name : LOCATION_NAMES) {
        if (auto path = location(name); path.has_value())
            locations.set(name, path->view());
    }

    JsonObject object;
    object.set("name"sv, profile.identifier());
    object.set("temporary"sv, profile.is_temporary());
    object.set("locations"sv, move(locations));
    return object;
}

static void show_location(StringView name)
{
    auto path = location(name);
    if (!path.has_value())
        return;

    // Xenon's own settings are a file, which the file manager shows in its folder rather than opening it.
    if (name == "xenonSettings"sv) {
#if defined(AK_OS_MACOS)
        QProcess::startDetached(QStringLiteral("/usr/bin/open"), { QStringLiteral("-R"), qstring_from_ak_string(*path) });
        return;
#else
        path = LexicalPath::dirname(*path);
#endif
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(qstring_from_ak_string(*path)));
}

static JsonObject bang_object(Bangs::Bang const& bang)
{
    JsonObject object;
    object.set("trigger"sv, bang.trigger);
    object.set("name"sv, bang.name);
    if (auto url = URL::Parser::basic_parse(bang.query_url); url.has_value())
        object.set("host"sv, url->serialized_host());
    return object;
}

// The bangs whose trigger starts with the query, shortest first, then those whose site's name has it in it.
static JsonArray search_bangs(StringView query)
{
    JsonArray results;
    if (query.starts_with('!'))
        query = query.substring_view(1);
    query = query.trim_whitespace();
    if (query.is_empty())
        return results;

    Vector<Bangs::Bang> by_trigger;
    Vector<Bangs::Bang> by_name;
    for (size_t i = 0; i < Bangs::bang_count(); ++i) {
        auto bang = Bangs::bang_at(i);
        if (bang.trigger.starts_with(query, CaseSensitivity::CaseInsensitive))
            by_trigger.append(bang);
        else if (by_name.size() < MAXIMUM_BANG_RESULTS && bang.name.contains(query, CaseSensitivity::CaseInsensitive))
            by_name.append(bang);
    }
    quick_sort(by_trigger, [](auto const& a, auto const& b) {
        if (a.trigger.length() != b.trigger.length())
            return a.trigger.length() < b.trigger.length();
        return a.trigger < b.trigger;
    });

    for (auto const* bangs : { &by_trigger, &by_name }) {
        for (auto const& bang : *bangs) {
            if (results.size() == MAXIMUM_BANG_RESULTS)
                return results;
            results.must_append(bang_object(bang));
        }
    }
    return results;
}

// One for each about:settings page, for as long as its WebUI lives: the WebUI's interfaces hold it.
class Bridge final
    : public RefCounted<Bridge>
    , public Weakable<Bridge>
    , public XenonSettings::Observer
    , public WebView::SettingsObserver {
public:
    static NonnullRefPtr<Bridge> create(WebView::WebUI& web_ui)
    {
        auto bridge = adopt_ref(*new Bridge(web_ui));
        Extensions::Registry::the().on_extensions_change([bridge = bridge->make_weak_ptr()] {
            if (!bridge)
                return false;
            bridge->send_settings();
            return true;
        });
        return bridge;
    }

    void send_settings() { send("xenon/settings"sv, settings()); }

    void send(StringView name, JsonValue data)
    {
        if (auto web_ui = m_web_ui.strong_ref(); web_ui && web_ui->is_open())
            web_ui->async_send_message(name, data);
    }

private:
    explicit Bridge(WebView::WebUI& web_ui)
        : m_web_ui(web_ui.make_weak_ptr<WebView::WebUI>())
    {
    }

    static JsonObject settings()
    {
        JsonObject settings;
        settings.set("layout"sv, layout_name(XenonSettings::chrome_layout()));
        settings.set("cycleTabsInRecentlyUsedOrder"sv, XenonSettings::cycle_tabs_in_recently_used_order());
        settings.set("splitViewDragAndDrop"sv, XenonSettings::split_view_drag_and_drop());

        JsonArray timeout_choices;
        for (auto minutes : TabHibernation::timeout_choices_in_minutes())
            timeout_choices.must_append(minutes * 60);
        JsonObject tab_hibernation;
        tab_hibernation.set("enabled"sv, XenonSettings::tab_hibernation_enabled());
        tab_hibernation.set("timeoutSeconds"sv, XenonSettings::tab_hibernation_timeout().to_seconds());
        tab_hibernation.set("timeoutChoices"sv, move(timeout_choices));
        settings.set("tabHibernation"sv, move(tab_hibernation));

        JsonObject bangs;
        bangs.set("enabled"sv, XenonSettings::bangs_enabled());
        bangs.set("count"sv, Bangs::bang_count());
        settings.set("bangs"sv, move(bangs));

        if (auto status = FilterLists::Updater::status(); status.has_value()) {
            JsonObject filter_lists;
            filter_lists.set("generatedAt"sv, status->generated_at.view());
            filter_lists.set("listCount"sv, status->list_count);
            filter_lists.set("turnedOff"sv, XenonSettings::disabled_filter_lists().size());
            filter_lists.set("downloaded"sv, status->downloaded);
            if (status->checked_at.has_value())
                filter_lists.set("checkedAt"sv, status->checked_at->milliseconds_since_epoch());
            filter_lists.set("checking"sv, status->checking);
            if (status->error.has_value())
                filter_lists.set("error"sv, status->error->view());
            settings.set("filterLists"sv, move(filter_lists));
        }

        settings.set("blockingDashboard"sv, Blocking::dashboard_url().serialize());

        settings.set("extensions"sv, extensions());
        settings.set("profile"sv, profile());
        return settings;
    }

    // XenonSettings::Observer
    virtual void xenon_settings_changed() override { send_settings(); }

    // WebView::SettingsObserver: the layout follows Ladybird's vertical tabs setting, and the filter lists' status its
    // blocking settings.
    virtual void tab_settings_changed() override { send_settings(); }
    virtual void content_blocker_settings_changed() override { send_settings(); }
    virtual void background_networking_settings_changed() override { send_settings(); }

    WeakPtr<WebView::WebUI> m_web_ui;
};

void register_interfaces(WebView::WebUI& web_ui)
{
    auto bridge = Bridge::create(web_ui);

    web_ui.register_interface("xenon/loadSettings"sv, [bridge](auto const&) {
        bridge->send_settings();
    });

    web_ui.register_interface("xenon/setLayout"sv, [](auto const& data) {
        if (!data.is_string())
            return;
        if (auto layout = layout_from_name(data.as_string()); layout.has_value())
            XenonSettings::set_chrome_layout(*layout);
    });

    web_ui.register_interface("xenon/setCycleTabsInRecentlyUsedOrder"sv, [](auto const& data) {
        if (data.is_bool())
            XenonSettings::set_cycle_tabs_in_recently_used_order(data.as_bool());
    });

    web_ui.register_interface("xenon/setTabHibernation"sv, [](auto const& data) {
        if (!data.is_object())
            return;
        auto const& object = data.as_object();
        if (auto timeout = object.get_i64("timeoutSeconds"sv); timeout.has_value() && *timeout > 0 && *timeout <= MAXIMUM_TAB_HIBERNATION_TIMEOUT_SECONDS)
            XenonSettings::set_tab_hibernation_timeout(AK::Duration::from_seconds(*timeout));
        if (auto enabled = object.get_bool("enabled"sv); enabled.has_value())
            XenonSettings::set_tab_hibernation_enabled(*enabled);
    });

    web_ui.register_interface("xenon/setSplitViewDragAndDrop"sv, [](auto const& data) {
        if (data.is_bool())
            XenonSettings::set_split_view_drag_and_drop(data.as_bool());
    });

    web_ui.register_interface("xenon/setBangsEnabled"sv, [](auto const& data) {
        if (data.is_bool())
            XenonSettings::set_bangs_enabled(data.as_bool());
    });

    web_ui.register_interface("xenon/searchBangs"sv, [bridge](auto const& data) {
        if (!data.is_string())
            return;
        JsonObject results;
        results.set("query"sv, data.as_string());
        results.set("bangs"sv, search_bangs(data.as_string()));
        bridge->send("xenon/bangs"sv, move(results));
    });

    web_ui.register_interface("xenon/checkFilterLists"sv, [bridge](auto const&) {
        FilterLists::Updater::check_now();
        bridge->send_settings();
    });

    web_ui.register_interface("xenon/showExtensions"sv, [](auto const&) {
        Extensions::ExtensionsDialog::show_dialog(QApplication::activeWindow());
    });

    web_ui.register_interface("xenon/showLocation"sv, [](auto const& data) {
        if (data.is_string())
            show_location(data.as_string());
    });
}

}
