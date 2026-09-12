#include "drivemonitor.h"

#include <QLocale>

#include <KLocalizedString>

#include <Solid/Block>
#include <Solid/DeviceNotifier>
#include <Solid/StorageAccess>
#include <Solid/StorageDrive>
#include <Solid/StorageVolume>

using namespace Qt::StringLiterals;

namespace
{
Solid::Device parentDrive(const Solid::Device &device)
{
    Solid::Device current = device.parent();
    while (current.isValid() && !current.is<Solid::StorageDrive>()) {
        current = current.parent();
    }
    return current;
}

QString errorText(Solid::ErrorType error, const QVariant &errorData)
{
    const QString detail = errorData.toString();
    if (!detail.isEmpty()) {
        return detail;
    }
    switch (error) {
    case Solid::NoError:
        return QString();
    case Solid::UnauthorizedOperation:
        return i18n("Not authorised to mount the drive");
    case Solid::DeviceBusy:
        return i18n("The drive is busy");
    case Solid::OperationFailed:
        return i18n("The operation failed");
    case Solid::UserCanceled:
        return i18n("Cancelled");
    case Solid::InvalidOption:
        return i18n("Invalid option");
    case Solid::MissingDriver:
        return i18n("A driver is missing");
    default:
        return i18n("Unknown error");
    }
}
}

DriveMonitor::DriveMonitor(QObject *parent)
    : QObject(parent)
{
    auto *notifier = Solid::DeviceNotifier::instance();
    connect(notifier, &Solid::DeviceNotifier::deviceAdded, this, &DriveMonitor::onDeviceAdded);
    connect(notifier, &Solid::DeviceNotifier::deviceRemoved, this, &DriveMonitor::onDeviceRemoved);
    refresh();
}

QVariantList DriveMonitor::availableDrives() const
{
    return m_drives;
}

bool DriveMonitor::showAllDrives() const
{
    return m_showAllDrives;
}

void DriveMonitor::setShowAllDrives(bool value)
{
    if (m_showAllDrives == value) {
        return;
    }
    m_showAllDrives = value;
    refresh();
}

QString DriveMonitor::targetUuid() const
{
    return m_targetUuid;
}

void DriveMonitor::setTargetUuid(const QString &uuid)
{
    if (m_targetUuid == uuid) {
        return;
    }
    m_targetUuid = uuid;
    m_target = findTargetDevice();
    m_targetPresent = m_target.isValid();
    Q_EMIT targetChanged();
}

bool DriveMonitor::targetPresent() const
{
    return m_targetPresent;
}

bool DriveMonitor::targetMounted() const
{
    const auto *access = m_target.as<Solid::StorageAccess>();
    return access && access->isAccessible();
}

QString DriveMonitor::targetMountPoint() const
{
    const auto *access = m_target.as<Solid::StorageAccess>();
    return access && access->isAccessible() ? access->filePath() : QString();
}

bool DriveMonitor::busy() const
{
    return m_busy;
}

void DriveMonitor::setBusy(bool value)
{
    if (m_busy == value) {
        return;
    }
    m_busy = value;
    Q_EMIT busyChanged();
}

bool DriveMonitor::isRemovableStorage(const Solid::Device &device)
{
    const Solid::Device drive = parentDrive(device);
    const auto *storage = drive.as<Solid::StorageDrive>();
    if (!storage) {
        return false;
    }
    // External disks in a USB enclosure often report themselves as fixed
    // media, so the connection bus counts just as much as the removable flag.
    return storage->isRemovable() || storage->isHotpluggable()
        || storage->bus() == Solid::StorageDrive::Usb
        || storage->bus() == Solid::StorageDrive::Ieee1394;
}

QVariantMap DriveMonitor::describe(const Solid::Device &device)
{
    const auto *volume = device.as<Solid::StorageVolume>();
    const auto *access = device.as<Solid::StorageAccess>();
    const Solid::Device drive = parentDrive(device);

    QString name = drive.isValid() ? QString(drive.vendor() + u' ' + drive.product()).trimmed() : QString();
    if (name.isEmpty()) {
        name = device.displayName();
    }

    const QString label = volume ? volume->label() : QString();
    const qulonglong size = volume ? volume->size() : 0;
    const QString sizeText = size > 0 ? QLocale().formattedDataSize(size) : QString();

    QString display = name;
    if (!label.isEmpty()) {
        display += u" — "_s + label;
    }
    if (!sizeText.isEmpty()) {
        display += u" ("_s + sizeText + u')';
    }

    return QVariantMap{
        {u"uuid"_s, volume ? volume->uuid() : QString()},
        {u"label"_s, label},
        {u"name"_s, name},
        {u"display"_s, display},
        {u"device"_s, device.as<Solid::Block>() ? device.as<Solid::Block>()->device() : QString()},
        {u"fsType"_s, volume ? volume->fsType() : QString()},
        {u"size"_s, sizeText},
        {u"mounted"_s, access && access->isAccessible()},
        {u"mountPoint"_s, access && access->isAccessible() ? access->filePath() : QString()},
        {u"removable"_s, isRemovableStorage(device)},
    };
}

Solid::Device DriveMonitor::findTargetDevice() const
{
    if (m_targetUuid.isEmpty()) {
        return Solid::Device();
    }
    const auto devices = Solid::Device::listFromType(Solid::DeviceInterface::StorageVolume);
    for (const Solid::Device &device : devices) {
        const auto *volume = device.as<Solid::StorageVolume>();
        if (volume && volume->uuid() == m_targetUuid) {
            return device;
        }
    }
    return Solid::Device();
}

void DriveMonitor::refresh()
{
    QVariantList drives;
    const auto devices = Solid::Device::listFromType(Solid::DeviceInterface::StorageAccess);
    for (const Solid::Device &device : devices) {
        const auto *volume = device.as<Solid::StorageVolume>();
        if (!volume || volume->isIgnored() || volume->uuid().isEmpty()) {
            continue;
        }
        if (volume->usage() != Solid::StorageVolume::FileSystem) {
            continue;
        }
        if (!m_showAllDrives && !isRemovableStorage(device)) {
            continue;
        }
        drives.append(describe(device));
    }

    if (drives != m_drives) {
        m_drives = drives;
        Q_EMIT availableDrivesChanged();
    }

    const Solid::Device target = findTargetDevice();
    const bool present = target.isValid();
    const bool wasPresent = m_targetPresent;
    const QString oldMountPoint = targetMountPoint();

    m_target = target;
    m_targetPresent = present;

    if (present && !wasPresent) {
        // Watch this particular volume so mounting from elsewhere is noticed
        // too. The connection has to be made with a member function pointer:
        // Qt::UniqueConnection is not allowed for lambdas.
        if (auto *access = m_target.as<Solid::StorageAccess>()) {
            connect(access, &Solid::StorageAccess::accessibilityChanged,
                    this, &DriveMonitor::targetChanged, Qt::UniqueConnection);
        }
        Q_EMIT targetChanged();
        Q_EMIT targetAppeared();
    } else if (!present && wasPresent) {
        Q_EMIT targetChanged();
        Q_EMIT targetVanished();
    } else if (oldMountPoint != targetMountPoint()) {
        Q_EMIT targetChanged();
    }
}

void DriveMonitor::onDeviceAdded(const QString &udi)
{
    Q_UNUSED(udi)
    refresh();
}

void DriveMonitor::onDeviceRemoved(const QString &udi)
{
    Q_UNUSED(udi)
    refresh();
}

void DriveMonitor::mountTarget()
{
    m_target = findTargetDevice();
    m_targetPresent = m_target.isValid();
    if (!m_targetPresent) {
        Q_EMIT mountFinished(false, i18n("The backup drive is not connected"));
        return;
    }

    auto *access = m_target.as<Solid::StorageAccess>();
    if (!access) {
        Q_EMIT mountFinished(false, i18n("The backup drive has no mountable filesystem"));
        return;
    }
    if (access->isAccessible()) {
        Q_EMIT mountFinished(true, access->filePath());
        return;
    }

    setBusy(true);
    connect(access, &Solid::StorageAccess::setupDone, this,
            [this, access](Solid::ErrorType error, const QVariant &errorData, const QString &udi) {
                Q_UNUSED(udi)
                disconnect(access, &Solid::StorageAccess::setupDone, this, nullptr);
                setBusy(false);
                refresh();
                if (error == Solid::NoError) {
                    Q_EMIT mountFinished(true, access->filePath());
                } else {
                    Q_EMIT mountFinished(false, errorText(error, errorData));
                }
            });

    if (!access->setup()) {
        disconnect(access, &Solid::StorageAccess::setupDone, this, nullptr);
        setBusy(false);
        Q_EMIT mountFinished(false, i18n("Could not start mounting the drive"));
    }
}

void DriveMonitor::unmountTarget()
{
    m_target = findTargetDevice();
    auto *access = m_target.isValid() ? m_target.as<Solid::StorageAccess>() : nullptr;
    if (!access || !access->isAccessible()) {
        Q_EMIT unmountFinished(true, QString());
        return;
    }

    setBusy(true);
    connect(access, &Solid::StorageAccess::teardownDone, this,
            [this, access](Solid::ErrorType error, const QVariant &errorData, const QString &udi) {
                Q_UNUSED(udi)
                disconnect(access, &Solid::StorageAccess::teardownDone, this, nullptr);
                setBusy(false);
                refresh();
                Q_EMIT unmountFinished(error == Solid::NoError, errorText(error, errorData));
            });

    if (!access->teardown()) {
        disconnect(access, &Solid::StorageAccess::teardownDone, this, nullptr);
        setBusy(false);
        Q_EMIT unmountFinished(false, i18n("Could not start unmounting the drive"));
    }
}
