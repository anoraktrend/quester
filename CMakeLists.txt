cmake_minimum_required(VERSION 3.16)

project("Quester" VERSION 1.0)

message("Project name: ${PROJECT_NAME}")
message("Project source dir: ${PROJECT_SOURCE_DIR}")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)

set(QT_MAJOR_VERSION 6)

set(REQUIRED_QT_VERSION 6.5.2)
set(REQUIRED_KF_VERSION 5.240.0)


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
    DBus
    Widgets
    Sql
    REQUIRED)

qt_standard_project_setup(REQUIRES 6.2)

qt_policy(SET QTP0001 NEW)
qt_policy(SET QTP0004 NEW)

# Find libmpdclient using pkg-config
find_package(PkgConfig REQUIRED)
pkg_check_modules(MPDCLIENT REQUIRED IMPORTED_TARGET libmpdclient)
pkg_check_modules(FFTW3 REQUIRED IMPORTED_TARGET fftw3)
pkg_check_modules(PULSEAUDIO REQUIRED IMPORTED_TARGET libpulse)
pkg_check_modules(LIBPULSE_SIMPLE REQUIRED IMPORTED_TARGET libpulse-simple)
qt_add_resources(quester_resources resources.qrc)
pkg_check_modules(PIPEWIRE REQUIRED libpipewire-0.3)
pkg_check_modules(SPA REQUIRED libspa-0.2)
pkg_check_modules(HWY REQUIRED libhwy)
pkg_check_modules(PROJECTM REQUIRED libprojectM)

qt_add_executable(quester
    main.cpp
    quester.cpp
    quester.h
    audiovisualizer.cpp
    audiovisualizer.h
    projectmvisualizer.cpp
    projectmvisualizer.h
    dbus.cpp
    dbus.h
    statistics.cpp
    statistics.h
    ${quester_resources}
)


target_include_directories(quester PRIVATE
    ${PIPEWIRE_INCLUDE_DIRS}
    ${SPA_INCLUDE_DIRS}
    ${PROJECTM_INCLUDE_DIRS}
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
    Qt6::DBus
    Qt6::Widgets
    Qt6::Sql
    PkgConfig::LIBPULSE_SIMPLE
    PkgConfig::MPDCLIENT
    PkgConfig::FFTW3
    PkgConfig::PULSEAUDIO
    ${PIPEWIRE_LIBRARIES}
    ${SPA_LIBRARIES}
    ${HWY_LIBRARIES}
    ${PROJECTM_LIBRARIES}
)

# --- Installation Rules ---

# Install the executable to the standard binary location (e.g., /usr/local/bin)
install(TARGETS quester
    RUNTIME DESTINATION ${KDE_INSTALL_BINDIR}
)

# Install the application icon.
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/Quester.svg
    DESTINATION ${KDE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps
    RENAME quester.svg
)


# Install the .desktop file to the standard applications directory
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/quester.desktop
    DESTINATION ${KDE_INSTALL_APPDIR}
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/visualizerGradients/
    DESTINATION ${KDE_INSTALL_DATADIR}/Quester/presets
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/visualizerGradients/
    DESTINATION /etc/config/Quester/presets
)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/presets-cream-of-the-crop")
    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/presets-cream-of-the-crop/
        DESTINATION ${KDE_INSTALL_DATADIR}/Quester/projectM-presets
    )
endif()
