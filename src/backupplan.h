#pragma once

#include <QDateTime>
#include <QObject>
#include <QProcess>
#include <QUrl>
#include <QVariantList>

#include "backupconfig.h"
#include "borgrunner.h"
#include "drivemonitor.h"

/**
 * One backup configuration at work: it knows when its backup is due, watches
 * for its own drive, drives borg against its own repository, and keeps its own
 * log and archive list.
 *
 * A plan never starts itself. When it decides a run is warranted it asks
 * through startRequested(), and BackupController - the only thing that can see
 * them all at once - turns it down while another one is running.
 */
class BackupPlan : public QObject
{
    Q_OBJECT

    Q_PROPERTY(BackupConfig *config READ config CONSTANT)
    Q_PROPERTY(DriveMonitor *drives READ drives CONSTANT)
    Q_PROPERTY(BorgRunner *runner READ runner CONSTANT)

    Q_PROPERTY(bool due READ due NOTIFY statusChanged)
    Q_PROPERTY(State state READ state NOTIFY statusChanged)
    Q_PROPERTY(bool active READ active NOTIFY statusChanged)
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
    /// How this plan wants to be shown, and what the tray makes of all of them.
    enum State {
        Idle, ///< up to date, nothing to do
        Disabled, ///< switched off, or not configured yet
        Due, ///< a backup is due
        Running,
        Failed,
    };
    Q_ENUM(State)

    /// Takes ownership of the configuration.
    explicit BackupPlan(BackupConfig *config, QObject *parent = nullptr);

    BackupConfig *config() const;
    DriveMonitor *drives() const;
    BorgRunner *runner() const;

    bool due() const;
    State state() const;
    /// True from the moment a run is under way, mounting included, until it ends.
    bool active() const;
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

    /// Re-checks due-ness and asks to run when one is warranted.
    void evaluate();

    /// Starts the run the controller allowed; mounts the drive first.
    void start();

    /// Asks the controller for a run; it is refused while another is going.
    Q_INVOKABLE void requestStart();
    Q_INVOKABLE void cancel();

    /**
     * Takes the folder the user picked for the repository and stores it as the
     * UUID of the drive it is on plus the path relative to that drive.
     *
     * Returns the resolution so the page can explain what was picked, or why
     * the folder is not usable.
     */
    Q_INVOKABLE QVariantMap selectRepositoryFolder(const QUrl &folder);

    /// Commits the setup page.
    Q_INVOKABLE void saveConfiguration();

    Q_INVOKABLE void mountDrive();
    Q_INVOKABLE void unmountDrive();
    void refreshArchives();
    Q_INVOKABLE void openRepositoryFolder();
    Q_INVOKABLE void clearLog();


Q_SIGNALS:
    void statusChanged();
    void logChanged();
    void archivesChanged();
    /// Shown as an inline message on the status page.
    void message(const QString &text, bool error);
    /// This plan would like to run; the controller decides when it may.
    void startRequested();
    /// The configuration has been committed to disk and is worth remembering.
    void configurationSaved();

private:
    void maybeStartAutomatically();
    void beginBackup();
    void onRunFinished(bool ok, const QString &message, const QString &archiveName);
    void appendLog(const QString &line);
    void notify(const QString &eventId, const QString &title, const QString &text);
    QDateTime dueSince() const;
    /// Decides what happens once the archive list has been read back.
    void afterListing();
    /// Ends the run, releasing the state that marks one as under way.
    void concludeRun();

    BackupConfig *m_config;
    DriveMonitor *m_drives;
    BorgRunner *m_runner;
    QProcess m_listProcess;
    QStringList m_log;
    QVariantList m_archives;
    QDateTime m_lastAttempt;
    bool m_startWhenMounted = false;
    bool m_unmountWhenListed = false;
    /// A run whose borg part is done but which has not released the drive yet.
    bool m_finishing = false;
    bool m_wasDue = false;
};
