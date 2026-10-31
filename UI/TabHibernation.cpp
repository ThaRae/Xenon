/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/HashMap.h>
#include <AK/QuickSort.h>
#include <AK/Time.h>
#include <AK/TypeCasts.h>
#include <AK/Vector.h>
#include <LibWebView/ViewImplementation.h>
#include <UI/BrowserWindow.h>
#include <UI/JavaScriptDialog.h>
#include <UI/MemoryPressure.h>
#include <UI/Tab.h>
#include <UI/TabHibernation.h>
#include <UI/TabMemory.h>
#include <UI/XenonSettings.h>

#include <QActionGroup>
#include <QApplication>
#include <QEvent>
#include <QIcon>
#include <QImage>
#include <QMenu>
#include <QPixmap>
#include <QPointer>
#include <QTimer>

namespace Ladybird {

namespace TabHibernation {

static constexpr auto AUDIO_PROTECTION_TIME = AK::Duration::from_seconds(60);

static constexpr i64 TIMEOUT_CHOICES_IN_MINUTES[] = { 15, 30, 60, 2 * 60, 4 * 60, 6 * 60 };

ReadonlySpan<i64> timeout_choices_in_minutes()
{
    return TIMEOUT_CHOICES_IN_MINUTES;
}

class Hibernator final
    : public QObject
    , public XenonSettings::Observer {
public:
    static Hibernator& the()
    {
        static auto* hibernator = new Hibernator(QApplication::instance());
        return *hibernator;
    }

    virtual void xenon_settings_changed() override { settings_changed(); }

    void settings_changed()
    {
        if (!XenonSettings::tab_hibernation_enabled()) {
            m_timer.stop();
            return;
        }

        auto timeout = XenonSettings::tab_hibernation_timeout().to_milliseconds();
        auto interval = clamp<i64>(timeout / 4, 1000, 30 * 1000);
        if (XenonSettings::hibernate_on_memory_pressure())
            interval = min(interval, static_cast<i64>(5000));
        m_timer.start(static_cast<int>(interval));
    }

    bool may_hibernate(Tab& tab)
    {
        auto* window = as_if<BrowserWindow>(tab.window());
        if (!window || !tab.is_loaded() || is_in_use(*window, tab))
            return false;

        if (tab.is_pinned() || tab.is_loading())
            return false;

        auto& view = tab.view();
        if (view.audibility() == Audio::Audibility::Audible || view.has_devtools_client() || has_picture_in_picture_window(view))
            return false;
        if (auto state = m_tabs.get(&tab); state.has_value() && state->last_audible.has_value() && MonotonicTime::now() - *state->last_audible < AUDIO_PROTECTION_TIME)
            return false;

        return tab.can_hibernate();
    }

    Optional<u64> memory_freed(Tab& tab)
    {
        if (auto state = m_tabs.get(&tab); state.has_value())
            return state->memory_freed;
        return {};
    }

    enum class Request {
        Timeout,
        MemoryPressure,
        User,
    };

    void hibernate_when_ready(Tab& tab, Request request)
    {
        auto& state = state_for(tab);
        if (state.is_preparing)
            return;
        state.is_preparing = true;
        state.request = request;

        auto preparation = tab.view().prepare_for_discard();
        preparation->when_resolved([this, tab = QPointer { &tab }](auto& result) {
            QTimer::singleShot(0, tab, [this, tab, has_unsubmitted_input = result.has_unsubmitted_input] {
                finish_hibernating(*tab, has_unsubmitted_input);
            });
        });
        preparation->when_rejected([this, tab = QPointer { &tab }](auto&) {
            if (auto tab_state = m_tabs.get(tab.data()); tab_state.has_value())
                tab_state->is_preparing = false;
        });
    }

private:
    explicit Hibernator(QObject* parent)
        : QObject(parent)
    {
        QObject::connect(&m_timer, &QTimer::timeout, this, [this] { check_tabs(); });
    }

    struct TabState {
        MonotonicTime last_used;
        Optional<MonotonicTime> last_audible {};
        bool is_preparing { false };
        Request request { Request::Timeout };
        Optional<u64> memory_freed {};
    };

    TabState& state_for(Tab& tab)
    {
        return m_tabs.ensure(&tab, [&] {
            tab.installEventFilter(this);
            QObject::connect(&tab, &QObject::destroyed, this, [this, tab = &tab] { m_tabs.remove(tab); });
            return TabState { .last_used = MonotonicTime::now() };
        });
    }

    virtual bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::Show || event->type() == QEvent::Hide) {
            if (auto state = m_tabs.get(static_cast<Tab*>(watched)); state.has_value())
                state->last_used = MonotonicTime::now();
        }
        return QObject::eventFilter(watched, event);
    }

    static bool is_in_use(BrowserWindow& window, Tab& tab)
    {
        return window.current_tab() == &tab || tab.isVisible();
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

    void check_tabs()
    {
        auto timeout = XenonSettings::tab_hibernation_timeout();
        auto now = MonotonicTime::now();

        for (auto* widget : QApplication::topLevelWidgets()) {
            auto* window = as_if<BrowserWindow>(widget);
            if (!window)
                continue;

            for (int i = 0; i < window->tab_count(); ++i) {
                auto& tab = *window->tab_at(i);
                auto& state = state_for(tab);
                if (!tab.is_loaded() || state.is_preparing)
                    continue;

                if (is_in_use(*window, tab)) {
                    state.last_used = now;
                    continue;
                }
                if (tab.view().audibility() == Audio::Audibility::Audible)
                    state.last_audible = now;

                if (now - state.last_used >= timeout && may_hibernate(tab))
                    hibernate_when_ready(tab, Request::Timeout);
            }
        }

        hibernate_under_memory_pressure();
    }

    void hibernate_under_memory_pressure()
    {
        if (!XenonSettings::hibernate_on_memory_pressure())
            return;

        struct Candidate {
            Tab* tab { nullptr };
            MonotonicTime last_used;
        };
        Vector<Candidate> eligible;
        for (auto* widget : QApplication::topLevelWidgets()) {
            auto* window = as_if<BrowserWindow>(widget);
            if (!window)
                continue;
            for (int i = 0; i < window->tab_count(); ++i) {
                auto& tab = *window->tab_at(i);
                auto& state = state_for(tab);
                if (state.is_preparing || !may_hibernate(tab))
                    continue;
                eligible.append({ &tab, state.last_used });
            }
        }
        quick_sort(eligible, [](Candidate const& a, Candidate const& b) { return a.last_used < b.last_used; });

        auto wanted = MemoryPressure::tabs_to_hibernate(MemoryPressure::level_for(MemoryPressure::sample()), eligible.size());
        for (size_t i = 0; i < wanted; ++i)
            hibernate_when_ready(*eligible[i].tab, Request::MemoryPressure);
    }

    void finish_hibernating(Tab& tab, bool has_unsubmitted_input)
    {
        auto state = m_tabs.get(&tab);
        if (!state.has_value())
            return;
        state->is_preparing = false;

        if (has_unsubmitted_input) {
            state->last_used = MonotonicTime::now();
            return;
        }

        if (state->request == Request::Timeout) {
            if (!XenonSettings::tab_hibernation_enabled() || MonotonicTime::now() - state->last_used < XenonSettings::tab_hibernation_timeout())
                return;
        } else if (state->request == Request::MemoryPressure) {
            if (!XenonSettings::tab_hibernation_enabled() || !XenonSettings::hibernate_on_memory_pressure())
                return;
        }
        if (!may_hibernate(tab))
            return;

        auto usage = TabMemory::usage(tab);
        state->memory_freed = usage.has_value() && !usage->is_estimate ? Optional<u64> { usage->bytes } : Optional<u64> {};
        tab.hibernate();
    }

    QTimer m_timer;
    HashMap<Tab*, TabState> m_tabs;
};

void start()
{
    Hibernator::the().settings_changed();
}

Optional<u64> memory_freed(Tab& tab)
{
    if (!tab.is_hibernated())
        return {};
    return Hibernator::the().memory_freed(tab);
}

static QString timeout_description(AK::Duration timeout)
{
    auto seconds = timeout.to_seconds();
    if (seconds % (60 * 60) == 0)
        return seconds == 60 * 60 ? QStringLiteral("After 1 Hour") : QStringLiteral("After %1 Hours").arg(seconds / (60 * 60));
    if (seconds % 60 == 0)
        return seconds == 60 ? QStringLiteral("After 1 Minute") : QStringLiteral("After %1 Minutes").arg(seconds / 60);
    return QStringLiteral("After %1 Seconds").arg(seconds);
}

QMenu* create_settings_menu(QWidget& parent)
{
    auto* menu = new QMenu("Tab &Hibernation", &parent);
    auto* group = new QActionGroup(menu);

    auto* off_action = menu->addAction("&Off");
    off_action->setCheckable(true);
    group->addAction(off_action);
    QObject::connect(off_action, &QAction::triggered, menu, [] {
        XenonSettings::set_tab_hibernation_enabled(false);
    });
    menu->addSeparator();

    auto add_timeout_action = [&](AK::Duration timeout) {
        auto* action = menu->addAction(timeout_description(timeout));
        action->setCheckable(true);
        group->addAction(action);
        QObject::connect(action, &QAction::triggered, menu, [timeout] {
            XenonSettings::set_tab_hibernation_enabled(true);
            XenonSettings::set_tab_hibernation_timeout(timeout);
        });
        return action;
    };

    Vector<QAction*> timeout_actions;
    for (auto minutes : TIMEOUT_CHOICES_IN_MINUTES)
        timeout_actions.append(add_timeout_action(AK::Duration::from_seconds(minutes * 60)));

    auto* other_timeout_action = menu->addAction(QString {});
    other_timeout_action->setCheckable(true);
    other_timeout_action->setEnabled(false);
    group->addAction(other_timeout_action);

    QObject::connect(menu, &QMenu::aboutToShow, menu, [off_action, other_timeout_action, timeout_actions = move(timeout_actions)] {
        if (!XenonSettings::tab_hibernation_enabled()) {
            off_action->setChecked(true);
            other_timeout_action->setVisible(false);
            return;
        }

        auto timeout = XenonSettings::tab_hibernation_timeout();
        auto* checked_action = other_timeout_action;
        for (size_t i = 0; i < timeout_actions.size(); ++i) {
            if (timeout.to_seconds() == TIMEOUT_CHOICES_IN_MINUTES[i] * 60)
                checked_action = timeout_actions[i];
        }
        other_timeout_action->setVisible(checked_action == other_timeout_action);
        other_timeout_action->setText(timeout_description(timeout));
        checked_action->setChecked(true);
    });

    return menu;
}

QAction* create_hibernate_tab_action(Tab& tab, QMenu& context_menu)
{
    auto* action = new QAction("&Hibernate Tab", &tab);
    action->setToolTip("Free this tab's memory until it's selected again");
    QObject::connect(action, &QAction::triggered, &tab, [&tab] {
        if (Hibernator::the().may_hibernate(tab))
            Hibernator::the().hibernate_when_ready(tab, Hibernator::Request::User);
    });
    QObject::connect(&context_menu, &QMenu::aboutToShow, action, [action, &tab] {
        action->setEnabled(Hibernator::the().may_hibernate(tab));
    });
    return action;
}

QIcon hibernated_tab_icon(QIcon const& favicon)
{
    QIcon greyscale;
    for (auto size : favicon.availableSizes()) {
        auto image = favicon.pixmap(size).toImage().convertToFormat(QImage::Format_ARGB32);
        for (int y = 0; y < image.height(); ++y) {
            auto* pixels = reinterpret_cast<QRgb*>(image.scanLine(y));
            for (int x = 0; x < image.width(); ++x) {
                auto grey = qGray(pixels[x]);
                pixels[x] = qRgba(grey, grey, grey, qAlpha(pixels[x]));
            }
        }
        greyscale.addPixmap(QPixmap::fromImage(image));
    }
    return greyscale.isNull() ? favicon : greyscale;
}

}

bool Tab::can_hibernate() const
{
    if (!is_loaded())
        return false;

    if (m_fixed_title.has_value() || m_report_review_navigation_listener.has_value())
        return false;

    if (m_javascript_dialog->is_open() || m_color_picker_dialog || m_file_picker_dialog || m_external_url_confirmation_dialog)
        return false;
    if (!m_permission_dialogs.is_empty() || m_before_unload_completion || m_already_requested_close || m_window_is_closing)
        return false;
    for (auto* child : findChildren<QWidget*>()) {
        if (child->isWindow() && child->isVisible())
            return false;
    }
    return true;
}

void Tab::hibernate()
{
    VERIFY(is_loaded());

    m_session_tab_id = view().session_tab_id();
    m_deferred_tab = DeferredTab {
        .url = url(),
        .title = m_title,
        .favicon = m_favicon,
        .history = view().session_history_snapshot(),
    };

    Vector<QPointer<QObject>> built;
    for (auto* child : children()) {
        if (!m_tab_shell_children.contains(child))
            built.append(child);
    }
    for (auto* child : m_toolbar_container->children())
        built.append(child);
    for (auto& object : built)
        delete object.data();
#ifdef AK_OS_MACOS
    delete m_view_layout;
    m_view_layout = nullptr;
#endif

    m_view = nullptr;
    m_javascript_dialog = nullptr;
    m_find_in_page = nullptr;
    m_toolbar = nullptr;
    m_performance_monitor = nullptr;
    m_toolbar_window_controls_separator = nullptr;
    m_toolbar_window_controls = nullptr;
    m_minimize_window_button = nullptr;
    m_maximize_window_button = nullptr;
    m_close_window_button = nullptr;
    m_bookmarks_bar = nullptr;
    m_bookmarks_bar_container = nullptr;
    m_bookmarks_bar_hover_expand_timer = nullptr;
    m_bookmarks_bar_hover_collapse_timer = nullptr;
    m_bookmarks_bar_hover_expand_animation = nullptr;
    m_private_badge = nullptr;
    m_hamburger_button = nullptr;
    m_content_blocking_button = nullptr;
    m_extension_toolbar = nullptr;
    m_downloads_button = nullptr;
    m_location_edit = nullptr;
    m_hover_label = nullptr;
    m_loading_animation_timer = nullptr;
    m_page_context_menu = nullptr;
    m_link_context_menu = nullptr;
    m_selected_text_link_context_menu = nullptr;
    m_image_context_menu = nullptr;
    m_media_context_menu = nullptr;
    m_navigate_back_action = nullptr;
    m_navigate_forward_action = nullptr;
    m_reload_action = nullptr;
    m_reload_bypassing_cache_action = nullptr;
    m_stop_action = nullptr;
    m_reload_button = nullptr;
    m_open_downloads_page_action = nullptr;

    m_is_loading = false;
    m_loading_animation_frame = 0;
    m_bookmarks_bar_hover_expanded = false;
    m_bookmarks_bar_hover_expand_progress = 0.0;
    m_is_updating_chrome_style = false;
    m_screen_display_sleep_inhibitor.clear();
    m_content_blocking_popover_reload_site.clear();
    m_downloads_button_icon.clear();
    m_downloads_button_tooltip = {};
    m_suppress_javascript_dialogs_until_navigation = false;
    m_preserve_close_retry_state_on_dialog_dismissal = false;

    update_tab_title();
    update_tab_icon();
}

}
