# The Xenon frontend. Meta/CMake/XenonHook.cmake includes this at the end of Ladybird's top-level CMakeLists.txt, once
# every engine library and helper process exists. CMake doesn't allow add_subdirectory() at that point, so this file is
# include()d into Ladybird's top-level directory instead: relative paths would resolve against Ladybird's tree, so every
# path here is absolute.
#
# This started as a copy of ladybird/UI/Qt and keeps its file layout and the `Ladybird` C++ namespace, so upstream
# fixes can still be diffed and merged in.

set(XENON_UI_DIR "${XENON_SOURCE_DIR}/UI")

find_package(Qt6 6.10 REQUIRED COMPONENTS Core Widgets)

if (NOT APPLE)
    find_package(Qt6 6.10 REQUIRED COMPONENTS Positioning)
endif()

if (APPLE OR (LINUX AND NOT ANDROID) OR HAS_DIRECTX)
    set(QT_NO_PRIVATE_MODULE_WARNING ON)
    find_package(Qt6 6.10 REQUIRED COMPONENTS GuiPrivate)
endif()

if (LINUX AND NOT ANDROID)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0 gio-unix-2.0)
    pkg_check_modules(XCB_ICCCM REQUIRED IMPORTED_TARGET xcb-icccm)
endif()

set(XENON_SOURCES
    main.cpp
    Application.cpp
    Autocomplete.cpp
    Bangs.cpp
    BookmarkDialog.cpp
    BookmarksBar.cpp
    BrowserWindow.cpp
    ChromeStyle.cpp
    ContentBlockingButton.cpp
    ContentBlockingPopover.cpp
    CrashReportNotifier.cpp
    CrashReportReviewWidget.cpp
    DevToolsBanner.cpp
    EventLoopImplementationQt.cpp
    EventLoopImplementationQtEventTarget.cpp
    Extensions/Extension.cpp
    Extensions/Locales.cpp
    Extensions/Manifest.cpp
    Extensions/MatchPattern.cpp
    Extensions/Storage.cpp
    FilterLists.cpp
    FindInPageWidget.cpp
    Icon.cpp
    HiddenPageSizing.cpp
    InputMethodUtils.cpp
    JavaScriptDialog.cpp
    LocationEdit.cpp
    Menu.cpp
    NativeWindowContainer.cpp
    NativeTextEditingLineEdit.cpp
    ProcessManagerWindow.cpp
    PerformanceMonitorWidget.cpp
    PictureInPictureWindow.cpp
    Popover.cpp
    ReportReview.cpp
    ScreenshotSelectionOverlay.cpp
    ScreenshotViewer.cpp
    SelectDropdown.cpp
    StringUtils.cpp
    Tab.cpp
    TabBar.cpp
    WebCompatReportReview.cpp
    WebContentView.cpp
    WindowCloseCoordinator.cpp
    WindowControlButton.cpp
    WindowScreenObserver.cpp
    XenonSettings.cpp
    xenon.qrc
)

if (NOT APPLE AND NOT WIN32 AND NOT ANDROID)
    list(APPEND XENON_SOURCES AccessibilityInterface.cpp)
endif()

if (Qt6Positioning_FOUND)
    list(APPEND XENON_SOURCES GeolocationProviderQt.cpp)
endif()

if (LINUX AND NOT ANDROID)
    list(APPEND XENON_SOURCES ExternalURLActivationToken.cpp ExternalURLHandler.cpp X11Window.cpp)
endif()

set(XENON_ARC_SOURCES
    LadybirdAccessibilityElement.mm
    SpellCheckerMac.mm
    WebContentViewAccessibility.mm
)

if (APPLE)
    list(APPEND XENON_SOURCES
        ApplicationIcon.mm
        Conversions.mm
        DictionaryLookup.mm
        ExternalURLHandler.mm
        MacWindow.mm
        MacWindowFocus.mm
        NativeTextEditingBindings.mm
        Menu.mm
        ScrollbarPreferencesMac.mm
        WebContentViewMac.mm
        ${XENON_ARC_SOURCES}
    )
endif()

if (HAS_DIRECTX)
    list(APPEND XENON_SOURCES WebContentViewWindows.cpp)
endif()

# Linux presents the page through Vulkan with DMA-BUF images when glslang is around to compile its shaders (see
# Ladybird's check_for_dependencies.cmake), as upstream's UI/Qt does. The generated headers get their own directory and
# target name, as upstream's ladybird target generates the same ones.
if (NOT APPLE AND USE_VULKAN_DMABUF_IMAGES)
    set(XENON_SHADER_DIR "${CMAKE_BINARY_DIR}/Xenon/Shaders")
    set(xenon_vert_shader_header "${XENON_SHADER_DIR}/WebContentViewLinuxVertShader.h")
    set(xenon_frag_shader_header "${XENON_SHADER_DIR}/WebContentViewLinuxFragShader.h")
    set(xenon_generated_shader_headers "${xenon_vert_shader_header}" "${xenon_frag_shader_header}")
    foreach (stage IN ITEMS vert frag)
        set(shader "${XENON_UI_DIR}/Shaders/WebContentViewLinux.${stage}")
        set(header "${xenon_${stage}_shader_header}")
        add_custom_command(
            OUTPUT "${header}"
            COMMAND ${CMAKE_COMMAND}
                -DLOG=${XENON_SHADER_DIR}/glslang-webcontent-${stage}.log
                -P "${LADYBIRD_SOURCE_DIR}/Meta/CMake/run_quiet.cmake"
                --
                "${GLSLANG_VALIDATOR}"
                -V
                --vn webcontent_${stage}_shader_spv
                -o "${header}"
                "${shader}"
            DEPENDS "${shader}"
            VERBATIM
        )
    endforeach()
    add_custom_target(xenon_generate_web_content_view_linux_shaders DEPENDS ${xenon_generated_shader_headers})
    list(APPEND XENON_SOURCES WebContentViewLinux.cpp)
endif()

list(TRANSFORM XENON_SOURCES PREPEND "${XENON_UI_DIR}/")
list(TRANSFORM XENON_ARC_SOURCES PREPEND "${XENON_UI_DIR}/")

# NB: Qt finalizes executables in a deferred call by default, and we are already running deferred, so finalize by hand.
qt_add_executable(xenon MANUAL_FINALIZATION ${XENON_SOURCES})
set_target_properties(xenon PROPERTIES AUTOMOC ON AUTORCC ON AUTOUIC ON)

target_link_libraries(xenon PRIVATE Qt::Core Qt::Gui Qt::Widgets)
target_link_libraries(xenon PRIVATE AK LibCore LibCrypto LibFileSystem LibGfx LibIPC LibMain LibWebView LibURL LibWakeLock)
target_link_libraries(xenon PRIVATE OpenSSL::Crypto OpenSSL::SSL)
# NB: Sources include each other as <UI/Foo.h>, which must resolve to Xenon's copies rather than anything in Ladybird's
#     tree, so Xenon's root comes first.
target_include_directories(xenon BEFORE PRIVATE ${XENON_SOURCE_DIR})
target_include_directories(xenon PRIVATE ${LADYBIRD_SOURCE_DIR} ${LADYBIRD_SOURCE_DIR}/Services/)

if (Qt6Positioning_FOUND)
    target_link_libraries(xenon PRIVATE Qt::Positioning)
    target_compile_definitions(xenon PRIVATE LADYBIRD_QT_HAVE_POSITIONING=1)
endif()

if (LINUX AND NOT ANDROID)
    target_link_libraries(xenon PRIVATE LibThreading PkgConfig::GIO PkgConfig::XCB_ICCCM Qt::GuiPrivate)
endif()

if (TARGET xenon_generate_web_content_view_linux_shaders)
    target_sources(xenon PRIVATE ${xenon_generated_shader_headers})
    target_include_directories(xenon PRIVATE "${XENON_SHADER_DIR}")
    target_link_libraries(xenon PRIVATE Vulkan::Vulkan)
    add_dependencies(xenon xenon_generate_web_content_view_linux_shaders)
endif()

if (APPLE)
    target_link_libraries(xenon PRIVATE Qt::GuiPrivate "-framework ApplicationServices" "-framework Cocoa" "-framework IOSurface" "-framework QuartzCore")
    set_source_files_properties(${XENON_ARC_SOURCES} PROPERTIES COMPILE_FLAGS "-fobjc-arc")
endif()

if (WIN32)
    target_link_libraries(xenon PRIVATE LibDevTools)
endif()

if (HAS_DIRECTX)
    target_link_libraries(xenon PRIVATE Qt::GuiPrivate d3d11)
endif()

set_target_properties(xenon PROPERTIES
    OUTPUT_NAME "Xenon"
    MACOSX_BUNDLE_GUI_IDENTIFIER org.xenon.Xenon
    MACOSX_BUNDLE_BUNDLE_VERSION ${LADYBIRD_BUNDLE_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${LADYBIRD_BUNDLE_VERSION}
    MACOSX_BUNDLE_INFO_PLIST "${XENON_UI_DIR}/Info.plist"
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

# The engine's helper processes (WebContent, RequestServer, ...) live next to the browser executable.
if (APPLE OR WIN32)
    set_target_properties(${ladybird_helper_processes} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:xenon>")
endif()
add_dependencies(xenon ${ladybird_helper_processes})

# NB: Ladybird's resource lists are variables of its UI directory, which don't reach this one.
include("${LADYBIRD_SOURCE_DIR}/UI/cmake/ResourceFiles.cmake")

# Xenon ships its own versions of some of Ladybird's internal pages, which take the place of upstream's in the bundle.
set(XENON_ABOUT_PAGE_OVERRIDES
    webui.css
    welcome.css
    welcome.html
    welcome.js
)
foreach (page IN LISTS XENON_ABOUT_PAGE_OVERRIDES)
    list(FIND ABOUT_PAGES "${LADYBIRD_SOURCE_DIR}/Base/res/ladybird/about-pages/${page}" index)
    if (index EQUAL -1)
        message(FATAL_ERROR "Xenon overrides about-pages/${page}, which Ladybird no longer ships. Check upstream's changes.")
    endif()
    list(REMOVE_AT ABOUT_PAGES ${index})
    list(INSERT ABOUT_PAGES ${index} "${XENON_UI_DIR}/Resources/about-pages/${page}")
endforeach()

if (APPLE)
    # NB: Ladybird sets this in its UI/CMakeLists.txt, whose variables don't reach this directory either.
    if (CMAKE_BUILD_TYPE MATCHES "Debug|RelWithDebInfo")
        set(XENON_ENTITLEMENTS_FILE "${LADYBIRD_SOURCE_DIR}/Meta/DebugEntitlements.plist")
    else()
        set(XENON_ENTITLEMENTS_FILE "${LADYBIRD_SOURCE_DIR}/Meta/Entitlements.plist")
    endif()

    set(xenon_bundle_dir "$<TARGET_BUNDLE_DIR:xenon>")
    set(xenon_resource_dir "${xenon_bundle_dir}/Contents/Resources")

    add_custom_command(TARGET xenon POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${xenon_bundle_dir}/Contents/Resources"
        COMMAND iconutil --convert icns "${XENON_UI_DIR}/Icons/macos/app_icon.iconset" --output "${xenon_bundle_dir}/Contents/Resources/app_icon.icns"
        # Not relocatable: the bundle links against libraries in the build directory, like Ladybird's dev bundle.
        COMMAND "${CMAKE_COMMAND}" -E create_symlink "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}" "${xenon_bundle_dir}/Contents/lib"
        # Sign last, after the helper processes (a dependency of this target) have been linked into the bundle.
        COMMAND codesign -s - -v -f --entitlements "${XENON_ENTITLEMENTS_FILE}" "${xenon_bundle_dir}"
        VERBATIM
    )
else()
    set(xenon_resource_dir "${CMAKE_BINARY_DIR}/${IN_BUILD_PREFIX}${CMAKE_INSTALL_DATADIR}/Lagom")
endif()

copy_resources_to_build("${xenon_resource_dir}" xenon)

# uBlock Origin's filter lists, converted for Ladybird by xenon-ublock (Meta/update-filter-lists.py refreshes them).
# UI/FilterLists.cpp hands the lists to the browser at startup; the notice and license travel with them.
file(GLOB XENON_FILTER_LISTS CONFIGURE_DEPENDS "${XENON_UI_DIR}/Resources/filter-lists/lists/*.txt")
copy_resource_set(xenon/filter-lists/lists RESOURCES ${XENON_FILTER_LISTS} DESTINATION "${xenon_resource_dir}" TARGET xenon)
copy_resource_set(xenon/filter-lists
    RESOURCES
        "${XENON_UI_DIR}/Resources/filter-lists/LICENSE-GPL-3.0.txt"
        "${XENON_UI_DIR}/Resources/filter-lists/NOTICE.txt"
        "${XENON_UI_DIR}/Resources/filter-lists/manifest.json"
    DESTINATION "${xenon_resource_dir}"
    TARGET xenon
)

qt_finalize_target(xenon)

# Xenon's unit tests, which Meta/xenon.py test builds and runs. Each is named TestXenon* to stay clear of Ladybird's.
ladybird_test("${XENON_SOURCE_DIR}/Tests/TestBangs.cpp" Xenon NAME TestXenonBangs LIBS LibURL)
target_sources(TestXenonBangs PRIVATE "${XENON_UI_DIR}/Bangs.cpp")
target_include_directories(TestXenonBangs BEFORE PRIVATE ${XENON_SOURCE_DIR})

ladybird_test("${XENON_SOURCE_DIR}/Tests/TestExtensions.cpp" Xenon NAME TestXenonExtensions LIBS LibCrypto LibFileSystem LibURL)
target_sources(TestXenonExtensions PRIVATE
    "${XENON_UI_DIR}/Extensions/Extension.cpp"
    "${XENON_UI_DIR}/Extensions/Locales.cpp"
    "${XENON_UI_DIR}/Extensions/Manifest.cpp"
    "${XENON_UI_DIR}/Extensions/MatchPattern.cpp"
    "${XENON_UI_DIR}/Extensions/Storage.cpp"
)
target_include_directories(TestXenonExtensions BEFORE PRIVATE ${XENON_SOURCE_DIR})

add_custom_target(xenon-tests DEPENDS TestXenonBangs TestXenonExtensions)
