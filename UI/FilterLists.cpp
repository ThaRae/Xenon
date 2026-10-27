/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/ByteString.h>
#include <AK/LexicalPath.h>
#include <AK/QuickSort.h>
#include <AK/Vector.h>
#include <LibCore/DirIterator.h>
#include <LibCore/System.h>
#include <LibFileSystem/FileSystem.h>
#include <UI/FilterLists.h>

namespace Ladybird::FilterLists {

static constexpr auto CONTENT_BLOCKER_LIST_OPTION = "--content-blocker-list"sv;

// The lists live in the application's resources, which Ladybird looks for next to the executable: in the bundle's
// Resources on macOS, and in share/Lagom under the install prefix elsewhere (see WebView::platform_init()).
static Optional<ByteString> bundled_lists_directory()
{
    auto executable = Core::System::current_executable_path();
    if (executable.is_error())
        return {};

    auto executable_directory = LexicalPath(executable.release_value()).parent();
#if defined(AK_OS_MACOS)
    auto directory = executable_directory.parent().append("Resources/xenon/filter-lists/lists"sv);
#else
    auto directory = executable_directory.parent().append("share/Lagom/xenon/filter-lists/lists"sv);
#endif

    if (!FileSystem::is_directory(directory.string()))
        return {};
    return directory.string();
}

Main::Arguments with_bundled_filter_lists(Main::Arguments arguments)
{
    // NB: Main::Arguments only views its strings, and the browser holds on to them, so they have to outlive main().
    static auto& list_paths = *new Vector<ByteString>;
    static auto& strings = *new Vector<StringView>;

    auto directory = bundled_lists_directory();
    if (!directory.has_value()) {
        warnln("Xenon's filter lists aren't where they should be, so content blocking has no lists to apply");
        return arguments;
    }

    Core::DirIterator iterator(*directory, Core::DirIterator::SkipDots);
    while (iterator.has_next()) {
        auto path = iterator.next_full_path();
        if (path.ends_with(".txt"sv) && FileSystem::is_regular_file(path))
            list_paths.append(move(path));
    }
    quick_sort(list_paths);

    // The lists go right after the program name, so that they are options however the user's own arguments end.
    strings.append(arguments.strings[0]);
    for (auto const& path : list_paths) {
        strings.append(CONTENT_BLOCKER_LIST_OPTION);
        strings.append(path);
    }
    strings.extend(Vector<StringView> { arguments.strings.slice(1) });

    arguments.strings = strings.span();
    return arguments;
}

}
