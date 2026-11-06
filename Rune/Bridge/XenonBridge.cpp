/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/ByteString.h>
#include <Rune/Bridge/Engine.h>
#include <Rune/Bridge/Input.h>
#include <Rune/Bridge/View.h>
#include <Rune/Bridge/XenonBridge.h>
#include <UI/XenonSettings.h>

using Xenon::Engine;
using Xenon::View;

static StringView string_view_from(XenonStr string)
{
    if (!string.data || string.length == 0)
        return {};
    return { string.data, string.length };
}

static Web::DevicePixelSize viewport_size_from(uint32_t width, uint32_t height)
{
    return { static_cast<int>(max(width, 1u)), static_cast<int>(max(height, 1u)) };
}

static double device_pixel_ratio_from(double device_pixel_ratio)
{
    return device_pixel_ratio > 0 ? device_pixel_ratio : 1.0;
}

static void post_to_view(XenonViewId id, Function<void(View&)> command)
{
    Engine::the().post([id, command = move(command)] {
        if (auto* view = Engine::the().view(id))
            command(*view);
    });
}

bool xenon_engine_start(XenonUiCallbacks const* callbacks, int argc, char const* const* argv)
{
    if (!callbacks)
        return false;
    return Engine::the().start(*callbacks, argc, argv);
}

void xenon_engine_shutdown(void)
{
    Engine::the().shutdown();
}

static Optional<size_t> tab_index_from(int64_t index)
{
    if (index < 0)
        return {};
    return static_cast<size_t>(index);
}

static void post_to_model(Function<void(Xenon::BrowserModel&)> command)
{
    Engine::the().post([command = move(command)] {
        if (auto* model = Engine::the().model())
            command(*model);
    });
}

void xenon_engine_set_window_viewport(XenonWindowId window, uint32_t width, uint32_t height, double device_pixel_ratio)
{
    Engine::the().post([window, size = viewport_size_from(width, height), device_pixel_ratio = device_pixel_ratio_from(device_pixel_ratio)] {
        Engine::the().set_window_viewport(window, size, device_pixel_ratio);
    });
}

void xenon_engine_set_system_dark(bool dark)
{
    Engine::the().post([dark] {
        Engine::the().set_system_dark(dark);
    });
}

void xenon_engine_set_chrome_layout(uint8_t layout)
{
    if (layout > XENON_CHROME_LAYOUT_VERTICAL)
        return;
    Engine::the().post([layout] {
        Ladybird::XenonSettings::set_chrome_layout(static_cast<Ladybird::XenonSettings::ChromeLayout>(layout));
    });
}

void xenon_engine_set_cycle_tabs_in_recently_used_order(bool enabled)
{
    Engine::the().post([enabled] {
        Ladybird::XenonSettings::set_cycle_tabs_in_recently_used_order(enabled);
    });
}

XenonWindowId xenon_engine_new_window(bool is_private)
{
    auto& engine = Engine::the();
    auto id = engine.allocate_window_id();
    engine.post([&engine, id, is_private] {
        engine.new_window(id, is_private ? WebView::IsPrivate::Yes : WebView::IsPrivate::No);
    });
    return id;
}

void xenon_engine_activate_window(XenonWindowId window)
{
    post_to_model([window](auto& model) {
        model.activate_window(window);
    });
}

void xenon_engine_close_window(XenonWindowId window)
{
    post_to_model([window](auto& model) {
        model.close_window(window);
    });
}

XenonViewId xenon_engine_new_tab(XenonWindowId window, XenonStr url, int64_t index, bool activate)
{
    auto& engine = Engine::the();
    auto id = engine.allocate_view_id();
    engine.post([&engine, window, id, input = ByteString { string_view_from(url) }, index = tab_index_from(index), activate] {
        engine.new_tab(window, id, input, index, activate ? Web::HTML::ActivateTab::Yes : Web::HTML::ActivateTab::No);
    });
    return id;
}

void xenon_engine_activate_action(uint32_t action)
{
    Engine::the().post([action] {
        Engine::the().app_menus().activate_action(action);
    });
}

void xenon_engine_request_app_menu(uint32_t which)
{
    Engine::the().post([which] {
        Engine::the().app_menus().show(which);
    });
}

void xenon_engine_activate_app_menu_item(uint64_t menu, uint32_t item)
{
    Engine::the().post([menu, item] {
        Engine::the().app_menus().activate_item(menu, item);
    });
}

void xenon_engine_request_history_menu(XenonViewId id, int32_t direction)
{
    post_to_view(id, [direction](View& view) {
        view.show_history_menu(direction);
    });
}

void xenon_engine_traverse_history_to_step(XenonViewId id, int32_t step)
{
    post_to_view(id, [step](View& view) {
        view.traverse_the_history_to_step(step);
    });
}

void xenon_engine_blocking_action(XenonViewId id, uint32_t action)
{
    post_to_view(id, [action](View& view) {
        view.run_blocking_action(action);
    });
}

void xenon_engine_toggle_mute(XenonViewId id)
{
    post_to_view(id, [](View& view) {
        view.toggle_mute();
    });
}

void xenon_engine_answer_unresponsive_page(XenonViewId id, bool keep_waiting)
{
    post_to_view(id, [keep_waiting](View& view) {
        if (keep_waiting)
            view.keep_waiting_for_unresponsive_page();
        else
            view.terminate_unresponsive_page();
    });
}

void xenon_engine_download_action(uint64_t download, uint32_t action)
{
    Engine::the().post([download, action] {
        Engine::the().run_download_action(download, action);
    });
}

XenonViewId xenon_engine_duplicate_tab(XenonViewId source)
{
    auto& engine = Engine::the();
    auto id = engine.allocate_view_id();
    engine.post([&engine, source, id] {
        engine.duplicate_tab(source, id);
    });
    return id;
}

void xenon_engine_request_tab_memory(XenonViewId view)
{
    Engine::the().post([view] {
        Engine::the().report_tab_memory(view);
    });
}

void xenon_engine_activate_tab(XenonViewId view)
{
    post_to_model([view](auto& model) {
        model.activate_tab(view);
    });
}

void xenon_engine_close_tab(XenonViewId view)
{
    post_to_model([view](auto& model) {
        model.close_tab(view);
    });
}

void xenon_engine_move_tab(XenonViewId view, XenonWindowId window, int64_t index)
{
    post_to_model([view, window, index = tab_index_from(index)](auto& model) {
        auto const* target = model.window(window);
        if (!target)
            return;
        model.move_tab(view, window, index.value_or(target->tabs.size()));
    });
}

void xenon_engine_set_tab_pinned(XenonViewId view, bool pinned)
{
    post_to_model([view, pinned](auto& model) {
        model.set_tab_pinned(view, pinned);
    });
}

void xenon_engine_reopen_closed_tab(XenonWindowId window)
{
    post_to_model([window](auto& model) {
        model.reopen_closed_tab(window != XENON_WINDOW_NONE ? Optional<XenonWindowId> { window } : Optional<XenonWindowId> {});
    });
}

void xenon_engine_cycle_recent_tabs(XenonWindowId window, bool toward_less_recent)
{
    post_to_model([window, toward_less_recent](auto& model) {
        (void)model.step_recent_tab(window, toward_less_recent ? Xenon::BrowserModel::RecentOrder::Direction::LessRecent : Xenon::BrowserModel::RecentOrder::Direction::MoreRecent);
    });
}

void xenon_engine_end_recent_tab_cycle(XenonWindowId window)
{
    post_to_model([window](auto& model) {
        model.end_recent_tab_cycle(window);
    });
}

void xenon_engine_load(XenonViewId id, XenonStr input)
{
    post_to_view(id, [input = ByteString { string_view_from(input) }](View& view) {
        view.load_user_input(input);
    });
}

void xenon_engine_go_back(XenonViewId id)
{
    post_to_view(id, [](View& view) {
        view.go_back();
    });
}

void xenon_engine_go_forward(XenonViewId id)
{
    post_to_view(id, [](View& view) {
        view.go_forward();
    });
}

void xenon_engine_reload(XenonViewId id)
{
    post_to_view(id, [](View& view) {
        view.reload();
    });
}

void xenon_engine_stop(XenonViewId id)
{
    post_to_view(id, [](View& view) {
        view.stop_loading();
    });
}

void xenon_engine_set_viewport(XenonViewId id, uint32_t width, uint32_t height, double device_pixel_ratio)
{
    post_to_view(id, [id, size = viewport_size_from(width, height), device_pixel_ratio = device_pixel_ratio_from(device_pixel_ratio)](View& view) {
        Engine::the().record_view_viewport(id, size, device_pixel_ratio);
        view.set_viewport(size, device_pixel_ratio);
    });
}

void xenon_engine_set_visible(XenonViewId id, bool visible)
{
    post_to_view(id, [visible](View& view) {
        view.set_visible(visible);
    });
}

void xenon_engine_set_focused(XenonViewId id, bool focused)
{
    post_to_view(id, [id, focused](View& view) {
        Engine::the().set_focused_view(id, focused);
        view.set_focused(focused);
    });
}

void xenon_engine_mouse_event(XenonViewId id, XenonMouseEvent const* event)
{
    if (!event)
        return;
    post_to_view(id, [event = *event](View& view) {
        view.handle_mouse_event(event);
    });
}

void xenon_engine_key_event(XenonViewId id, XenonKeyEvent const* event)
{
    if (!event)
        return;
    auto type = event->type == XENON_KEY_EVENT_UP ? XENON_KEY_EVENT_UP : XENON_KEY_EVENT_DOWN;
    auto key_event = Xenon::key_event_from(type, string_view_from(event->code), string_view_from(event->key), event->modifiers, event->repeat);
    post_to_view(id, [key_event = move(key_event)](View& view) mutable {
        view.handle_key_event(move(key_event));
    });
}

void xenon_engine_insert_text(XenonViewId id, XenonStr text)
{
    post_to_view(id, [text = ByteString { string_view_from(text) }](View& view) {
        view.insert_text(text);
    });
}
