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

# FIXME: Port the Vulkan/DMA-BUF presentation path (WebContentViewLinux.cpp + Shaders/) once Xenon targets Linux.

list(TRANSFORM XENON_SOURCES PREPEND "${XENON_UI_DIR}/")
list(TRANSFORM XENON_ARC_SOURCES PREPEND "${XENON_UI_DIR}/")

# NB: Qt finalizes executables in a deferred call by default, and we are already running deferred, so finalize by hand.
qt_add_executable(xenon MANUAL_FINALIZATION ${XENON_SOURCES})
set_target_properties(xenon PROPERTIES AUTOMOC ON AUTORCC ON AUTOUIC ON)

target_link_libraries(xenon PRIVATE Qt::Core Qt::Gui Qt::Widgets)
target_link_libraries(xenon PRIVATE AK LibCore LibGfx LibIPC LibMain LibWebView LibURL LibWakeLock)
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

if (APPLE)
    # NB: Ladybird sets this in its UI/CMakeLists.txt, whose variables don't reach this directory either.
    if (CMAKE_BUILD_TYPE MATCHES "Debug|RelWithDebInfo")
        set(XENON_ENTITLEMENTS_FILE "${LADYBIRD_SOURCE_DIR}/Meta/DebugEntitlements.plist")
    else()
        set(XENON_ENTITLEMENTS_FILE "${LADYBIRD_SOURCE_DIR}/Meta/Entitlements.plist")
    endif()

    set(xenon_bundle_dir "$<TARGET_BUNDLE_DIR:xenon>")
    copy_resources_to_build("${xenon_bundle_dir}/Contents/Resources" xenon)

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
    copy_resources_to_build("${CMAKE_BINARY_DIR}/${IN_BUILD_PREFIX}${CMAKE_INSTALL_DATADIR}/Lagom" xenon)
endif()

qt_finalize_target(xenon)
