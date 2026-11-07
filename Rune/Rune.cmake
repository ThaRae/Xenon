set(XENON_RUNE_DIR "${XENON_SOURCE_DIR}/Rune")
set(XENON_RUNE_UI_MANIFEST "${XENON_RUNE_DIR}/ui/Cargo.toml")

if (NOT EXISTS "${XENON_RUNE_DIR}/ui/Cargo.lock")
    message(FATAL_ERROR "Rune/ui has no Cargo.lock. Run: cargo generate-lockfile --manifest-path Rune/ui/Cargo.toml")
endif()

function(xenon_import_rune_ui_crate)
    cmake_parse_arguments(PARSE_ARGV 0 ARG "" "MANIFEST_PATH;CRATE_NAME" "")

    file(RELATIVE_PATH manifest_path_from_source_dir "${CMAKE_CURRENT_SOURCE_DIR}" "${ARG_MANIFEST_PATH}")
    _rust_crate_common_setup(
        MANIFEST_PATH "${manifest_path_from_source_dir}"
        CRATE_NAME ${ARG_CRATE_NAME}
        TARGET_DIR "${CMAKE_BINARY_DIR}/cargo/${ARG_CRATE_NAME}"
    )
    set(cargo_feature_flags "")

    set(output_lib "${cargo_output_dir}/lib${ARG_CRATE_NAME}.a")
    _rust_crate_build_command(OUTPUT "${output_lib}" DEPFILE "${cargo_output_dir}/lib${ARG_CRATE_NAME}.d")

    add_custom_target(${ARG_CRATE_NAME}-build DEPENDS "${output_lib}")
    add_library(${ARG_CRATE_NAME} STATIC IMPORTED GLOBAL)
    set_target_properties(${ARG_CRATE_NAME} PROPERTIES IMPORTED_LOCATION "${output_lib}")
    add_dependencies(${ARG_CRATE_NAME} ${ARG_CRATE_NAME}-build)

    if (APPLE)
        target_link_libraries(${ARG_CRATE_NAME} INTERFACE
            "-framework AppKit"
            "-framework AVFoundation"
            "-framework Carbon"
            "-framework CoreFoundation"
            "-framework CoreMedia"
            "-framework CoreGraphics"
            "-framework CoreText"
            "-framework CoreVideo"
            "-framework Foundation"
            "-framework IOSurface"
            "-framework Metal"
            "-framework QuartzCore"
            objc
            iconv
        )
    elseif (LINUX)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(XKBCOMMON REQUIRED IMPORTED_TARGET xkbcommon)
        target_link_libraries(${ARG_CRATE_NAME} INTERFACE PkgConfig::XKBCOMMON ${CMAKE_DL_LIBS} Threads::Threads m rt util)
    endif()
endfunction()

find_package(Python3 COMPONENTS Interpreter REQUIRED)

add_custom_target(xenon-rune-vendor
    COMMAND ${Python3_EXECUTABLE} "${XENON_SOURCE_DIR}/Meta/rune.py" prepare
    COMMENT "Preparing the patched Rune source tree"
    VERBATIM
)

xenon_import_rune_ui_crate(MANIFEST_PATH "${XENON_RUNE_UI_MANIFEST}" CRATE_NAME xenon_ui)
add_dependencies(xenon_ui-build xenon-rune-vendor)

set(XENON_RUNE_BRIDGE_SOURCES
    "${XENON_RUNE_DIR}/Bridge/AppMenus.cpp"
    "${XENON_RUNE_DIR}/Bridge/Application.cpp"
    "${XENON_RUNE_DIR}/Bridge/Engine.cpp"
    "${XENON_RUNE_DIR}/Bridge/ExtensionsBrowser.cpp"
    "${XENON_RUNE_DIR}/Bridge/Input.cpp"
    "${XENON_RUNE_DIR}/Bridge/TabMemory.cpp"
    "${XENON_RUNE_DIR}/Bridge/View.cpp"
    "${XENON_RUNE_DIR}/Bridge/XenonBridge.cpp"
)

if (APPLE)
    list(APPEND XENON_RUNE_BRIDGE_SOURCES "${XENON_RUNE_DIR}/Bridge/ClipboardMac.mm" "${XENON_RUNE_DIR}/Bridge/PlatformMac.mm")
    list(APPEND XENON_RUNE_BRIDGE_SOURCES "${XENON_RUNE_DIR}/Bridge/AccessibilityMac.mm")
else()
    list(APPEND XENON_RUNE_BRIDGE_SOURCES "${XENON_RUNE_DIR}/Bridge/PlatformOther.cpp")
    list(APPEND XENON_RUNE_BRIDGE_SOURCES "${XENON_RUNE_DIR}/Bridge/AccessibilityOther.cpp")
endif()

add_library(xenon-rune-bridge OBJECT ${XENON_RUNE_BRIDGE_SOURCES})
target_link_libraries(xenon-rune-bridge PUBLIC xenon-core LibThreading)
target_compile_definitions(xenon-rune-bridge PRIVATE XENON_VERSION="${XENON_VERSION}")

add_executable(xenon-rune "${XENON_RUNE_DIR}/Bridge/main.cpp")
target_link_libraries(xenon-rune PRIVATE xenon_ui xenon-rune-bridge)

if (APPLE)
    set_target_properties(xenon-rune PROPERTIES RUNTIME_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:xenon>")
    add_dependencies(xenon-rune xenon)
else()
    set_target_properties(xenon-rune PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${XENON_PREFIX_DIR}/${CMAKE_INSTALL_BINDIR}")
    add_dependencies(xenon-rune ${ladybird_helper_processes} xenon_build_resource_files)
    add_custom_command(TARGET xenon-rune POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E create_symlink "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}" "${XENON_PREFIX_DIR}/${CMAKE_INSTALL_LIBDIR}"
        VERBATIM
    )
endif()

ladybird_test("${XENON_SOURCE_DIR}/Tests/TestXenonRuneBridge.cpp" Xenon NAME TestXenonRuneBridge LIBS xenon-rune-bridge)
if (TARGET ladybird-helper-processes)
    add_dependencies(TestXenonRuneBridge ladybird-helper-processes)
endif()
if (TARGET ladybird_build_resource_files)
    add_dependencies(TestXenonRuneBridge ladybird_build_resource_files)
endif()
add_dependencies(xenon-tests TestXenonRuneBridge)

target_sources(xenon-rune-bridge PRIVATE "${XENON_RUNE_DIR}/Bridge/XenonBridgePage.cpp")
target_sources(xenon-rune-bridge PRIVATE "${XENON_RUNE_DIR}/Bridge/XenonBridgePrompts.cpp")

target_sources(xenon-rune-bridge PRIVATE "${XENON_RUNE_DIR}/Bridge/Drop.cpp")

target_sources(xenon-rune-bridge PRIVATE "${XENON_RUNE_DIR}/Bridge/PictureInPicture.cpp" "${XENON_RUNE_DIR}/Bridge/ViewMedia.cpp")
target_link_libraries(xenon-rune-bridge PUBLIC LibWakeLock)

target_sources(xenon-rune-bridge PRIVATE "${XENON_RUNE_DIR}/Bridge/Screenshots.cpp")

target_sources(xenon-rune-bridge PRIVATE "${XENON_RUNE_DIR}/Bridge/CrashReports.cpp")

target_sources(xenon-rune-bridge PRIVATE "${XENON_RUNE_DIR}/Bridge/UpdaterBridge.cpp")
if (APPLE)
    target_sources(xenon-rune-bridge PRIVATE "${XENON_RUNE_DIR}/Bridge/UpdaterMac.mm")
    set_source_files_properties("${XENON_RUNE_DIR}/Bridge/UpdaterMac.mm" PROPERTIES COMPILE_FLAGS "-fobjc-arc -F${XENON_SPARKLE_DIR}")
    target_link_options(xenon-rune-bridge PUBLIC "-F${XENON_SPARKLE_DIR}" "-Wl,-rpath,@executable_path/../Frameworks")
    target_link_libraries(xenon-rune-bridge PUBLIC "-framework Sparkle")
else()
    target_sources(xenon-rune-bridge PRIVATE "${XENON_RUNE_DIR}/Bridge/Updater.cpp")
endif()

target_sources(xenon-rune-bridge PRIVATE "${XENON_RUNE_DIR}/Bridge/PlatformServices.cpp")
if (APPLE)
    target_sources(xenon-rune-bridge PRIVATE "${XENON_RUNE_DIR}/Bridge/PlatformServicesMac.mm" "${XENON_UI_DIR}/DictionaryLookup.mm" "${XENON_UI_DIR}/SpellCheckerMac.mm")
    set_source_files_properties("${XENON_RUNE_DIR}/Bridge/PlatformServicesMac.mm" "${XENON_UI_DIR}/SpellCheckerMac.mm" PROPERTIES COMPILE_FLAGS "-fobjc-arc")
elseif (LINUX AND NOT ANDROID)
    target_sources(xenon-rune-bridge PRIVATE "${XENON_RUNE_DIR}/Bridge/PlatformServicesOther.cpp" "${XENON_RUNE_DIR}/Bridge/ExternalURLActivationToken.cpp" "${XENON_UI_DIR}/ExternalURLHandler.cpp")
else()
    target_sources(xenon-rune-bridge PRIVATE "${XENON_RUNE_DIR}/Bridge/PlatformServicesOther.cpp")
endif()

target_sources(xenon-rune-bridge PRIVATE "${XENON_RUNE_DIR}/Bridge/BrowserModel.cpp")
target_sources(xenon-rune-bridge PRIVATE "${XENON_RUNE_DIR}/Bridge/BrowserModelTabGroups.cpp" "${XENON_RUNE_DIR}/Bridge/XenonBridgeTabGroups.cpp")
ladybird_test("${XENON_SOURCE_DIR}/Tests/TestXenonBrowserModel.cpp" Xenon NAME TestXenonBrowserModel LIBS xenon-rune-bridge)
add_dependencies(xenon-tests TestXenonBrowserModel)

target_sources(xenon-rune-bridge PRIVATE "${XENON_RUNE_DIR}/Bridge/XenonBridgeLocation.cpp")

target_sources(xenon-rune-bridge PRIVATE
    "${XENON_RUNE_DIR}/Bridge/TabHibernation.cpp"
    "${XENON_RUNE_DIR}/Bridge/XenonBridgeHibernation.cpp"
    "${XENON_RUNE_DIR}/Bridge/XenonBridgeTaskManager.cpp"
    "${XENON_RUNE_DIR}/Bridge/XenonBridgeBookmarks.cpp"
    "${XENON_RUNE_DIR}/Bridge/XenonBridgeElementPicker.cpp"
    "${XENON_RUNE_DIR}/Bridge/XenonBridgeLocationMenu.cpp"
)
target_sources(xenon-rune-bridge PRIVATE "${XENON_RUNE_DIR}/Bridge/XenonBridgeExtensions.cpp")

ladybird_test("${XENON_SOURCE_DIR}/Tests/TestXenonRunePage.cpp" Xenon NAME TestXenonRunePage LIBS xenon-rune-bridge)
if (TARGET ladybird-helper-processes)
    add_dependencies(TestXenonRunePage ladybird-helper-processes)
endif()
if (TARGET ladybird_build_resource_files)
    add_dependencies(TestXenonRunePage ladybird_build_resource_files)
endif()
add_dependencies(xenon-tests TestXenonRunePage)

target_sources(xenon-rune-bridge PRIVATE "${XENON_RUNE_DIR}/Bridge/WindowLifecycle.cpp" "${XENON_RUNE_DIR}/Bridge/XenonBridgeWindows.cpp")
if (APPLE)
    target_sources(xenon-rune-bridge PRIVATE "${XENON_RUNE_DIR}/Bridge/PlatformWindowsMac.mm")
    set_source_files_properties("${XENON_RUNE_DIR}/Bridge/PlatformWindowsMac.mm" PROPERTIES COMPILE_FLAGS "-fobjc-arc")
else()
    target_sources(xenon-rune-bridge PRIVATE "${XENON_RUNE_DIR}/Bridge/PlatformWindowsOther.cpp")
endif()

ladybird_test("${XENON_SOURCE_DIR}/Tests/TestXenonRuneWindows.cpp" Xenon NAME TestXenonRuneWindows LIBS xenon-rune-bridge)
if (TARGET ladybird-helper-processes)
    add_dependencies(TestXenonRuneWindows ladybird-helper-processes)
endif()
if (TARGET ladybird_build_resource_files)
    add_dependencies(TestXenonRuneWindows ladybird_build_resource_files)
endif()
add_dependencies(xenon-tests TestXenonRuneWindows)

ladybird_test("${XENON_SOURCE_DIR}/Tests/TestXenonRuneBookmarks.cpp" Xenon NAME TestXenonRuneBookmarks LIBS xenon-rune-bridge)
if (TARGET ladybird-helper-processes)
    add_dependencies(TestXenonRuneBookmarks ladybird-helper-processes)
endif()
if (TARGET ladybird_build_resource_files)
    add_dependencies(TestXenonRuneBookmarks ladybird_build_resource_files)
endif()
add_dependencies(xenon-tests TestXenonRuneBookmarks)

if (NOT APPLE)
    set_target_properties(TestXenonRuneBridge TestXenonRunePage TestXenonRuneWindows TestXenonRuneBookmarks PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${XENON_PREFIX_DIR}/tests")
endif()

target_sources(xenon-rune-bridge PRIVATE "${XENON_RUNE_DIR}/Bridge/XenonBridgeAccessibility.cpp")

ladybird_test("${XENON_SOURCE_DIR}/Tests/TestXenonRuneAccessibility.cpp" Xenon NAME TestXenonRuneAccessibility LIBS xenon-rune-bridge)
if (TARGET ladybird-helper-processes)
    add_dependencies(TestXenonRuneAccessibility ladybird-helper-processes)
endif()
if (TARGET ladybird_build_resource_files)
    add_dependencies(TestXenonRuneAccessibility ladybird_build_resource_files)
endif()
add_dependencies(xenon-tests TestXenonRuneAccessibility)
if (NOT APPLE)
    set_target_properties(TestXenonRuneAccessibility PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${XENON_PREFIX_DIR}/tests")
endif()

ladybird_test("${XENON_SOURCE_DIR}/Tests/TestXenonRuneExtensions.cpp" Xenon NAME TestXenonRuneExtensions LIBS xenon-rune-bridge)
target_compile_definitions(TestXenonRuneExtensions PRIVATE XENON_SOURCE_DIR="${XENON_SOURCE_DIR}")
if (TARGET ladybird-helper-processes)
    add_dependencies(TestXenonRuneExtensions ladybird-helper-processes)
endif()
if (TARGET ladybird_build_resource_files)
    add_dependencies(TestXenonRuneExtensions ladybird_build_resource_files)
endif()
add_dependencies(xenon-tests TestXenonRuneExtensions)
if (NOT APPLE)
    set_target_properties(TestXenonRuneExtensions PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${XENON_PREFIX_DIR}/tests")
endif()

target_sources(xenon-rune-bridge PRIVATE "${XENON_RUNE_DIR}/Bridge/SplitViews.cpp" "${XENON_RUNE_DIR}/Bridge/XenonBridgeSplitView.cpp")
ladybird_test("${XENON_SOURCE_DIR}/Tests/TestXenonSplitViews.cpp" Xenon NAME TestXenonSplitViews LIBS xenon-rune-bridge)
add_dependencies(xenon-tests TestXenonSplitViews)

ladybird_test("${XENON_SOURCE_DIR}/Tests/TestXenonRunePrompts.cpp" Xenon NAME TestXenonRunePrompts LIBS xenon-rune-bridge)
if (TARGET ladybird-helper-processes)
    add_dependencies(TestXenonRunePrompts ladybird-helper-processes)
endif()
if (TARGET ladybird_build_resource_files)
    add_dependencies(TestXenonRunePrompts ladybird_build_resource_files)
endif()
add_dependencies(xenon-tests TestXenonRunePrompts)
if (NOT APPLE)
    set_target_properties(TestXenonRunePrompts PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${XENON_PREFIX_DIR}/tests")
endif()

ladybird_test("${XENON_SOURCE_DIR}/Tests/TestXenonRunePrompts.cpp" Xenon NAME TestXenonRunePrompts LIBS xenon-rune-bridge)
if (TARGET ladybird-helper-processes)
    add_dependencies(TestXenonRunePrompts ladybird-helper-processes)
endif()
if (TARGET ladybird_build_resource_files)
    add_dependencies(TestXenonRunePrompts ladybird_build_resource_files)
endif()
add_dependencies(xenon-tests TestXenonRunePrompts)
if (NOT APPLE)
    set_target_properties(TestXenonRunePrompts PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${XENON_PREFIX_DIR}/tests")
endif()
