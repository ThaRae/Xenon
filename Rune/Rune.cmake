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
            "-framework CoreFoundation"
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

xenon_import_rune_ui_crate(MANIFEST_PATH "${XENON_RUNE_UI_MANIFEST}" CRATE_NAME xenon_ui)

set(XENON_RUNE_BRIDGE_SOURCES
    "${XENON_RUNE_DIR}/Bridge/Application.cpp"
    "${XENON_RUNE_DIR}/Bridge/Engine.cpp"
    "${XENON_RUNE_DIR}/Bridge/Input.cpp"
    "${XENON_RUNE_DIR}/Bridge/View.cpp"
    "${XENON_RUNE_DIR}/Bridge/XenonBridge.cpp"
    "${XENON_RUNE_DIR}/Bridge/XenonSettings.cpp"
    "${XENON_UI_DIR}/Bangs.cpp"
    "${XENON_UI_DIR}/FilterLists.cpp"
    "${XENON_UI_DIR}/MemoryPressure.cpp"
    "${XENON_UI_DIR}/MemoryPressureRelease.cpp"
)

add_library(xenon-rune-bridge STATIC ${XENON_RUNE_BRIDGE_SOURCES})
target_include_directories(xenon-rune-bridge BEFORE PUBLIC ${XENON_SOURCE_DIR})
target_include_directories(xenon-rune-bridge PUBLIC ${LADYBIRD_SOURCE_DIR} ${LADYBIRD_SOURCE_DIR}/Services/ ${XENON_GENERATED_DIR})
target_link_libraries(xenon-rune-bridge PUBLIC AK LibCore LibFileSystem LibGfx LibIPC LibThreading LibURL LibWebView)
add_dependencies(xenon-rune-bridge xenon_generate_bang_table)

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
