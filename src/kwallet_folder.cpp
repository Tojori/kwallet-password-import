// SPDX-License-Identifier: LGPL-2.1-or-later
#include "kwallet_folder.h"

#include <KWallet>

namespace kwimport {

KWalletFolder::~KWalletFolder()
{
    delete m_wallet;
}

bool KWalletFolder::open(const QString &walletName, const QString &folder, bool create, QString *error)
{
    if (!KWallet::Wallet::isEnabled()) {
        *error = QStringLiteral("KWallet is disabled");
        return false;
    }
    m_wallet = KWallet::Wallet::openWallet(walletName, 0, KWallet::Wallet::Synchronous);
    if (!m_wallet || !m_wallet->isOpen()) {
        *error = QStringLiteral("could not open wallet \"%1\" (denied or cancelled)").arg(walletName);
        return false;
    }
    if (!m_wallet->hasFolder(folder)) {
        if (!create) {
            m_folder = folder; // empty: keys() returns nothing
            return true;
        }
        if (!m_wallet->createFolder(folder)) {
            *error = QStringLiteral("could not create folder \"%1\"").arg(folder);
            return false;
        }
    }
    m_folder = folder;
    return m_wallet->setFolder(folder);
}

QStringList KWalletFolder::keys()
{
    if (!m_wallet || !m_wallet->hasFolder(m_folder) || !m_wallet->setFolder(m_folder)) {
        return {};
    }
    return m_wallet->entryList();
}

std::optional<EntryMap> KWalletFolder::readMap(const QString &key)
{
    if (!m_wallet || m_wallet->entryType(key) != KWallet::Wallet::Map) {
        return std::nullopt;
    }
    QMap<QString, QString> value;
    if (m_wallet->readMap(key, value) != 0) {
        return std::nullopt;
    }
    return value;
}

bool KWalletFolder::writeMap(const QString &key, const EntryMap &value)
{
    return m_wallet && m_wallet->writeMap(key, value) == 0;
}

bool KWalletFolder::remove(const QString &key)
{
    return m_wallet && m_wallet->removeEntry(key) == 0;
}

} // namespace kwimport
