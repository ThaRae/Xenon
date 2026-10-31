/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Array.h>
#include <AK/LexicalPath.h>
#include <AK/StdLibExtras.h>
#include <AK/TemporaryChange.h>
#include <AK/TypeCasts.h>
#include <LibWebView/Application.h>
#include <LibWebView/BrowsingSession.h>
#include <LibWebView/Profile.h>
#include <UI/Application.h>
#include <UI/BrandColors.h>
#include <UI/BrowserWindow.h>
#include <UI/ChromeStyle.h>
#include <UI/Menu.h>
#include <UI/StringUtils.h>
#include <UI/Tab.h>
#include <UI/TabBar.h>
#include <UI/TabGroups.h>

#include <QApplication>
#include <QFile>
#include <QFontMetricsF>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QSaveFile>
#include <QTimer>

namespace Ladybird {

static constexpr qreal TAB_GROUP_CHIP_HEIGHT = 20;
static constexpr qreal TAB_GROUP_CHIP_PADDING = 8;
static constexpr qreal TAB_GROUP_CHIP_MAX_WIDTH = 160;
static constexpr qreal TAB_GROUP_DOT_SIZE = 12;

struct TabGroupColorTones {
    char const* name;
    QRgb light;
    QRgb dark;
};

static constexpr TabGroupColorTones s_tab_group_color_tones[TAB_GROUP_COLOR_COUNT] = {
    { "Rose", BrandColors::Rose300, BrandColors::Rose40 },
    { "Sky", qRgb(0x07, 0x87, 0xb5), qRgb(0x6b, 0xce, 0xff) },
    { "Amber", qRgb(0xa3, 0x71, 0x00), qRgb(0xeb, 0xb7, 0x61) },
    { "Jade", qRgb(0x08, 0x91, 0x74), qRgb(0x61, 0xda, 0xb8) },
    { "Orchid", qRgb(0xab, 0x4a, 0xc7), qRgb(0xe1, 0xa8, 0xf3) },
    { "Ember", qRgb(0xcd, 0x4d, 0x01), qRgb(0xff, 0xa8, 0x87) },
    { "Iris", qRgb(0x56, 0x6b, 0xf0), qRgb(0xab, 0xbe, 0xfe) },
    { "Moss", qRgb(0x4a, 0x90, 0x02), qRgb(0x9f, 0xd2, 0x83) },
    { "Slate", qRgb(0x70, 0x7d, 0x8e), qRgb(0xb4, 0xc2, 0xd5) },
};

QString tab_group_color_name(TabGroupColor color)
{
    return QString::fromLatin1(s_tab_group_color_tones[to_underlying(color)].name);
}

static Optional<TabGroupColor> tab_group_color_from_name(QString const& name)
{
    for (int index = 0; index < TAB_GROUP_COLOR_COUNT; ++index) {
        if (name.compare(QLatin1String(s_tab_group_color_tones[index].name), Qt::CaseInsensitive) == 0)
            return static_cast<TabGroupColor>(index);
    }
    return {};
}

QColor tab_group_color(TabGroupColor color, QPalette const& palette)
{
    auto const& tones = s_tab_group_color_tones[to_underlying(color)];
    return QColor::fromRgb(ChromeStyle::is_dark(palette) ? tones.dark : tones.light);
}

QColor tab_group_title_color(TabGroupColor, QPalette const& palette)
{
    return ChromeStyle::is_dark(palette) ? QColor(0x1c, 0x1b, 0x1f) : QColor(Qt::white);
}

QFont tab_group_title_font(QFont font)
{
    font = ChromeStyle::chrome_title_font(AK::move(font));
    font.setWeight(QFont::DemiBold);
    return font;
}

QSizeF tab_group_chip_size(QString const& title, QFont const& title_font)
{
    if (title.isEmpty())
        return { TAB_GROUP_DOT_SIZE, TAB_GROUP_DOT_SIZE };

    auto text_width = QFontMetricsF(title_font).horizontalAdvance(title);
    return { min(TAB_GROUP_CHIP_MAX_WIDTH, ceil(text_width) + (2 * TAB_GROUP_CHIP_PADDING)), TAB_GROUP_CHIP_HEIGHT };
}

void paint_tab_group_chip(QPainter& painter, QRectF const& chip, QString const& title, TabGroupColor color, QPalette const& palette, QFont const& title_font)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(tab_group_color(color, palette));
    painter.drawRoundedRect(chip, chip.height() / 2, chip.height() / 2);

    if (!title.isEmpty()) {
        auto text_rect = chip.adjusted(TAB_GROUP_CHIP_PADDING, 0, -TAB_GROUP_CHIP_PADDING, 0);
        QFontMetricsF metrics(title_font);
        auto text = metrics.elidedText(title, Qt::ElideRight, text_rect.width());
        painter.setFont(title_font);
        painter.setPen(tab_group_title_color(color, palette));
        painter.drawText(text_rect, Qt::AlignCenter | Qt::TextSingleLine, text);
    }
    painter.restore();
}

QString tab_group_tooltip(QString const& title, int tab_count)
{
    auto tabs = tab_count == 1 ? QStringLiteral("1 tab") : QStringLiteral("%1 tabs").arg(tab_count);
    if (title.isEmpty())
        return tabs;
    return QStringLiteral("%1 (%2)").arg(title, tabs);
}

static BrowserWindow* browser_window_of(QWidget& widget)
{
    return qobject_cast<BrowserWindow*>(widget.window());
}

static Optional<QString> saved_tab_groups_path()
{
    if (!WebView::Application::default_session().session_store->is_persistent())
        return {};
    return qstring_from_ak_string(LexicalPath::join(WebView::Application::profile().paths().data, "TabGroups.json"sv).string());
}

static QString saved_tab_groups_key(WebView::SessionWindowId window_id)
{
    return QStringLiteral("window%1").arg(window_id);
}

static QJsonObject read_saved_tab_groups()
{
    auto path = saved_tab_groups_path();
    if (!path.has_value())
        return {};
    QFile file(*path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

static void write_saved_tab_groups(QJsonObject const& saved_groups)
{
    auto path = saved_tab_groups_path();
    if (!path.has_value())
        return;
    QSaveFile file(*path);
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(saved_groups).toJson(QJsonDocument::Compact)) < 0 || !file.commit())
        dbgln("Unable to save the tab groups to {}", ak_byte_string_from_qstring(*path));
}

static void forget_groups_of_windows_not_restored()
{
    static bool s_scheduled = false;
    if (exchange(s_scheduled, true))
        return;

    QTimer::singleShot(0, qApp, [] {
        auto saved_groups = read_saved_tab_groups();
        QJsonObject kept_groups;
        for (auto* widget : QApplication::topLevelWidgets()) {
            auto* window = as_if<BrowserWindow>(widget);
            if (!window || !window->session_window_id().has_value())
                continue;
            auto key = saved_tab_groups_key(*window->session_window_id());
            if (saved_groups.contains(key))
                kept_groups.insert(key, saved_groups.value(key));
        }
        if (kept_groups != saved_groups)
            write_saved_tab_groups(kept_groups);
    });
}

TabGroups::TabGroups(TabWidget& tab_widget)
    : QObject(&tab_widget)
    , m_tab_widget(tab_widget)
{
    connect(tab_widget.tab_bar(), &QTabBar::currentChanged, this, &TabGroups::did_change_current_tab);
    forget_groups_of_windows_not_restored();
}

Tab* TabGroups::tab_at(int index) const
{
    if (index < 0 || index >= count())
        return nullptr;
    return m_tab_widget.tab(index);
}

int TabGroups::index_of(Tab const* tab) const
{
    return m_tab_widget.index_of(const_cast<Tab*>(tab));
}

int TabGroups::count() const
{
    return m_tab_widget.count();
}

int TabGroups::first_unpinned_index() const
{
    return m_tab_widget.tab_bar()->pinned_tab_count();
}

TabGroup* TabGroups::group_of(Tab const* tab) const
{
    return tab ? m_groups_of_tabs.value(tab, nullptr) : nullptr;
}

TabGroup* TabGroups::group_at(int index) const
{
    return group_of(tab_at(index));
}

Optional<TabGroupSpan> TabGroups::span_of(TabGroup const* group) const
{
    for (auto const& span : m_spans) {
        if (span.group == group)
            return span;
    }
    return {};
}

int TabGroups::tab_count(TabGroup const& group) const
{
    if (auto span = span_of(&group); span.has_value())
        return span->last - span->first + 1;
    return 0;
}

bool TabGroups::holds_every_tab(TabGroup const& group) const
{
    return tab_count(group) == count();
}

TabGroupColor TabGroups::next_color() const
{
    Array<int, TAB_GROUP_COLOR_COUNT> uses {};
    for (auto const& span : m_spans)
        ++uses[to_underlying(span.group->color())];

    auto color = 0;
    for (int index = 1; index < TAB_GROUP_COLOR_COUNT; ++index) {
        if (uses[index] < uses[color])
            color = index;
    }
    return static_cast<TabGroupColor>(color);
}

TabGroup& TabGroups::create_group()
{
    return *new TabGroup(this, next_color());
}

void TabGroups::move_tab(int from, int to)
{
    if (from == to)
        return;
    TemporaryChange moving { m_is_moving_tabs, true };
    m_tab_widget.tab_bar()->moveTab(from, to);
}

TabGroup& TabGroups::add_to_new_group(Tab& tab)
{
    if (tab.is_pinned()) {
        if (auto* window = browser_window_of(m_tab_widget))
            window->unpin_tab(tab);
    }
    remove_from_group(tab);

    auto& group = create_group();
    m_groups_of_tabs.insert(&tab, &group);
    update();
    emit edit_requested(&group);
    return group;
}

void TabGroups::add_to_group(Tab& tab, TabGroup& group)
{
    if (group_of(&tab) == &group)
        return;
    if (tab.is_pinned()) {
        if (auto* window = browser_window_of(m_tab_widget))
            window->unpin_tab(tab);
    }
    remove_from_group(tab);

    auto span = span_of(&group);
    if (!span.has_value())
        return;

    auto index = index_of(&tab);
    move_tab(index, index < span->first ? span->last : span->last + 1);
    m_groups_of_tabs.insert(&tab, &group);
    update();
}

void TabGroups::remove_from_group(Tab& tab)
{
    auto* group = group_of(&tab);
    if (!group)
        return;

    auto span = span_of(group);
    m_groups_of_tabs.remove(&tab);

    if (auto index = index_of(&tab); span.has_value() && index > span->first && index < span->last)
        move_tab(index, span->last);
    update();
}

void TabGroups::set_title(TabGroup& group, QString const& title)
{
    if (group.m_title == title)
        return;
    group.m_title = title;
    update();
}

void TabGroups::set_color(TabGroup& group, TabGroupColor color)
{
    if (group.m_color == color)
        return;
    group.m_color = color;
    update();
}

void TabGroups::set_collapsed(TabGroup& group, bool collapsed)
{
    if (group.m_collapsed == collapsed)
        return;

    auto span = span_of(&group);
    if (!span.has_value())
        return;

    if (collapsed) {
        auto current = m_tab_widget.current_index();
        if (current >= span->first && current <= span->last) {
            auto* tab_bar = m_tab_widget.tab_bar();
            Optional<int> replacement;
            for (auto index = span->last + 1; index < count() && !replacement.has_value(); ++index) {
                if (tab_bar->isTabVisible(index))
                    replacement = index;
            }
            for (auto index = span->first - 1; index >= 0 && !replacement.has_value(); --index) {
                if (tab_bar->isTabVisible(index))
                    replacement = index;
            }

            if (replacement.has_value()) {
                m_tab_widget.set_current_index(*replacement);
            } else if (auto* window = browser_window_of(m_tab_widget)) {
                window->open_new_tab();
            }
        }
    }

    group.m_collapsed = collapsed;
    update();
}

void TabGroups::ungroup(TabGroup& group)
{
    m_groups_of_tabs.removeIf([&](auto const& entry) { return entry.value() == &group; });
    update();
}

void TabGroups::close_group(TabGroup& group)
{
    auto span = span_of(&group);
    auto* window = browser_window_of(m_tab_widget);
    if (!span.has_value() || !window)
        return;

    if (holds_every_tab(group)) {
        window->open_new_tab();
    } else if (auto current = m_tab_widget.current_index(); current >= span->first && current <= span->last) {
        set_collapsed(group, true);
    }

    Vector<QPointer<Tab>> tabs;
    for (auto index = span->first; index <= span->last; ++index)
        tabs.append(tab_at(index));
    for (auto& tab : tabs.in_reverse()) {
        if (tab)
            tab->request_close();
    }
}

void TabGroups::open_new_tab_in_group(TabGroup& group)
{
    auto span = span_of(&group);
    auto* window = browser_window_of(m_tab_widget);
    if (!span.has_value() || !window)
        return;

    auto& tab = window->new_tab_from_url(WebView::Application::settings().new_tab_page_url(), Web::HTML::ActivateTab::Yes, BrowserWindow::TabLocation::at_index(span->last + 1));
    m_groups_of_tabs.insert(&tab, &group);
    update();
    tab.set_url_is_hidden(true);
    tab.focus_location_editor();
}

void TabGroups::move_group_to_new_window(TabGroup& group)
{
    auto span = span_of(&group);
    auto* source_window = browser_window_of(m_tab_widget);
    if (!span.has_value() || !source_window || holds_every_tab(group))
        return;

    Vector<QPointer<Tab>> tabs;
    for (auto index = span->first; index <= span->last; ++index)
        tabs.append(tab_at(index));
    auto title = group.title();
    auto color = group.color();

    auto& target_window = Application::the().new_window({}, Application::the().configuration_for_new_window(), BrowserWindow::IsPopupWindow::No, source_window->is_private(), nullptr, {}, ShowWindow::No);
    auto& target_groups = target_window.tab_groups();
    TabGroup* target_group = nullptr;
    for (auto& tab : tabs) {
        if (!tab)
            continue;
        source_window->move_tab_to_window(source_window->tab_index(tab), target_window, target_window.tab_count());
        if (!target_group)
            target_group = &target_groups.add_to_new_group_quietly(*tab);
        else
            target_groups.add_to_group(*tab, *target_group);
    }
    if (target_group) {
        target_groups.set_title(*target_group, title);
        target_groups.set_color(*target_group, color);
    }

    target_window.show();
    target_window.activateWindow();
    target_window.raise();
}

TabGroup& TabGroups::add_to_new_group_quietly(Tab& tab)
{
    auto& group = create_group();
    m_groups_of_tabs.insert(&tab, &group);
    update();
    return group;
}

void TabGroups::set_group_after_drop(Tab& tab, TabGroup* group)
{
    auto index = index_of(&tab);
    if (index < 0 || tab.is_pinned() || group_of(&tab) == group)
        return;

    if (!group)
        m_groups_of_tabs.remove(&tab);
    else if (group_at(index - 1) == group || group_at(index + 1) == group)
        m_groups_of_tabs.insert(&tab, group);
    update();
}

void TabGroups::add_opened_tab(Tab& opener, Tab& opened)
{
    auto* group = group_of(&opener);
    if (!group || opened.is_pinned() || group_of(&opened))
        return;
    add_to_group(opened, *group);
}

void TabGroups::will_pin_tab(Tab& tab)
{
    if (!group_of(&tab))
        return;
    m_groups_of_tabs.remove(&tab);
    update();
}

void TabGroups::did_insert_tab(int index)
{
    if (auto* tab = tab_at(index); tab && !tab->is_pinned() && !m_is_moving_tabs) {
        if (auto* group = group_at(index - 1); group && group == group_at(index + 1))
            m_groups_of_tabs.insert(tab, group);
    }
    update();
}

void TabGroups::will_remove_tab(int index)
{
    auto* tab = tab_at(index);
    if (!tab)
        return;

    auto* tab_bar = m_tab_widget.tab_bar();
    auto another_tab_shows = false;
    for (int other = 0; other < count() && !another_tab_shows; ++other)
        another_tab_shows = other != index && tab_bar->isTabVisible(other);

    if (!another_tab_shows && count() > 1) {
        TabGroup* group = nullptr;
        for (auto other = index + 1; other < count() && !group; ++other)
            group = group_at(other);
        for (auto other = index - 1; other >= 0 && !group; --other)
            group = group_at(other);
        if (group) {
            group->m_collapsed = false;
            update_tab_visibility();
        }
    }

    m_groups_of_tabs.remove(tab);
}

void TabGroups::did_remove_tab()
{
    update();
}

void TabGroups::did_move_tab(int, int to)
{
    if (m_is_moving_tabs)
        return;

    if (auto* tab = tab_at(to); tab && !tab->is_pinned()) {
        auto* left = group_at(to - 1);
        auto* right = group_at(to + 1);
        auto* group = group_of(tab);
        if (left && left == right)
            m_groups_of_tabs.insert(tab, left);
        else if (group && group != left && group != right && tab_count(*group) > 1)
            m_groups_of_tabs.remove(tab);
    }
    update();
}

void TabGroups::did_change_current_tab(int index)
{
    if (auto* window = browser_window_of(m_tab_widget); window && window->is_checking_whether_to_close())
        return;
    if (auto* group = group_at(index); group && group->is_collapsed())
        set_collapsed(*group, false);
}

void TabGroups::keep_groups_together()
{
    Vector<TabGroup*> groups_at_indices;
    groups_at_indices.ensure_capacity(count());
    for (int index = 0; index < count(); ++index) {
        auto* tab = tab_at(index);
        auto* group = group_of(tab);
        groups_at_indices.unchecked_append(group && !tab->is_pinned() ? group : nullptr);
    }

    QHash<TabGroup*, TabGroupSpan> longest_runs;
    for (int index = 0; index < count();) {
        auto* group = groups_at_indices[index];
        auto end = index + 1;
        while (end < count() && group && groups_at_indices[end] == group)
            ++end;
        if (group) {
            auto run = longest_runs.value(group, { group, 0, -1 });
            if (end - index > run.last - run.first + 1)
                longest_runs.insert(group, { group, index, end - 1 });
        }
        index = end;
    }

    m_groups_of_tabs.clear();
    m_spans.clear();
    for (int index = 0; index < count(); ++index) {
        auto* group = groups_at_indices[index];
        if (!group)
            continue;
        if (auto run = longest_runs.value(group); index < run.first || index > run.last)
            continue;
        m_groups_of_tabs.insert(tab_at(index), group);
        if (!m_spans.is_empty() && m_spans.last().group == group)
            m_spans.last().last = index;
        else
            m_spans.append({ group, index, index });
    }

    for (auto* group : findChildren<TabGroup*>(Qt::FindDirectChildrenOnly)) {
        if (!span_of(group).has_value()) {
            group->setParent(nullptr);
            group->deleteLater();
        }
    }
}

void TabGroups::update_tab_visibility()
{
    auto* tab_bar = m_tab_widget.tab_bar();

    if (auto* group = group_at(m_tab_widget.current_index()); group && group->is_collapsed())
        group->m_collapsed = false;

    for (int index = 0; index < count(); ++index) {
        auto* group = group_at(index);
        auto visible = !group || !group->is_collapsed();
        if (tab_bar->isTabVisible(index) != visible)
            tab_bar->setTabVisible(index, visible);
    }
}

void TabGroups::update()
{
    keep_groups_together();
    update_tab_visibility();
    save();
    emit changed();
}

static QJsonArray tab_ids_of(TabGroups const& groups, TabWidget const& tab_widget, TabGroupSpan const& span)
{
    QJsonArray tab_ids;
    for (auto index = span.first; index <= span.last; ++index) {
        if (auto* tab = tab_widget.tab(index); tab && groups.group_of(tab) == span.group && tab->session_tab_id().has_value())
            tab_ids.append(static_cast<qint64>(*tab->session_tab_id()));
    }
    return tab_ids;
}

void TabGroups::save() const
{
    auto* window = browser_window_of(m_tab_widget);
    if (!window || window->is_private() == WebView::IsPrivate::Yes || !window->session_window_id().has_value())
        return;
    if (m_spans.is_empty() && !m_has_saved_groups)
        return;
    m_has_saved_groups = true;

    QJsonArray groups;
    for (auto const& span : m_spans) {
        groups.append(QJsonObject {
            { "title", span.group->title() },
            { "color", tab_group_color_name(span.group->color()).toLower() },
            { "collapsed", span.group->is_collapsed() },
            { "tabs", tab_ids_of(*this, m_tab_widget, span) },
        });
    }
    auto saved_groups = read_saved_tab_groups();
    auto key = saved_tab_groups_key(*window->session_window_id());
    if (groups.isEmpty())
        saved_groups.remove(key);
    else
        saved_groups.insert(key, groups);
    write_saved_tab_groups(saved_groups);
}

void TabGroups::restore(WebView::SessionWindowId window_id)
{
    auto saved_groups = read_saved_tab_groups().value(saved_tab_groups_key(window_id)).toArray();
    if (saved_groups.isEmpty())
        return;

    m_has_saved_groups = true;

    QHash<i64, Tab*> tabs_by_id;
    for (int index = 0; index < count(); ++index) {
        if (auto* tab = tab_at(index); tab && tab->session_tab_id().has_value())
            tabs_by_id.insert(*tab->session_tab_id(), tab);
    }

    for (auto const& value : saved_groups) {
        auto saved_group = value.toObject();
        TabGroup* group = nullptr;
        for (auto const& tab_id : saved_group.value("tabs").toArray()) {
            auto* tab = tabs_by_id.value(tab_id.toInteger(), nullptr);
            if (!tab || tab->is_pinned() || group_of(tab))
                continue;
            if (!group) {
                group = &create_group();
                group->m_title = saved_group.value("title").toString();
                group->m_color = tab_group_color_from_name(saved_group.value("color").toString()).value_or(TabGroupColor::Rose);
                group->m_collapsed = saved_group.value("collapsed").toBool();
            }
            m_groups_of_tabs.insert(tab, group);
        }
    }
    update();
}

static QIcon tab_group_menu_icon(TabGroup const& group, QPalette const& palette)
{
    auto device_pixel_ratio = qApp->devicePixelRatio();
    QPixmap pixmap(QSize(16, 16) * device_pixel_ratio);
    pixmap.setDevicePixelRatio(device_pixel_ratio);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(tab_group_color(group.color(), palette));
    painter.drawEllipse(QRectF(2, 2, 12, 12));
    return QIcon(pixmap);
}

static QString tab_group_menu_text(TabGroups const& groups, TabWidget const& tab_widget, TabGroup const& group)
{
    auto text = group.title();
    if (text.isEmpty()) {
        auto span = groups.span_of(&group);
        auto* first_tab = span.has_value() ? tab_widget.tab(span->first) : nullptr;
        auto first_title = first_tab ? first_tab->title() : QString {};
        auto other_tabs = groups.tab_count(group) - 1;
        if (other_tabs == 0)
            text = QStringLiteral("“%1”").arg(first_title);
        else if (other_tabs == 1)
            text = QStringLiteral("“%1” and 1 other tab").arg(first_title);
        else
            text = QStringLiteral("“%1” and %2 other tabs").arg(first_title).arg(other_tabs);
    }
    return text.replace(QLatin1Char('&'), QLatin1String("&&"));
}

void add_tab_group_actions(QMenu& menu, Tab& tab)
{
    auto* add_to_new_group_action = new QAction("Add Tab to &New Group", &menu);
    auto* add_to_group_menu = new QMenu("Add Tab to &Group", &menu);
    auto* remove_from_group_action = new QAction("&Remove from Group", &menu);

    auto groups_of = [&tab]() -> TabGroups* {
        auto* window = browser_window_of(tab);
        return window ? &window->tab_groups() : nullptr;
    };

    QObject::connect(add_to_new_group_action, &QAction::triggered, &tab, [&tab, groups_of] {
        if (auto* groups = groups_of())
            groups->add_to_new_group(tab);
    });
    QObject::connect(remove_from_group_action, &QAction::triggered, &tab, [&tab, groups_of] {
        if (auto* groups = groups_of())
            groups->remove_from_group(tab);
    });

    menu.addSeparator();
    menu.addAction(add_to_new_group_action);
    auto* add_to_group_action = menu.addMenu(add_to_group_menu);
    menu.addAction(remove_from_group_action);

    QObject::connect(&menu, &QMenu::aboutToShow, &tab, [&tab, groups_of, add_to_new_group_action, add_to_group_action, add_to_group_menu, remove_from_group_action] {
        auto* groups = groups_of();
        auto* tab_widget = groups ? qobject_cast<TabWidget*>(groups->parent()) : nullptr;
        auto* own_group = groups ? groups->group_of(&tab) : nullptr;

        add_to_group_menu->clear();
        if (groups && tab_widget) {
            auto* new_group_action = add_to_group_menu->addAction("&New Group");
            QObject::connect(new_group_action, &QAction::triggered, &tab, [&tab, groups_of] {
                if (auto* groups = groups_of())
                    groups->add_to_new_group(tab);
            });
            add_to_group_menu->addSeparator();

            for (auto const& span : groups->spans()) {
                if (span.group == own_group)
                    continue;
                auto* action = add_to_group_menu->addAction(tab_group_menu_icon(*span.group, tab.palette()), tab_group_menu_text(*groups, *tab_widget, *span.group));
                QObject::connect(action, &QAction::triggered, &tab, [&tab, groups_of, group = QPointer<TabGroup>(span.group)] {
                    if (auto* groups = groups_of(); groups && group)
                        groups->add_to_group(tab, *group);
                });
            }
#if defined(AK_OS_MACOS)
            enable_menu_icons(*add_to_group_menu);
#endif
        }

        auto has_other_groups = add_to_group_menu->actions().size() > 2;
        add_to_new_group_action->setVisible(groups && !has_other_groups);
        add_to_group_action->setVisible(groups && has_other_groups);
        remove_from_group_action->setVisible(own_group != nullptr);
    });
}

}
