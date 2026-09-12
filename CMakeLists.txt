cmake_minimum_required(VERSION 3.20)

project(kamora VERSION 1.0 LANGUAGES CXX)

set(QT_MIN_VERSION 6.6.0)
set(KF_MIN_VERSION 6.0.0)

find_package(ECM ${KF_MIN_VERSION} REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(ECMQmlModule)
include(FeatureSummary)

find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    Quick
    QuickControls2
    Qml
    DBus
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
    Wallet
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

add_executable(kamora)

target_sources(kamora PRIVATE
    src/main.cpp
    src/backupconfig.cpp
    src/backupconfig.h
    src/backupcontroller.cpp
    src/backupcontroller.h
    src/borgrunner.cpp
    src/borgrunner.h
    src/drivemonitor.cpp
    src/drivemonitor.h
    src/passphrasestore.cpp
    src/passphrasestore.h
    src/trayicon.cpp
    src/trayicon.h
)

ecm_add_qml_module(kamora URI org.kamora.backup GENERATE_PLUGIN_SOURCE)

ecm_target_qml_sources(kamora SOURCES
    src/qml/Main.qml
    src/qml/WelcomePage.qml
    src/qml/SetupPage.qml
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
    Qt6::DBus
    KF6::CoreAddons
    KF6::ConfigCore
    KF6::I18n
    KF6::IconThemes
    KF6::Notifications
    KF6::StatusNotifierItem
    KF6::DBusAddons
    KF6::Solid
    KF6::Wallet
)

ecm_finalize_qml_module(kamora)

install(TARGETS kamora ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
install(PROGRAMS data/org.kamora.Backup.desktop DESTINATION ${KDE_INSTALL_APPDIR})
install(FILES data/kamora.notifyrc DESTINATION ${KDE_INSTALL_KNOTIFYRCDIR})

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
