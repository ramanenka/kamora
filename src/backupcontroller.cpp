#include "backupcontroller.h"

#include <QCoreApplication>
#include <QWindow>

#include <KConfigGroup>
#include <KLocalizedString>

#include "kamoraconfig.h"

using namespace Qt::StringLiterals;

BackupController::BackupController(QObject *parent)
    : QObject(parent)
    , m_config(KSharedConfig::openConfig(QString::fromLatin1(KAMORA_BINARY_NAME "rc")))
    , m_settings(new AppSettings(m_config, this))
    , m_tray(new TrayIcon(this))
{
    const KConfigGroup general = m_config->group(u"General"_s);
    const QStringList ids = general.readEntry("Configs", QStringList());
    const KConfigGroup backups = m_config->group(u"Backups"_s);
    bool healed = false;
    for (const QString &id : ids) {
        // A configuration that was added but never saved left an id behind and
        // no group to go with it. There is nothing in it to keep.
        if (!backups.hasGroup(id)) {
            healed = true;
            m_nextId = qMax(m_nextId, id.toInt() + 1);
            continue;
        }
        BackupPlan *plan = createPlan(id);
        m_plans.append(plan);
        m_nextId = qMax(m_nextId, id.toInt() + 1);
    }
    if (healed) {
        savePlanList();
    }

    connect(m_tray, &TrayIcon::showWindowRequested, this, &BackupController::showWindow);
    connect(m_tray, &TrayIcon::quitRequested, this, &BackupController::quitApplication);

    // A minute is fine: due-ness only changes on the scale of hours.
    m_tick.setInterval(60 * 1000);
    connect(&m_tick, &QTimer::timeout, this, [this]() {
        for (BackupPlan *plan : std::as_const(m_plans)) {
            plan->evaluate();
        }
        evaluate();
    });
    m_tick.start();

    for (BackupPlan *plan : std::as_const(m_plans)) {
        plan->evaluate();
    }
    evaluate();
}

BackupPlan *BackupController::createPlan(const QString &id)
{
    auto *plan = new BackupPlan(new BackupConfig(m_config, id), this);
    connectPlan(plan);
    return plan;
}

void BackupController::connectPlan(BackupPlan *plan)
{
    connect(plan, &BackupPlan::statusChanged, this, &BackupController::evaluate);
    connect(plan, &BackupPlan::startRequested, this, [this, plan]() {
        onStartRequested(plan);
    });
    // A configuration reaches the stored list once it has been saved, so an
    // abandoned one leaves nothing behind.
    connect(plan, &BackupPlan::configurationSaved, this, &BackupController::savePlanList);
    connect(plan, &BackupPlan::message, this, [this, plan](const QString &text, bool error) {
        if (m_plans.size() > 1) {
            Q_EMIT message(i18nc("message from one plan", "%1: %2",
                                 plan->config()->displayName(), text),
                           error);
        } else {
            Q_EMIT message(text, error);
        }
    });
    connect(plan->config(), &BackupConfig::changed, this, &BackupController::statusChanged);
}

void BackupController::savePlanList()
{
    QStringList ids;
    ids.reserve(m_plans.size());
    for (const BackupPlan *plan : std::as_const(m_plans)) {
        ids << plan->config()->id();
    }
    KConfigGroup general = m_config->group(u"General"_s);
    general.writeEntry("Configs", ids);
    m_config->sync();
}

AppSettings *BackupController::settings() const
{
    return m_settings;
}

QList<QObject *> BackupController::plans() const
{
    QList<QObject *> result;
    result.reserve(m_plans.size());
    for (BackupPlan *plan : m_plans) {
        result.append(plan);
    }
    return result;
}

int BackupController::planCount() const
{
    return m_plans.size();
}

bool BackupController::configured() const
{
    for (const BackupPlan *plan : m_plans) {
        if (plan->config()->configured()) {
            return true;
        }
    }
    return false;
}

bool BackupController::anyRunning() const
{
    for (const BackupPlan *plan : m_plans) {
        if (plan->active()) {
            return true;
        }
    }
    return false;
}

bool BackupController::borgAvailable() const
{
    return !BorgRunner::borgExecutable().isEmpty();
}

QString BackupController::summary() const
{
    if (m_plans.isEmpty()) {
        return i18n("No backup plans yet");
    }
    int running = 0;
    int failed = 0;
    int due = 0;
    int idle = 0;
    for (const BackupPlan *plan : m_plans) {
        switch (plan->state()) {
        case BackupPlan::Running:
            ++running;
            break;
        case BackupPlan::Failed:
            ++failed;
            break;
        case BackupPlan::Due:
            ++due;
            break;
        case BackupPlan::Idle:
            ++idle;
            break;
        case BackupPlan::Disabled:
            break;
        }
    }
    if (running > 0) {
        return i18np("One backup is running", "%1 backups are running", running);
    }
    if (failed > 0) {
        return i18np("One backup failed", "%1 backups failed", failed);
    }
    if (due > 0) {
        return i18np("One backup is due", "%1 backups are due", due);
    }
    if (idle == 0) {
        // Every configuration is switched off, or none is finished being set up.
        return i18n("No backup is watching for a drive");
    }
    return i18np("The backup is up to date", "All %1 backups are up to date", idle);
}

BackupPlan *BackupController::addPlan()
{
    const QString id = QString::number(m_nextId++);
    BackupPlan *plan = createPlan(id);
    m_plans.append(plan);
    Q_EMIT plansChanged();
    evaluate();
    return plan;
}

void BackupController::removePlan(BackupPlan *plan)
{
    if (!plan || !m_plans.contains(plan)) {
        return;
    }
    plan->cancel();
    plan->config()->erase();
    m_plans.removeAll(plan);
    savePlanList();
    plan->deleteLater();
    Q_EMIT plansChanged();
    evaluate();
}

void BackupController::discardIfUnconfigured(BackupPlan *plan)
{
    if (plan && !plan->config()->configured()) {
        removePlan(plan);
    }
}

void BackupController::cancelAll()
{
    for (BackupPlan *plan : std::as_const(m_plans)) {
        if (plan->active()) {
            plan->cancel();
        }
    }
    evaluate();
}

void BackupController::onStartRequested(BackupPlan *plan)
{
    if (!m_plans.contains(plan) || plan->active()) {
        return;
    }
    // One backup at a time. Refusing is silent on purpose: the only requests
    // that get here while one is running are automatic ones - the buttons are
    // disabled meanwhile - and they ask again on the next tick anyway.
    if (anyRunning()) {
        return;
    }
    plan->start();
    evaluate();
}

TrayIcon::State BackupController::trayState() const
{
    TrayIcon::State state = TrayIcon::Idle;
    for (const BackupPlan *plan : m_plans) {
        switch (plan->state()) {
        case BackupPlan::Running:
            return TrayIcon::Running;
        case BackupPlan::Failed:
            state = TrayIcon::Failed;
            break;
        case BackupPlan::Due:
            if (state != TrayIcon::Failed) {
                state = TrayIcon::Due;
            }
            break;
        case BackupPlan::Idle:
        case BackupPlan::Disabled:
            break;
        }
    }
    return state;
}

void BackupController::evaluate()
{
    m_tray->setState(trayState(), summary());
    Q_EMIT statusChanged();
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

void BackupController::quitApplication()
{
    cancelAll();
    QCoreApplication::quit();
}
