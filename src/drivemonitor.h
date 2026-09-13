#pragma once

#include <QObject>
#include <QUrl>
#include <QVariantMap>

#include <Solid/Device>

/**
 * Watches removable storage through Solid.
 *
 * The backup drive is remembered by the UUID of its filesystem, so it is
 * recognised no matter which device node or mount point it gets this time.
 */
class DriveMonitor : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString targetUuid READ targetUuid WRITE setTargetUuid NOTIFY targetChanged)
    Q_PROPERTY(bool targetPresent READ targetPresent NOTIFY targetChanged)
    Q_PROPERTY(bool targetMounted READ targetMounted NOTIFY targetChanged)
    Q_PROPERTY(QString targetMountPoint READ targetMountPoint NOTIFY targetChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    explicit DriveMonitor(QObject *parent = nullptr);

    QString targetUuid() const;
    void setTargetUuid(const QString &uuid);

    bool targetPresent() const;
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

    /// Mounts the configured drive; emits mountFinished() when done.
    Q_INVOKABLE void mountTarget();

    /// Unmounts the configured drive so it can be unplugged safely.
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
    Solid::Device findTargetDevice() const;
    void setBusy(bool value);
    static QVariantMap describe(const Solid::Device &device);
    static bool isRemovableStorage(const Solid::Device &device);

    QString m_targetUuid;
    bool m_targetPresent = false;
    bool m_busy = false;
    Solid::Device m_target;
};
