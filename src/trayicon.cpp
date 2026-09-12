#include "trayicon.h"

#include <QAction>
#include <QMenu>

#include <KLocalizedString>
#include <KStatusNotifierItem>

using namespace Qt::StringLiterals;

TrayIcon::TrayIcon(QObject *parent)
    : QObject(parent)
    , m_item(new KStatusNotifierItem(u"kamora"_s, this))
{
    m_item->setCategory(KStatusNotifierItem::SystemServices);
    m_item->setTitle(i18n("Kamora Backup"));
    m_item->setIconByName(u"backup"_s);
    m_item->setAttentionIconByName(u"state-error"_s);
    m_item->setStandardActionsEnabled(false);
    m_item->setStatus(KStatusNotifierItem::Passive);

    auto *menu = new QMenu();
    m_backupAction = menu->addAction(QIcon::fromTheme(u"backup"_s), i18n("Back Up Now"));
    connect(m_backupAction, &QAction::triggered, this, &TrayIcon::backupRequested);

    QAction *open = menu->addAction(QIcon::fromTheme(u"window"_s), i18n("Open Kamora…"));
    connect(open, &QAction::triggered, this, &TrayIcon::showWindowRequested);

    QAction *configure = menu->addAction(QIcon::fromTheme(u"configure"_s), i18n("Configure…"));
    connect(configure, &QAction::triggered, this, &TrayIcon::configureRequested);

    menu->addSeparator();

    QAction *quit = menu->addAction(QIcon::fromTheme(u"application-exit"_s), i18n("Quit"));
    connect(quit, &QAction::triggered, this, &TrayIcon::quitRequested);

    m_item->setContextMenu(menu);

    connect(m_item, &KStatusNotifierItem::activateRequested, this, [this](bool active, const QPoint &) {
        Q_UNUSED(active)
        Q_EMIT showWindowRequested();
    });

    setState(Idle, QString());
}

void TrayIcon::setAssociatedWindow(QWindow *window)
{
    m_item->setAssociatedWindow(window);
}

void TrayIcon::setBackupActionEnabled(bool enabled)
{
    m_backupAction->setEnabled(enabled);
}

void TrayIcon::setState(State state, const QString &subtitle)
{
    if (m_state == state && m_subtitle == subtitle) {
        return;
    }
    m_state = state;
    m_subtitle = subtitle;

    switch (state) {
    case Idle:
        m_item->setStatus(KStatusNotifierItem::Passive);
        m_item->setIconByName(u"backup"_s);
        m_item->setOverlayIconByName(QString());
        m_item->setToolTip(u"backup"_s, i18n("Kamora Backup"), subtitle);
        break;
    case Due:
        m_item->setStatus(KStatusNotifierItem::Active);
        m_item->setIconByName(u"backup"_s);
        m_item->setOverlayIconByName(u"state-warning"_s);
        m_item->setToolTip(u"backup"_s, i18n("Backup due"), subtitle);
        break;
    case Running:
        m_item->setStatus(KStatusNotifierItem::Active);
        m_item->setIconByName(u"backup"_s);
        m_item->setOverlayIconByName(u"state-sync"_s);
        m_item->setToolTip(u"backup"_s, i18n("Backing up…"), subtitle);
        break;
    case Failed:
        m_item->setStatus(KStatusNotifierItem::NeedsAttention);
        m_item->setIconByName(u"backup"_s);
        m_item->setOverlayIconByName(u"state-error"_s);
        m_item->setToolTip(u"state-error"_s, i18n("Backup failed"), subtitle);
        break;
    }
}
