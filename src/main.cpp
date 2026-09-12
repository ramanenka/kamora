#include <QApplication>
#include <QCommandLineParser>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>

#include <KAboutData>
#include <KDBusService>
#include <KIconTheme>
#include <KLocalizedString>

#include "backupconfig.h"
#include "backupcontroller.h"
#include "borgrunner.h"
#include "drivemonitor.h"

using namespace Qt::StringLiterals;

int main(int argc, char *argv[])
{
    // Has to happen before the QApplication exists, otherwise icons from the
    // theme are not found when the app is started from a session autostart.
    KIconTheme::initTheme();

    QApplication app(argc, argv);
    QApplication::setQuitOnLastWindowClosed(false);
    QCoreApplication::setOrganizationName(u"Kamora"_s);
    QCoreApplication::setOrganizationDomain(u"kamora.org"_s);
    QCoreApplication::setApplicationName(u"kamora"_s);

    KLocalizedString::setApplicationDomain(QByteArrayLiteral("kamora"));
    QQuickStyle::setStyle(u"org.kde.desktop"_s);

    KAboutData about(u"kamora"_s,
                     i18n("Kamora Backup"),
                     u"1.0"_s,
                     i18n("Scheduled borg backups to a USB drive"),
                     KAboutLicense::GPL_V3);
    about.setDesktopFileName(u"org.kamora.Backup"_s);
    // KAboutData defaults this to kde.org, which would name the unique
    // D-Bus service org.kde.kamora instead of matching the desktop entry.
    about.setOrganizationDomain(QByteArrayLiteral("kamora.org"));
    KAboutData::setApplicationData(about);

    QCommandLineParser parser;
    QCommandLineOption backgroundOption(u"background"_s,
                                        i18n("Start in the background, without opening the window"));
    QCommandLineOption backupNowOption(u"backup-now"_s, i18n("Start a backup right away"));
    QCommandLineOption configureOption(u"configure"_s, i18n("Open the configuration page"));
    parser.addOption(backgroundOption);
    parser.addOption(backupNowOption);
    parser.addOption(configureOption);
    about.setupCommandLine(&parser);
    parser.process(app);
    about.processCommandLine(&parser);

    // A second launch hands its arguments to the instance that is already there.
    KDBusService service(KDBusService::Unique);

    BackupController controller;

    qmlRegisterUncreatableType<BackupConfig>("org.kamora.backup", 1, 0, "BackupConfig",
                                             u"Reached through Kamora.config"_s);
    qmlRegisterUncreatableType<DriveMonitor>("org.kamora.backup", 1, 0, "DriveMonitor",
                                             u"Reached through Kamora.drives"_s);
    qmlRegisterUncreatableType<BorgRunner>("org.kamora.backup", 1, 0, "BorgRunner",
                                           u"Reached through Kamora.runner"_s);
    qmlRegisterSingletonInstance("org.kamora.backup", 1, 0, "Kamora", &controller);

    QQmlApplicationEngine engine;
    engine.loadFromModule("org.kamora.backup", u"Main"_s);
    if (engine.rootObjects().isEmpty()) {
        return 1;
    }

    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    controller.setWindow(window);

    // Without a configuration there is nothing for the tray to be relevant
    // about, so the window always opens on a first run.
    if (!parser.isSet(backgroundOption) || !controller.config()->configured()) {
        controller.showWindow();
    }
    if (parser.isSet(backupNowOption)) {
        controller.startBackup();
    }
    if (parser.isSet(configureOption)) {
        controller.requestConfigure();
    }

    QObject::connect(&service, &KDBusService::activateRequested, &controller,
                     [&controller](const QStringList &arguments, const QString &) {
                         if (arguments.contains(u"--backup-now"_s)) {
                             controller.startBackup();
                         } else if (arguments.contains(u"--configure"_s)) {
                             controller.requestConfigure();
                         } else {
                             controller.showWindow();
                         }
                     });

    return app.exec();
}
