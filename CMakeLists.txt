cmake_minimum_required(VERSION 3.21)

project(Hyperlancer VERSION 0.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(HYPERLANCER_BUILD_TESTS "Build the unit tests" ON)

# Qt 6.11 is the supported target (CI and releases use 6.11.3). Network Authorization needs
# 6.9 or newer for refreshTokens() and scope tokens; WebSockets is reserved for an upcoming
# feature and linked now so deployment already carries it.
find_package(Qt6 6.11 REQUIRED COMPONENTS
    Concurrent Gui Network NetworkAuth Qml Quick QuickControls2 ShaderTools Sql WebSockets)

qt_standard_project_setup(REQUIRES 6.11)

qt_add_executable(Hyperlancer WIN32
    src/main.cpp
)

# Everything except main.cpp and the QML layer lives in this library, so the unit tests
# can link the same code the app runs.
set(HYPERLANCER_CORE_SOURCES
    src/AppPaths.h src/AppPaths.cpp
    src/DatabaseConfig.h src/DatabaseConfig.cpp
    src/Database.h src/Database.cpp
    src/DbTime.h src/DbTime.cpp
    src/Migrations.h src/Migrations.cpp
    src/LogFile.h src/LogFile.cpp
    src/SecretStore.h
    src/PublicSuffixList.h src/PublicSuffixList.cpp
    src/UrlNormalizer.h src/UrlNormalizer.cpp
    src/JsonLines.h src/JsonLines.cpp
    src/SqlExec.h
    src/SiteStore.h src/SiteStore.cpp
    src/JobStore.h src/JobStore.cpp
    src/ProfileIngest.h src/ProfileIngest.cpp
    src/ToolPaths.h src/ToolPaths.cpp
    src/LangSearchClient.h src/LangSearchClient.cpp
    src/GmailQuery.h src/GmailQuery.cpp
    src/ChromeProfile.h src/ChromeProfile.cpp
    src/SiteOrganizer.h src/SiteOrganizer.cpp
    src/PageSpeedClient.h src/PageSpeedClient.cpp
)

if(WIN32)
    list(APPEND HYPERLANCER_CORE_SOURCES src/SecretStoreWin.cpp)
else()
    list(APPEND HYPERLANCER_CORE_SOURCES src/SecretStoreGeneric.cpp)
endif()

qt_add_library(HyperlancerCore STATIC ${HYPERLANCER_CORE_SOURCES})
target_include_directories(HyperlancerCore PUBLIC src)
target_link_libraries(HyperlancerCore PUBLIC Qt6::Core Qt6::Concurrent Qt6::Network Qt6::Sql)
if(WIN32)
    # Windows Credential Manager (CredWriteW/CredReadW).
    target_link_libraries(HyperlancerCore PRIVATE advapi32)
endif()

qt_add_resources(HyperlancerCore "hyperlancer_data"
    PREFIX "/"
    BASE resources
    FILES
        resources/data/public_suffix_list.dat
        resources/migrations/sqlite/0001_init.sql
        resources/migrations/sqlite/0002_organize.sql
        resources/migrations/mysql/0001_init.sql
        resources/migrations/mysql/0002_organize.sql
)

set(HYPERLANCER_APP_SOURCES
    src/AmbientClock.h src/AmbientClock.cpp
    src/AppSettings.h src/AppSettings.cpp
    src/FrameStats.h src/FrameStats.cpp
    src/WindowChrome.h src/WindowChrome.cpp
    src/PlatformWindow.h
    src/AppContext.h src/AppContext.cpp
    src/JobRunner.h src/JobRunner.cpp
    src/JobListModel.h src/JobListModel.cpp
    src/SiteListModel.h src/SiteListModel.cpp
    src/SiteDetail.h src/SiteDetail.cpp
    src/Dashboard.h src/Dashboard.cpp
    src/Discovery.h src/Discovery.cpp
    src/Integrations.h src/Integrations.cpp
    src/GmailService.h src/GmailService.cpp
    src/HealthModel.h src/HealthModel.cpp
)

if(WIN32)
    list(APPEND HYPERLANCER_APP_SOURCES src/PlatformWindowWin.cpp)
else()
    list(APPEND HYPERLANCER_APP_SOURCES src/PlatformWindowGeneric.cpp)
endif()

set(HYPERLANCER_SINGLETONS
    src/qml/theme/Theme.qml
    src/qml/theme/Motion.qml
    src/qml/theme/Palettes.qml
    src/qml/theme/Icons.qml
    src/qml/theme/Format.qml
)

set(HYPERLANCER_QML_FILES
    src/qml/Main.qml
    ${HYPERLANCER_SINGLETONS}
    src/qml/shell/ResizeBorder.qml
    src/qml/shell/TitleBar.qml
    src/qml/shell/WindowButton.qml
    src/qml/navigation/LiquidIndicator.qml
    src/qml/navigation/Sidebar.qml
    src/qml/navigation/SidebarItem.qml
    src/qml/pages/PageBase.qml
    src/qml/pages/PageHeader.qml
    src/qml/pages/PageHost.qml
    src/qml/pages/DashboardPage.qml
    src/qml/pages/SitesPage.qml
    src/qml/pages/SiteDetailView.qml
    src/qml/pages/ProfileSections.qml
    src/qml/pages/SiteEmails.qml
    src/qml/pages/SiteWorkspace.qml
    src/qml/pages/DiscoverPage.qml
    src/qml/pages/JobsPage.qml
    src/qml/pages/SettingsPage.qml
    src/qml/controls/AnimatedNumber.qml
    src/qml/controls/Badge.qml
    src/qml/controls/EmptyState.qml
    src/qml/controls/FactRow.qml
    src/qml/controls/GlassCard.qml
    src/qml/controls/GlassPanel.qml
    src/qml/controls/Icon.qml
    src/qml/controls/IconButton.qml
    src/qml/controls/PerformanceOverlay.qml
    src/qml/controls/PrimaryButton.qml
    src/qml/controls/ProgressRing.qml
    src/qml/controls/SecondaryButton.qml
    src/qml/controls/SectionPanel.qml
    src/qml/controls/SiteBadge.qml
    src/qml/controls/SiteTableRow.qml
    src/qml/controls/SegmentedControl.qml
    src/qml/controls/SettingRow.qml
    src/qml/controls/StatTile.qml
    src/qml/controls/StatusCard.qml
    src/qml/controls/StatusDot.qml
    src/qml/controls/StepTimeline.qml
    src/qml/controls/StyledSlider.qml
    src/qml/controls/StyledTextField.qml
    src/qml/controls/TableHeader.qml
    src/qml/controls/ThinProgress.qml
    src/qml/controls/Toast.qml
    src/qml/controls/ToggleSwitch.qml
    src/qml/effects/AuroraBackground.qml
    src/qml/effects/EnergyOrb.qml
    src/qml/effects/SoftShadow.qml
    src/qml/DemoTour.qml
)

set_source_files_properties(${HYPERLANCER_SINGLETONS} PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

# The folders under src/qml only organize the sources. Aliasing every file to the module
# root keeps all types in one directory, so they see each other without extra imports.
foreach(qml_file IN LISTS HYPERLANCER_QML_FILES)
    get_filename_component(qml_name "${qml_file}" NAME)
    set_source_files_properties("${qml_file}" PROPERTIES QT_RESOURCE_ALIAS "${qml_name}")
endforeach()

qt_add_qml_module(Hyperlancer
    URI Hyperlancer
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    # Keeps the module folder from colliding with the executable of the same name.
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml/Hyperlancer
    SOURCES ${HYPERLANCER_APP_SOURCES}
    QML_FILES ${HYPERLANCER_QML_FILES}
)

qt_add_shaders(Hyperlancer "hyperlancer_shaders"
    PREFIX "/shaders"
    BASE src/shaders
    FILES
        src/shaders/aurora.frag
        src/shaders/glass.frag
        src/shaders/orb.frag
        src/shaders/ring.frag
        src/shaders/softrect.frag
)

qt_add_resources(Hyperlancer "hyperlancer_fonts"
    PREFIX "/fonts"
    BASE resources/fonts
    FILES
        resources/fonts/Inter-Regular.ttf
        resources/fonts/Inter-Medium.ttf
        resources/fonts/Inter-SemiBold.ttf
        resources/fonts/Inter-Bold.ttf
        resources/fonts/Phosphor.ttf
        resources/fonts/Phosphor-Fill.ttf
)

target_include_directories(Hyperlancer PRIVATE src)
target_compile_definitions(Hyperlancer PRIVATE PROJECT_VERSION_STRING="${PROJECT_VERSION}")

target_link_libraries(Hyperlancer
    PRIVATE
        HyperlancerCore
        Qt6::Concurrent
        Qt6::Gui
        Qt6::NetworkAuth
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::WebSockets
)

if(WIN32)
    target_link_libraries(Hyperlancer PRIVATE dwmapi)
endif()

if(HYPERLANCER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
