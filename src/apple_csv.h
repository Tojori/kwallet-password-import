// SPDX-License-Identifier: LGPL-2.1-or-later
// Parser for the CSV file exported by Apple's Passwords app (macOS:
// File > Export All Passwords to File). Columns (header row):
//   Title,URL,Username,Password,Notes,OTPAuth
// Columns are located by header name, so order and extra columns do not matter.
#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <optional>

namespace kwimport {

// RFC 4180 CSV: quoted fields may contain commas, quotes ("") and newlines.
// Accepts LF or CRLF line ends and a UTF-8 BOM. Returns nullopt on malformed
// input (e.g. an unterminated quote).
std::optional<QList<QStringList>> parseCsv(const QString &text);

struct AppleEntry {
    QString title;
    QString url;
    QString username;
    QString password;
    QString notes;
    QString otpAuth; // otpauth:// URI (TOTP secret) if present
};

struct ParseResult {
    QList<AppleEntry> entries;
    QString error;    // non-empty: file rejected as a whole
    int skipped = 0;  // rows without any usable data
};

ParseResult parseAppleExport(const QByteArray &fileContent);

} // namespace kwimport
