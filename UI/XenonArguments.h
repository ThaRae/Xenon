/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteString.h>
#include <AK/Optional.h>
#include <AK/Vector.h>
#include <LibCore/Forward.h>

namespace Ladybird {

struct XenonArguments {
    void add_options(Core::ArgsParser&);

    Vector<ByteString> extensions_to_load;
    Optional<ByteString> password_manager_choice;
};

}
