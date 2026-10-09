// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <optional>

namespace kwimport {

// RFC 4180 CSV: quoted fields may contain commas, quotes ("") and newlines.
// Accepts LF, CRLF or CR line ends and a UTF-8 BOM. Returns nullopt on
// malformed input (e.g. an unterminated quote).
std::optional<QList<QStringList>> parseCsv(const QString &text);

} // namespace kwimport
