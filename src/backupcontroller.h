#pragma once

#include <QDateTime>
#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QTimer>
#include <QVariantList>

#include "backupconfig.h"
#include "borgrunner.h"
#include "drivemonitor.h"
#include "trayicon.h"

class QWindow;

/**
 * Ties everything together: it decides when a backup is due, reacts to the
 * backup drive being plugged in, drives borg, and keeps the tray icon in sync.
 */
class BackupController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(BackupConfig *config READ config CONSTANT)
    Q_PROPERTY(DriveMonitor *drives READ drives CONSTANT)
    Q_PROPERTY(BorgRunner *runner READ runner CONSTANT)

    Q_PROPERTY(bool due READ due NOTIFY statusChanged)
    Q_PROPERTY(QString headline READ headline NOTIFY statusChanged)
    Q_PROPERTY(QString subtitle READ subtitle NOTIFY statusChanged)
    Q_PROPERTY(QString statusIcon READ statusIcon NOTIFY statusChanged)
    Q_PROPERTY(QString lastBackupText READ lastBackupText NOTIFY statusChanged)
    Q_PROPERTY(QString nextBackupText READ nextBackupText NOTIFY statusChanged)
    Q_PROPERTY(QString repositoryPath READ repositoryPath NOTIFY statusChanged)
    Q_PROPERTY(bool repositoryExists READ repositoryExists NOTIFY statusChanged)
    Q_PROPERTY(QUrl browseStartFolder READ browseStartFolder NOTIFY statusChanged)
    Q_PROPERTY(bool canBackupNow READ canBackupNow NOTIFY statusChanged)
    Q_PROPERTY(bool borgAvailable READ borgAvailable CONSTANT)
    Q_PROPERTY(QString logText READ logText NOTIFY logChanged)
    Q_PROPERTY(QVariantList archives READ archives NOTIFY archivesChanged)

public:
    explicit BackupController(QObject *parent = nullptr);

    BackupConfig *config() const;
    DriveMonitor *drives() const;
    BorgRunner *runner() const;

    bool due() const;
    QString headline() const;
    QString subtitle() const;
    QString statusIcon() const;
    QString lastBackupText() const;
    QString nextBackupText() const;
    QString repositoryPath() const;
    /// Whether a borg repository is already present on the mounted drive.
    bool repositoryExists() const;
    /// Where the folder dialog should open.
    QUrl browseStartFolder() const;
    bool canBackupNow() const;
    bool borgAvailable() const;
    QString logText() const;
    QVariantList archives() const;

    void setWindow(QWindow *window);

    /// Starts a backup, mounting the drive first if needed.
    Q_INVOKABLE void startBackup();
    Q_INVOKABLE void cancelBackup();

    /**
     * Takes the folder the user picked for the repository and stores it as the
     * UUID of the drive it is on plus the path relative to that drive.
     *
     * Returns the resolution so the page can explain what was picked, or why
     * the folder is not usable.
     */
    Q_INVOKABLE QVariantMap selectRepositoryFolder(const QUrl &folder);

    /// Commits the setup page; an empty passphrase leaves the stored one alone.
    Q_INVOKABLE void saveConfiguration(const QString &passphrase);
    Q_INVOKABLE void forgetConfiguration();

    Q_INVOKABLE void mountDrive();
    Q_INVOKABLE void unmountDrive();
    Q_INVOKABLE void refreshArchives();
    Q_INVOKABLE void openRepositoryFolder();
    Q_INVOKABLE void clearLog();

    Q_INVOKABLE void showWindow();
    /// Opens the window on the configuration page.
    Q_INVOKABLE void requestConfigure();
    Q_INVOKABLE void hideWindow();
    Q_INVOKABLE void quitApplication();

Q_SIGNALS:
    void statusChanged();
    void logChanged();
    void archivesChanged();
    /// Shown as an inline message on the status page.
    void message(const QString &text, bool error);
    void configureRequested();

private:
    void evaluate();
    void maybeStartAutomatically();
    void beginBackup();
    void onRunFinished(bool ok, const QString &message, const QString &archiveName);
    void appendLog(const QString &line);
    void applyAutostart();
    void notify(const QString &eventId, const QString &title, const QString &text);
    TrayIcon::State trayState() const;
    QDateTime dueSince() const;

    BackupConfig *m_config;
    DriveMonitor *m_drives;
    BorgRunner *m_runner;
    TrayIcon *m_tray;
    QTimer m_tick;
    QProcess m_listProcess;
    QWindow *m_window = nullptr;
    QStringList m_log;
    QVariantList m_archives;
    QDateTime m_lastAttempt;
    bool m_startWhenMounted = false;
    bool m_unmountWhenListed = false;
    bool m_wasDue = false;
};
