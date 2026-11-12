cmake_minimum_required(VERSION 3.21)

project(Garimpo VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(GARIMPO_BUILD_TESTS "Build the test suite" ON)
option(GARIMPO_BUILD_APP "Build the desktop app (off builds only the command-line tool)" ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core)
if(GARIMPO_BUILD_APP)
    find_package(Qt6 REQUIRED COMPONENTS Gui Widgets PrintSupport)
endif()
find_package(ZLIB REQUIRED)

# Search engine, shared by the app, the command-line tool and the tests. Qt Core only.
add_library(garimpo_core STATIC
    src/core/Documents.cpp
    src/core/Export.cpp
    src/core/Expression.cpp
    src/core/FileWalker.cpp
    src/core/Matcher.cpp
    src/core/Query.cpp
    src/core/Replace.cpp
    src/core/Results.cpp
    src/core/SearchEngine.cpp
    src/core/Storage.cpp
    src/core/TextCodec.cpp
    src/core/TextLines.cpp
)
target_include_directories(garimpo_core PUBLIC src/core)
target_link_libraries(garimpo_core PUBLIC Qt6::Core ZLIB::ZLIB)
target_compile_definitions(garimpo_core PUBLIC QT_NO_KEYWORDS QT_USE_QSTRINGBUILDER)

# Command-line tool: no GUI libraries are loaded.
add_executable(garimpo-cli src/cli/main.cpp)
target_link_libraries(garimpo-cli PRIVATE garimpo_core)
set_target_properties(garimpo-cli PROPERTIES OUTPUT_NAME garimpo)

if(GARIMPO_BUILD_APP)
    # Desktop app.
    set(APP_SOURCES
        src/app/main.cpp
        src/app/AppSettings.cpp
        src/app/FileActions.cpp
        src/app/HighlightDelegate.cpp
        src/app/MainWindow.cpp
        src/app/PreviewView.cpp
        src/app/ReplaceDialog.cpp
        src/app/ResultsModel.cpp
        src/app/SearchForm.cpp
        src/app/SearchTab.cpp
        src/app/SettingsDialog.cpp
    )

    if(APPLE)
        set(MACOSX_BUNDLE_ICON_FILE AppIcon.icns)
        set(APP_ICON ${CMAKE_SOURCE_DIR}/resources/AppIcon.icns)
        set_source_files_properties(${APP_ICON} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
        add_executable(garimpo-app MACOSX_BUNDLE ${APP_SOURCES} ${APP_ICON})
        set_target_properties(garimpo-app PROPERTIES
            OUTPUT_NAME Garimpo
            MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/resources/Info.plist.in
            MACOSX_BUNDLE_GUI_IDENTIFIER com.lucasdutra.garimpo
            MACOSX_BUNDLE_BUNDLE_NAME Garimpo
            MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
            MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        )
        # The command-line tool travels inside the bundle.
        add_custom_command(TARGET garimpo-app POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_BUNDLE_CONTENT_DIR:garimpo-app>/Helpers
            COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:garimpo-cli> $<TARGET_BUNDLE_CONTENT_DIR:garimpo-app>/Helpers/garimpo
        )
        add_dependencies(garimpo-app garimpo-cli)
    else()
        add_executable(garimpo-app ${APP_SOURCES})
        set_target_properties(garimpo-app PROPERTIES OUTPUT_NAME garimpo-gui)
    endif()
    target_include_directories(garimpo-app PRIVATE src/app)
    target_compile_definitions(garimpo-app PRIVATE GARIMPO_VERSION="${PROJECT_VERSION}")
    target_link_libraries(garimpo-app PRIVATE garimpo_core Qt6::Widgets Qt6::PrintSupport)

endif()

include(GNUInstallDirs)
install(TARGETS garimpo-cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(GARIMPO_BUILD_APP)
    if(APPLE)
        install(TARGETS garimpo-app BUNDLE DESTINATION .)
    else()
        install(TARGETS garimpo-app RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
        install(FILES resources/linux/garimpo.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
        install(FILES resources/linux/garimpo.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/512x512/apps)
    endif()
endif()

if(GARIMPO_BUILD_TESTS)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_executable(garimpo-tests tests/CoreTests.cpp)
    target_link_libraries(garimpo-tests PRIVATE garimpo_core Qt6::Test)
    add_test(NAME garimpo-tests COMMAND garimpo-tests)
endif()
