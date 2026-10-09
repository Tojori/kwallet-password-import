// SPDX-License-Identifier: LGPL-2.1-or-later
#include "csv.h"

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

} // namespace kwimport
