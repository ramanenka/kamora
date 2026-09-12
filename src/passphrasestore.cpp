#include "passphrasestore.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>

#include <KWallet>

using namespace Qt::StringLiterals;

namespace
{
const QString folderName = u"Kamora"_s;

QString fallbackFilePath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + u"/kamora"_s;
    QDir().mkpath(dir);
    return dir + u"/passphrase"_s;
}

KWallet::Wallet *openWallet()
{
    KWallet::Wallet *wallet = KWallet::Wallet::openWallet(KWallet::Wallet::LocalWallet(), 0,
                                                         KWallet::Wallet::Synchronous);
    if (!wallet || !wallet->isOpen()) {
        delete wallet;
        return nullptr;
    }
    if (!wallet->hasFolder(folderName) && !wallet->createFolder(folderName)) {
        delete wallet;
        return nullptr;
    }
    if (!wallet->setFolder(folderName)) {
        delete wallet;
        return nullptr;
    }
    return wallet;
}
}

bool PassphraseStore::store(const QString &repoId, const QString &passphrase)
{
    if (KWallet::Wallet *wallet = openWallet()) {
        const bool ok = wallet->writePassword(repoId, passphrase) == 0;
        delete wallet;
        if (ok) {
            return true;
        }
    }

    QFile file(fallbackFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return false;
    }
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    file.write(passphrase.toUtf8());
    file.close();
    return true;
}

QString PassphraseStore::lookup(const QString &repoId)
{
    if (KWallet::Wallet *wallet = openWallet()) {
        QString value;
        const bool ok = wallet->readPassword(repoId, value) == 0;
        delete wallet;
        if (ok && !value.isEmpty()) {
            return value;
        }
    }

    QFile file(fallbackFilePath());
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString::fromUtf8(file.readAll()).trimmed();
    }
    return QString();
}

void PassphraseStore::remove(const QString &repoId)
{
    if (KWallet::Wallet *wallet = openWallet()) {
        wallet->removeEntry(repoId);
        delete wallet;
    }
    QFile::remove(fallbackFilePath());
}
