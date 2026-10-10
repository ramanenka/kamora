#pragma once

#include <QList>
#include <QObject>
#include <QTimer>

#include <KSharedConfig>

#include "appsettings.h"
#include "backupplan.h"
#include "trayicon.h"

class QWindow;

/**
 * The application itself: it holds the plans, the settings that are not about
 * any one of them, the tray icon and the window.
 *
 * Each configuration is one Backup, which runs itself. The controller only
 * decides whether one may start: they are taken one at a time, because two at
 * once would compete for the same disk - and, when two share a drive, for the
 * same borg lock. One that asks while another is running is turned down, and
 * tries again on the next tick.
 */
class BackupController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(AppSettings *settings READ settings CONSTANT)
    Q_PROPERTY(QList<QObject *> plans READ plans NOTIFY plansChanged)
    Q_PROPERTY(int planCount READ planCount NOTIFY plansChanged)
    /// True once at least one configuration is complete enough to run.
    Q_PROPERTY(bool configured READ configured NOTIFY statusChanged)
    Q_PROPERTY(bool anyRunning READ anyRunning NOTIFY statusChanged)
    Q_PROPERTY(QString summary READ summary NOTIFY statusChanged)
    Q_PROPERTY(bool borgAvailable READ borgAvailable CONSTANT)

public:
    explicit BackupController(QObject *parent = nullptr);

    AppSettings *settings() const;
    QList<QObject *> plans() const;
    int planCount() const;
    bool configured() const;
    bool anyRunning() const;
    QString summary() const;
    bool borgAvailable() const;

    void setWindow(QWindow *window);

    /// Adds an empty configuration and returns it, ready to be set up.
    Q_INVOKABLE BackupPlan *addPlan();
    /// Removes a configuration for good.
    Q_INVOKABLE void removePlan(BackupPlan *plan);
    /// Drops a configuration that was added but never saved.
    Q_INVOKABLE void discardIfUnconfigured(BackupPlan *plan);

    Q_INVOKABLE void showWindow();
    Q_INVOKABLE void quitApplication();

Q_SIGNALS:
    void plansChanged();
    void statusChanged();
    /// Shown as an inline message, prefixed with the configuration's name.
    void message(const QString &text, bool error);

private:
    /// Stops whichever backup is running.
    void cancelAll();
    /// Lets one start unless another is already under way.
    void onStartRequested(BackupPlan *plan);
    BackupPlan *createPlan(const QString &id);
    void connectPlan(BackupPlan *plan);
    /// Remembers which configurations exist, and in which order.
    void savePlanList();
    void evaluate();
    TrayIcon::State trayState() const;

    KSharedConfig::Ptr m_config;
    AppSettings *m_settings;
    TrayIcon *m_tray;
    QList<BackupPlan *> m_plans;
    QTimer m_tick;
    QWindow *m_window = nullptr;
};
