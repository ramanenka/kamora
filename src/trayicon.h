#pragma once

#include <QObject>

class KStatusNotifierItem;
class QWindow;

/**
 * The system tray entry.
 *
 * Plasma's "Show when relevant" default hides items whose status is Passive,
 * so Kamora is only in the tray while a backup is actually due, running or
 * broken - and disappears again as soon as a backup has been taken.
 */
class TrayIcon : public QObject
{
    Q_OBJECT

public:
    enum State {
        Idle, ///< nothing to do, the icon is not relevant
        Due, ///< a backup is due
        Running,
        Failed,
    };
    Q_ENUM(State)

    explicit TrayIcon(QObject *parent = nullptr);

    void setState(State state, const QString &subtitle);
    void setAssociatedWindow(QWindow *window);

Q_SIGNALS:
    void showWindowRequested();
    void quitRequested();

private:
    KStatusNotifierItem *m_item = nullptr;
    State m_state = Idle;
    QString m_subtitle;
};
