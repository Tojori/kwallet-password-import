// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "importer.h"

namespace KWallet {
class Wallet;
}

namespace kwimport {

// A folder in a KDE wallet (KF6Wallet; on Plasma 6 backed by ksecretd).
class KWalletFolder : public WalletFolder
{
public:
    ~KWalletFolder() override;
    // Opens `walletName` (may show KWallet's unlock dialog) and creates the
    // folder if needed. On failure `error` explains why.
    bool open(const QString &walletName, const QString &folder, bool create, QString *error);

    QStringList keys() override;
    std::optional<EntryMap> readMap(const QString &key) override;
    bool writeMap(const QString &key, const EntryMap &value) override;
    bool remove(const QString &key) override;

private:
    KWallet::Wallet *m_wallet = nullptr;
    QString m_folder;
};

} // namespace kwimport
