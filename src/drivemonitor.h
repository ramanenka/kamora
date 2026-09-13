#pragma once

#include <QObject>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

#include <Solid/Device>

/**
 * Watches removable storage through Solid.
 *
 * The backup drive is remembered by the UUID of its filesystem, so it is
 * recognised no matter which device node or mount point it gets this time.
 *
 * An encrypted drive needs a second identifier. The filesystem UUID lives
 * inside the LUKS payload and cannot be read until the drive is unlocked, so
 * a locked drive is recognised by the UUID in its LUKS header instead.
 *
 * On an encrypted drive the header UUID is the identity: a drive whose header
 * matches is the configured drive even if the filesystem inside it is not the
 * one that was set up, because a drive that was restored or recreated is still
 * the user's backup drive. The filesystem UUID is not enforced there, only
 * compared - a difference is reported through targetFilesystemChanged rather
 * than hiding the drive, since borg would otherwise be pointed at an
 * unrecognised filesystem without anyone being told.
 */
class DriveMonitor : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString targetUuid READ targetUuid WRITE setTargetUuid NOTIFY targetChanged)
    Q_PROPERTY(QString targetContainerUuid READ targetContainerUuid WRITE setTargetContainerUuid
                   NOTIFY targetChanged)
    Q_PROPERTY(bool targetPresent READ targetPresent NOTIFY targetChanged)
    Q_PROPERTY(bool targetLocked READ targetLocked NOTIFY targetChanged)
    Q_PROPERTY(bool targetFilesystemChanged READ targetFilesystemChanged NOTIFY targetChanged)
    Q_PROPERTY(bool targetMounted READ targetMounted NOTIFY targetChanged)
    Q_PROPERTY(QString targetMountPoint READ targetMountPoint NOTIFY targetChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    explicit DriveMonitor(QObject *parent = nullptr);

    QString targetUuid() const;
    void setTargetUuid(const QString &uuid);

    QString targetContainerUuid() const;
    void setTargetContainerUuid(const QString &uuid);

    bool targetPresent() const;
    /// True when the drive is connected but its filesystem is still encrypted.
    bool targetLocked() const;
    /// True when the drive was matched by its LUKS header but holds a different
    /// filesystem than the one it was configured with.
    bool targetFilesystemChanged() const;
    bool targetMounted() const;
    QString targetMountPoint() const;
    bool busy() const;

    /// Rescans the currently attached storage devices.
    Q_INVOKABLE void refresh();

    /**
     * Works out which volume a folder lives on.
     *
     * Returns the description of that volume plus "relativePath", the part of
     * the folder below the volume's mount point - the pair Kamora stores, so
     * that the repository is found again wherever the drive turns up next.
     * "found" is false when the folder is not on any mounted volume.
     */
    Q_INVOKABLE QVariantMap resolvePath(const QUrl &folder) const;

    /// Mounts the configured drive, unlocking it first if it is encrypted;
    /// emits mountFinished() when done.
    Q_INVOKABLE void mountTarget();

    /// Unmounts the configured drive, and locks it again if it is encrypted,
    /// so it can be unplugged safely.
    Q_INVOKABLE void unmountTarget();

Q_SIGNALS:
    void targetChanged();
    void targetAppeared();
    void targetVanished();
    void mountFinished(bool ok, const QString &message);
    void unmountFinished(bool ok, const QString &message);
    void busyChanged();

private:
    void onDeviceAdded(const QString &udi);
    void onDeviceRemoved(const QString &udi);
    Solid::Device findTargetDevice(bool *locked) const;
    /// Re-resolves the target device without announcing arrival or departure.
    void updateTarget();
    void startSetup(const Solid::Device &target, bool unlocking);
    /// Carries on a mount that is waiting for Solid to catch up with the drive.
    void continuePendingMount();
    void startTeardown(const Solid::Device &target, bool locking);
    void setBusy(bool value);
    static QVariantMap describe(const Solid::Device &device);
    static bool isRemovableStorage(const Solid::Device &device);

    QString m_targetUuid;
    QString m_targetContainerUuid;
    bool m_targetPresent = false;
    bool m_targetLocked = false;
    bool m_targetFilesystemChanged = false;
    bool m_busy = false;
    bool m_mountPending = false;
    QTimer m_mountWait;
    Solid::Device m_target;
};
