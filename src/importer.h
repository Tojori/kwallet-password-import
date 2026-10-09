// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "entry.h"

#include <QMap>
#include <QStringList>
#include <optional>

namespace kwimport {

using EntryMap = QMap<QString, QString>;

// Value of the "Source" field for entries created by this tool from a given
// source, e.g. "Firefox (kwallet-password-import)". --prune only ever removes
// entries carrying the marker of the source being imported.
QString sourceMarker(const QString &sourceName);
// Markers of older versions that are treated as equivalent for `sourceName`.
QStringList legacyMarkers(const QString &sourceName);

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
// to the title).
QString entryKey(const Entry &e);
EntryMap toMap(const Entry &e, const QString &marker);

struct Plan {
    QStringList add;
    QStringList update;
    QStringList unchanged;
    QStringList stale;              // imported earlier from this source, no longer present
    QMap<QString, EntryMap> values; // key -> new value (add + update)
};

Plan makePlan(const QList<Entry> &entries, WalletFolder &folder, const QString &sourceName);

struct ApplyResult {
    int written = 0;
    int removed = 0;
    int failed = 0;
};
ApplyResult apply(const Plan &plan, WalletFolder &folder, bool prune);

} // namespace kwimport
