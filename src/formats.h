// SPDX-License-Identifier: LGPL-2.1-or-later
// Supported export formats, detection and parsing.
#pragma once

#include "entry.h"

#include <QByteArray>
#include <QList>
#include <QString>
#include <optional>

namespace kwimport {

enum class Format {
    Auto,
    Apple,         // Apple Passwords / iCloud Keychain CSV (macOS)
    Firefox,       // about:logins -> Export Logins (CSV)
    Chromium,      // Chrome, Chromium, Edge, Brave, Vivaldi, Google Password Manager (CSV)
    BitwardenCsv,  // Bitwarden / Vaultwarden, unencrypted CSV
    BitwardenJson, // Bitwarden / Vaultwarden, unencrypted JSON
    KeePassXC,     // KeePassXC: Database -> Export -> CSV
    OnePassword,   // 1Password CSV (best effort, columns matched by name)
    ProtonPass,    // Proton Pass CSV
    Cxf,           // FIDO Credential Exchange Format 1.0 (JSON), standard
};

struct FormatInfo {
    Format format;
    QString id;            // command line name
    QString displayName;
    QString sourceName;    // origin, shared by variants (Bitwarden CSV/JSON); used in the Source marker
    QString defaultFolder; // KDE wallet folder
    QString howToExport;   // short instruction, English (UI translates)
};

const QList<FormatInfo> &formats();
const FormatInfo &info(Format f);
std::optional<Format> formatFromId(const QString &id);

// Recognises the format from the content (CSV header or JSON structure).
// Returns Format::Auto if unknown.
Format detect(const QByteArray &content);

// Parses `content`; `f` may be Format::Auto (then detect() is used).
// `detected` receives the format actually used.
ParseResult parse(const QByteArray &content, Format f, Format *detected = nullptr);

// Turns a bare Base32 TOTP secret into an otpauth:// URI; URIs pass through.
QString normaliseOtp(const QString &value, const QString &label);

} // namespace kwimport
