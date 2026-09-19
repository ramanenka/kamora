#pragma once

#include <QDateTime>
#include <QObject>
#include <QStringList>
#include <QUrl>

#include <KSharedConfig>

class KConfigGroup;

/**
 * One backup configuration plus the state of its last run.
 *
 * Several of these live side by side in kamorarc, each in its own [Backups][id]
 * group, so the same Kamora can keep an offsite drive and a daily drive without
 * the two knowing about each other. Settings that are not about one particular
 * repository - launching at login, for one - belong in AppSettings instead.
 *
 * All properties share a single change notification: the configuration is
 * small, it changes rarely, and QML only ever re-evaluates a handful of
 * bindings when it does.
 */
class BackupConfig : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString id READ id CONSTANT)
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY changed)
    Q_PROPERTY(QString displayName READ displayName NOTIFY changed)
    Q_PROPERTY(bool configured READ configured WRITE setConfigured NOTIFY changed)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)

    Q_PROPERTY(QString driveUuid READ driveUuid WRITE setDriveUuid NOTIFY changed)
    Q_PROPERTY(QString driveContainerUuid READ driveContainerUuid WRITE setDriveContainerUuid
                   NOTIFY changed)
    Q_PROPERTY(QString borgRepoId READ borgRepoId WRITE setBorgRepoId NOTIFY changed)
    Q_PROPERTY(QString driveLabel READ driveLabel WRITE setDriveLabel NOTIFY changed)
    Q_PROPERTY(QString driveDisplay READ driveDisplay WRITE setDriveDisplay NOTIFY changed)

    Q_PROPERTY(QString repoPath READ repoPath WRITE setRepoPath NOTIFY changed)
    Q_PROPERTY(QString compression READ compression WRITE setCompression NOTIFY changed)

    Q_PROPERTY(QStringList includePaths READ includePaths WRITE setIncludePaths NOTIFY changed)
    Q_PROPERTY(QStringList excludePatterns READ excludePatterns WRITE setExcludePatterns NOTIFY changed)

    Q_PROPERTY(int intervalHours READ intervalHours WRITE setIntervalHours NOTIFY changed)
    Q_PROPERTY(bool backupOnConnect READ backupOnConnect WRITE setBackupOnConnect NOTIFY changed)
    Q_PROPERTY(bool unmountAfter READ unmountAfter WRITE setUnmountAfter NOTIFY changed)

    Q_PROPERTY(int keepDaily READ keepDaily WRITE setKeepDaily NOTIFY changed)
    Q_PROPERTY(int keepWeekly READ keepWeekly WRITE setKeepWeekly NOTIFY changed)
    Q_PROPERTY(int keepMonthly READ keepMonthly WRITE setKeepMonthly NOTIFY changed)

    Q_PROPERTY(QDateTime lastBackup READ lastBackup NOTIFY changed)
    Q_PROPERTY(QString lastStatus READ lastStatus NOTIFY changed)
    Q_PROPERTY(QString lastError READ lastError NOTIFY changed)
    Q_PROPERTY(QString lastArchive READ lastArchive NOTIFY changed)

public:
    /// The id names this configuration's group in the config file; it never changes.
    explicit BackupConfig(KSharedConfig::Ptr config, const QString &id, QObject *parent = nullptr);

    QString id() const;

    /// What the user called this configuration; may be empty.
    QString name() const;
    void setName(const QString &value);
    /// name(), or something recognisable derived from the drive and folder.
    QString displayName() const;

    bool configured() const;
    void setConfigured(bool value);

    /// A configuration that is switched off is never backed up automatically.
    bool enabled() const;
    void setEnabled(bool value);

    QString driveUuid() const;
    void setDriveUuid(const QString &value);

    /// UUID of the LUKS header, empty when the drive is not encrypted. It is
    /// what identifies the drive while it is still locked.
    QString driveContainerUuid() const;
    void setDriveContainerUuid(const QString &value);

    /**
     * The id borg generated for the repository, from its "config" file. Empty
     * until the first run against a repository, then compared before every
     * backup so a different repository at the configured path is refused
     * rather than written to.
     */
    QString borgRepoId() const;
    void setBorgRepoId(const QString &value);
    QString driveLabel() const;
    void setDriveLabel(const QString &value);
    QString driveDisplay() const;
    void setDriveDisplay(const QString &value);

    QString repoPath() const;
    void setRepoPath(const QString &value);
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

    /// Editing helpers used by the setup page.
    Q_INVOKABLE void beginEdit();
    Q_INVOKABLE void rollback();
    Q_INVOKABLE void commit();

    Q_INVOKABLE void addIncludePath(const QUrl &url);
    Q_INVOKABLE void removeIncludePath(int index);
    Q_INVOKABLE void addExcludePattern(const QString &pattern);
    Q_INVOKABLE void addExcludeFolder(const QUrl &url);
    Q_INVOKABLE void removeExcludePattern(int index);

    /// Drops this configuration's groups from kamorarc.
    void erase();

    void save();

Q_SIGNALS:
    void changed();

private:
    struct Settings {
        QString name;
        bool configured = false;
        bool enabled = true;
        QString driveUuid;
        QString driveContainerUuid;
        QString borgRepoId;
        QString driveLabel;
        QString driveDisplay;
        QString repoPath;
        QString compression = QStringLiteral("zstd");
        QStringList includePaths;
        QStringList excludePatterns;
        int intervalHours = 24;
        bool backupOnConnect = true;
        bool unmountAfter = true;
        int keepDaily = 7;
        int keepWeekly = 4;
        int keepMonthly = 6;
        QDateTime lastBackup;
        QString lastStatus;
        QString lastError;
        QString lastArchive;
    };

    void load();
    KConfigGroup group() const;
    KConfigGroup stateGroup() const;
    template<typename T>
    void assign(T &target, const T &value);

    KSharedConfig::Ptr m_config;
    QString m_id;
    Settings m_settings;
    Settings m_editBackup;
};
