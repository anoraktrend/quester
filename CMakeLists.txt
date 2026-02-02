cmake_minimum_required(VERSION 3.16)

project("Quester" VERSION 1.0)

message("Project name: ${PROJECT_NAME}")
message("Project source dir: ${PROJECT_SOURCE_DIR}")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)

include(GNUInstallDirs)

find_package(Qt6 COMPONENTS
    Core
    Gui
    Qml
    Quick
    QmlModels
    Network
    Multimedia
    REQUIRED)

qt_standard_project_setup(REQUIRES 6.2)

qt_policy(SET QTP0001 NEW)

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

qt_add_executable(quester
    main.cpp
    quester.cpp
    quester.h
    audiovisualizer.cpp
    audiovisualizer.h
    ${quester_resources}
)


target_include_directories(quester PRIVATE
    ${PIPEWIRE_INCLUDE_DIRS}
    ${SPA_INCLUDE_DIRS}
)
# Pass source dir to C++ to allow running from the build directory (development)
target_compile_definitions(quester PRIVATE
    "QT_QML_SOURCE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\""
    "APP_DATADIR=\"${CMAKE_INSTALL_FULL_DATADIR}/Quester\"")

qt_add_qml_module(quester
    URI Quester
    VERSION 1.0
    QML_FILES
        main.qml
        HeaderBar.qml
        VisualizerView.qml
)

target_link_libraries(quester
    PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QmlModels
    Qt6::Network
    Qt6::Multimedia
    PkgConfig::LIBPULSE_SIMPLE
    PkgConfig::MPDCLIENT
    PkgConfig::FFTW3
    PkgConfig::PULSEAUDIO
    ${PIPEWIRE_LIBRARIES}
    ${SPA_LIBRARIES}
    ${HWY_LIBRARIES}
)

# --- Installation Rules ---

# Install the executable to the standard binary location (e.g., /usr/local/bin)
install(TARGETS quester
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Install the application icon.
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/Quester.svg
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps
    RENAME quester.svg
)


# Install the .desktop file to the standard applications directory
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/quester.desktop
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/visualizerGradients/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/Quester/presets
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/visualizerGradients/
    DESTINATION /etc/config/Quester/presets
)
