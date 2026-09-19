#include "backupplan.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

#include <KConfig>
#include <KConfigGroup>
#include <KFormat>
#include <KLocalizedString>
#include <KNotification>

using namespace Qt::StringLiterals;

namespace
{
// borg keeps the repository id in the repository's "config" file, so it can be
// checked without running borg.
QString readBorgRepositoryId(const QString &repository)
{
    const QString file = QDir(repository).filePath(u"config"_s);
    if (!QFileInfo::exists(file)) {
        return QString();
    }
    KConfig config(file, KConfig::SimpleConfig);
    return config.group(u"repository"_s).readEntry("id", QString());
}

/// Wait this long before retrying automatically after a failed run.
constexpr int retryCooldownSeconds = 30 * 60;
/// Never let one automatic run follow another immediately.
constexpr int minimumAttemptGapSeconds = 60;
constexpr int maxLogLines = 2000;
}

BackupPlan::BackupPlan(BackupConfig *config, QObject *parent)
    : QObject(parent)
    , m_config(config)
    , m_drives(new DriveMonitor(this))
    , m_runner(new BorgRunner(this))
{
    m_config->setParent(this);

    m_drives->setTargetUuid(m_config->driveUuid());
    m_drives->setTargetContainerUuid(m_config->driveContainerUuid());

    connect(m_config, &BackupConfig::changed, this, [this]() {
        m_drives->setTargetUuid(m_config->driveUuid());
        m_drives->setTargetContainerUuid(m_config->driveContainerUuid());
        evaluate();
    });

    connect(m_drives, &DriveMonitor::targetAppeared, this, [this]() {
        if (!m_config->configured()) {
            return;
        }
        appendLog(i18n("Backup drive connected: %1", m_config->driveDisplay()));
        evaluate();
    });
    connect(m_drives, &DriveMonitor::targetVanished, this, [this]() {
        if (m_config->configured()) {
            appendLog(i18n("Backup drive disconnected"));
        }
        evaluate();
    });
    connect(m_drives, &DriveMonitor::targetChanged, this, &BackupPlan::statusChanged);

    connect(m_drives, &DriveMonitor::mountFinished, this, [this](bool ok, const QString &text) {
        if (ok) {
            appendLog(i18n("Drive mounted at %1", text));
        } else {
            appendLog(i18n("Mounting failed: %1", text));
            Q_EMIT message(i18n("Mounting the backup drive failed: %1", text), true);
        }
        if (m_startWhenMounted) {
            m_startWhenMounted = false;
            if (ok) {
                beginBackup();
            } else {
                // The run cannot go ahead, so let it end here.
                concludeRun();
                return;
            }
        }
        evaluate();
    });
    connect(m_drives, &DriveMonitor::unmountFinished, this, [this](bool ok, const QString &text) {
        if (ok) {
            appendLog(i18n("Drive unmounted, it is safe to unplug it"));
        } else {
            appendLog(i18n("Unmounting failed: %1", text));
            Q_EMIT message(i18n("Unmounting the drive failed: %1", text), true);
        }
        if (m_finishing) {
            concludeRun();
            return;
        }
        evaluate();
    });

    connect(m_runner, &BorgRunner::logLine, this, &BackupPlan::appendLog);
    connect(m_runner, &BorgRunner::finished, this, &BackupPlan::onRunFinished);
    connect(m_runner, &BorgRunner::runningChanged, this, &BackupPlan::evaluate);
    connect(m_runner, &BorgRunner::progressChanged, this, &BackupPlan::statusChanged);

    connect(&m_listProcess, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus) {
        QVariantList archives;
        if (exitCode <= 1) {
            const QJsonDocument document = QJsonDocument::fromJson(m_listProcess.readAllStandardOutput());
            const QJsonArray entries = document.object().value(u"archives"_s).toArray();
            for (const QJsonValue &entry : entries) {
                const QJsonObject object = entry.toObject();
                archives.prepend(QVariantMap{
                    {u"name"_s, object.value(u"name"_s).toString()},
                    {u"time"_s, object.value(u"time"_s).toString()},
                });
            }
        }
        m_archives = archives;
        Q_EMIT archivesChanged();
        afterListing();
    });

    refreshArchives();
}

BackupConfig *BackupPlan::config() const
{
    return m_config;
}

DriveMonitor *BackupPlan::drives() const
{
    return m_drives;
}

BorgRunner *BackupPlan::runner() const
{
    return m_runner;
}

QDateTime BackupPlan::dueSince() const
{
    const QDateTime last = m_config->lastBackup();
    if (!last.isValid()) {
        return QDateTime::currentDateTime();
    }
    return last.addSecs(qint64(m_config->intervalHours()) * 3600);
}

bool BackupPlan::due() const
{
    if (!m_config->configured() || !m_config->enabled()) {
        return false;
    }
    return dueSince() <= QDateTime::currentDateTime();
}

BackupPlan::State BackupPlan::state() const
{
    if (active()) {
        return Running;
    }
    if (!m_config->configured() || !m_config->enabled()) {
        return Disabled;
    }
    if (m_config->lastStatus() == u"failed"_s) {
        return Failed;
    }
    return due() ? Due : Idle;
}

bool BackupPlan::active() const
{
    return m_runner->running() || m_startWhenMounted || m_finishing;
}

bool BackupPlan::borgAvailable() const
{
    return !BorgRunner::borgExecutable().isEmpty();
}

bool BackupPlan::canBackupNow() const
{
    return m_config->configured() && borgAvailable() && !active() && !m_drives->busy();
}

QString BackupPlan::repositoryPath() const
{
    const QString mountPoint = m_drives->targetMountPoint();
    if (mountPoint.isEmpty()) {
        return QString();
    }
    return QDir(mountPoint).filePath(m_config->repoPath());
}

bool BackupPlan::repositoryExists() const
{
    const QString repository = repositoryPath();
    return !repository.isEmpty() && QFileInfo::exists(QDir(repository).filePath(u"config"_s));
}

QUrl BackupPlan::browseStartFolder() const
{
    const QString mountPoint = m_drives->targetMountPoint();
    if (!mountPoint.isEmpty()) {
        return QUrl::fromLocalFile(mountPoint);
    }
    // Removable media land here on this distribution; fall back to the home
    // directory when nothing is mounted.
    for (const QString &base : {u"/run/media/"_s, u"/media/"_s}) {
        const QString candidate = base + QDir::home().dirName();
        if (QFileInfo::exists(candidate)) {
            return QUrl::fromLocalFile(candidate);
        }
    }
    return QUrl::fromLocalFile(QDir::homePath());
}

QString BackupPlan::headline() const
{
    if (m_runner->running()) {
        return i18n("Backing up…");
    }
    if (m_startWhenMounted || m_finishing) {
        return i18n("Backing up…");
    }
    if (!m_config->configured()) {
        return i18n("Not set up yet");
    }
    if (!m_config->enabled()) {
        return i18n("Switched off");
    }
    if (m_config->lastStatus() == u"failed"_s) {
        return i18n("Last backup failed");
    }
    if (due()) {
        return i18n("Backup due");
    }
    return i18n("Backup up to date");
}

QString BackupPlan::subtitle() const
{
    if (m_runner->running()) {
        return m_runner->progressText().isEmpty() ? m_runner->stepLabel() : m_runner->progressText();
    }
    if (m_startWhenMounted) {
        return i18n("Mounting the drive…");
    }
    if (m_finishing) {
        return i18n("Finishing up…");
    }
    if (!m_config->configured()) {
        return i18n("Choose a repository folder and the folders to back up");
    }
    if (!m_config->enabled()) {
        return i18n("Backups run only when you start them by hand");
    }
    if (m_config->lastStatus() == u"failed"_s && !m_config->lastError().isEmpty()) {
        return m_config->lastError();
    }
    if (due()) {
        if (!m_drives->targetPresent()) {
            return i18n("Connect %1 and the backup starts on its own",
                        m_config->driveLabel().isEmpty() ? m_config->driveDisplay() : m_config->driveLabel());
        }
        return m_config->backupOnConnect() ? i18n("The drive is connected, starting shortly")
                                           : i18n("The drive is connected, start the backup when you like");
    }
    return i18n("Next backup %1", nextBackupText());
}

QString BackupPlan::statusIcon() const
{
    switch (state()) {
    case Running:
        return u"state-sync"_s;
    case Failed:
        return u"state-error"_s;
    case Due:
        return u"state-warning"_s;
    case Disabled:
        return u"state-offline"_s;
    case Idle:
        break;
    }
    return u"state-ok"_s;
}

QString BackupPlan::lastBackupText() const
{
    const QDateTime last = m_config->lastBackup();
    if (!last.isValid()) {
        return i18n("never");
    }
    return KFormat().formatRelativeDateTime(last, QLocale::ShortFormat);
}

QString BackupPlan::nextBackupText() const
{
    if (!m_config->configured() || !m_config->enabled()) {
        return QString();
    }
    if (due()) {
        return i18n("now");
    }
    const qint64 seconds = QDateTime::currentDateTime().secsTo(dueSince());
    return i18n("in %1", KFormat().formatSpelloutDuration(seconds * 1000));
}

QString BackupPlan::logText() const
{
    return m_log.join(u'\n');
}

QVariantList BackupPlan::archives() const
{
    return m_archives;
}

void BackupPlan::appendLog(const QString &line)
{
    const QString stamped = QDateTime::currentDateTime().toString(u"HH:mm:ss"_s) + u"  "_s + line;
    m_log.append(stamped);
    while (m_log.size() > maxLogLines) {
        m_log.removeFirst();
    }
    Q_EMIT logChanged();
}

void BackupPlan::clearLog()
{
    m_log.clear();
    Q_EMIT logChanged();
}

void BackupPlan::evaluate()
{
    const bool nowDue = due();
    if (nowDue && !m_wasDue && !active()) {
        notify(u"backupDue"_s, i18n("Backup due"),
               m_drives->targetPresent() ? i18n("The backup drive is connected.")
                                         : i18n("Connect %1 to run the backup.", m_config->driveDisplay()));
    }
    m_wasDue = nowDue;

    Q_EMIT statusChanged();
    maybeStartAutomatically();
}

void BackupPlan::maybeStartAutomatically()
{
    if (!m_config->configured() || !m_config->enabled() || !m_config->backupOnConnect()) {
        return;
    }
    if (!due() || active() || !borgAvailable()) {
        return;
    }
    if (!m_drives->targetPresent()) {
        return;
    }
    if (m_lastAttempt.isValid()) {
        const qint64 sinceAttempt = m_lastAttempt.secsTo(QDateTime::currentDateTime());
        // Never chain two automatic runs back to back, and let a failed one
        // rest before trying again.
        if (sinceAttempt < minimumAttemptGapSeconds) {
            return;
        }
        if (m_config->lastStatus() == u"failed"_s && sinceAttempt < retryCooldownSeconds) {
            return;
        }
    }
    requestStart();
    // The request is answered synchronously, so this only records runs that
    // were actually allowed to start - not the ticks spent waiting for
    // another backup to finish.
    if (active()) {
        appendLog(i18n("Backup is due and the drive is available, started automatically"));
    }
}

void BackupPlan::requestStart()
{
    if (!m_config->configured()) {
        Q_EMIT message(i18n("Finish setting this backup up first"), true);
        return;
    }
    if (!borgAvailable()) {
        Q_EMIT message(i18n("borg is not installed. Install the borgbackup package to run backups."), true);
        return;
    }
    if (active()) {
        return;
    }
    if (!m_drives->targetPresent()) {
        Q_EMIT message(i18n("The backup drive is not connected"), true);
        return;
    }
    Q_EMIT startRequested();
}

void BackupPlan::start()
{
    if (active()) {
        return;
    }
    m_lastAttempt = QDateTime::currentDateTime();

    if (!m_drives->targetPresent()) {
        Q_EMIT message(i18n("The backup drive is not connected"), true);
        return;
    }

    if (!m_drives->targetMounted()) {
        appendLog(i18n("Mounting the backup drive…"));
        m_startWhenMounted = true;
        m_drives->mountTarget();
        Q_EMIT statusChanged();
        return;
    }
    beginBackup();
}

void BackupPlan::beginBackup()
{
    const QString repository = repositoryPath();
    if (repository.isEmpty()) {
        Q_EMIT message(i18n("The backup drive is not mounted"), true);
        concludeRun();
        return;
    }

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(u"BORG_UNKNOWN_UNENCRYPTED_REPO_ACCESS_IS_OK"_s, u"yes"_s);
    // The mount point changes between sessions, which borg would flag as a move.
    environment.insert(u"BORG_RELOCATED_REPO_ACCESS_IS_OK"_s, u"yes"_s);
    environment.insert(u"BORG_HOSTNAME_IS_UNIQUE"_s, u"yes"_s);

    // A repository that is not the configured one must not be written to, and
    // a missing one must not be silently replaced with an empty repository.
    const QString expectedId = m_config->borgRepoId();
    if (!expectedId.isEmpty()) {
        const QString actualId = readBorgRepositoryId(repository);
        QString why;
        if (actualId.isEmpty()) {
            why = i18n("The backup repository is no longer at %1. Nothing was backed up, so the "
                       "existing archives are not replaced by an empty repository. Check the "
                       "drive, or set the repository up again to start a new one.",
                       repository);
        } else if (actualId != expectedId) {
            why = i18n("The repository at %1 is a different one than this backup was set up "
                       "with. Nothing was backed up. Set the repository up again if this is "
                       "the one you want to use now.",
                       repository);
        }
        if (!why.isEmpty()) {
            appendLog(why);
            m_config->recordRun(u"failed"_s, QString(), why);
            Q_EMIT message(why, true);
            concludeRun();
            return;
        }
    }

    QList<BorgStep> steps;

    const bool repositoryPresent = QFileInfo::exists(QDir(repository).filePath(u"config"_s));
    if (!repositoryPresent) {
        QDir().mkpath(repository);
        steps.append(BorgStep{i18n("Creating the repository"),
                              {u"init"_s, u"--log-json"_s, u"--encryption"_s, u"none"_s, repository},
                              false});
    }

    QStringList createArgs{
        u"create"_s,
        u"--json"_s,
        u"--log-json"_s,
        u"--progress"_s,
        u"--stats"_s,
        u"--exclude-caches"_s,
        u"--compression"_s,
        m_config->compression(),
    };
    const auto excludes = m_config->excludePatterns();
    for (const QString &pattern : excludes) {
        createArgs << u"--exclude"_s << pattern;
    }
    createArgs << repository + u"::{hostname}-{now:%Y-%m-%d_%H-%M-%S}"_s;
    createArgs << m_config->includePaths();
    steps.append(BorgStep{i18n("Creating the archive"), createArgs, true});

    if (m_config->keepDaily() > 0 || m_config->keepWeekly() > 0 || m_config->keepMonthly() > 0) {
        QStringList pruneArgs{u"prune"_s, u"--log-json"_s, u"--progress"_s, u"--list"_s};
        if (m_config->keepDaily() > 0) {
            pruneArgs << u"--keep-daily"_s << QString::number(m_config->keepDaily());
        }
        if (m_config->keepWeekly() > 0) {
            pruneArgs << u"--keep-weekly"_s << QString::number(m_config->keepWeekly());
        }
        if (m_config->keepMonthly() > 0) {
            pruneArgs << u"--keep-monthly"_s << QString::number(m_config->keepMonthly());
        }
        pruneArgs << repository;
        steps.append(BorgStep{i18n("Removing old archives"), pruneArgs, false});
        steps.append(BorgStep{i18n("Compacting the repository"),
                              {u"compact"_s, u"--log-json"_s, u"--progress"_s, repository},
                              false});
    }

    appendLog(i18n("Backing up to %1", repository));
    m_runner->run(steps, environment);
    Q_EMIT statusChanged();
}

void BackupPlan::cancel()
{
    const bool wasWaitingForMount = m_startWhenMounted;
    m_startWhenMounted = false;
    if (m_runner->running()) {
        m_runner->cancel();
        return;
    }
    if (wasWaitingForMount) {
        concludeRun();
    }
}

void BackupPlan::onRunFinished(bool ok, const QString &text, const QString &archiveName)
{
    appendLog(text);
    m_config->recordRun(ok ? u"ok"_s : u"failed"_s, archiveName, ok ? QString() : text);

    if (ok && m_config->borgRepoId().isEmpty()) {
        // First run against this repository: remember what borg calls it.
        const QString id = readBorgRepositoryId(repositoryPath());
        if (!id.isEmpty()) {
            m_config->setBorgRepoId(id);
            m_config->save();
            appendLog(i18n("Repository identified as %1", id));
        }
    }

    if (ok) {
        notify(u"backupFinished"_s, i18n("Backup finished"),
               archiveName.isEmpty() ? text : i18n("Created archive %1", archiveName));
    } else {
        notify(u"backupFailed"_s, i18n("Backup failed"), text);
    }
    Q_EMIT message(text, !ok);

    // The drive stays until the archive list has been read back off it.
    m_finishing = true;
    m_unmountWhenListed = ok && m_config->unmountAfter();
    refreshArchives();
}

void BackupPlan::afterListing()
{
    if (m_unmountWhenListed) {
        m_unmountWhenListed = false;
        // concludeRun() follows from unmountFinished().
        m_drives->unmountTarget();
        return;
    }
    if (m_finishing) {
        concludeRun();
        return;
    }
    evaluate();
}

void BackupPlan::concludeRun()
{
    m_finishing = false;
    m_unmountWhenListed = false;
    evaluate();
}

QVariantMap BackupPlan::selectRepositoryFolder(const QUrl &folder)
{
    const QVariantMap resolved = m_drives->resolvePath(folder);
    if (!resolved.value(u"found"_s).toBool()) {
        Q_EMIT message(i18n("That folder is not on a mounted drive"), true);
        return resolved;
    }

    // A different folder or drive is a different repository; the id is learned
    // again on the next successful run.
    m_config->setBorgRepoId(QString());
    m_config->setDriveUuid(resolved.value(u"uuid"_s).toString());
    m_config->setDriveContainerUuid(resolved.value(u"containerUuid"_s).toString());
    m_config->setDriveLabel(resolved.value(u"label"_s).toString());
    m_config->setDriveDisplay(resolved.value(u"display"_s).toString());
    m_config->setRepoPath(resolved.value(u"relativePath"_s).toString());
    return resolved;
}

void BackupPlan::saveConfiguration()
{
    m_config->commit();
    m_drives->setTargetUuid(m_config->driveUuid());
    m_drives->setTargetContainerUuid(m_config->driveContainerUuid());
    appendLog(i18n("Plan saved"));
    Q_EMIT configurationSaved();
    evaluate();
    refreshArchives();
}

void BackupPlan::mountDrive()
{
    m_drives->mountTarget();
}

void BackupPlan::unmountDrive()
{
    m_drives->unmountTarget();
}

void BackupPlan::refreshArchives()
{
    if (m_listProcess.state() != QProcess::NotRunning) {
        // Its own finish handler settles whatever is waiting on the listing.
        return;
    }
    const QString repository = repositoryPath();
    if (!borgAvailable() || repository.isEmpty()
        || !QFileInfo::exists(QDir(repository).filePath(u"config"_s))) {
        if (!m_archives.isEmpty()) {
            m_archives.clear();
            Q_EMIT archivesChanged();
        }
        afterListing();
        return;
    }

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(u"BORG_UNKNOWN_UNENCRYPTED_REPO_ACCESS_IS_OK"_s, u"yes"_s);
    environment.insert(u"BORG_RELOCATED_REPO_ACCESS_IS_OK"_s, u"yes"_s);
    m_listProcess.setProcessEnvironment(environment);
    m_listProcess.start(BorgRunner::borgExecutable(),
                        {u"list"_s, u"--json"_s, u"--last"_s, u"20"_s, repository});
}

void BackupPlan::openRepositoryFolder()
{
    const QString repository = repositoryPath();
    if (!repository.isEmpty()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(repository));
    }
}

void BackupPlan::notify(const QString &eventId, const QString &title, const QString &text)
{
    // With several configurations the notification has to say which one it is
    // about, so the name goes in the title.
    KNotification::event(eventId, i18nc("notification title for one plan", "%1 — %2",
                                        m_config->displayName(), title),
                         text, u"backup"_s, KNotification::CloseOnTimeout);
}
