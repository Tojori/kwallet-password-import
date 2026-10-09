// SPDX-License-Identifier: LGPL-2.1-or-later
#include "formats.h"

#include "csv.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QStringDecoder>
#include <QUrl>
#include <QUrlQuery>

using namespace Qt::StringLiterals;

namespace kwimport {

namespace {

// Column name aliases per field (compared case-insensitively).
struct CsvProfile {
    Format format;
    QStringList title, url, username, password, notes, otp, type;
    QStringList fallbackUsername; // e.g. Proton Pass "email" when username is empty
};

const QList<CsvProfile> &csvProfiles()
{
    static const QList<CsvProfile> p = {
        {Format::Apple, {u"title"_s}, {u"url"_s}, {u"username"_s}, {u"password"_s}, {u"notes"_s}, {u"otpauth"_s}, {}, {}},
        {Format::Firefox, {}, {u"url"_s}, {u"username"_s}, {u"password"_s}, {}, {}, {}, {}},
        {Format::Chromium, {u"name"_s}, {u"url"_s}, {u"username"_s}, {u"password"_s}, {u"note"_s}, {}, {}, {}},
        {Format::BitwardenCsv, {u"name"_s}, {u"login_uri"_s}, {u"login_username"_s}, {u"login_password"_s}, {u"notes"_s},
         {u"login_totp"_s}, {u"type"_s}, {}},
        {Format::KeePassXC, {u"title"_s}, {u"url"_s}, {u"username"_s}, {u"password"_s}, {u"notes"_s}, {u"totp"_s}, {}, {}},
        {Format::OnePassword, {u"title"_s}, {u"url"_s, u"website"_s}, {u"username"_s}, {u"password"_s}, {u"notes"_s, u"notesplain"_s},
         {u"otpauth"_s, u"one-time password"_s}, {}, {}},
        {Format::ProtonPass, {u"name"_s}, {u"url"_s}, {u"username"_s}, {u"password"_s}, {u"note"_s}, {u"totp"_s}, {u"type"_s},
         {u"email"_s}},
    };
    return p;
}

QString hostOf(const QString &url)
{
    return QUrl::fromUserInput(url).host();
}

Format detectCsv(const QStringList &header)
{
    QSet<QString> h;
    for (const QString &c : header) {
        h.insert(c.trimmed().toLower());
    }
    auto has = [&h](std::initializer_list<const char16_t *> cols) {
        for (const char16_t *c : cols) {
            if (!h.contains(QString::fromUtf16(c))) {
                return false;
            }
        }
        return true;
    };
    // Most specific signatures first.
    if (has({u"login_uri", u"login_username", u"login_password"})) {
        return Format::BitwardenCsv;
    }
    if (has({u"url", u"username", u"password"}) && (h.contains(u"httprealm"_s) || h.contains(u"formactionorigin"_s))) {
        return Format::Firefox;
    }
    if (has({u"group", u"title", u"username", u"password", u"url"})) {
        return Format::KeePassXC;
    }
    if (has({u"name", u"url", u"password", u"vault"}) || has({u"name", u"url", u"email", u"username", u"password", u"totp"})) {
        return Format::ProtonPass;
    }
    if (has({u"title", u"password"}) && (h.contains(u"website"_s) || h.contains(u"favorite"_s) || h.contains(u"archived"_s))) {
        return Format::OnePassword;
    }
    if (has({u"title", u"url", u"username", u"password", u"otpauth"})) {
        return Format::Apple;
    }
    if (has({u"name", u"url", u"username", u"password"})) {
        return Format::Chromium;
    }
    return Format::Auto;
}

ParseResult parseCsvProfile(const QList<QStringList> &rows, Format f)
{
    ParseResult result;
    const CsvProfile *profile = nullptr;
    for (const CsvProfile &p : csvProfiles()) {
        if (p.format == f) {
            profile = &p;
        }
    }
    if (!profile) {
        result.error = u"internal: no CSV profile"_s;
        return result;
    }
    const QStringList &header = rows.first();
    auto column = [&header](const QStringList &aliases) {
        for (const QString &a : aliases) {
            for (qsizetype i = 0; i < header.size(); ++i) {
                if (header.at(i).trimmed().compare(a, Qt::CaseInsensitive) == 0) {
                    return int(i);
                }
            }
        }
        return -1;
    };
    const int cTitle = column(profile->title), cUrl = column(profile->url), cUser = column(profile->username),
              cPass = column(profile->password), cNotes = column(profile->notes), cOtp = column(profile->otp),
              cType = column(profile->type), cAltUser = column(profile->fallbackUsername);
    if (cPass < 0) {
        result.error = u"the file has no password column"_s;
        return result;
    }
    auto at = [](const QStringList &r, int c) { return c >= 0 && c < r.size() ? r.at(c) : QString(); };
    for (qsizetype n = 1; n < rows.size(); ++n) {
        const QStringList &r = rows.at(n);
        const QString type = at(r, cType).trimmed().toLower();
        if (f == Format::BitwardenCsv && !type.isEmpty() && type != u"login" && type != u"note") {
            ++result.skipped; // cards, identities
            continue;
        }
        Entry e;
        e.url = at(r, cUrl).trimmed();
        e.title = at(r, cTitle).trimmed();
        if (e.title.isEmpty()) {
            e.title = hostOf(e.url); // Firefox has no title column
        }
        e.username = at(r, cUser);
        if (e.username.isEmpty()) {
            e.username = at(r, cAltUser);
        }
        e.password = at(r, cPass);
        e.notes = at(r, cNotes);
        e.otpAuth = normaliseOtp(at(r, cOtp), e.title);
        if (e.password.isEmpty() && e.otpAuth.isEmpty() && e.notes.isEmpty()) {
            ++result.skipped;
            continue;
        }
        result.entries.append(e);
    }
    return result;
}

ParseResult parseBitwardenJson(const QJsonObject &root)
{
    ParseResult result;
    if (root.value(u"encrypted"_s).toBool()) {
        result.error = u"this Bitwarden export is encrypted; export as unencrypted JSON or CSV"_s;
        return result;
    }
    for (const QJsonValue &v : root.value(u"items"_s).toArray()) {
        const QJsonObject item = v.toObject();
        const int type = item.value(u"type"_s).toInt();
        Entry e;
        e.title = item.value(u"name"_s).toString();
        e.notes = item.value(u"notes"_s).toString();
        if (type == 1) { // login
            const QJsonObject login = item.value(u"login"_s).toObject();
            e.username = login.value(u"username"_s).toString();
            e.password = login.value(u"password"_s).toString();
            e.otpAuth = normaliseOtp(login.value(u"totp"_s).toString(), e.title);
            const QJsonArray uris = login.value(u"uris"_s).toArray();
            if (!uris.isEmpty()) {
                e.url = uris.first().toObject().value(u"uri"_s).toString();
            }
            result.skippedPasskeys += int(login.value(u"fido2Credentials"_s).toArray().size());
        } else if (type != 2) { // 2 = secure note; cards and identities are skipped
            ++result.skipped;
            continue;
        }
        if (e.password.isEmpty() && e.otpAuth.isEmpty() && e.notes.isEmpty()) {
            ++result.skipped;
            continue;
        }
        result.entries.append(e);
    }
    return result;
}

// FIDO CXF 1.0 §3.3.3 BasicAuth, §3.3.11 Note, §3.3.16 TOTP, §3.2.4 CredentialScope.
ParseResult parseCxf(const QJsonObject &root)
{
    ParseResult result;
    if (root.value(u"version"_s).toObject().value(u"major"_s).toInt() != 1) {
        result.error = u"unsupported CXF version"_s;
        return result;
    }
    auto field = [](const QJsonObject &o, const QString &name) {
        return o.value(name).toObject().value(u"value"_s).toString(); // EditableField.value
    };
    for (const QJsonValue &acc : root.value(u"accounts"_s).toArray()) {
        for (const QJsonValue &iv : acc.toObject().value(u"items"_s).toArray()) {
            const QJsonObject item = iv.toObject();
            const QJsonArray urls = item.value(u"scope"_s).toObject().value(u"urls"_s).toArray();
            const QString url = urls.isEmpty() ? QString() : urls.first().toString();
            QList<Entry> logins;
            QString notes;
            QString otp;
            for (const QJsonValue &cv : item.value(u"credentials"_s).toArray()) {
                const QJsonObject c = cv.toObject();
                const QString type = c.value(u"type"_s).toString();
                if (type == u"basic-auth") {
                    Entry e;
                    e.username = field(c, u"username"_s);
                    e.password = field(c, u"password"_s);
                    logins.append(e);
                } else if (type == u"note") {
                    notes += (notes.isEmpty() ? QString() : u"\n\n"_s) + field(c, u"content"_s);
                } else if (type == u"totp") {
                    const QString alg = c.value(u"algorithm"_s).toString(u"sha1"_s).toLower();
                    if (alg != u"sha1" && alg != u"sha256" && alg != u"sha512") {
                        continue; // §3.3.16: importers MUST ignore unknown algorithms
                    }
                    const QString issuer = c.value(u"issuer"_s).toString();
                    const QString account = c.value(u"username"_s).toString();
                    QUrl u;
                    u.setScheme(u"otpauth"_s);
                    u.setHost(u"totp"_s);
                    u.setPath(u"/"_s + (issuer.isEmpty() ? account : issuer + u":"_s + account));
                    QUrlQuery q;
                    q.addQueryItem(u"secret"_s, c.value(u"secret"_s).toString());
                    if (!issuer.isEmpty()) {
                        q.addQueryItem(u"issuer"_s, issuer);
                    }
                    q.addQueryItem(u"algorithm"_s, alg.toUpper());
                    q.addQueryItem(u"digits"_s, QString::number(c.value(u"digits"_s).toInt(6)));
                    q.addQueryItem(u"period"_s, QString::number(c.value(u"period"_s).toInt(30)));
                    u.setQuery(q);
                    otp = u.toString(QUrl::FullyEncoded);
                } else if (type == u"passkey") {
                    ++result.skippedPasskeys;
                } else {
                    ++result.skipped; // cards, documents, SSH keys, ... (§2.1.1: ignore)
                }
            }
            if (logins.isEmpty() && (!notes.isEmpty() || !otp.isEmpty())) {
                logins.append(Entry{}); // secure note / TOTP-only item
            }
            for (Entry &e : logins) {
                e.title = item.value(u"title"_s).toString();
                e.url = url;
                e.notes = notes;
                e.otpAuth = otp;
                if (e.password.isEmpty() && e.otpAuth.isEmpty() && e.notes.isEmpty()) {
                    ++result.skipped;
                    continue;
                }
                result.entries.append(e);
            }
        }
    }
    return result;
}

std::optional<QJsonObject> jsonObject(const QByteArray &content)
{
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(content, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return std::nullopt;
    }
    return doc.object();
}

} // namespace

const QList<FormatInfo> &formats()
{
    static const QList<FormatInfo> list = {
        {Format::Apple, u"apple"_s, u"Apple Passwords / iCloud Keychain (CSV)"_s, u"Apple Passwords"_s, u"iCloud Passwords"_s,
         u"On a Mac: Passwords > File > Export All Passwords to File."_s},
        {Format::Firefox, u"firefox"_s, u"Firefox (CSV)"_s, u"Firefox"_s, u"Firefox Passwords"_s,
         u"Firefox: open about:logins, menu (…) > Export Passwords."_s},
        {Format::Chromium, u"chromium"_s, u"Chrome / Chromium / Edge / Brave / Google Password Manager (CSV)"_s,
         u"Chrome"_s, u"Chrome Passwords"_s, u"Chrome: Google Password Manager > Settings > Export passwords."_s},
        {Format::BitwardenCsv, u"bitwarden-csv"_s, u"Bitwarden / Vaultwarden (CSV)"_s, u"Bitwarden"_s, u"Bitwarden"_s,
         u"Bitwarden: Tools > Export vault > File format .csv."_s},
        {Format::BitwardenJson, u"bitwarden-json"_s, u"Bitwarden / Vaultwarden (JSON, unencrypted)"_s, u"Bitwarden"_s, u"Bitwarden"_s,
         u"Bitwarden: Tools > Export vault > File format .json (not the encrypted variant)."_s},
        {Format::KeePassXC, u"keepassxc"_s, u"KeePassXC (CSV)"_s, u"KeePassXC"_s, u"KeePassXC"_s,
         u"KeePassXC: Database > Export > CSV File."_s},
        {Format::OnePassword, u"1password"_s, u"1Password (CSV)"_s, u"1Password"_s, u"1Password"_s,
         u"1Password 8: File > Export > choose account > CSV."_s},
        {Format::ProtonPass, u"protonpass"_s, u"Proton Pass (CSV)"_s, u"Proton Pass"_s, u"Proton Pass"_s,
         u"Proton Pass: Settings > Export > CSV."_s},
        {Format::Cxf, u"cxf"_s, u"FIDO Credential Exchange Format (CXF, JSON)"_s, u"CXF"_s, u"Imported Passwords"_s,
         u"Any password manager exporting FIDO CXF 1.0 JSON."_s},
    };
    return list;
}

const FormatInfo &info(Format f)
{
    for (const FormatInfo &i : formats()) {
        if (i.format == f) {
            return i;
        }
    }
    static const FormatInfo unknown{Format::Auto, u"auto"_s, u"Unknown"_s, u"Unknown"_s, u"Imported Passwords"_s, {}};
    return unknown;
}

std::optional<Format> formatFromId(const QString &id)
{
    if (id == u"auto") {
        return Format::Auto;
    }
    for (const FormatInfo &i : formats()) {
        if (i.id == id) {
            return i.format;
        }
    }
    return std::nullopt;
}

QString normaliseOtp(const QString &value, const QString &label)
{
    const QString v = value.trimmed();
    if (v.isEmpty() || v.startsWith(u"otpauth://", Qt::CaseInsensitive)) {
        return v;
    }
    // Bare Base32 secret (possibly with spaces): build a standard key URI.
    QString secret = v;
    secret.remove(QLatin1Char(' '));
    QUrl u;
    u.setScheme(u"otpauth"_s);
    u.setHost(u"totp"_s);
    u.setPath(u"/"_s + (label.isEmpty() ? u"imported"_s : label));
    QUrlQuery q;
    q.addQueryItem(u"secret"_s, secret.toUpper());
    u.setQuery(q);
    return u.toString(QUrl::FullyEncoded);
}

Format detect(const QByteArray &content)
{
    if (auto obj = jsonObject(content)) {
        if (obj->contains(u"accounts"_s) && obj->value(u"version"_s).isObject()) {
            return Format::Cxf;
        }
        if (obj->contains(u"items"_s) && obj->contains(u"encrypted"_s)) {
            return Format::BitwardenJson;
        }
        return Format::Auto;
    }
    QStringDecoder decoder(QStringDecoder::Utf8);
    QString head = decoder(content.left(4096));
    const qsizetype nl = head.indexOf(QLatin1Char('\n'));
    if (nl >= 0) {
        head.truncate(nl);
    }
    const auto rows = parseCsv(head);
    return rows && !rows->isEmpty() ? detectCsv(rows->first()) : Format::Auto;
}

ParseResult parse(const QByteArray &content, Format f, Format *detected)
{
    ParseResult result;
    if (f == Format::Auto) {
        f = detect(content);
    }
    if (detected) {
        *detected = f;
    }
    if (f == Format::Auto) {
        result.error = u"unknown file format"_s;
        return result;
    }
    if (f == Format::BitwardenJson || f == Format::Cxf) {
        const auto obj = jsonObject(content);
        if (!obj) {
            result.error = u"not a valid JSON file"_s;
            return result;
        }
        return f == Format::Cxf ? parseCxf(*obj) : parseBitwardenJson(*obj);
    }
    QStringDecoder decoder(QStringDecoder::Utf8);
    const QString text = decoder(content);
    if (decoder.hasError()) {
        result.error = u"file is not valid UTF-8"_s;
        return result;
    }
    const auto rows = parseCsv(text);
    if (!rows) {
        result.error = u"malformed CSV (unterminated quote)"_s;
        return result;
    }
    if (rows->isEmpty()) {
        result.error = u"file is empty"_s;
        return result;
    }
    return parseCsvProfile(*rows, f);
}

} // namespace kwimport
