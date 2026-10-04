#include "trayicon.h"

#include <QAction>
#include <QMenu>

#include <KLocalizedString>
#include <KStatusNotifierItem>

#include "kamoraconfig.h"

using namespace Qt::StringLiterals;

namespace
{
QString trayIcon(const char *variant = "")
{
    return QString::fromLatin1(KAMORA_APP_ID "-tray") + QLatin1StringView(variant);
}
}

TrayIcon::TrayIcon(QObject *parent)
    : QObject(parent)
    , m_item(new KStatusNotifierItem(QString::fromLatin1(KAMORA_BINARY_NAME), this))
{
    m_item->setCategory(KStatusNotifierItem::SystemServices);
    m_item->setTitle(i18n("Kamora Backup"));
    m_item->setIconByName(trayIcon());
    m_item->setAttentionIconByName(trayIcon("-error"));
    m_item->setStandardActionsEnabled(false);
    m_item->setStatus(KStatusNotifierItem::Passive);

    auto *menu = new QMenu();
    QAction *open = menu->addAction(QIcon::fromTheme(u"window"_s), i18n("Open Kamora…"));
    connect(open, &QAction::triggered, this, &TrayIcon::showWindowRequested);

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
        m_item->setIconByName(trayIcon());
        m_item->setToolTip(trayIcon(), i18n("Kamora Backup"), subtitle);
        break;
    case Due:
        m_item->setStatus(KStatusNotifierItem::Active);
        m_item->setIconByName(trayIcon("-attention"));
        m_item->setToolTip(trayIcon("-attention"), i18n("Backup due"), subtitle);
        break;
    case Running:
        m_item->setStatus(KStatusNotifierItem::Active);
        m_item->setIconByName(trayIcon("-sync"));
        m_item->setToolTip(trayIcon("-sync"), i18n("Backing up…"), subtitle);
        break;
    case Failed:
        m_item->setStatus(KStatusNotifierItem::NeedsAttention);
        m_item->setIconByName(trayIcon("-error"));
        m_item->setToolTip(trayIcon("-error"), i18n("Backup failed"), subtitle);
        break;
    }
}
