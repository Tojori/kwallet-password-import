// SPDX-License-Identifier: LGPL-2.1-or-later
#include "importer.h"

#include <QSet>
#include <QUrl>

namespace kwimport {

QString sourceMarker(const QString &sourceName)
{
    return QStringLiteral("%1 (kwallet-password-import)").arg(sourceName);
}

QStringList legacyMarkers(const QString &sourceName)
{
    // version 0.1 was "kwallet-icloud-import" and only knew Apple exports
    if (sourceName.startsWith(QLatin1String("Apple"))) {
        return {QStringLiteral("Apple Passwords (kwallet-icloud-import)")};
    }
    return {};
}

QString entryKey(const Entry &e)
{
    QString base = QUrl::fromUserInput(e.url).host().toLower();
    if (base.isEmpty()) {
        base = e.title.trimmed().isEmpty() ? QStringLiteral("(untitled)") : e.title.trimmed();
    }
    // KWallet keys are displayed in a tree; slashes would look like paths.
    base.replace(QLatin1Char('/'), QLatin1Char('_'));
    return e.username.isEmpty() ? base : QStringLiteral("%1 (%2)").arg(base, e.username);
}

EntryMap toMap(const Entry &e, const QString &marker)
{
    EntryMap m;
    m.insert(QStringLiteral("Title"), e.title);
    m.insert(QStringLiteral("URL"), e.url);
    m.insert(QStringLiteral("Username"), e.username);
    m.insert(QStringLiteral("Password"), e.password);
    if (!e.notes.isEmpty()) {
        m.insert(QStringLiteral("Notes"), e.notes);
    }
    if (!e.otpAuth.isEmpty()) {
        m.insert(QStringLiteral("OTPAuth"), e.otpAuth);
    }
    m.insert(QStringLiteral("Source"), marker);
    return m;
}

Plan makePlan(const QList<Entry> &entries, WalletFolder &folder, const QString &sourceName)
{
    Plan plan;
    const QString marker = sourceMarker(sourceName);
    QStringList ownMarkers = legacyMarkers(sourceName);
    ownMarkers.append(marker);
    const QStringList existingKeys = folder.keys();
    const QSet<QString> existing(existingKeys.cbegin(), existingKeys.cend());
    QSet<QString> seen;
    for (const Entry &e : entries) {
        // Same host and username twice in one export: keep both, numbered in
        // export order so that repeated imports map them to the same keys.
        const QString base = entryKey(e);
        QString key = base;
        for (int n = 2; seen.contains(key); ++n) {
            key = QStringLiteral("%1 [%2]").arg(base).arg(n);
        }
        seen.insert(key);
        const EntryMap value = toMap(e, marker);
        if (!existing.contains(key)) {
            plan.add.append(key);
            plan.values.insert(key, value);
        } else if (folder.readMap(key) == value) {
            plan.unchanged.append(key);
        } else {
            plan.update.append(key);
            plan.values.insert(key, value);
        }
    }
    for (const QString &key : existingKeys) {
        if (!seen.contains(key)) {
            const auto m = folder.readMap(key);
            if (m && ownMarkers.contains(m->value(QStringLiteral("Source")))) {
                plan.stale.append(key); // never touch entries we did not create
            }
        }
    }
    return plan;
}

ApplyResult apply(const Plan &plan, WalletFolder &folder, bool prune)
{
    ApplyResult r;
    for (auto it = plan.values.cbegin(); it != plan.values.cend(); ++it) {
        if (folder.writeMap(it.key(), it.value())) {
            ++r.written;
        } else {
            ++r.failed;
        }
    }
    if (prune) {
        for (const QString &key : plan.stale) {
            if (folder.remove(key)) {
                ++r.removed;
            } else {
                ++r.failed;
            }
        }
    }
    return r;
}

} // namespace kwimport
