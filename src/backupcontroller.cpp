#include "backupcontroller.h"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QUrl>
#include <QWindow>

#include <KFormat>
#include <KLocalizedString>
#include <KNotification>

#include "backupconfig.h"
#include "borgrunner.h"
#include "drivemonitor.h"
#include "passphrasestore.h"

using namespace Qt::StringLiterals;

namespace
{
/// Wait this long before retrying automatically after a failed run.
constexpr int retryCooldownSeconds = 30 * 60;
/// Never let one automatic run follow another immediately.
constexpr int minimumAttemptGapSeconds = 60;
constexpr int maxLogLines = 2000;

QString autostartFilePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + u"/autostart/org.kamora.Backup.desktop"_s;
}
}

BackupController::BackupController(QObject *parent)
    : QObject(parent)
    , m_config(new BackupConfig(this))
    , m_drives(new DriveMonitor(this))
    , m_runner(new BorgRunner(this))
    , m_tray(new TrayIcon(this))
{
    m_drives->setTargetUuid(m_config->driveUuid());

    connect(m_config, &BackupConfig::changed, this, [this]() {
        m_drives->setTargetUuid(m_config->driveUuid());
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
    connect(m_drives, &DriveMonitor::targetChanged, this, &BackupController::statusChanged);

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
        evaluate();
    });

    connect(m_runner, &BorgRunner::logLine, this, &BackupController::appendLog);
    connect(m_runner, &BorgRunner::finished, this, &BackupController::onRunFinished);
    connect(m_runner, &BorgRunner::runningChanged, this, &BackupController::evaluate);
    connect(m_runner, &BorgRunner::progressChanged, this, [this]() {
        m_tray->setState(trayState(), m_runner->progressText());
        Q_EMIT statusChanged();
    });

    connect(m_tray, &TrayIcon::backupRequested, this, &BackupController::startBackup);
    connect(m_tray, &TrayIcon::showWindowRequested, this, &BackupController::showWindow);
    connect(m_tray, &TrayIcon::configureRequested, this, &BackupController::requestConfigure);
    connect(m_tray, &TrayIcon::quitRequested, this, &BackupController::quitApplication);

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

        if (m_unmountWhenListed) {
            m_unmountWhenListed = false;
            m_drives->unmountTarget();
        }
    });

    // A minute is fine: due-ness only changes on the scale of hours.
    m_tick.setInterval(60 * 1000);
    connect(&m_tick, &QTimer::timeout, this, &BackupController::evaluate);
    m_tick.start();

    applyAutostart();
    evaluate();
    refreshArchives();
}

BackupConfig *BackupController::config() const
{
    return m_config;
}

DriveMonitor *BackupController::drives() const
{
    return m_drives;
}

BorgRunner *BackupController::runner() const
{
    return m_runner;
}

QDateTime BackupController::dueSince() const
{
    const QDateTime last = m_config->lastBackup();
    if (!last.isValid()) {
        return QDateTime::currentDateTime();
    }
    return last.addSecs(qint64(m_config->intervalHours()) * 3600);
}

bool BackupController::due() const
{
    if (!m_config->configured()) {
        return false;
    }
    return dueSince() <= QDateTime::currentDateTime();
}

bool BackupController::borgAvailable() const
{
    return !BorgRunner::borgExecutable().isEmpty();
}

bool BackupController::canBackupNow() const
{
    return m_config->configured() && borgAvailable() && !m_runner->running() && !m_drives->busy();
}

QString BackupController::repositoryPath() const
{
    const QString mountPoint = m_drives->targetMountPoint();
    if (mountPoint.isEmpty()) {
        return QString();
    }
    return QDir(mountPoint).filePath(m_config->repoPath());
}

bool BackupController::repositoryExists() const
{
    const QString repository = repositoryPath();
    return !repository.isEmpty() && QFileInfo::exists(QDir(repository).filePath(u"config"_s));
}

QString BackupController::headline() const
{
    if (m_runner->running()) {
        return i18n("Backing up…");
    }
    if (m_config->lastStatus() == u"failed"_s) {
        return i18n("Last backup failed");
    }
    if (due()) {
        return i18n("Backup due");
    }
    return i18n("Backup up to date");
}

QString BackupController::subtitle() const
{
    if (m_runner->running()) {
        return m_runner->progressText().isEmpty() ? m_runner->stepLabel() : m_runner->progressText();
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

QString BackupController::statusIcon() const
{
    if (m_runner->running()) {
        return u"state-sync"_s;
    }
    if (m_config->lastStatus() == u"failed"_s) {
        return u"state-error"_s;
    }
    if (due()) {
        return u"state-warning"_s;
    }
    return u"state-ok"_s;
}

QString BackupController::lastBackupText() const
{
    const QDateTime last = m_config->lastBackup();
    if (!last.isValid()) {
        return i18n("never");
    }
    return KFormat().formatRelativeDateTime(last, QLocale::ShortFormat);
}

QString BackupController::nextBackupText() const
{
    if (!m_config->configured()) {
        return QString();
    }
    if (due()) {
        return i18n("now");
    }
    const qint64 seconds = QDateTime::currentDateTime().secsTo(dueSince());
    return i18n("in %1", KFormat().formatSpelloutDuration(seconds * 1000));
}

QString BackupController::logText() const
{
    return m_log.join(u'\n');
}

QVariantList BackupController::archives() const
{
    return m_archives;
}

void BackupController::appendLog(const QString &line)
{
    const QString stamped = QDateTime::currentDateTime().toString(u"HH:mm:ss"_s) + u"  "_s + line;
    m_log.append(stamped);
    while (m_log.size() > maxLogLines) {
        m_log.removeFirst();
    }
    Q_EMIT logChanged();
}

void BackupController::clearLog()
{
    m_log.clear();
    Q_EMIT logChanged();
}

TrayIcon::State BackupController::trayState() const
{
    if (m_runner->running()) {
        return TrayIcon::Running;
    }
    if (m_config->configured() && m_config->lastStatus() == u"failed"_s) {
        return TrayIcon::Failed;
    }
    if (due()) {
        return TrayIcon::Due;
    }
    return TrayIcon::Idle;
}

void BackupController::evaluate()
{
    const TrayIcon::State state = trayState();
    QString tip;
    switch (state) {
    case TrayIcon::Running:
        tip = m_runner->progressText();
        break;
    case TrayIcon::Failed:
        tip = m_config->lastError();
        break;
    case TrayIcon::Due:
        tip = m_drives->targetPresent() ? i18n("The backup drive is connected")
                                        : i18n("Connect the backup drive to run it");
        break;
    case TrayIcon::Idle:
        tip = i18n("Last backup: %1", lastBackupText());
        break;
    }
    m_tray->setState(state, tip);
    m_tray->setBackupActionEnabled(canBackupNow());

    const bool nowDue = due();
    if (nowDue && !m_wasDue && !m_runner->running()) {
        notify(u"backupDue"_s, i18n("Backup due"),
               m_drives->targetPresent() ? i18n("The backup drive is connected.")
                                         : i18n("Connect %1 to run the backup.", m_config->driveDisplay()));
    }
    m_wasDue = nowDue;

    Q_EMIT statusChanged();
    maybeStartAutomatically();
}

void BackupController::maybeStartAutomatically()
{
    if (!m_config->configured() || !m_config->backupOnConnect()) {
        return;
    }
    if (!due() || m_runner->running() || m_startWhenMounted || !borgAvailable()) {
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
    appendLog(i18n("Backup is due and the drive is available, starting automatically"));
    startBackup();
}

void BackupController::startBackup()
{
    if (!m_config->configured()) {
        Q_EMIT message(i18n("Set up a backup configuration first"), true);
        return;
    }
    if (!borgAvailable()) {
        Q_EMIT message(i18n("borg is not installed. Install the borgbackup package to run backups."), true);
        return;
    }
    if (m_runner->running()) {
        return;
    }
    if (!m_drives->targetPresent()) {
        Q_EMIT message(i18n("The backup drive is not connected"), true);
        return;
    }

    m_lastAttempt = QDateTime::currentDateTime();

    if (!m_drives->targetMounted()) {
        appendLog(i18n("Mounting the backup drive…"));
        m_startWhenMounted = true;
        m_drives->mountTarget();
        Q_EMIT statusChanged();
        return;
    }
    beginBackup();
}

void BackupController::beginBackup()
{
    const QString repository = repositoryPath();
    if (repository.isEmpty()) {
        Q_EMIT message(i18n("The backup drive is not mounted"), true);
        return;
    }

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(u"BORG_UNKNOWN_UNENCRYPTED_REPO_ACCESS_IS_OK"_s, u"yes"_s);
    // The mount point changes between sessions, which borg would flag as a move.
    environment.insert(u"BORG_RELOCATED_REPO_ACCESS_IS_OK"_s, u"yes"_s);
    environment.insert(u"BORG_HOSTNAME_IS_UNIQUE"_s, u"yes"_s);
    if (m_config->encryption() != u"none"_s) {
        const QString passphrase = PassphraseStore::lookup(m_config->repoId());
        if (passphrase.isEmpty()) {
            Q_EMIT message(i18n("No passphrase stored for this repository"), true);
            return;
        }
        environment.insert(u"BORG_PASSPHRASE"_s, passphrase);
    }

    QList<BorgStep> steps;

    const bool repositoryExists = QFileInfo::exists(QDir(repository).filePath(u"config"_s));
    if (!repositoryExists) {
        QDir().mkpath(repository);
        steps.append(BorgStep{i18n("Creating the repository"),
                              {u"init"_s, u"--log-json"_s, u"--encryption"_s, m_config->encryption(), repository},
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

void BackupController::cancelBackup()
{
    m_startWhenMounted = false;
    m_runner->cancel();
}

void BackupController::onRunFinished(bool ok, const QString &text, const QString &archiveName)
{
    appendLog(text);
    m_config->recordRun(ok ? u"ok"_s : u"failed"_s, archiveName, ok ? QString() : text);

    if (ok) {
        notify(u"backupFinished"_s, i18n("Backup finished"),
               archiveName.isEmpty() ? text : i18n("Created archive %1", archiveName));
    } else {
        notify(u"backupFailed"_s, i18n("Backup failed"), text);
    }
    Q_EMIT message(text, !ok);

    // Read the archive list before releasing the drive.
    m_unmountWhenListed = ok && m_config->unmountAfter();
    refreshArchives();
    if (!m_unmountWhenListed) {
        evaluate();
    }
}

void BackupController::selectDrive(const QVariantMap &drive)
{
    m_config->setDriveUuid(drive.value(u"uuid"_s).toString());
    m_config->setDriveLabel(drive.value(u"label"_s).toString());
    m_config->setDriveDisplay(drive.value(u"display"_s).toString());
    m_config->setDriveDevice(drive.value(u"device"_s).toString());
}

void BackupController::saveConfiguration(const QString &passphrase)
{
    m_config->commit();
    if (m_config->encryption() != u"none"_s && !passphrase.isEmpty()) {
        if (!PassphraseStore::store(m_config->repoId(), passphrase)) {
            Q_EMIT message(i18n("The passphrase could not be saved"), true);
        }
    }
    m_drives->setTargetUuid(m_config->driveUuid());
    applyAutostart();
    appendLog(i18n("Configuration saved"));
    evaluate();
    refreshArchives();
}

void BackupController::forgetConfiguration()
{
    PassphraseStore::remove(m_config->repoId());
    m_config->forget();
    m_archives.clear();
    Q_EMIT archivesChanged();
    applyAutostart();
    evaluate();
}

void BackupController::mountDrive()
{
    m_drives->mountTarget();
}

void BackupController::unmountDrive()
{
    m_drives->unmountTarget();
}

void BackupController::refreshArchives()
{
    if (m_listProcess.state() != QProcess::NotRunning || !borgAvailable()) {
        return;
    }
    const QString repository = repositoryPath();
    if (repository.isEmpty() || !QFileInfo::exists(QDir(repository).filePath(u"config"_s))) {
        if (!m_archives.isEmpty()) {
            m_archives.clear();
            Q_EMIT archivesChanged();
        }
        if (m_unmountWhenListed) {
            m_unmountWhenListed = false;
            m_drives->unmountTarget();
        }
        return;
    }

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(u"BORG_UNKNOWN_UNENCRYPTED_REPO_ACCESS_IS_OK"_s, u"yes"_s);
    environment.insert(u"BORG_RELOCATED_REPO_ACCESS_IS_OK"_s, u"yes"_s);
    if (m_config->encryption() != u"none"_s) {
        environment.insert(u"BORG_PASSPHRASE"_s, PassphraseStore::lookup(m_config->repoId()));
    }
    m_listProcess.setProcessEnvironment(environment);
    m_listProcess.start(BorgRunner::borgExecutable(),
                        {u"list"_s, u"--json"_s, u"--last"_s, u"20"_s, repository});
}

void BackupController::openRepositoryFolder()
{
    const QString repository = repositoryPath();
    if (!repository.isEmpty()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(repository));
    }
}

void BackupController::applyAutostart()
{
    const QString path = autostartFilePath();
    if (!m_config->configured() || !m_config->autostart()) {
        QFile::remove(path);
        return;
    }

    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return;
    }
    const QString contents = u"[Desktop Entry]\n"
                             "Type=Application\n"
                             "Name=Kamora Backup\n"
                             "Comment=Scheduled borg backups to a USB drive\n"
                             "Icon=backup\n"
                             "Exec=%1 --background\n"
                             "Terminal=false\n"
                             "X-GNOME-Autostart-enabled=true\n"
                             "X-KDE-autostart-after=panel\n"_s.arg(QCoreApplication::applicationFilePath());
    file.write(contents.toUtf8());
}

void BackupController::notify(const QString &eventId, const QString &title, const QString &text)
{
    KNotification::event(eventId, title, text, u"backup"_s, KNotification::CloseOnTimeout);
}

void BackupController::setWindow(QWindow *window)
{
    m_window = window;
    m_tray->setAssociatedWindow(window);
}

void BackupController::showWindow()
{
    if (!m_window) {
        return;
    }
    m_window->show();
    m_window->raise();
    m_window->requestActivate();
}

void BackupController::requestConfigure()
{
    showWindow();
    Q_EMIT configureRequested();
}

void BackupController::hideWindow()
{
    if (m_window) {
        m_window->hide();
    }
}

void BackupController::quitApplication()
{
    if (m_runner->running()) {
        m_runner->cancel();
    }
    QCoreApplication::quit();
}
