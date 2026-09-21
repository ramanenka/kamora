#include "appsettings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

#include <KConfigGroup>

using namespace Qt::StringLiterals;

namespace
{
QString autostartFilePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + u"/autostart/io.github.ramanenka.kamora.desktop"_s;
}
}

AppSettings::AppSettings(KSharedConfig::Ptr config, QObject *parent)
    : QObject(parent)
    , m_config(std::move(config))
{
    load();
    applyAutostart();
}

bool AppSettings::autostart() const
{
    return m_autostart;
}

void AppSettings::setAutostart(bool value)
{
    if (m_autostart == value) {
        return;
    }
    m_autostart = value;
    save();
    applyAutostart();
    Q_EMIT changed();
}

bool AppSettings::startInBackground() const
{
    return m_startInBackground;
}

void AppSettings::setStartInBackground(bool value)
{
    if (m_startInBackground == value) {
        return;
    }
    m_startInBackground = value;
    save();
    applyAutostart();
    Q_EMIT changed();
}

void AppSettings::load()
{
    const KConfigGroup group = m_config->group(u"General"_s);
    m_autostart = group.readEntry("Autostart", m_autostart);
    m_startInBackground = group.readEntry("StartInBackground", m_startInBackground);
}

void AppSettings::save()
{
    KConfigGroup group = m_config->group(u"General"_s);
    group.writeEntry("Autostart", m_autostart);
    group.writeEntry("StartInBackground", m_startInBackground);
    m_config->sync();
}

void AppSettings::applyAutostart() const
{
    const QString path = autostartFilePath();
    if (!m_autostart) {
        QFile::remove(path);
        return;
    }

    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return;
    }
    const QString arguments = m_startInBackground ? u" --background"_s : QString();
    const QString contents = u"[Desktop Entry]\n"
                             "Type=Application\n"
                             "Name=Kamora Backup\n"
                             "Comment=Scheduled borg backups to a USB drive\n"
                             "Icon=io.github.ramanenka.kamora\n"
                             "Exec=%1%2\n"
                             "Terminal=false\n"
                             "X-GNOME-Autostart-enabled=true\n"
                             "X-KDE-autostart-after=panel\n"_s
                                 .arg(QCoreApplication::applicationFilePath(), arguments);
    file.write(contents.toUtf8());
}
