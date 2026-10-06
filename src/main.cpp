#include <QApplication>
#include <QCommandLineParser>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>

#include <KAboutData>
#include <KDBusService>
#include <KIconTheme>
#include <KLocalizedString>

#include "appsettings.h"
#include "backupconfig.h"
#include "backupcontroller.h"
#include "backupplan.h"
#include "borgrunner.h"
#include "drivemonitor.h"
#include "kamoraconfig.h"
#include "kamoraversion.h"

using namespace Qt::StringLiterals;

int main(int argc, char *argv[])
{
    // Has to happen before the QApplication exists, otherwise icons from the
    // theme are not found when the app is started from a session autostart.
    KIconTheme::initTheme();

    QApplication app(argc, argv);
    QApplication::setQuitOnLastWindowClosed(false);
    QCoreApplication::setOrganizationName(u"Kamora"_s);
    QCoreApplication::setOrganizationDomain(u"ramanenka.github.io"_s);
    QCoreApplication::setApplicationName(QString::fromLatin1(KAMORA_BINARY_NAME));

    KLocalizedString::setApplicationDomain(QByteArrayLiteral("kamora"));
    QQuickStyle::setStyle(u"org.kde.desktop"_s);

    KAboutData about(QString::fromLatin1(KAMORA_BINARY_NAME),
                     i18n("Kamora Backup"),
                     QString::fromLatin1(KAMORA_VERSION),
                     i18n("Scheduled borg backups to a USB drive"),
                     KAboutLicense::GPL_V3);
    about.setDesktopFileName(QString::fromLatin1(KAMORA_APP_ID));
    // KAboutData defaults this to kde.org, which would name the unique
    // D-Bus service org.kde.kamora instead of matching the desktop entry.
    about.setOrganizationDomain(QByteArrayLiteral("ramanenka.github.io"));
    KAboutData::setApplicationData(about);

    QCommandLineParser parser;
    QCommandLineOption backgroundOption(u"background"_s,
                                        i18n("Start in the background, without opening the window"));
    parser.addOption(backgroundOption);
    about.setupCommandLine(&parser);
    parser.process(app);
    about.processCommandLine(&parser);

    // A second launch hands its arguments to the instance that is already there.
    KDBusService service(KDBusService::Unique);

    BackupController controller;

    qmlRegisterUncreatableType<AppSettings>("io.github.ramanenka.kamora", 1, 0, "AppSettings",
                                            u"Reached through Kamora.settings"_s);
    qmlRegisterUncreatableType<BackupPlan>("io.github.ramanenka.kamora", 1, 0, "BackupPlan",
                                           u"Reached through Kamora.plans"_s);
    qmlRegisterUncreatableType<BackupConfig>("io.github.ramanenka.kamora", 1, 0, "BackupConfig",
                                             u"Reached through BackupPlan.config"_s);
    qmlRegisterUncreatableType<DriveMonitor>("io.github.ramanenka.kamora", 1, 0, "DriveMonitor",
                                             u"Reached through BackupPlan.driveMonitor"_s);
    qmlRegisterUncreatableType<BorgRunner>("io.github.ramanenka.kamora", 1, 0, "BorgRunner",
                                           u"Reached through BackupPlan.runner"_s);
    qmlRegisterSingletonInstance("io.github.ramanenka.kamora", 1, 0, "Kamora", &controller);

    QQmlApplicationEngine engine;
    engine.loadFromModule("io.github.ramanenka.kamora", u"Main"_s);
    if (engine.rootObjects().isEmpty()) {
        return 1;
    }

    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    controller.setWindow(window);

    // Without a plan there is nothing for the tray to be relevant
    // about, so the window always opens on a first run.
    if (!parser.isSet(backgroundOption) || !controller.configured()) {
        controller.showWindow();
    }

    QObject::connect(&service, &KDBusService::activateRequested, &controller,
                     [&controller](const QStringList &, const QString &) {
                         controller.showWindow();
                     });

    return app.exec();
}
