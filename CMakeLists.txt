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
    set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING "Minimum OS X deployment version")
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
# Find all required KF6 Kirigami components to ensure plugin targets are available
find_package(KF6 REQUIRED COMPONENTS
    Kirigami
    KirigamiPlatform
)

# Create aliases for the plugin targets that Qt6's qml plugin detection expects.
# Qt6's qt6_import_qml_plugins() automatically discovers QML plugins from imported
# targets and tries to link them. If the target doesn't exist in the current scope,
# it issues a warning. We create these aliases to satisfy the linker.
#
# The actual plugins are defined in the KF6KirigamiTargets.cmake as internal
# targets with _qt_qml_module_* properties. We create public aliases so that
# Qt6 can find and link them.

# Helper macro to create plugin target aliases
macro(create_kirigami_plugin_alias plugin_name target_base)
    set(target_name "${target_base}${plugin_name}")
    set(alias_name "${target_base}${plugin_name}")
    
    # Create alias without :: prefix
    if(NOT TARGET ${target_name})
        if(TARGET ${target_base})
            # The base target exists, create alias to it
            add_library(${target_name} ALIAS ${target_base})
        endif()
    endif()
endmacro()

# Create aliases for all Kirigami plugin targets
# Main Kirigami plugin
if(NOT TARGET KF6Kirigamiplugin AND TARGET KF6::Kirigami)
    # In recent KF6 versions, KF6::Kirigami is an ALIAS target.
    # add_library(... ALIAS ...) cannot take an alias as its target, so we
    # resolve the underlying target first.
    get_target_property(_target KF6::Kirigami ALIASED_TARGET)
    if(_target)
        # KF6::Kirigami is an alias, use the underlying target.
        add_library(KF6Kirigamiplugin ALIAS ${_target})
    else()
        # KF6::Kirigami is not an alias (older KF6 versions), use it directly.
        add_library(KF6Kirigamiplugin ALIAS KF6::Kirigami)
    endif()
endif()

# Kirigami private plugin  
if(NOT TARGET KF6KirigamiPrivateplugin AND TARGET KF6KirigamiPrivate)
    add_library(KF6KirigamiPrivateplugin ALIAS KF6KirigamiPrivate)
endif()

# Kirigami polyfill plugin
if(NOT TARGET KF6KirigamiPolyfillplugin AND TARGET KF6KirigamiPolyfill)
    add_library(KF6KirigamiPolyfillplugin ALIAS KF6KirigamiPolyfill)
endif()

# Kirigami platform plugin (with :: prefix)
if(NOT TARGET KF6::KirigamiPlatformplugin AND TARGET KF6::KirigamiPlatform)
    add_library(KF6::KirigamiPlatformplugin ALIAS KF6::KirigamiPlatform)
endif()

# Kirigami dialogs plugin
if(NOT TARGET KF6KirigamiDialogsplugin AND TARGET KF6KirigamiDialogs)
    add_library(KF6KirigamiDialogsplugin ALIAS KF6KirigamiDialogs)
endif()

# Kirigami layouts plugin
if(NOT TARGET KF6KirigamiLayoutsplugin AND TARGET KF6KirigamiLayouts)
    add_library(KF6KirigamiLayoutsplugin ALIAS KF6KirigamiLayouts)
endif()

# Kirigami layouts private plugin
if(NOT TARGET KF6KirigamiLayoutsPrivateplugin AND TARGET KF6KirigamiLayoutsPrivate)
    add_library(KF6KirigamiLayoutsPrivateplugin ALIAS KF6KirigamiLayoutsPrivate)
endif()

# KDEInstallDirs must be included BEFORE find_package(Qt6 ...) since Qt6
# internally includes GNUInstallDirs which sets CMAKE_INSTALL_* variables.
# This avoids warnings about KDE_INSTALL_*DIR variables getting their
# values from CMAKE_INSTALL_*DIR.

set(QT_NO_PRIVATE_MODULE_WARNING ON)

# QtQuick Controls 2 Desktop Style plugin
# This is typically from Qt6::QuickPrivate or Qt6::QuickControls2Private
find_package(Qt6 QUIET COMPONENTS QuickPrivate)
if(Qt6QuickPrivate_FOUND)
    if(NOT TARGET qqc2desktopstyleplugin AND TARGET Qt6::QuickPrivate)
        add_library(qqc2desktopstyleplugin ALIAS Qt6::QuickPrivate)
    endif()
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
    OpenGL
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
qt_add_resources(quester_resources resources/resources.qrc)

# Linux-specific audio libraries
if(NOT APPLE)
    pkg_check_modules(PULSEAUDIO REQUIRED IMPORTED_TARGET libpulse)
    pkg_check_modules(LIBPULSE_SIMPLE REQUIRED IMPORTED_TARGET libpulse-simple)
    pkg_check_modules(PIPEWIRE REQUIRED IMPORTED_TARGET libpipewire-0.3)
    pkg_check_modules(SPA REQUIRED IMPORTED_TARGET libspa-0.2)
endif()

# FFT Library Detection and Selection
# Priority: Accelerate FFT (macOS) > FFTW (Linux if available) > Kiss FFT (fallback)
option(USE_SYSTEM_FFTW "Prefer system FFTW library over Kiss FFT" OFF)

if(APPLE)
    message(STATUS "Apple platform detected - using Accelerate FFT")
    set(GIST_USE_FFT "ACCELERATE")
elseif(UNIX AND NOT ANDROID)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(FFTW3 QUIET libfftw3)
    endif()

    if(NOT FFTW3_FOUND)
        find_package(FFTW3 QUIET)
    endif()

    if(NOT FFTW3_FOUND)
        if(EXISTS "/usr/include/fftw3.h")
            message(STATUS "FFTW3 found manually at /usr/include/fftw3.h")
            set(FFTW3_FOUND TRUE)
            set(FFTW3_INCLUDE_DIRS "/usr/include")
            set(FFTW3_LIBRARIES "/usr/lib/libfftw3.so")
        endif()
    endif()

    if(FFTW3_FOUND)
        message(STATUS "FFTW3 found - using FFTW")
        set(GIST_USE_FFT "FFTW")
        if(NOT TARGET FFTW3::FFTW)
            if(FFTW3_LIBRARIES)
                add_library(FFTW3::FFTW STATIC IMPORTED)
                set_target_properties(FFTW3::FFTW PROPERTIES
                    IMPORTED_LOCATION "${FFTW3_LIBRARIES}"
                    INTERFACE_INCLUDE_DIRECTORIES "${FFTW3_INCLUDE_DIRS}"
                )
            endif()
        endif()
    else()
        message(STATUS "FFTW3 not found - using Kiss FFT (fallback)")
        set(GIST_USE_FFT "KISS")
    endif()
else()
    message(STATUS "Unknown platform - using Kiss FFT (fallback)")
    add_compile_definitions(USE_KISS_FFT)
endif()

add_subdirectory(vendor/gist)

qt_add_executable(quester
    src/main.cpp
    src/quester.cpp
    src/quester.h
    src/audiovisualizer.cpp
    src/audiovisualizer.h
    src/statistics.cpp
    src/statistics.h
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
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/gist/src
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/gist/libs/kiss_fft130
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
        qml/VisualizerView.qml
        qml/WrappedView.qml
        qml/SettingsWindow.qml
        qml/GridViewMode.qml
    RESOURCE_PREFIX qml
)

target_link_libraries(quester
    PRIVATE
    KF6::Kirigami
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
    Qt6::OpenGL
    PkgConfig::MPDCLIENT
    Gist
)

# Platform-specific libraries
if(NOT APPLE)
    target_link_libraries(quester PRIVATE
        Qt6::DBus
        PkgConfig::LIBPULSE_SIMPLE
        PkgConfig::PULSEAUDIO
        PkgConfig::PIPEWIRE
        PkgConfig::SPA
    )
else()
    target_link_libraries(quester PRIVATE
        "-framework AudioToolbox"
        "-framework Accelerate"
    )
endif()

# --- Installation Rules ---

if(NOT APPLE)
    # Install only on Linux
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

else()
    # Install on macOS
    set(MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION})
    set(MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})
    
    install(TARGETS quester
        BUNDLE DESTINATION .
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
    
    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/presets/visualizerGradients/
        DESTINATION ${CMAKE_INSTALL_PREFIX}/Quester.app/Contents/Resources/presets
    )
    
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/resources/Quester512.png
        DESTINATION ${CMAKE_INSTALL_PREFIX}/Quester.app/Contents/Resources
        RENAME Quester.icns
    )
endif()
