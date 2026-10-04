cmake_minimum_required(VERSION 3.20)

project(kamora VERSION 0.1 LANGUAGES CXX)

option(KAMORA_DEV "Build the development flavour, installable beside a release build" OFF)

if(KAMORA_DEV)
    set(KAMORA_BINARY_NAME kamoradev)
    set(KAMORA_APP_ID io.github.ramanenka.kamoradev)
    set(KAMORA_APP_NAME "Kamora DEV")
    if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
        set(CMAKE_INSTALL_PREFIX "$ENV{HOME}/.local" CACHE PATH "Install prefix" FORCE)
        set(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT FALSE)
    endif()
else()
    set(KAMORA_BINARY_NAME kamora)
    set(KAMORA_APP_ID io.github.ramanenka.kamora)
    set(KAMORA_APP_NAME "Kamora")
endif()

set(QT_MIN_VERSION 6.6.0)
set(KF_MIN_VERSION 6.0.0)

find_package(ECM ${KF_MIN_VERSION} REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(ECMQmlModule)
include(ECMInstallIcons)
include(FeatureSummary)
include(KamoraDevIcons)

find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    Quick
    QuickControls2
    Qml
)

find_package(KF6 ${KF_MIN_VERSION} REQUIRED COMPONENTS
    CoreAddons
    Config
    I18n
    IconThemes
    Kirigami
    Notifications
    StatusNotifierItem
    DBusAddons
    Solid
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

configure_file(src/kamoraconfig.h.in ${CMAKE_CURRENT_BINARY_DIR}/kamoraconfig.h @ONLY)

add_executable(kamora)
set_target_properties(kamora PROPERTIES OUTPUT_NAME ${KAMORA_BINARY_NAME})
target_include_directories(kamora PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

target_sources(kamora PRIVATE
    src/main.cpp
    src/appsettings.cpp
    src/appsettings.h
    src/backupconfig.cpp
    src/backupconfig.h
    src/backupcontroller.cpp
    src/backupcontroller.h
    src/backupplan.cpp
    src/backupplan.h
    src/borgrunner.cpp
    src/borgrunner.h
    src/drivemonitor.cpp
    src/drivemonitor.h
    src/trayicon.cpp
    src/trayicon.h
)

ecm_add_qml_module(kamora URI io.github.ramanenka.kamora GENERATE_PLUGIN_SOURCE)

ecm_target_qml_sources(kamora SOURCES
    src/qml/Main.qml
    src/qml/OverviewPage.qml
    src/qml/SetupPage.qml
    src/qml/SettingsPage.qml
    src/qml/StatusPage.qml
    src/qml/PathListEditor.qml
    src/qml/LogSheet.qml
)

target_link_libraries(kamora PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Qml
    KF6::CoreAddons
    KF6::ConfigCore
    KF6::I18n
    KF6::IconThemes
    KF6::Notifications
    KF6::StatusNotifierItem
    KF6::DBusAddons
    KF6::Solid
)

ecm_finalize_qml_module(kamora)

configure_file(data/kamora.desktop.in ${CMAKE_CURRENT_BINARY_DIR}/${KAMORA_APP_ID}.desktop @ONLY)
configure_file(data/kamora.notifyrc.in ${CMAKE_CURRENT_BINARY_DIR}/${KAMORA_BINARY_NAME}.notifyrc @ONLY)

set(KAMORA_ICONS
    data/icons/sc-apps-io.github.ramanenka.kamora.svg
    data/icons/sc-status-io.github.ramanenka.kamora-tray.svg
    data/icons/sc-status-io.github.ramanenka.kamora-tray-attention.svg
    data/icons/sc-status-io.github.ramanenka.kamora-tray-sync.svg
    data/icons/sc-status-io.github.ramanenka.kamora-tray-error.svg
    data/icons/22-status-io.github.ramanenka.kamora-tray.svg
    data/icons/22-status-io.github.ramanenka.kamora-tray-attention.svg
    data/icons/22-status-io.github.ramanenka.kamora-tray-sync.svg
    data/icons/22-status-io.github.ramanenka.kamora-tray-error.svg
)
if(KAMORA_DEV)
    kamora_mark_dev_icons(KAMORA_ICONS ${KAMORA_ICONS})
endif()

install(TARGETS kamora ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/${KAMORA_APP_ID}.desktop DESTINATION ${KDE_INSTALL_APPDIR})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${KAMORA_BINARY_NAME}.notifyrc DESTINATION ${KDE_INSTALL_KNOTIFYRCDIR})
ecm_install_icons(ICONS ${KAMORA_ICONS} DESTINATION ${KDE_INSTALL_ICONDIR})

add_feature_info(KAMORA_DEV KAMORA_DEV "development flavour, installed as ${KAMORA_BINARY_NAME}")
feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
