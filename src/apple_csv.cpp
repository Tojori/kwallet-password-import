// SPDX-License-Identifier: LGPL-2.1-or-later
#include "apple_csv.h"

#include <QStringDecoder>

namespace kwimport {

std::optional<QList<QStringList>> parseCsv(const QString &text)
{
    QList<QStringList> rows;
    QStringList row;
    QString field;
    bool inQuotes = false;
    bool fieldStarted = false;
    qsizetype i = 0;
    if (text.startsWith(QChar(0xFEFF))) {
        i = 1; // BOM
    }
    auto endField = [&] {
        row.append(field);
        field.clear();
        fieldStarted = false;
    };
    auto endRow = [&] {
        endField();
        // ignore completely empty lines
        if (!(row.size() == 1 && row.first().isEmpty())) {
            rows.append(row);
        }
        row.clear();
    };
    for (; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (inQuotes) {
            if (c == QLatin1Char('"')) {
                if (i + 1 < text.size() && text.at(i + 1) == QLatin1Char('"')) {
                    field += QLatin1Char('"');
                    ++i;
                } else {
                    inQuotes = false;
                }
            } else {
                field += c;
            }
            continue;
        }
        if (c == QLatin1Char('"') && !fieldStarted && field.isEmpty()) {
            inQuotes = true;
            fieldStarted = true;
        } else if (c == QLatin1Char(',')) {
            endField();
        } else if (c == QLatin1Char('\r')) {
            // handled together with the following \n (or alone, old Mac line ends)
            endRow();
            if (i + 1 < text.size() && text.at(i + 1) == QLatin1Char('\n')) {
                ++i;
            }
        } else if (c == QLatin1Char('\n')) {
            endRow();
        } else {
            field += c;
            fieldStarted = true;
        }
    }
    if (inQuotes) {
        return std::nullopt;
    }
    if (fieldStarted || !field.isEmpty() || !row.isEmpty()) {
        endRow();
    }
    return rows;
}

ParseResult parseAppleExport(const QByteArray &fileContent)
{
    ParseResult result;
    QStringDecoder decoder(QStringDecoder::Utf8);
    const QString text = decoder(fileContent);
    if (decoder.hasError()) {
        result.error = QStringLiteral("file is not valid UTF-8");
        return result;
    }
    const auto rows = parseCsv(text);
    if (!rows) {
        result.error = QStringLiteral("malformed CSV (unterminated quote)");
        return result;
    }
    if (rows->isEmpty()) {
        result.error = QStringLiteral("file is empty");
        return result;
    }
    const QStringList header = rows->first();
    auto column = [&header](const char *name) {
        for (qsizetype i = 0; i < header.size(); ++i) {
            if (header.at(i).trimmed().compare(QLatin1String(name), Qt::CaseInsensitive) == 0) {
                return int(i);
            }
        }
        return -1;
    };
    const int cTitle = column("Title");
    const int cUrl = column("URL");
    const int cUser = column("Username");
    const int cPass = column("Password");
    const int cNotes = column("Notes");
    const int cOtp = column("OTPAuth");
    if (cPass < 0 || (cUrl < 0 && cTitle < 0)) {
        result.error = QStringLiteral("not an Apple Passwords export (header must contain Password and URL or Title)");
        return result;
    }
    auto at = [](const QStringList &r, int c) { return c >= 0 && c < r.size() ? r.at(c) : QString(); };
    for (qsizetype n = 1; n < rows->size(); ++n) {
        const QStringList &r = rows->at(n);
        AppleEntry e{at(r, cTitle), at(r, cUrl).trimmed(), at(r, cUser), at(r, cPass), at(r, cNotes), at(r, cOtp).trimmed()};
        if (e.password.isEmpty() && e.otpAuth.isEmpty() && e.notes.isEmpty()) {
            ++result.skipped;
            continue;
        }
        result.entries.append(e);
    }
    return result;
}

} // namespace kwimport
