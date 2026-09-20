#pragma once

#include <QObject>

#include <KSharedConfig>

/**
 * Settings that belong to Kamora itself rather than to any one backup
 * configuration.
 *
 * They live in the [General] group of kamorarc, next to the [Backups] groups
 * the individual configurations use.
 */
class AppSettings : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool autostart READ autostart WRITE setAutostart NOTIFY changed)
    Q_PROPERTY(bool startInBackground READ startInBackground WRITE setStartInBackground NOTIFY changed)

public:
    explicit AppSettings(KSharedConfig::Ptr config, QObject *parent = nullptr);

    /// Whether Kamora is launched at login.
    bool autostart() const;
    void setAutostart(bool value);

    /// Whether the autostarted instance goes straight to the tray.
    bool startInBackground() const;
    void setStartInBackground(bool value);

Q_SIGNALS:
    void changed();

private:
    void load();
    void save();
    /// Writes or removes ~/.config/autostart/io.github.ramanenka.kamora.desktop.
    void applyAutostart() const;

    KSharedConfig::Ptr m_config;
    bool m_autostart = true;
    bool m_startInBackground = true;
};
