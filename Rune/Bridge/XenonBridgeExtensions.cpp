/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/String.h>
#include <Rune/Bridge/Engine.h>
#include <Rune/Bridge/ExtensionsHost.h>
#include <Rune/Bridge/XenonBridgeExtensions.h>

using Xenon::Engine;
namespace ExtensionsHost = Xenon::ExtensionsHost;

static String string_from(XenonStr string)
{
    if (!string.data || string.length == 0)
        return {};
    return String::from_utf8_with_replacement_character({ string.data, string.length });
}

void xenon_engine_set_extension_callbacks(XenonExtensionCallbacks const* callbacks)
{
    ExtensionsHost::set_callbacks(callbacks ? *callbacks : XenonExtensionCallbacks {});
}

void xenon_engine_extension_action_clicked(XenonViewId tab, XenonStr extension_id)
{
    Engine::the().post([tab, id = string_from(extension_id)] {
        ExtensionsHost::action_clicked(tab, id);
    });
}

void xenon_engine_close_extension_popup(XenonViewId popup)
{
    Engine::the().post([popup] {
        ExtensionsHost::close_popup(popup);
    });
}

void xenon_engine_answer_extension_permissions(XenonPromptId prompt, bool granted)
{
    Engine::the().post([prompt, granted] {
        ExtensionsHost::answer_permissions(prompt, granted);
    });
}

void xenon_engine_run_extension_command(XenonViewId tab, XenonStr extension_id, XenonStr command)
{
    Engine::the().post([tab, id = string_from(extension_id), command = string_from(command)] {
        ExtensionsHost::run_command(tab, id, command);
    });
}

void xenon_engine_open_extension_options(XenonStr extension_id)
{
    Engine::the().post([id = string_from(extension_id)] {
        ExtensionsHost::open_options(id);
    });
}

void xenon_engine_reload_extension(XenonStr extension_id)
{
    Engine::the().post([id = string_from(extension_id)] {
        ExtensionsHost::reload(id);
    });
}

void xenon_engine_remove_extension(XenonStr extension_id)
{
    Engine::the().post([id = string_from(extension_id)] {
        ExtensionsHost::remove(id);
    });
}

void xenon_engine_load_unpacked_extension(XenonStr directory)
{
    Engine::the().post([directory = string_from(directory)] {
        ExtensionsHost::load_unpacked(directory);
    });
}

void xenon_engine_choose_password_manager(XenonStr id)
{
    Engine::the().post([id = string_from(id)] {
        ExtensionsHost::choose_password_manager(id);
    });
}

void xenon_engine_show_extensions(void)
{
    Engine::the().post([] {
        ExtensionsHost::show_extensions();
    });
}
