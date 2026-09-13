#include "drivemonitor.h"

#include <QDir>
#include <QFileInfo>
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
// The single block device that sysfs links to this one under "relation", or an
// invalid device when there is not exactly one: several means the stack
// branches, and then no single device answers for it.
Solid::Device linkedBlockDevice(const Solid::Device &device, const QString &relation)
{
    const auto *block = device.as<Solid::Block>();
    if (!block) {
        return Solid::Device();
    }
    const QString node = QFileInfo(block->device()).fileName();
    const QDir links(u"/sys/class/block/"_s + node + u'/' + relation);
    const QStringList names = links.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    if (names.size() != 1) {
        return Solid::Device();
    }
    const QString linkedNode = u"/dev/"_s + names.constFirst();
    const auto devices = Solid::Device::listFromType(Solid::DeviceInterface::Block);
    for (const Solid::Device &candidate : devices) {
        const auto *candidateBlock = candidate.as<Solid::Block>();
        if (candidateBlock && candidateBlock->device() == linkedNode) {
            return candidate;
        }
    }
    return Solid::Device();
}

// Solid gives an unlocked LUKS volume no parent, so the walk to the drive stops
// at the device-mapper node. The kernel still records what the volume sits on,
// so step across the gap through sysfs and carry on upwards.
Solid::Device backingDevice(const Solid::Device &device)
{
    return linkedBlockDevice(device, u"slaves"_s);
}

Solid::Device parentDrive(const Solid::Device &device)
{
    Solid::Device current = device;
    while (current.isValid()) {
        Solid::Device next = current.parent();
        if (!next.isValid()) {
            next = backingDevice(current);
        }
        if (!next.isValid()) {
            return Solid::Device();
        }
        if (next.is<Solid::StorageDrive>()) {
            return next;
        }
        current = next;
    }
    return Solid::Device();
}

// The cleartext device that a LUKS container is currently unlocked into. While
// it is locked nothing is stacked on the container, so "holders" is empty and
// the device comes back invalid. The inverse of encryptedContainer(), and
// single-level for the same reason.
Solid::Device cleartextDevice(const Solid::Device &container)
{
    return linkedBlockDevice(container, u"holders"_s);
}

// The LUKS container holding a volume: the device itself when it is the
// container, the device underneath when it is the filesystem inside one.
//
// One level down, deliberately, because cleartextDevice() resolves one level
// the other way and the two have to agree: a container found deeper than that
// could not be resolved back to the filesystem inside it. So a stack with more
// between the two - LVM on LUKS, say - reports no container here. Such a drive
// is identified by its filesystem UUID alone, which means it is recognised only
// while unlocked, and unmounting it does not lock it again.
Solid::Device encryptedContainer(const Solid::Device &device)
{
    const auto *volume = device.as<Solid::StorageVolume>();
    if (volume && volume->usage() == Solid::StorageVolume::Encrypted) {
        return device;
    }
    const Solid::Device backing = backingDevice(device);
    const auto *backingVolume = backing.as<Solid::StorageVolume>();
    if (backingVolume && backingVolume->usage() == Solid::StorageVolume::Encrypted) {
        return backing;
    }
    return Solid::Device();
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

    // Bounds the wait below, so a device that never turns up cannot leave the
    // monitor busy for good.
    m_mountWait.setSingleShot(true);
    m_mountWait.setInterval(10000);
    connect(&m_mountWait, &QTimer::timeout, this, [this]() {
        m_mountPending = false;
        setBusy(false);
        Q_EMIT mountFinished(false, i18n("The drive was unlocked but its filesystem did not appear"));
    });

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
    updateTarget();
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

QString DriveMonitor::targetContainerUuid() const
{
    return m_targetContainerUuid;
}

void DriveMonitor::setTargetContainerUuid(const QString &uuid)
{
    if (m_targetContainerUuid == uuid) {
        return;
    }
    m_targetContainerUuid = uuid;
    updateTarget();
    Q_EMIT targetChanged();
}

bool DriveMonitor::targetLocked() const
{
    return m_targetLocked;
}

bool DriveMonitor::targetFilesystemChanged() const
{
    return m_targetFilesystemChanged;
}

void DriveMonitor::updateTarget()
{
    bool locked = false;
    m_target = findTargetDevice(&locked);
    m_targetPresent = m_target.isValid();
    m_targetLocked = m_targetPresent && locked;

    // Only the LUKS fallback can return a volume the filesystem UUID did not
    // select, so a mismatch here is exactly the restored-or-recreated case.
    m_targetFilesystemChanged = false;
    if (m_targetPresent && !m_targetLocked && !m_targetUuid.isEmpty()) {
        const auto *volume = m_target.as<Solid::StorageVolume>();
        m_targetFilesystemChanged = volume && volume->uuid() != m_targetUuid;
    }
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
    if (!label.isEmpty() && label != name) {
        display += u" — "_s + label;
    }
    if (!sizeText.isEmpty()) {
        display += u" ("_s + sizeText + u')';
    }

    const Solid::Device container = encryptedContainer(device);
    const auto *containerVolume = container.as<Solid::StorageVolume>();

    return QVariantMap{
        {u"uuid"_s, volume ? volume->uuid() : QString()},
        {u"containerUuid"_s, containerVolume ? containerVolume->uuid() : QString()},
        {u"encrypted"_s, container.isValid()},
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

Solid::Device DriveMonitor::findTargetDevice(bool *locked) const
{
    if (locked) {
        *locked = false;
    }
    const auto devices = Solid::Device::listFromType(Solid::DeviceInterface::StorageVolume);

    // The filesystem UUID identifies the repository, but on an encrypted drive
    // it only exists once the drive is unlocked.
    if (!m_targetUuid.isEmpty()) {
        for (const Solid::Device &device : devices) {
            const auto *volume = device.as<Solid::StorageVolume>();
            if (volume && volume->uuid() == m_targetUuid
                && volume->usage() != Solid::StorageVolume::Encrypted) {
                return device;
            }
        }
    }

    // While locked the LUKS header is all that can be read, so the drive is
    // recognised by the UUID stored there instead.
    if (!m_targetContainerUuid.isEmpty()) {
        for (const Solid::Device &device : devices) {
            const auto *volume = device.as<Solid::StorageVolume>();
            if (!volume || volume->uuid() != m_targetContainerUuid
                || volume->usage() != Solid::StorageVolume::Encrypted) {
                continue;
            }
            // Already unlocked. Reaching here means the filesystem UUID did
            // not match above, so this is a drive that was restored or
            // recreated: accepted as ours, but flagged by updateTarget().
            const Solid::Device cleartext = cleartextDevice(device);
            if (cleartext.isValid()) {
                return cleartext;
            }
            if (locked) {
                *locked = true;
            }
            return device;
        }
    }
    return Solid::Device();
}

namespace
{
bool pathIsWithin(const QString &path, const QString &root)
{
    if (root == u"/"_s) {
        return path.startsWith(u'/');
    }
    return path == root || path.startsWith(root + u'/');
}
}

QVariantMap DriveMonitor::resolvePath(const QUrl &folder) const
{
    const QString path =
        QDir::cleanPath(folder.isLocalFile() ? folder.toLocalFile() : folder.toString());

    QVariantMap result;
    result.insert(u"found"_s, false);
    result.insert(u"path"_s, path);
    if (!path.startsWith(u'/')) {
        return result;
    }

    // The deepest mount point containing the folder is the volume it is on:
    // /home beats / for a folder below /home.
    Solid::Device volumeDevice;
    QString volumeMountPoint;
    const auto devices = Solid::Device::listFromType(Solid::DeviceInterface::StorageAccess);
    for (const Solid::Device &device : devices) {
        const auto *access = device.as<Solid::StorageAccess>();
        const auto *volume = device.as<Solid::StorageVolume>();
        if (!access || !access->isAccessible() || !volume || volume->uuid().isEmpty()) {
            continue;
        }
        const QString mountPoint = QDir::cleanPath(access->filePath());
        if (mountPoint.isEmpty() || !pathIsWithin(path, mountPoint)) {
            continue;
        }
        if (!volumeDevice.isValid() || mountPoint.length() > volumeMountPoint.length()) {
            volumeDevice = device;
            volumeMountPoint = mountPoint;
        }
    }

    if (!volumeDevice.isValid()) {
        return result;
    }

    QString relative = path.mid(volumeMountPoint.length());
    while (relative.startsWith(u'/')) {
        relative.remove(0, 1);
    }

    result = describe(volumeDevice);
    result.insert(u"found"_s, true);
    result.insert(u"path"_s, path);
    result.insert(u"mountPoint"_s, volumeMountPoint);
    result.insert(u"relativePath"_s, relative);
    return result;
}

void DriveMonitor::refresh()
{
    const bool wasPresent = m_targetPresent;
    const bool wasLocked = m_targetLocked;
    const bool wasChanged = m_targetFilesystemChanged;
    const QString oldMountPoint = targetMountPoint();
    const QString oldUdi = m_target.udi();

    updateTarget();

    // Unlocking swaps the target from the container to the filesystem inside
    // it, so the watch follows whichever device represents the drive now.
    // The connection has to be made with a member function pointer:
    // Qt::UniqueConnection is not allowed for lambdas.
    if (m_targetPresent && m_target.udi() != oldUdi) {
        if (auto *access = m_target.as<Solid::StorageAccess>()) {
            connect(access, &Solid::StorageAccess::accessibilityChanged,
                    this, &DriveMonitor::targetChanged, Qt::UniqueConnection);
        }
    }

    if (m_targetPresent && !wasPresent) {
        Q_EMIT targetChanged();
        Q_EMIT targetAppeared();
    } else if (!m_targetPresent && wasPresent) {
        Q_EMIT targetChanged();
        Q_EMIT targetVanished();
    } else if (oldMountPoint != targetMountPoint() || wasLocked != m_targetLocked
               || wasChanged != m_targetFilesystemChanged) {
        Q_EMIT targetChanged();
    }

    continuePendingMount();
}

void DriveMonitor::continuePendingMount()
{
    if (!m_mountPending) {
        return;
    }
    if (!m_targetPresent) {
        m_mountPending = false;
        m_mountWait.stop();
        setBusy(false);
        Q_EMIT mountFinished(false, i18n("The backup drive is not connected"));
        return;
    }
    if (m_targetLocked) {
        // Unlocked, but the filesystem inside has still not been enumerated.
        return;
    }
    m_mountPending = false;
    m_mountWait.stop();
    if (targetMounted()) {
        setBusy(false);
        Q_EMIT mountFinished(true, targetMountPoint());
        return;
    }
    // The filesystem has appeared; mounting it is the step that is left.
    startSetup(m_target, false);
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
    m_mountPending = false;
    m_mountWait.stop();
    updateTarget();
    if (!m_targetPresent) {
        Q_EMIT mountFinished(false, i18n("The backup drive is not connected"));
        return;
    }
    startSetup(m_target, m_targetLocked);
}

void DriveMonitor::startSetup(const Solid::Device &target, bool unlocking)
{
    // Held by value: refresh() reassigns m_target, and the interface below
    // belongs to whichever device object owns it.
    Solid::Device device = target;
    auto *access = device.as<Solid::StorageAccess>();
    if (!access) {
        setBusy(false);
        Q_EMIT mountFinished(false, i18n("The backup drive has no mountable filesystem"));
        return;
    }
    if (access->isAccessible()) {
        setBusy(false);
        Q_EMIT mountFinished(true, access->filePath());
        return;
    }

    setBusy(true);
    connect(access, &Solid::StorageAccess::setupDone, this,
            [this, device, unlocking](Solid::ErrorType error, const QVariant &errorData,
                                      const QString &udi) mutable {
                Q_UNUSED(udi)
                auto *finished = device.as<Solid::StorageAccess>();
                disconnect(finished, &Solid::StorageAccess::setupDone, this, nullptr);
                refresh();

                if (error != Solid::NoError) {
                    setBusy(false);
                    Q_EMIT mountFinished(false, errorText(error, errorData));
                    return;
                }
                // Unlocking only exposes the filesystem; mounting it is a
                // second step, on the device that has just appeared.
                if (unlocking && m_targetPresent && !m_targetLocked) {
                    startSetup(m_target, false);
                    return;
                }
                if (targetMounted()) {
                    setBusy(false);
                    Q_EMIT mountFinished(true, targetMountPoint());
                    return;
                }
                // udisks answers before Solid has necessarily seen the device
                // the call produced. Nothing is mounted yet, so wait for the
                // device event rather than report a mount that did not happen.
                m_mountPending = true;
                m_mountWait.start();
            });

    if (!access->setup()) {
        disconnect(access, &Solid::StorageAccess::setupDone, this, nullptr);
        setBusy(false);
        Q_EMIT mountFinished(false, unlocking ? i18n("Could not start unlocking the drive")
                                              : i18n("Could not start mounting the drive"));
    }
}

void DriveMonitor::unmountTarget()
{
    m_mountPending = false;
    m_mountWait.stop();
    updateTarget();
    if (!m_targetPresent) {
        Q_EMIT unmountFinished(true, QString());
        return;
    }
    startTeardown(m_target, false);
}

void DriveMonitor::startTeardown(const Solid::Device &target, bool locking)
{
    Solid::Device device = target;
    auto *access = device.as<Solid::StorageAccess>();
    if (!access || !access->isAccessible()) {
        // Nothing left to unmount. An unlocked container still holds the drive
        // open, so close that too before calling it safe to unplug.
        const Solid::Device container = encryptedContainer(device);
        if (!locking && container.isValid() && cleartextDevice(container).isValid()) {
            startTeardown(container, true);
            return;
        }
        setBusy(false);
        Q_EMIT unmountFinished(true, QString());
        return;
    }

    setBusy(true);
    connect(access, &Solid::StorageAccess::teardownDone, this,
            [this, device, locking](Solid::ErrorType error, const QVariant &errorData,
                                    const QString &udi) mutable {
                Q_UNUSED(udi)
                auto *finished = device.as<Solid::StorageAccess>();
                disconnect(finished, &Solid::StorageAccess::teardownDone, this, nullptr);
                refresh();

                if (error != Solid::NoError) {
                    setBusy(false);
                    Q_EMIT unmountFinished(false, errorText(error, errorData));
                    return;
                }
                // Unmounted, but the LUKS mapping may still be open.
                const Solid::Device container = encryptedContainer(device);
                if (!locking && container.isValid() && cleartextDevice(container).isValid()) {
                    startTeardown(container, true);
                    return;
                }
                setBusy(false);
                Q_EMIT unmountFinished(true, QString());
            });

    if (!access->teardown()) {
        disconnect(access, &Solid::StorageAccess::teardownDone, this, nullptr);
        setBusy(false);
        Q_EMIT unmountFinished(false, locking ? i18n("Could not start locking the drive")
                                              : i18n("Could not start unmounting the drive"));
    }
}
