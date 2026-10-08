// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "apple_csv.h"

#include <QMap>
#include <QStringList>
#include <optional>

namespace kwimport {

using EntryMap = QMap<QString, QString>;

// Map value "Source" marks entries created by this tool; --prune only ever
// touches entries carrying it.
inline const QString SourceValue = QStringLiteral("Apple Passwords (kwallet-icloud-import)");

// One folder of a wallet.
class WalletFolder
{
public:
    virtual ~WalletFolder() = default;
    virtual QStringList keys() = 0;
    virtual std::optional<EntryMap> readMap(const QString &key) = 0;
    virtual bool writeMap(const QString &key, const EntryMap &value) = 0;
    virtual bool remove(const QString &key) = 0;
};

// Wallet entry key: "<host> (<username>)", host taken from the URL (falls back
// to the title). Lower-case host, "www." kept as-is (different hosts may hold
// different accounts).
QString entryKey(const AppleEntry &e);
EntryMap toMap(const AppleEntry &e);

struct Plan {
    QStringList add;       // keys
    QStringList update;
    QStringList unchanged;
    QStringList stale;     // imported earlier, no longer in the export
    QMap<QString, EntryMap> values; // key -> new value (add + update)
};

Plan makePlan(const QList<AppleEntry> &entries, WalletFolder &folder);

struct ApplyResult {
    int written = 0;
    int removed = 0;
    int failed = 0;
};
ApplyResult apply(const Plan &plan, WalletFolder &folder, bool prune);

} // namespace kwimport
