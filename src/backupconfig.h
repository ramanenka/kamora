#pragma once

#include <QDateTime>
#include <QObject>
#include <QStringList>
#include <QUrl>

#include <KSharedConfig>

/**
 * Everything the user configured plus the state of the last run.
 *
 * All properties share a single change notification: the configuration is
 * small, it changes rarely, and QML only ever re-evaluates a handful of
 * bindings when it does.
 */
class BackupConfig : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool configured READ configured WRITE setConfigured NOTIFY changed)

    Q_PROPERTY(QString driveUuid READ driveUuid WRITE setDriveUuid NOTIFY changed)
    Q_PROPERTY(QString driveLabel READ driveLabel WRITE setDriveLabel NOTIFY changed)
    Q_PROPERTY(QString driveDisplay READ driveDisplay WRITE setDriveDisplay NOTIFY changed)
    Q_PROPERTY(QString driveDevice READ driveDevice WRITE setDriveDevice NOTIFY changed)

    Q_PROPERTY(QString repoPath READ repoPath WRITE setRepoPath NOTIFY changed)
    Q_PROPERTY(QString encryption READ encryption WRITE setEncryption NOTIFY changed)
    Q_PROPERTY(QString compression READ compression WRITE setCompression NOTIFY changed)

    Q_PROPERTY(QStringList includePaths READ includePaths WRITE setIncludePaths NOTIFY changed)
    Q_PROPERTY(QStringList excludePatterns READ excludePatterns WRITE setExcludePatterns NOTIFY changed)

    Q_PROPERTY(int intervalHours READ intervalHours WRITE setIntervalHours NOTIFY changed)
    Q_PROPERTY(bool backupOnConnect READ backupOnConnect WRITE setBackupOnConnect NOTIFY changed)
    Q_PROPERTY(bool unmountAfter READ unmountAfter WRITE setUnmountAfter NOTIFY changed)
    Q_PROPERTY(bool autostart READ autostart WRITE setAutostart NOTIFY changed)

    Q_PROPERTY(int keepDaily READ keepDaily WRITE setKeepDaily NOTIFY changed)
    Q_PROPERTY(int keepWeekly READ keepWeekly WRITE setKeepWeekly NOTIFY changed)
    Q_PROPERTY(int keepMonthly READ keepMonthly WRITE setKeepMonthly NOTIFY changed)

    Q_PROPERTY(QDateTime lastBackup READ lastBackup NOTIFY changed)
    Q_PROPERTY(QString lastStatus READ lastStatus NOTIFY changed)
    Q_PROPERTY(QString lastError READ lastError NOTIFY changed)
    Q_PROPERTY(QString lastArchive READ lastArchive NOTIFY changed)

public:
    explicit BackupConfig(QObject *parent = nullptr);

    bool configured() const;
    void setConfigured(bool value);

    QString driveUuid() const;
    void setDriveUuid(const QString &value);
    QString driveLabel() const;
    void setDriveLabel(const QString &value);
    QString driveDisplay() const;
    void setDriveDisplay(const QString &value);
    QString driveDevice() const;
    void setDriveDevice(const QString &value);

    QString repoPath() const;
    void setRepoPath(const QString &value);
    QString encryption() const;
    void setEncryption(const QString &value);
    QString compression() const;
    void setCompression(const QString &value);

    QStringList includePaths() const;
    void setIncludePaths(const QStringList &value);
    QStringList excludePatterns() const;
    void setExcludePatterns(const QStringList &value);

    int intervalHours() const;
    void setIntervalHours(int value);
    bool backupOnConnect() const;
    void setBackupOnConnect(bool value);
    bool unmountAfter() const;
    void setUnmountAfter(bool value);
    bool autostart() const;
    void setAutostart(bool value);

    int keepDaily() const;
    void setKeepDaily(int value);
    int keepWeekly() const;
    void setKeepWeekly(int value);
    int keepMonthly() const;
    void setKeepMonthly(int value);

    QDateTime lastBackup() const;
    QString lastStatus() const;
    QString lastError() const;
    QString lastArchive() const;

    /// Records the outcome of a run and saves it right away.
    void recordRun(const QString &status, const QString &archive, const QString &error);

    /// Key under which the repository passphrase is stored.
    Q_INVOKABLE QString repoId() const;

    /// Editing helpers used by the setup page.
    Q_INVOKABLE void beginEdit();
    Q_INVOKABLE void rollback();
    Q_INVOKABLE void commit();

    Q_INVOKABLE void addIncludePath(const QUrl &url);
    Q_INVOKABLE void removeIncludePath(int index);
    Q_INVOKABLE void addExcludePattern(const QString &pattern);
    Q_INVOKABLE void addExcludeFolder(const QUrl &url);
    Q_INVOKABLE void removeExcludePattern(int index);

    /// Drops the whole configuration, returning to the welcome screen.
    Q_INVOKABLE void forget();

    void save();

Q_SIGNALS:
    void changed();

private:
    struct Settings {
        bool configured = false;
        QString driveUuid;
        QString driveLabel;
        QString driveDisplay;
        QString driveDevice;
        QString repoPath;
        QString encryption = QStringLiteral("none");
        QString compression = QStringLiteral("zstd");
        QStringList includePaths;
        QStringList excludePatterns;
        int intervalHours = 24;
        bool backupOnConnect = true;
        bool unmountAfter = true;
        bool autostart = true;
        int keepDaily = 7;
        int keepWeekly = 4;
        int keepMonthly = 6;
        QDateTime lastBackup;
        QString lastStatus;
        QString lastError;
        QString lastArchive;
    };

    void load();
    template<typename T>
    void assign(T &target, const T &value);

    KSharedConfig::Ptr m_config;
    Settings m_settings;
    Settings m_editBackup;
};
