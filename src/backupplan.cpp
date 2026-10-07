#include "backupplan.h"
#include "progressnotification.h"

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

#include "kamoraconfig.h"

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

/// Every borg invocation Kamora makes gets the same environment.
QProcessEnvironment borgEnvironment()
{
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(u"BORG_UNKNOWN_UNENCRYPTED_REPO_ACCESS_IS_OK"_s, u"yes"_s);
    // The mount point changes between sessions, which borg would flag as a move.
    environment.insert(u"BORG_RELOCATED_REPO_ACCESS_IS_OK"_s, u"yes"_s);
    environment.insert(u"BORG_HOSTNAME_IS_UNIQUE"_s, u"yes"_s);
    // Kamora does not work with encrypted repositories, and running into one
    // has to fail rather than wait: borg asks for a passphrase on /dev/tty,
    // where nothing is there to answer it and the process would hang for good.
    // An empty passphrase, with every other source of one taken away, turns
    // that into an ordinary error borg reports and returns from.
    environment.insert(u"BORG_PASSPHRASE"_s, QString());
    environment.remove(u"BORG_PASSCOMMAND"_s);
    environment.remove(u"BORG_PASSPHRASE_FD"_s);
    environment.remove(u"BORG_KEY_FILE"_s);
    return environment;
}

/// The last error borg reported through --log-json.
struct BorgFailure {
    QString msgid;
    QString message;
};

BorgFailure readBorgFailure(const QByteArray &standardError)
{
    BorgFailure failure;
    bool haveOne = false;
    const QList<QByteArray> lines = standardError.split('\n');
    for (const QByteArray &line : lines) {
        const QJsonObject object = QJsonDocument::fromJson(line).object();
        if (object.value(u"type"_s).toString() != u"log_message"_s) {
            continue;
        }
        const QString level = object.value(u"levelname"_s).toString();
        if (level != u"ERROR"_s && level != u"CRITICAL"_s) {
            continue;
        }

        // borg names the error it recognised, and then, when it recognised
        // none, follows it with a whole traceback as a second error carrying no
        // msgid at all. The first error that names itself is the one to keep -
        // taking the last would put a page of Python in front of the user.
        const QString msgid = object.value(u"msgid"_s).toString();
        if (!haveOne || (failure.msgid.isEmpty() && !msgid.isEmpty())) {
            failure.msgid = msgid;
            failure.message = BorgRunner::condenseMessage(object.value(u"message"_s).toString());
            haveOne = true;
        }
        if (!failure.msgid.isEmpty()) {
            break;
        }
    }
    return failure;
}

/**
 * Whether borg turned a repository down because it is encrypted.
 *
 * An encrypted repository cannot be told apart from an unencrypted one by
 * looking at its "config" file: the repokey modes leave a "key" entry there,
 * but a keyfile repository keeps its key elsewhere and its config file is
 * indistinguishable. What does give it away is that borg refuses to open it
 * without a key, and says which way it is locked.
 */
bool failureMeansEncrypted(const QString &msgid)
{
    static const QStringList locked = {
        u"PassphraseWrong"_s,      u"NoPassphraseFailure"_s, u"PasscommandFailure"_s,
        u"KeyfileNotFoundError"_s, u"KeyfileInvalidError"_s, u"RepoKeyNotFoundError"_s,
    };
    return locked.contains(msgid);
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
    , m_driveMonitor(new DriveMonitor(this))
    , m_runner(new BorgRunner(this))
{
    m_config->setParent(this);

    m_driveMonitor->setTargetUuid(m_config->driveUuid());
    m_driveMonitor->setTargetContainerUuid(m_config->driveContainerUuid());

    connect(m_config, &BackupConfig::changed, this, [this]() {
        m_driveMonitor->setTargetUuid(m_config->driveUuid());
        m_driveMonitor->setTargetContainerUuid(m_config->driveContainerUuid());
        evaluate();
    });

    connect(m_driveMonitor, &DriveMonitor::targetAppeared, this, [this]() {
        if (!m_config->configured()) {
            return;
        }
        appendLog(i18n("Backup drive connected: %1", m_config->driveDisplay()));
        evaluate();
        checkRepository();
    });
    connect(m_driveMonitor, &DriveMonitor::targetVanished, this, [this]() {
        if (m_config->configured()) {
            appendLog(i18n("Backup drive disconnected"));
        }
        evaluate();
        checkRepository();
    });
    connect(m_driveMonitor, &DriveMonitor::targetChanged, this, &BackupPlan::statusChanged);

    connect(m_driveMonitor, &DriveMonitor::mountFinished, this, [this](bool ok, const QString &text) {
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
        checkRepository();
    });
    connect(m_driveMonitor, &DriveMonitor::unmountFinished, this, [this](bool ok, const QString &text) {
        if (ok) {
            QString outcome;
            if (m_driveMonitor->targetPresent()) {
                if (m_driveMonitor->targetMounted()) {
                    outcome = i18n("Drive left as it was found, it was already in use");
                } else if (!m_config->driveContainerUuid().isEmpty() && !m_driveMonitor->targetLocked()) {
                    outcome = i18n("Drive unmounted, but it is still unlocked");
                } else {
                    outcome = i18n("Drive unmounted, it is safe to unplug it");
                }
            }
            if (!outcome.isEmpty()) {
                appendLog(outcome);
            }
        } else {
            appendLog(i18n("Unmounting failed: %1", text));
            Q_EMIT message(i18n("Unmounting the drive failed: %1", text), true);
        }
        if (m_finishing) {
            concludeRun();
            return;
        }
        evaluate();
        checkRepository();
    });

    connect(m_runner, &BorgRunner::logLine, this, &BackupPlan::appendLog);
    connect(m_runner, &BorgRunner::finished, this, &BackupPlan::onRunFinished);
    connect(m_runner, &BorgRunner::runningChanged, this, &BackupPlan::evaluate);
    connect(m_runner, &BorgRunner::progressChanged, this, &BackupPlan::statusChanged);
    connect(m_runner, &BorgRunner::estimateProgress, this, [this](qint64 bytes, qint64 files) {
        if (m_progressNotification) {
            m_progressNotification->showEstimate(bytes, files);
        }
    });
    connect(m_runner,
            &BorgRunner::archiveProgress,
            this,
            [this](qint64 bytes, qint64 newBytes, qint64 files, const QString &path) {
                if (m_progressNotification) {
                    m_progressNotification->showProgress(bytes, newBytes, files, path);
                }
            });
    connect(m_runner, &BorgRunner::stepLabelChanged, this, [this](const QString &label) {
        if (m_progressNotification && !label.isEmpty()) {
            m_progressNotification->showStep(label);
        }
    });
    connect(m_runner, &BorgRunner::percentProgress, this, [this](qint64 current, qint64 total) {
        if (m_progressNotification) {
            m_progressNotification->showPercent(current, total);
        }
    });

    connect(&m_repoProcess, &QProcess::finished, this,
            [this](int exitCode, QProcess::ExitStatus status) {
                if (m_creatingRepository) {
                    m_creatingRepository = false;
                    onRepositoryCreated(exitCode, status);
                    return;
                }
                onRepositoryChecked(exitCode, status);
            });
    // finished() never comes when borg is not there to start, so the state
    // would otherwise be left saying the repository is still being looked at.
    connect(&m_repoProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) {
            return;
        }
        const bool wasCreating = m_creatingRepository;
        m_creatingRepository = false;
        m_repoProblem = i18n("borg could not be started");
        setRepositoryState(wasCreating ? RepositoryCreateFailed : RepositoryUnusable);
    });

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
    checkRepository();
}

BackupConfig *BackupPlan::config() const
{
    return m_config;
}

DriveMonitor *BackupPlan::driveMonitor() const
{
    return m_driveMonitor;
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
    return m_config->configured() && borgAvailable() && !active() && !m_driveMonitor->busy();
}

QString BackupPlan::repositoryPath() const
{
    const QString mountPoint = m_driveMonitor->targetMountPoint();
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

BackupPlan::RepositoryState BackupPlan::repositoryState() const
{
    return m_repoState;
}

QString BackupPlan::repositoryProblem() const
{
    return m_repoProblem;
}

bool BackupPlan::repositoryBusy() const
{
    return m_repoState == RepositoryChecking || m_repoState == RepositoryCreating;
}

void BackupPlan::setRepositoryState(RepositoryState state)
{
    // The details behind a state can change while the state itself does not,
    // so this always notifies.
    m_repoState = state;
    Q_EMIT repositoryChanged();
}

void BackupPlan::checkRepository()
{
    if (m_repoProcess.state() != QProcess::NotRunning) {
        return;
    }
    // borg keeps the repository locked while it works, so asking about it
    // during a run would only wait on that lock. concludeRun() comes back to it.
    if (active()) {
        return;
    }

    m_repoProblem.clear();

    if (!borgAvailable()) {
        setRepositoryState(RepositoryUnknown);
        return;
    }
    const QString repository = repositoryPath();
    if (repository.isEmpty()) {
        setRepositoryState(RepositoryDriveAway);
        return;
    }
    if (!repositoryExists()) {
        setRepositoryState(RepositoryMissing);
        return;
    }

    setRepositoryState(RepositoryChecking);
    m_repoProcess.setProcessEnvironment(borgEnvironment());
    m_repoProcess.start(BorgRunner::borgExecutable(),
                        {u"list"_s, u"--json"_s, u"--log-json"_s, u"--last"_s, u"1"_s, repository});
    // borg must not be left waiting on input that is never going to come.
    m_repoProcess.closeWriteChannel();
}

void BackupPlan::onRepositoryChecked(int exitCode, QProcess::ExitStatus status)
{
    const QByteArray output = m_repoProcess.readAllStandardOutput();
    const BorgFailure failure = readBorgFailure(m_repoProcess.readAllStandardError());

    // borg uses exit code 1 for warnings, which still leave usable output.
    if (status == QProcess::NormalExit && exitCode <= 1) {
        const QJsonObject root = QJsonDocument::fromJson(output).object();
        const QString id = root.value(u"repository"_s).toObject().value(u"id"_s).toString();

        if (id.isEmpty()) {
            m_repoProblem = i18n("borg did not report an id for the repository");
            setRepositoryState(RepositoryUnusable);
            return;
        }

        const QString known = m_config->borgRepoId();
        if (!known.isEmpty() && known != id) {
            // Adopting it here would quietly point the plan at someone else's
            // archives; choosing the folder again is what adopts a repository.
            setRepositoryState(RepositoryOther);
            return;
        }
        if (known.isEmpty()) {
            // Noted now, written out when the configuration is saved.
            m_config->setBorgRepoId(id);
            appendLog(i18n("Repository identified as %1", id));
        }
        setRepositoryState(RepositoryReady);
        return;
    }

    if (failureMeansEncrypted(failure.msgid)) {
        setRepositoryState(RepositoryEncrypted);
        return;
    }

    m_repoProblem =
        failure.message.isEmpty() ? i18n("borg exited with code %1", exitCode) : failure.message;
    setRepositoryState(RepositoryUnusable);
}

void BackupPlan::createRepository()
{
    if (m_repoProcess.state() != QProcess::NotRunning || active()) {
        return;
    }
    if (!borgAvailable()) {
        Q_EMIT message(i18n("borg is not installed, so there is nothing to create the "
                            "repository with"),
                       true);
        return;
    }
    const QString repository = repositoryPath();
    if (repository.isEmpty()) {
        Q_EMIT message(i18n("The backup drive is not mounted"), true);
        return;
    }
    if (repositoryExists()) {
        // Something turned up there since the last look; take that instead of
        // running borg init over it.
        checkRepository();
        return;
    }
    if (!QDir().mkpath(repository)) {
        m_repoProblem = i18n("The folder %1 could not be created", repository);
        setRepositoryState(RepositoryCreateFailed);
        return;
    }

    m_creatingRepository = true;
    m_repoProblem.clear();
    setRepositoryState(RepositoryCreating);
    appendLog(i18n("Creating a repository at %1", repository));
    m_repoProcess.setProcessEnvironment(borgEnvironment());
    m_repoProcess.start(BorgRunner::borgExecutable(),
                        {u"init"_s, u"--log-json"_s, u"--encryption"_s, u"none"_s, repository});
    m_repoProcess.closeWriteChannel();
}

void BackupPlan::onRepositoryCreated(int exitCode, QProcess::ExitStatus status)
{
    const BorgFailure failure = readBorgFailure(m_repoProcess.readAllStandardError());
    if (status != QProcess::NormalExit || exitCode > 1) {
        m_repoProblem =
            failure.message.isEmpty() ? i18n("borg exited with code %1", exitCode) : failure.message;
        appendLog(i18n("Creating the repository failed: %1", m_repoProblem));
        setRepositoryState(RepositoryCreateFailed);
        return;
    }
    appendLog(i18n("Repository created"));
    // The id is read back out of the repository borg has just written.
    checkRepository();
}

QUrl BackupPlan::browseStartFolder() const
{
    const QString mountPoint = m_driveMonitor->targetMountPoint();
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
        if (!m_driveMonitor->targetPresent()) {
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
               m_driveMonitor->targetPresent() ? i18n("The backup drive is connected.")
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
    if (!m_driveMonitor->targetPresent()) {
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
    if (!m_driveMonitor->targetPresent()) {
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

    if (!m_driveMonitor->targetPresent()) {
        Q_EMIT message(i18n("The backup drive is not connected"), true);
        return;
    }

    if (!m_driveMonitor->targetMounted()) {
        appendLog(i18n("Mounting the backup drive…"));
        m_startWhenMounted = true;
        m_driveMonitor->mountTarget();
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

    const QProcessEnvironment environment = borgEnvironment();

    // A repository that is not the configured one must not be written to, and
    // a missing one must not be silently replaced with an empty repository.
    // Creating one is a step of setting the plan up, never a side effect of a run.
    const QString expectedId = m_config->borgRepoId();
    {
        const QString actualId = readBorgRepositoryId(repository);
        QString why;
        if (actualId.isEmpty()) {
            why = i18n("There is no borg repository at %1. Nothing was backed up, so no empty "
                       "repository is put in its place. Check the drive, or open the plan's "
                       "settings to create a repository there.",
                       repository);
        } else if (!expectedId.isEmpty() && actualId != expectedId) {
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

    QStringList sourceArgs{u"--exclude-caches"_s};
    const auto excludes = m_config->excludePatterns();
    for (const QString &pattern : excludes) {
        sourceArgs << u"--exclude"_s << pattern;
    }
    sourceArgs << repository + u"::{hostname}-{now:%Y-%m-%d_%H-%M-%S}"_s;
    sourceArgs << m_config->includePaths();

    steps.append(BorgStep{i18n("Counting files"),
                          QStringList{u"create"_s, u"--dry-run"_s, u"--list"_s, u"--log-json"_s} + sourceArgs,
                          false,
                          true});

    QStringList createArgs{
        u"create"_s,
        u"--json"_s,
        u"--log-json"_s,
        u"--progress"_s,
        u"--stats"_s,
        u"--compression"_s,
        m_config->compression(),
    };
    createArgs << sourceArgs;
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
    m_progressNotification =
        new ProgressNotification(i18n("Backing up %1", m_config->displayName()), this);
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
    if (m_progressNotification) {
        m_progressNotification->finish(ok, m_runner->cancelled(), text, archiveName);
    }
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
        m_driveMonitor->unmountTarget();
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
    // Looking at the repository was held off while the run had it locked.
    checkRepository();
}

QVariantMap BackupPlan::selectRepositoryFolder(const QUrl &folder)
{
    const QVariantMap resolved = m_driveMonitor->resolvePath(folder);
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

    // Whether there is a usable repository in the chosen folder, and what its
    // id is, is the thing the setup page now waits on before it can be saved.
    checkRepository();
    return resolved;
}

void BackupPlan::saveConfiguration()
{
    m_config->commit();
    m_driveMonitor->setTargetUuid(m_config->driveUuid());
    m_driveMonitor->setTargetContainerUuid(m_config->driveContainerUuid());
    appendLog(i18n("Plan saved"));
    Q_EMIT configurationSaved();
    evaluate();
    refreshArchives();
}

void BackupPlan::refreshArchives()
{
    if (m_listProcess.state() != QProcess::NotRunning) {
        // Its own finish handler settles whatever is waiting on the listing.
        return;
    }
    const QString repository = repositoryPath();
    if (!borgAvailable() || !repositoryExists()) {
        if (!m_archives.isEmpty()) {
            m_archives.clear();
            Q_EMIT archivesChanged();
        }
        afterListing();
        return;
    }

    m_listProcess.setProcessEnvironment(borgEnvironment());
    m_listProcess.start(BorgRunner::borgExecutable(),
                        {u"list"_s, u"--json"_s, u"--last"_s, u"20"_s, repository});
    m_listProcess.closeWriteChannel();
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
                         text, u"backup"_s, KNotification::CloseOnTimeout,
                         QString::fromLatin1(KAMORA_BINARY_NAME));
}
