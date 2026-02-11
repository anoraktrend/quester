cmake_minimum_required(VERSION 3.16)

project("Quester" VERSION 1.0)

message("Project name: ${PROJECT_NAME}")
message("Project source dir: ${PROJECT_SOURCE_DIR}")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Remove Qt's -mno-direct-extern-access security flag if present
string(REPLACE "-mno-direct-extern-access" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
string(REPLACE "-mno-direct-extern-access=1" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)

set(QT_MAJOR_VERSION 6)

set(REQUIRED_QT_VERSION 6.5.2)
set(REQUIRED_KF_VERSION 5.240.0)

# macOS specific settings
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum OS X deployment version")
    set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "Build architectures for macOS")
endif()

# Set a default build type if none was specified
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "Setting build type to 'Release' as none was specified.")
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
  # Set the possible values of build type for cmake-gui
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

find_package(ECM ${REQUIRED_KF_VERSION} REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})
# set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake" ${ECM_MODULE_PATH})

if(UNIX AND NOT APPLE AND NOT ANDROID)
    include(KDEInstallDirs)
    include(KDECMakeSettings)
endif()


find_package(Qt6 COMPONENTS
    Core
    Gui
    Qml
    Quick
    QuickControls2
    QmlModels
    Network
    Multimedia
    Widgets
    Sql
    Concurrent
    REQUIRED)

# D-Bus is not available on macOS by default
if(NOT APPLE)
    find_package(Qt6 REQUIRED COMPONENTS DBus)
endif()

qt_standard_project_setup(REQUIRES 6.2)

qt_policy(SET QTP0001 NEW)
qt_policy(SET QTP0004 NEW)

find_package(Qt6 REQUIRED COMPONENTS Xml)

# Find libmpdclient using pkg-config
find_package(PkgConfig REQUIRED)
pkg_check_modules(MPDCLIENT REQUIRED IMPORTED_TARGET libmpdclient)
pkg_check_modules(FFTW3 REQUIRED IMPORTED_TARGET fftw3)

qt_add_resources(quester_resources resources/resources.qrc)

# Linux-specific audio libraries
if(NOT APPLE)
    pkg_check_modules(PULSEAUDIO REQUIRED IMPORTED_TARGET libpulse)
    pkg_check_modules(LIBPULSE_SIMPLE REQUIRED IMPORTED_TARGET libpulse-simple)
    pkg_check_modules(PIPEWIRE REQUIRED libpipewire-0.3)
    pkg_check_modules(SPA REQUIRED libspa-0.2)
endif()

find_path(LIBLASTFM_INCLUDE_DIR NAMES ws.h Track.h Audioscrobbler.h Auth.h PATHS /usr/include/lastfm6)
find_library(LIBLASTFM_LIBRARY NAMES lastfm6 lastfm PATHS /usr/lib /usr/lib/x86_64-linux-gnu)
if(NOT LIBLASTFM_INCLUDE_DIR OR NOT LIBLASTFM_LIBRARY)
    message(FATAL_ERROR "liblastfm not found")
endif()
set(LIBLASTFM_INCLUDE_DIRS ${LIBLASTFM_INCLUDE_DIR})
set(LIBLASTFM_LIBRARIES ${LIBLASTFM_LIBRARY})
pkg_check_modules(PROJECTM REQUIRED libprojectM)

qt_add_executable(quester
    src/main.cpp
    src/quester.cpp
    src/quester.h
    src/audiovisualizer.cpp
    src/audiovisualizer.h
    src/projectmvisualizer.cpp
    src/projectmvisualizer.h
    ${quester_resources}
)

# Include D-Bus related files only on non-macOS platforms
if(NOT APPLE)
    target_sources(quester PRIVATE
        src/dbus.cpp
        src/dbus.h
    )
endif()

if(APPLE)
    set_target_properties(quester PROPERTIES
        MACOSX_BUNDLE YES
        MACOSX_BUNDLE_BUNDLE_NAME "Quester"
        MACOSX_BUNDLE_GUI_IDENTIFIER "net.helltop.quester"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/resources/MacOSXBundleInfo.plist.in"
    )
endif()

target_include_directories(quester PRIVATE
    src/
    ${PIPEWIRE_INCLUDE_DIRS}
    ${SPA_INCLUDE_DIRS}
    ${PROJECTM_INCLUDE_DIRS}
    ${LIBLASTFM_INCLUDE_DIRS}
)
# Pass source dir to C++ to allow running from the build directory (development)
target_compile_definitions(quester PRIVATE
    "QT_QML_SOURCE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\""
    "APP_DATADIR=\"${CMAKE_INSTALL_FULL_DATADIR}/Quester\"")

qt_add_qml_module(quester
    URI net.helltop.quester
    VERSION 1.0
    QML_FILES
        qml/main.qml
        qml/HeaderBar.qml
        qml/VisualizerView.qml
        qml/WrappedView.qml
        qml/SettingsWindow.qml
    RESOURCE_PREFIX qml
)

target_link_libraries(quester
    PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::QmlModels
    Qt6::Network
    Qt6::Multimedia
    Qt6::Widgets
    Qt6::Sql
    Qt6::Concurrent
    Qt6::Xml
    PkgConfig::MPDCLIENT
    PkgConfig::FFTW3
    ${PROJECTM_LIBRARIES}
    ${LIBLASTFM_LIBRARIES}
)

# Platform-specific libraries
if(NOT APPLE)
    target_link_libraries(quester PRIVATE
        Qt6::DBus
        PkgConfig::LIBPULSE_SIMPLE
        PkgConfig::PULSEAUDIO
        ${PIPEWIRE_LIBRARIES}
        ${SPA_LIBRARIES}
    )
else()
    target_link_libraries(quester PRIVATE
        "-framework AudioToolbox"
    )
endif()

# --- Installation Rules ---

# Install only on Linux
if(NOT APPLE)
    # Install the executable to the standard binary location (e.g., /usr/local/bin)
    install(TARGETS quester
        RUNTIME DESTINATION ${KDE_INSTALL_BINDIR}
    )

    # Install the application icon.
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/resources/Quester.svg
        DESTINATION ${KDE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps
        RENAME quester.svg
    )


    # Install the .desktop file to the standard applications directory
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/resources/quester.desktop
        DESTINATION ${KDE_INSTALL_APPDIR}
    )

    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/presets/visualizerGradients/
        DESTINATION ${KDE_INSTALL_DATADIR}/Quester/presets
    )

    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/presets/visualizerGradients/
        DESTINATION /etc/config/Quester/presets
    )

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/presets/presets-cream-of-the-crop")
        install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/presets/presets-cream-of-the-crop/
            DESTINATION ${KDE_INSTALL_DATADIR}/Quester/projectM-presets
        )
    endif()
endif()
