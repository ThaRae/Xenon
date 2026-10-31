set(XENON_UI_DIR "${XENON_SOURCE_DIR}/UI")

file(STRINGS "${XENON_SOURCE_DIR}/VERSION.txt" XENON_VERSION LIMIT_COUNT 1)
if (NOT XENON_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "VERSION.txt must be major.minor.patch: ${XENON_VERSION}")
endif()
set_property(DIRECTORY "${CMAKE_SOURCE_DIR}" APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${XENON_SOURCE_DIR}/VERSION.txt")

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
    Blocking.cpp
    BlockingDashboard.cpp
    BlockingPopover.cpp
    BookmarkDialog.cpp
    BookmarksBar.cpp
    BrowserWindow.cpp
    CertificateDetails.cpp
    CertificateErrors.cpp
    ChromeStyle.cpp
    ContentBlockingButton.cpp
    ContentBlockingPopover.cpp
    CrashReportNotifier.cpp
    CrashReportReviewWidget.cpp
    DevToolsBanner.cpp
    Download.cpp
    ElementPicker.cpp
    EventLoopImplementationQt.cpp
    EventLoopImplementationQtEventTarget.cpp
    Extensions/Alarms.cpp
    Extensions/Browser.cpp
    Extensions/CRX.cpp
    Extensions/ContentScripts.cpp
    Extensions/Dialog.cpp
    Extensions/Extension.cpp
    Extensions/Locales.cpp
    Extensions/Manifest.cpp
    Extensions/MatchPattern.cpp
    Extensions/NativeMessaging.cpp
    Extensions/Popup.cpp
    Extensions/Registry.cpp
    Extensions/Router.cpp
    Extensions/Scripting.cpp
    Extensions/Storage.cpp
    Extensions/Toolbar.cpp
    Extensions/WebStore.cpp
    FilterListBundle.cpp
    FilterListUpdates.cpp
    FilterLists.cpp
    FindInPageWidget.cpp
    Icon.cpp
    HiddenPageSizing.cpp
    InputMethodUtils.cpp
    JavaScriptDialog.cpp
    LocationEdit.cpp
    Menu.cpp
    MemoryPressure.cpp
    NativeWindowContainer.cpp
    NativeTextEditingLineEdit.cpp
    NewTabPage.cpp
    PasswordManagers.cpp
    ProcessManagerWindow.cpp
    PerformanceMonitorWidget.cpp
    PictureInPictureWindow.cpp
    Popover.cpp
    RelaunchToUpdate.cpp
    ReportReview.cpp
    ScreenshotSelectionOverlay.cpp
    ScreenshotViewer.cpp
    SelectDropdown.cpp
    SessionSaving.cpp
    SettingsPage.cpp
    SplitView.cpp
    StringUtils.cpp
    Tab.cpp
    TabBar.cpp
    TabCycler.cpp
    TabGroupEditor.cpp
    TabGroups.cpp
    TabHibernation.cpp
    TabMemory.cpp
    UpdateButton.cpp
    WebAuthnPopover.cpp
    WebCompatReportReview.cpp
    WebContentView.cpp
    WebUIInterfaces.cpp
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

if (NOT APPLE)
    list(APPEND XENON_SOURCES Updater.cpp)
endif()

set(XENON_ARC_SOURCES
    LadybirdAccessibilityElement.mm
    SpellCheckerMac.mm
    Updater.mm
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

set(XENON_GENERATED_DIR "${CMAKE_BINARY_DIR}/Xenon")
embed_as_string(xenon_chrome_api "${XENON_UI_DIR}/Resources/extensions/chrome-api.js" "${XENON_GENERATED_DIR}/ChromeAPI.cpp" chrome_api_source NAMESPACE "Ladybird::Extensions")
embed_as_string(xenon_background_page "${XENON_UI_DIR}/Resources/extensions/background-page.js" "${XENON_GENERATED_DIR}/BackgroundPage.cpp" background_page_source NAMESPACE "Ladybird::Extensions")
embed_as_string(xenon_worker_bridge "${XENON_UI_DIR}/Resources/extensions/worker-bridge.js" "${XENON_GENERATED_DIR}/WorkerBridge.cpp" worker_bridge_source NAMESPACE "Ladybird::Extensions")
embed_as_string(xenon_externally_connectable "${XENON_UI_DIR}/Resources/extensions/externally-connectable.js" "${XENON_GENERATED_DIR}/ExternallyConnectable.cpp" externally_connectable_source NAMESPACE "Ladybird::Extensions")
embed_as_string(xenon_externally_connectable_relay "${XENON_UI_DIR}/Resources/extensions/externally-connectable-relay.js" "${XENON_GENERATED_DIR}/ExternallyConnectableRelay.cpp" externally_connectable_relay_source NAMESPACE "Ladybird::Extensions")
list(APPEND XENON_SOURCES "${XENON_GENERATED_DIR}/ChromeAPI.cpp" "${XENON_GENERATED_DIR}/BackgroundPage.cpp" "${XENON_GENERATED_DIR}/WorkerBridge.cpp")
list(APPEND XENON_SOURCES "${XENON_GENERATED_DIR}/ExternallyConnectable.cpp" "${XENON_GENERATED_DIR}/ExternallyConnectableRelay.cpp")

set(XENON_BANG_TABLE "${XENON_GENERATED_DIR}/UI/BangTable.h")
add_custom_command(
    OUTPUT "${XENON_BANG_TABLE}"
    COMMAND "${Python3_EXECUTABLE}" "${XENON_SOURCE_DIR}/Meta/generate-bang-table.py" "${XENON_UI_DIR}/Resources/bangs/bangs.json" -o "${XENON_BANG_TABLE}"
    DEPENDS "${XENON_SOURCE_DIR}/Meta/generate-bang-table.py" "${XENON_UI_DIR}/Resources/bangs/bangs.json"
    VERBATIM
)
add_custom_target(xenon_generate_bang_table DEPENDS "${XENON_BANG_TABLE}")

qt_add_executable(xenon MANUAL_FINALIZATION ${XENON_SOURCES})
set_target_properties(xenon PROPERTIES AUTOMOC ON AUTORCC ON AUTOUIC ON)

target_link_libraries(xenon PRIVATE Qt::Core Qt::Gui Qt::Widgets)
target_link_libraries(xenon PRIVATE AK LibCore LibCrypto LibFileSystem LibGfx LibHTTP LibIPC LibMain LibRequests LibWebView LibURL LibWakeLock LibXML)
target_link_libraries(xenon PRIVATE OpenSSL::Crypto OpenSSL::SSL ZLIB::ZLIB)
target_include_directories(xenon BEFORE PRIVATE ${XENON_SOURCE_DIR})
target_include_directories(xenon PRIVATE ${LADYBIRD_SOURCE_DIR} ${LADYBIRD_SOURCE_DIR}/Services/ ${XENON_GENERATED_DIR})
target_compile_definitions(xenon PRIVATE XENON_VERSION="${XENON_VERSION}")
add_dependencies(xenon xenon_generate_bang_table)

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

    set(XENON_SPARKLE_VERSION 2.10.1)
    set(XENON_SPARKLE_SHA256 1a856d2a9edb8e628de88121d7be04cf0e2167f8641650b78fca00b14244cf27)
    set(XENON_SPARKLE_DIR "${CMAKE_BINARY_DIR}/Xenon/Sparkle-${XENON_SPARKLE_VERSION}")
    if (NOT EXISTS "${XENON_SPARKLE_DIR}/Sparkle.framework")
        set(sparkle_archive "${CMAKE_BINARY_DIR}/Xenon/Sparkle-${XENON_SPARKLE_VERSION}.tar.xz")
        file(DOWNLOAD "https://github.com/sparkle-project/Sparkle/releases/download/${XENON_SPARKLE_VERSION}/Sparkle-${XENON_SPARKLE_VERSION}.tar.xz"
            "${sparkle_archive}" EXPECTED_HASH SHA256=${XENON_SPARKLE_SHA256} TLS_VERIFY ON)
        file(ARCHIVE_EXTRACT INPUT "${sparkle_archive}" DESTINATION "${XENON_SPARKLE_DIR}")
        file(REMOVE "${sparkle_archive}")
    endif()
    target_compile_options(xenon PRIVATE "-F${XENON_SPARKLE_DIR}")
    target_link_options(xenon PRIVATE "-F${XENON_SPARKLE_DIR}" "-Wl,-rpath,@executable_path/../Frameworks")
    target_link_libraries(xenon PRIVATE "-framework Sparkle")
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
    MACOSX_BUNDLE_BUNDLE_VERSION ${XENON_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${XENON_VERSION}
    MACOSX_BUNDLE_INFO_PLIST "${XENON_UI_DIR}/Info.plist"
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

if (APPLE OR WIN32)
    set_target_properties(${ladybird_helper_processes} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:xenon>")
else()
    set(XENON_PREFIX_DIR "${CMAKE_BINARY_DIR}/xenon")
    set_target_properties(xenon PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${XENON_PREFIX_DIR}/${CMAKE_INSTALL_BINDIR}")
    set_target_properties(${ladybird_helper_processes} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${XENON_PREFIX_DIR}/${CMAKE_INSTALL_LIBEXECDIR}")
    add_custom_command(TARGET xenon POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E create_symlink "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}" "${XENON_PREFIX_DIR}/${CMAKE_INSTALL_LIBDIR}"
        VERBATIM
    )
endif()
add_dependencies(xenon ${ladybird_helper_processes})

if (BUILD_TESTING)
    if (APPLE OR WIN32)
        set(ladybird_helper_process_dir "$<TARGET_FILE_DIR:ladybird>")
    else()
        set(ladybird_helper_process_dir "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_LIBEXECDIR}")
    endif()
    set(ladybird_helper_process_files)
    foreach (helper IN LISTS ladybird_helper_processes)
        list(APPEND ladybird_helper_process_files "$<TARGET_FILE:${helper}>")
    endforeach()
    add_custom_target(ladybird-helper-processes ALL
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${ladybird_helper_process_dir}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${ladybird_helper_process_files} "${ladybird_helper_process_dir}"
        VERBATIM
    )
    add_dependencies(ladybird-helper-processes ladybird ${ladybird_helper_processes})
    if (TARGET sign_ladybird_bundle)
        add_dependencies(sign_ladybird_bundle ladybird-helper-processes)
    endif()
endif()

include("${LADYBIRD_SOURCE_DIR}/UI/cmake/ResourceFiles.cmake")

set(XENON_ABOUT_PAGE_OVERRIDES
    newtab.html
    settings.html
    version.html
    webui.css
    welcome.css
    welcome.html
    welcome.js
)
set(XENON_ABOUT_PAGE_ADDITIONS
    newtab.css
    newtab.js
    settings.css
    settings.js
    xenon.css
)
foreach (page IN LISTS XENON_ABOUT_PAGE_OVERRIDES)
    list(FIND ABOUT_PAGES "${LADYBIRD_SOURCE_DIR}/Base/res/ladybird/about-pages/${page}" index)
    if (index EQUAL -1)
        message(FATAL_ERROR "Xenon overrides about-pages/${page}, which Ladybird no longer ships. Check upstream's changes.")
    endif()
    list(REMOVE_AT ABOUT_PAGES ${index})
    list(INSERT ABOUT_PAGES ${index} "${XENON_UI_DIR}/Resources/about-pages/${page}")
endforeach()
foreach (page IN LISTS XENON_ABOUT_PAGE_ADDITIONS)
    list(APPEND ABOUT_PAGES "${XENON_UI_DIR}/Resources/about-pages/${page}")
endforeach()

set(XENON_ICON_OVERRIDES
    48x48/app-browser.png
    128x128/app-browser.png
    128x128/app-browser-dark.png
)
foreach (icon IN LISTS XENON_ICON_OVERRIDES)
    get_filename_component(icon_size "${icon}" DIRECTORY)
    list(FIND ${icon_size}_ICONS "${LADYBIRD_SOURCE_DIR}/Base/res/icons/${icon}" index)
    if (index EQUAL -1)
        message(FATAL_ERROR "Xenon overrides icons/${icon}, which Ladybird no longer ships. Check upstream's changes.")
    endif()
    list(REMOVE_AT ${icon_size}_ICONS ${index})
    list(INSERT ${icon_size}_ICONS ${index} "${XENON_UI_DIR}/Resources/icons/${icon}")
endforeach()

if (APPLE)
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
        COMMAND "${CMAKE_COMMAND}" -E create_symlink "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}" "${xenon_bundle_dir}/Contents/lib"
        COMMAND "${CMAKE_COMMAND}" -E rm -rf "${xenon_bundle_dir}/Contents/Frameworks/Sparkle.framework"
        COMMAND ditto "${XENON_SPARKLE_DIR}/Sparkle.framework" "${xenon_bundle_dir}/Contents/Frameworks/Sparkle.framework"
        COMMAND codesign -s - -v -f --entitlements "${XENON_ENTITLEMENTS_FILE}" "${xenon_bundle_dir}"
        VERBATIM
    )
else()
    set(xenon_resource_dir "${XENON_PREFIX_DIR}/${CMAKE_INSTALL_DATADIR}/Lagom")
endif()

copy_resources_to_build("${xenon_resource_dir}" xenon)

file(GLOB XENON_FILTER_LISTS CONFIGURE_DEPENDS "${XENON_UI_DIR}/Resources/filter-lists/lists/*.txt")
copy_resource_set(xenon/filter-lists/lists RESOURCES ${XENON_FILTER_LISTS} DESTINATION "${xenon_resource_dir}" TARGET xenon)
file(GLOB XENON_TRUSTED_FILTER_LISTS CONFIGURE_DEPENDS "${XENON_UI_DIR}/Resources/filter-lists/trusted-lists/*.txt")
copy_resource_set(xenon/filter-lists/trusted-lists RESOURCES ${XENON_TRUSTED_FILTER_LISTS} DESTINATION "${xenon_resource_dir}" TARGET xenon)
file(GLOB XENON_FILTER_LIST_RESOURCES CONFIGURE_DEPENDS "${XENON_UI_DIR}/Resources/filter-lists/resources/*")
copy_resource_set(xenon/filter-lists/resources RESOURCES ${XENON_FILTER_LIST_RESOURCES} DESTINATION "${xenon_resource_dir}" TARGET xenon)
copy_resource_set(xenon/filter-lists
    RESOURCES
        "${XENON_UI_DIR}/Resources/filter-lists/LICENSE-GPL-3.0.txt"
        "${XENON_UI_DIR}/Resources/filter-lists/NOTICE.txt"
        "${XENON_UI_DIR}/Resources/filter-lists/manifest.json"
    DESTINATION "${xenon_resource_dir}"
    TARGET xenon
)

copy_resource_set(xenon/blocking
    RESOURCES
        "${XENON_UI_DIR}/Resources/blocking/dashboard.css"
        "${XENON_UI_DIR}/Resources/blocking/dashboard.html"
        "${XENON_UI_DIR}/Resources/blocking/dashboard.js"
    DESTINATION "${xenon_resource_dir}"
    TARGET xenon
)

copy_resource_set(xenon/certificate-error
    RESOURCES
        "${XENON_UI_DIR}/Resources/certificate-error/certificate-error.css"
        "${XENON_UI_DIR}/Resources/certificate-error/certificate-error.html"
        "${XENON_UI_DIR}/Resources/certificate-error/certificate-error.js"
    DESTINATION "${xenon_resource_dir}"
    TARGET xenon
)

qt_finalize_target(xenon)

ladybird_test("${XENON_SOURCE_DIR}/Tests/TestBangs.cpp" Xenon NAME TestXenonBangs LIBS LibURL)
target_sources(TestXenonBangs PRIVATE "${XENON_UI_DIR}/Bangs.cpp")
target_include_directories(TestXenonBangs BEFORE PRIVATE ${XENON_SOURCE_DIR})
target_include_directories(TestXenonBangs PRIVATE ${XENON_GENERATED_DIR})
add_dependencies(TestXenonBangs xenon_generate_bang_table)

ladybird_test("${XENON_SOURCE_DIR}/Tests/TestFilterLists.cpp" Xenon NAME TestXenonFilterLists LIBS LibCrypto)
target_sources(TestXenonFilterLists PRIVATE "${XENON_UI_DIR}/FilterListBundle.cpp")
target_include_directories(TestXenonFilterLists BEFORE PRIVATE ${XENON_SOURCE_DIR})
target_compile_definitions(TestXenonFilterLists PRIVATE XENON_SOURCE_DIR="${XENON_SOURCE_DIR}")

ladybird_test("${XENON_SOURCE_DIR}/Tests/TestExtensions.cpp" Xenon NAME TestXenonExtensions LIBS LibCrypto LibFileSystem LibURL)
target_sources(TestXenonExtensions PRIVATE
    "${XENON_UI_DIR}/Extensions/Extension.cpp"
    "${XENON_UI_DIR}/Extensions/Locales.cpp"
    "${XENON_UI_DIR}/Extensions/Manifest.cpp"
    "${XENON_UI_DIR}/Extensions/MatchPattern.cpp"
    "${XENON_UI_DIR}/Extensions/Storage.cpp"
)
target_include_directories(TestXenonExtensions BEFORE PRIVATE ${XENON_SOURCE_DIR})

ladybird_test("${XENON_SOURCE_DIR}/Tests/TestCertificateDetails.cpp" Xenon NAME TestXenonCertificateDetails LIBS LibCore LibURL)
target_sources(TestXenonCertificateDetails PRIVATE "${XENON_UI_DIR}/CertificateDetails.cpp")
target_include_directories(TestXenonCertificateDetails BEFORE PRIVATE ${XENON_SOURCE_DIR})
target_link_libraries(TestXenonCertificateDetails PRIVATE OpenSSL::Crypto)
target_compile_definitions(TestXenonCertificateDetails PRIVATE XENON_SOURCE_DIR="${XENON_SOURCE_DIR}")

ladybird_test("${XENON_SOURCE_DIR}/Tests/TestCRX.cpp" Xenon NAME TestXenonCRX LIBS LibCrypto LibFileSystem LibHTTP LibRequests LibURL LibWebView LibXML)
target_sources(TestXenonCRX PRIVATE
    "${XENON_UI_DIR}/Download.cpp"
    "${XENON_UI_DIR}/Extensions/CRX.cpp"
    "${XENON_UI_DIR}/Extensions/Extension.cpp"
    "${XENON_UI_DIR}/Extensions/Locales.cpp"
    "${XENON_UI_DIR}/Extensions/Manifest.cpp"
    "${XENON_UI_DIR}/Extensions/MatchPattern.cpp"
    "${XENON_UI_DIR}/Extensions/WebStore.cpp"
)
target_include_directories(TestXenonCRX BEFORE PRIVATE ${XENON_SOURCE_DIR})
target_link_libraries(TestXenonCRX PRIVATE OpenSSL::Crypto ZLIB::ZLIB)
target_compile_definitions(TestXenonCRX PRIVATE XENON_SOURCE_DIR="${XENON_SOURCE_DIR}")

ladybird_test("${XENON_SOURCE_DIR}/Tests/TestNativeMessaging.cpp" Xenon NAME TestXenonNativeMessaging LIBS LibCore LibFileSystem)
target_sources(TestXenonNativeMessaging PRIVATE "${XENON_UI_DIR}/Extensions/NativeMessaging.cpp")
target_include_directories(TestXenonNativeMessaging BEFORE PRIVATE ${XENON_SOURCE_DIR})
target_compile_definitions(TestXenonNativeMessaging PRIVATE XENON_SOURCE_DIR="${XENON_SOURCE_DIR}")

ladybird_test("${XENON_SOURCE_DIR}/Tests/TestNewTabPage.cpp" Xenon NAME TestXenonNewTabPage LIBS LibURL LibWebView)
target_sources(TestXenonNewTabPage PRIVATE "${XENON_UI_DIR}/NewTabPage.cpp")
target_include_directories(TestXenonNewTabPage BEFORE PRIVATE ${XENON_SOURCE_DIR})

ladybird_test("${XENON_SOURCE_DIR}/Tests/TestRecentTabOrder.cpp" Xenon NAME TestXenonRecentTabOrder)
target_include_directories(TestXenonRecentTabOrder BEFORE PRIVATE ${XENON_SOURCE_DIR})

ladybird_test("${XENON_SOURCE_DIR}/Tests/TestMemoryPressure.cpp" Xenon NAME TestXenonMemoryPressure)
target_sources(TestXenonMemoryPressure PRIVATE "${XENON_UI_DIR}/MemoryPressure.cpp")
target_include_directories(TestXenonMemoryPressure BEFORE PRIVATE ${XENON_SOURCE_DIR})

ladybird_test("${XENON_SOURCE_DIR}/Tests/TestAllocatorRelease.cpp" Xenon NAME TestXenonAllocatorRelease)
target_include_directories(TestXenonAllocatorRelease BEFORE PRIVATE ${XENON_SOURCE_DIR})

ladybird_test("${XENON_SOURCE_DIR}/Tests/TestUserAgent.cpp" Xenon NAME TestXenonUserAgent LIBS LibWebView)
target_include_directories(TestXenonUserAgent BEFORE PRIVATE ${XENON_SOURCE_DIR})

ladybird_test("${XENON_SOURCE_DIR}/Tests/TestContentBlockerMemory.cpp" Xenon NAME TestXenonContentBlockerMemory LIBS LibWebView LibWeb LibURL)
target_include_directories(TestXenonContentBlockerMemory BEFORE PRIVATE ${XENON_SOURCE_DIR})

ladybird_test("${XENON_SOURCE_DIR}/Tests/TestBlockingPopover.cpp" Xenon NAME TestXenonBlockingPopover LIBS LibCore LibURL)
target_sources(TestXenonBlockingPopover PRIVATE
    "${XENON_UI_DIR}/BlockingPopover.cpp"
    "${XENON_UI_DIR}/ChromeStyle.cpp"
    "${XENON_UI_DIR}/Popover.cpp"
    "${XENON_UI_DIR}/StringUtils.cpp"
)
target_include_directories(TestXenonBlockingPopover BEFORE PRIVATE ${XENON_SOURCE_DIR})
target_link_libraries(TestXenonBlockingPopover PRIVATE Qt::Core Qt::Gui Qt::Widgets)
if (LINUX AND NOT ANDROID)
    target_sources(TestXenonBlockingPopover PRIVATE "${XENON_UI_DIR}/X11Window.cpp")
    target_link_libraries(TestXenonBlockingPopover PRIVATE PkgConfig::XCB_ICCCM)
endif()

ladybird_test("${XENON_SOURCE_DIR}/Tests/TestUpdateButton.cpp" Xenon NAME TestXenonUpdateButton LIBS LibCore LibURL)
target_sources(TestXenonUpdateButton PRIVATE
    "${XENON_UI_DIR}/ChromeStyle.cpp"
    "${XENON_UI_DIR}/Popover.cpp"
    "${XENON_UI_DIR}/StringUtils.cpp"
    "${XENON_UI_DIR}/UpdateButton.cpp"
)
target_include_directories(TestXenonUpdateButton BEFORE PRIVATE ${XENON_SOURCE_DIR})
target_link_libraries(TestXenonUpdateButton PRIVATE Qt::Core Qt::Gui Qt::Widgets)
if (LINUX AND NOT ANDROID)
    target_sources(TestXenonUpdateButton PRIVATE "${XENON_UI_DIR}/X11Window.cpp")
    target_link_libraries(TestXenonUpdateButton PRIVATE PkgConfig::XCB_ICCCM)
endif()

add_custom_target(xenon-tests DEPENDS TestXenonAllocatorRelease TestXenonBangs TestXenonBlockingPopover TestXenonCertificateDetails TestXenonContentBlockerMemory TestXenonCRX TestXenonExtensions TestXenonFilterLists TestXenonMemoryPressure TestXenonNativeMessaging TestXenonNewTabPage TestXenonRecentTabOrder TestXenonUpdateButton TestXenonUserAgent)
