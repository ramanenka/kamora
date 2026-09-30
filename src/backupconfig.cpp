#include "backupconfig.h"

#include <QDir>

#include <KConfigGroup>
#include <KLocalizedString>

using namespace Qt::StringLiterals;

namespace
{
const QStringList defaultExcludes()
{
    return {
        u"sh:**/.cache"_s,
        u"sh:**/node_modules"_s,
        u"sh:**/.local/share/Trash"_s,
        u"sh:**/*.pyc"_s,
    };
}
}

BackupConfig::BackupConfig(KSharedConfig::Ptr config, const QString &id, QObject *parent)
    : QObject(parent)
    , m_config(std::move(config))
    , m_id(id)
{
    m_settings.excludePatterns = defaultExcludes();
    load();
}

KConfigGroup BackupConfig::group() const
{
    return m_config->group(u"Backups"_s).group(m_id);
}

KConfigGroup BackupConfig::stateGroup() const
{
    return group().group(u"State"_s);
}

template<typename T>
void BackupConfig::assign(T &target, const T &value)
{
    if (target == value) {
        return;
    }
    target = value;
    Q_EMIT changed();
}

QString BackupConfig::id() const
{
    return m_id;
}

QString BackupConfig::name() const
{
    return m_settings.name;
}

void BackupConfig::setName(const QString &value)
{
    assign(m_settings.name, value);
}

QString BackupConfig::displayName() const
{
    if (!m_settings.name.isEmpty()) {
        return m_settings.name;
    }
    const QString drive =
        m_settings.driveLabel.isEmpty() ? m_settings.driveDisplay : m_settings.driveLabel;
    if (drive.isEmpty()) {
        return i18n("New backup");
    }
    return m_settings.repoPath.isEmpty() ? drive : i18nc("repository on a drive", "%1 on %2",
                                                         m_settings.repoPath, drive);
}

bool BackupConfig::configured() const
{
    return m_settings.configured;
}

void BackupConfig::setConfigured(bool value)
{
    assign(m_settings.configured, value);
}

bool BackupConfig::enabled() const
{
    return m_settings.enabled;
}

void BackupConfig::setEnabled(bool value)
{
    assign(m_settings.enabled, value);
}

QString BackupConfig::driveUuid() const
{
    return m_settings.driveUuid;
}

void BackupConfig::setDriveUuid(const QString &value)
{
    assign(m_settings.driveUuid, value);
}

QString BackupConfig::driveContainerUuid() const
{
    return m_settings.driveContainerUuid;
}

void BackupConfig::setDriveContainerUuid(const QString &value)
{
    assign(m_settings.driveContainerUuid, value);
}

QString BackupConfig::borgRepoId() const
{
    return m_settings.borgRepoId;
}

void BackupConfig::setBorgRepoId(const QString &value)
{
    assign(m_settings.borgRepoId, value);
}

QString BackupConfig::driveLabel() const
{
    return m_settings.driveLabel;
}

void BackupConfig::setDriveLabel(const QString &value)
{
    assign(m_settings.driveLabel, value);
}

QString BackupConfig::driveDisplay() const
{
    return m_settings.driveDisplay;
}

void BackupConfig::setDriveDisplay(const QString &value)
{
    assign(m_settings.driveDisplay, value);
}

QString BackupConfig::repoPath() const
{
    return m_settings.repoPath;
}

void BackupConfig::setRepoPath(const QString &value)
{
    assign(m_settings.repoPath, value);
}

QString BackupConfig::compression() const
{
    return m_settings.compression;
}

void BackupConfig::setCompression(const QString &value)
{
    assign(m_settings.compression, value);
}

QStringList BackupConfig::includePaths() const
{
    return m_settings.includePaths;
}

void BackupConfig::setIncludePaths(const QStringList &value)
{
    assign(m_settings.includePaths, value);
}

QStringList BackupConfig::excludePatterns() const
{
    return m_settings.excludePatterns;
}

void BackupConfig::setExcludePatterns(const QStringList &value)
{
    assign(m_settings.excludePatterns, value);
}

int BackupConfig::intervalHours() const
{
    return m_settings.intervalHours;
}

void BackupConfig::setIntervalHours(int value)
{
    assign(m_settings.intervalHours, qMax(1, value));
}

bool BackupConfig::backupOnConnect() const
{
    return m_settings.backupOnConnect;
}

void BackupConfig::setBackupOnConnect(bool value)
{
    assign(m_settings.backupOnConnect, value);
}

bool BackupConfig::unmountAfter() const
{
    return m_settings.unmountAfter;
}

void BackupConfig::setUnmountAfter(bool value)
{
    assign(m_settings.unmountAfter, value);
}

int BackupConfig::keepDaily() const
{
    return m_settings.keepDaily;
}

void BackupConfig::setKeepDaily(int value)
{
    assign(m_settings.keepDaily, qMax(0, value));
}

int BackupConfig::keepWeekly() const
{
    return m_settings.keepWeekly;
}

void BackupConfig::setKeepWeekly(int value)
{
    assign(m_settings.keepWeekly, qMax(0, value));
}

int BackupConfig::keepMonthly() const
{
    return m_settings.keepMonthly;
}

void BackupConfig::setKeepMonthly(int value)
{
    assign(m_settings.keepMonthly, qMax(0, value));
}

QDateTime BackupConfig::lastBackup() const
{
    return m_settings.lastBackup;
}

QString BackupConfig::lastStatus() const
{
    return m_settings.lastStatus;
}

QString BackupConfig::lastError() const
{
    return m_settings.lastError;
}

QString BackupConfig::lastArchive() const
{
    return m_settings.lastArchive;
}

void BackupConfig::recordRun(const QString &status, const QString &archive, const QString &error)
{
    m_settings.lastStatus = status;
    m_settings.lastError = error;
    if (status != u"failed"_s) {
        m_settings.lastBackup = QDateTime::currentDateTime();
        if (!archive.isEmpty()) {
            m_settings.lastArchive = archive;
        }
    }
    save();
}

void BackupConfig::beginEdit()
{
    m_editBackup = m_settings;
}

void BackupConfig::rollback()
{
    m_settings = m_editBackup;
    Q_EMIT changed();
}

void BackupConfig::commit()
{
    m_settings.name = m_settings.name.trimmed();
    m_settings.repoPath = m_settings.repoPath.trimmed();
    while (m_settings.repoPath.startsWith(u'/')) {
        m_settings.repoPath.remove(0, 1);
    }
    m_settings.configured = !m_settings.driveUuid.isEmpty() && !m_settings.includePaths.isEmpty()
        && !m_settings.borgRepoId.isEmpty();
    save();
}

void BackupConfig::addIncludePath(const QUrl &url)
{
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    const QString clean = QDir::cleanPath(path);
    if (clean.isEmpty() || m_settings.includePaths.contains(clean)) {
        return;
    }
    m_settings.includePaths.append(clean);
    m_settings.includePaths.sort();
    Q_EMIT changed();
}

void BackupConfig::removeIncludePath(int index)
{
    if (index < 0 || index >= m_settings.includePaths.size()) {
        return;
    }
    m_settings.includePaths.removeAt(index);
    Q_EMIT changed();
}

void BackupConfig::addExcludePattern(const QString &pattern)
{
    const QString clean = pattern.trimmed();
    if (clean.isEmpty() || m_settings.excludePatterns.contains(clean)) {
        return;
    }
    m_settings.excludePatterns.append(clean);
    Q_EMIT changed();
}

void BackupConfig::addExcludeFolder(const QUrl &url)
{
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    addExcludePattern(QDir::cleanPath(path));
}

void BackupConfig::removeExcludePattern(int index)
{
    if (index < 0 || index >= m_settings.excludePatterns.size()) {
        return;
    }
    m_settings.excludePatterns.removeAt(index);
    Q_EMIT changed();
}

void BackupConfig::erase()
{
    KConfigGroup own = group();
    own.deleteGroup();
    m_config->sync();
}

void BackupConfig::load()
{
    const KConfigGroup own = group();
    m_settings.name = own.readEntry("Name", m_settings.name);
    m_settings.configured = own.readEntry("Configured", m_settings.configured);
    m_settings.enabled = own.readEntry("Enabled", m_settings.enabled);
    m_settings.driveUuid = own.readEntry("DriveUuid", m_settings.driveUuid);
    m_settings.driveContainerUuid =
        own.readEntry("DriveContainerUuid", m_settings.driveContainerUuid);
    m_settings.borgRepoId = own.readEntry("BorgRepoId", m_settings.borgRepoId);
    m_settings.driveLabel = own.readEntry("DriveLabel", m_settings.driveLabel);
    m_settings.driveDisplay = own.readEntry("DriveDisplay", m_settings.driveDisplay);
    m_settings.repoPath = own.readEntry("RepoPath", m_settings.repoPath);
    m_settings.compression = own.readEntry("Compression", m_settings.compression);
    m_settings.includePaths = own.readEntry("IncludePaths", m_settings.includePaths);
    m_settings.excludePatterns = own.readEntry("ExcludePatterns", m_settings.excludePatterns);
    m_settings.intervalHours = own.readEntry("IntervalHours", m_settings.intervalHours);
    m_settings.backupOnConnect = own.readEntry("BackupOnConnect", m_settings.backupOnConnect);
    m_settings.unmountAfter = own.readEntry("UnmountAfter", m_settings.unmountAfter);
    m_settings.keepDaily = own.readEntry("KeepDaily", m_settings.keepDaily);
    m_settings.keepWeekly = own.readEntry("KeepWeekly", m_settings.keepWeekly);
    m_settings.keepMonthly = own.readEntry("KeepMonthly", m_settings.keepMonthly);

    const KConfigGroup state = stateGroup();
    m_settings.lastBackup = state.readEntry("LastBackup", QDateTime());
    m_settings.lastStatus = state.readEntry("LastStatus", QString());
    m_settings.lastError = state.readEntry("LastError", QString());
    m_settings.lastArchive = state.readEntry("LastArchive", QString());

    Q_EMIT changed();
}

void BackupConfig::save()
{
    KConfigGroup own = group();
    own.writeEntry("Name", m_settings.name);
    own.writeEntry("Configured", m_settings.configured);
    own.writeEntry("Enabled", m_settings.enabled);
    own.writeEntry("DriveUuid", m_settings.driveUuid);
    own.writeEntry("DriveContainerUuid", m_settings.driveContainerUuid);
    own.writeEntry("BorgRepoId", m_settings.borgRepoId);
    own.writeEntry("DriveLabel", m_settings.driveLabel);
    own.writeEntry("DriveDisplay", m_settings.driveDisplay);
    own.writeEntry("RepoPath", m_settings.repoPath);
    own.writeEntry("Compression", m_settings.compression);
    own.writeEntry("IncludePaths", m_settings.includePaths);
    own.writeEntry("ExcludePatterns", m_settings.excludePatterns);
    own.writeEntry("IntervalHours", m_settings.intervalHours);
    own.writeEntry("BackupOnConnect", m_settings.backupOnConnect);
    own.writeEntry("UnmountAfter", m_settings.unmountAfter);
    own.writeEntry("KeepDaily", m_settings.keepDaily);
    own.writeEntry("KeepWeekly", m_settings.keepWeekly);
    own.writeEntry("KeepMonthly", m_settings.keepMonthly);

    KConfigGroup state = stateGroup();
    state.writeEntry("LastBackup", m_settings.lastBackup);
    state.writeEntry("LastStatus", m_settings.lastStatus);
    state.writeEntry("LastError", m_settings.lastError);
    state.writeEntry("LastArchive", m_settings.lastArchive);

    m_config->sync();
    Q_EMIT changed();
}
