/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibCore/ArgsParser.h>
#include <UI/Extensions/Router.h>
#include <UI/XenonArguments.h>

namespace Ladybird {

void XenonArguments::add_options(Core::ArgsParser& args_parser)
{
    args_parser.add_option(Core::ArgsParser::Option {
        .argument_mode = Core::ArgsParser::OptionArgumentMode::Required,
        .help_string = "Load the unpacked extensions in these directories, separated by commas",
        .long_name = "load-extension",
        .short_name = 0,
        .value_name = "paths",
        .accept_value = [this](StringView paths) -> ErrorOr<bool> {
            for (auto path : paths.split_view(','))
                extensions_to_load.append(path);
            return true;
        },
        .hide_mode = Core::ArgsParser::OptionHideMode::None,
    });
    args_parser.add_option(Core::ArgsParser::Option {
        .argument_mode = Core::ArgsParser::OptionArgumentMode::Required,
        .help_string = "Stop an extension's background once it has been idle this many seconds (30 by default, as in Chrome)",
        .long_name = "extension-idle-timeout",
        .short_name = 0,
        .value_name = "seconds",
        .accept_value = [](StringView seconds) -> ErrorOr<bool> {
            auto value = seconds.to_number<u32>();
            if (!value.has_value() || *value == 0)
                return false;
            Extensions::Router::the().set_background_idle_timeout(AK::Duration::from_seconds(*value));
            return true;
        },
        .hide_mode = Core::ArgsParser::OptionHideMode::None,
    });
    args_parser.add_option(Core::ArgsParser::Option {
        .argument_mode = Core::ArgsParser::OptionArgumentMode::Required,
        .help_string = "Install this password manager (bitwarden, proton-pass, 1password, or icloud-passwords on macOS) in place of the profile's, or remove it with none",
        .long_name = "password-manager",
        .short_name = 0,
        .value_name = "name",
        .accept_value = [this](StringView name) -> ErrorOr<bool> {
            password_manager_choice = name;
            return true;
        },
        .hide_mode = Core::ArgsParser::OptionHideMode::None,
    });
}

}
