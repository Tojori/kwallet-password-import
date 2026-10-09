// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <QList>
#include <QString>

namespace kwimport {

// One login (or secure note) from any supported export.
struct Entry {
    QString title;
    QString url;
    QString username;
    QString password;
    QString notes;
    QString otpAuth; // normalised to an otpauth:// URI
};

struct ParseResult {
    QList<Entry> entries;
    QString error;        // non-empty: file rejected as a whole
    int skipped = 0;      // rows/items without usable data or of unsupported types
    int skippedPasskeys = 0; // CXF/Bitwarden passkeys: use kpasskey for those
};

} // namespace kwimport
