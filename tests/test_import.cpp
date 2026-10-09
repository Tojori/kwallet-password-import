// SPDX-License-Identifier: LGPL-2.1-or-later
#include "csv.h"
#include "formats.h"
#include "importer.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <QTest>

using namespace kwimport;

namespace {

class FakeFolder : public WalletFolder
{
public:
    QMap<QString, EntryMap> entries;
    bool failWrites = false;
    QStringList keys() override { return entries.keys(); }
    std::optional<EntryMap> readMap(const QString &key) override
    {
        if (!entries.contains(key)) {
            return std::nullopt;
        }
        return entries.value(key);
    }
    bool writeMap(const QString &key, const EntryMap &value) override
    {
        if (failWrites) {
            return false;
        }
        entries.insert(key, value);
        return true;
    }
    bool remove(const QString &key) override { return entries.remove(key) > 0; }
};

const QByteArray Header = "Title,URL,Username,Password,Notes,OTPAuth\n";
const QString Apple = QStringLiteral("Apple Passwords");

} // namespace

class TestImport : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void csvQuotingAndNewlines()
    {
        const auto rows = parseCsv(QStringLiteral("a,\"b,c\",\"say \"\"hi\"\"\",\"line1\nline2\"\r\nx,,z\n\n"));
        QVERIFY(rows);
        QCOMPARE(rows->size(), 2);
        QCOMPARE(rows->at(0), QStringList({QStringLiteral("a"), QStringLiteral("b,c"), QStringLiteral("say \"hi\""),
                                           QStringLiteral("line1\nline2")}));
        QCOMPARE(rows->at(1), QStringList({QStringLiteral("x"), QString(), QStringLiteral("z")}));
    }

    void csvRejectsUnterminatedQuote()
    {
        QVERIFY(!parseCsv(QStringLiteral("a,\"b\nc")));
    }

    void csvLastLineWithoutNewline()
    {
        const auto rows = parseCsv(QStringLiteral("a,b\nc,d"));
        QCOMPARE(rows->size(), 2);
        QCOMPARE(rows->at(1).at(1), QStringLiteral("d"));
    }

    void appleExport()
    {
        const QByteArray csv = "\xEF\xBB\xBF" + Header
            + "example.com (alice),https://example.com/login,alice,p@ss\"w,\"multi\nline\",otpauth://totp/x?secret=ABC\n"
              "Router,http://192.168.1.1,,admin,,\n"
              ",,,,,\n";
        const ParseResult r = parse(csv, Format::Auto);
        QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
        QCOMPARE(r.entries.size(), 2);
        QCOMPARE(r.skipped, 1);
        QCOMPARE(r.entries.at(0).username, QStringLiteral("alice"));
        QCOMPARE(r.entries.at(0).password, QStringLiteral("p@ss\"w"));
        QCOMPARE(r.entries.at(0).notes, QStringLiteral("multi\nline"));
        QCOMPARE(r.entries.at(0).otpAuth, QStringLiteral("otpauth://totp/x?secret=ABC"));
    }

    void columnsFoundByName()
    {
        const ParseResult r = parse("Password,Username,URL\nsecret,bob,https://a.example\n", Format::Apple);
        QVERIFY(r.error.isEmpty());
        QCOMPARE(r.entries.first().password, QStringLiteral("secret"));
        QCOMPARE(r.entries.first().username, QStringLiteral("bob"));
    }

    void rejectsForeignFiles()
    {
        QVERIFY(!parse("name,value\na,b\n", Format::Auto).error.isEmpty());
        QVERIFY(!parse("", Format::Auto).error.isEmpty());
        QVERIFY(!parse("Title,URL,Username,Password,Notes,OTPAuth\n\xff\xfe broken\n", Format::Apple).error.isEmpty()); // not UTF-8
    }

    void entryKeys()
    {
        QCOMPARE(entryKey({QStringLiteral("T"), QStringLiteral("https://Login.Example.com/x"), QStringLiteral("alice")}),
                 QStringLiteral("login.example.com (alice)"));
        QCOMPARE(entryKey({QStringLiteral("My Router"), QString(), QString()}), QStringLiteral("My Router"));
        QCOMPARE(entryKey({QStringLiteral("a/b"), QString(), QStringLiteral("u")}), QStringLiteral("a_b (u)"));
    }

    void firstImportThenUnchanged()
    {
        FakeFolder folder;
        const QList<Entry> entries{{QStringLiteral("A"), QStringLiteral("https://a.example"), QStringLiteral("u"), QStringLiteral("pw")}};
        Plan p = makePlan(entries, folder, Apple);
        QCOMPARE(p.add.size(), 1);
        QCOMPARE(apply(p, folder, false).written, 1);
        QCOMPARE(folder.entries.value(QStringLiteral("a.example (u)")).value(QStringLiteral("Password")), QStringLiteral("pw"));

        p = makePlan(entries, folder, Apple);
        QCOMPARE(p.unchanged.size(), 1);
        QVERIFY(p.add.isEmpty() && p.update.isEmpty());
    }

    void changedPasswordIsUpdated()
    {
        FakeFolder folder;
        apply(makePlan({{QStringLiteral("A"), QStringLiteral("https://a.example"), QStringLiteral("u"), QStringLiteral("old")}}, folder, Apple), folder, false);
        const Plan p = makePlan({{QStringLiteral("A"), QStringLiteral("https://a.example"), QStringLiteral("u"), QStringLiteral("new")}}, folder, Apple);
        QCOMPARE(p.update, QStringList{QStringLiteral("a.example (u)")});
        apply(p, folder, false);
        QCOMPARE(folder.entries.value(QStringLiteral("a.example (u)")).value(QStringLiteral("Password")), QStringLiteral("new"));
    }

    void duplicatesAreNumberedStably()
    {
        FakeFolder folder;
        const QList<Entry> entries{
            {QStringLiteral("A1"), QStringLiteral("https://a.example"), QStringLiteral("u"), QStringLiteral("one")},
            {QStringLiteral("A2"), QStringLiteral("https://a.example"), QStringLiteral("u"), QStringLiteral("two")},
        };
        apply(makePlan(entries, folder, Apple), folder, false);
        QCOMPARE(folder.entries.size(), 2);
        QVERIFY(folder.entries.contains(QStringLiteral("a.example (u) [2]")));
        QCOMPARE(makePlan(entries, folder, Apple).unchanged.size(), 2); // same keys on re-import
    }

    void pruneOnlyTouchesOwnEntries()
    {
        FakeFolder folder;
        folder.entries.insert(QStringLiteral("manual entry"), EntryMap{{QStringLiteral("Password"), QStringLiteral("x")}});
        apply(makePlan({{QStringLiteral("A"), QStringLiteral("https://a.example"), QStringLiteral("u"), QStringLiteral("pw")},
                        {QStringLiteral("B"), QStringLiteral("https://b.example"), QStringLiteral("u"), QStringLiteral("pw")}},
                       folder, Apple),
              folder, false);
        // B was deleted in iCloud
        const Plan p = makePlan({{QStringLiteral("A"), QStringLiteral("https://a.example"), QStringLiteral("u"), QStringLiteral("pw")}}, folder, Apple);
        QCOMPARE(p.stale, QStringList{QStringLiteral("b.example (u)")}); // not "manual entry"
        QCOMPARE(apply(p, folder, false).removed, 0);                      // without --prune nothing is deleted
        QCOMPARE(apply(p, folder, true).removed, 1);
        QVERIFY(folder.entries.contains(QStringLiteral("manual entry")));
    }

    void detectsAllCsvFormats_data()
    {
        QTest::addColumn<QByteArray>("csv");
        QTest::addColumn<int>("format");
        QTest::addColumn<QString>("user");
        QTest::addColumn<QString>("password");
        QTest::addColumn<QString>("url");
        QTest::newRow("apple") << QByteArray("Title,URL,Username,Password,Notes,OTPAuth\nT,https://a.example,u,p1,,\n")
                               << int(Format::Apple) << "u" << "p1" << "https://a.example";
        QTest::newRow("firefox") << QByteArray("\"url\",\"username\",\"password\",\"httpRealm\",\"formActionOrigin\",\"guid\",\"timeCreated\",\"timeLastUsed\",\"timePasswordChanged\"\n"
                                               "\"https://f.example\",\"fu\",\"fp\",,\"https://f.example\",\"{1}\",\"1\",\"1\",\"1\"\n")
                                 << int(Format::Firefox) << "fu" << "fp" << "https://f.example";
        QTest::newRow("chromium") << QByteArray("name,url,username,password,note\nsite,https://c.example/,cu,cp,n\n")
                                  << int(Format::Chromium) << "cu" << "cp" << "https://c.example/";
        QTest::newRow("bitwarden") << QByteArray("folder,favorite,type,name,notes,fields,reprompt,login_uri,login_username,login_password,login_totp\n"
                                                 ",,login,B,,,0,https://b.example,bu,bp,JBSWY3DPEHPK3PXP\n")
                                   << int(Format::BitwardenCsv) << "bu" << "bp" << "https://b.example";
        QTest::newRow("keepassxc") << QByteArray("\"Group\",\"Title\",\"Username\",\"Password\",\"URL\",\"Notes\",\"TOTP\",\"Icon\",\"Last Modified\",\"Created\"\n"
                                                 "\"Root\",\"K\",\"ku\",\"kp\",\"https://k.example\",\"\",\"\",\"0\",\"\",\"\"\n")
                                   << int(Format::KeePassXC) << "ku" << "kp" << "https://k.example";
        QTest::newRow("1password") << QByteArray("Title,Url,Username,Password,OTPAuth,Favorite,Archived,Tags,Notes\nO,https://o.example,ou,op,,false,false,,\n")
                                   << int(Format::OnePassword) << "ou" << "op" << "https://o.example";
        QTest::newRow("protonpass") << QByteArray("name,url,email,username,password,note,totp,createTime,modifyTime,vault\nP,https://p.example,pe@x.example,,pp,,,0,0,Personal\n")
                                    << int(Format::ProtonPass) << "pe@x.example" << "pp" << "https://p.example";
    }
    void detectsAllCsvFormats()
    {
        QFETCH(QByteArray, csv);
        QFETCH(int, format);
        QFETCH(QString, user);
        QFETCH(QString, password);
        QFETCH(QString, url);
        QCOMPARE(int(detect(csv)), format);
        Format used = Format::Auto;
        const ParseResult r = parse(csv, Format::Auto, &used);
        QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
        QCOMPARE(int(used), format);
        QCOMPARE(r.entries.size(), 1);
        QCOMPARE(r.entries.first().username, user);
        QCOMPARE(r.entries.first().password, password);
        QCOMPARE(r.entries.first().url, url);
        QVERIFY(!r.entries.first().title.isEmpty()); // Firefox: derived from the host
    }

    void bareTotpSecretBecomesUri()
    {
        const QString uri = normaliseOtp(QStringLiteral("jbsw y3dp ehpk 3pxp"), QStringLiteral("Site"));
        QVERIFY(uri.startsWith(QStringLiteral("otpauth://totp/Site?secret=JBSWY3DPEHPK3PXP")));
        QCOMPARE(normaliseOtp(QStringLiteral("otpauth://totp/x?secret=A"), {}), QStringLiteral("otpauth://totp/x?secret=A"));
    }

    void bitwardenJson()
    {
        const QJsonObject root{
            {QStringLiteral("encrypted"), false},
            {QStringLiteral("items"), QJsonArray{
                QJsonObject{{QStringLiteral("type"), 1}, {QStringLiteral("name"), QStringLiteral("Login")},
                            {QStringLiteral("login"), QJsonObject{{QStringLiteral("username"), QStringLiteral("u")},
                                                                  {QStringLiteral("password"), QStringLiteral("p")},
                                                                  {QStringLiteral("uris"), QJsonArray{QJsonObject{{QStringLiteral("uri"), QStringLiteral("https://bw.example")}}}},
                                                                  {QStringLiteral("fido2Credentials"), QJsonArray{QJsonObject{}}}}}},
                QJsonObject{{QStringLiteral("type"), 2}, {QStringLiteral("name"), QStringLiteral("Note")}, {QStringLiteral("notes"), QStringLiteral("secret note")}},
                QJsonObject{{QStringLiteral("type"), 3}, {QStringLiteral("name"), QStringLiteral("Card")}},
            }}};
        const QByteArray json = QJsonDocument(root).toJson();
        QCOMPARE(detect(json), Format::BitwardenJson);
        const ParseResult r = parse(json, Format::Auto);
        QCOMPARE(r.entries.size(), 2);
        QCOMPARE(r.skipped, 1);
        QCOMPARE(r.skippedPasskeys, 1);
        QCOMPARE(r.entries.first().url, QStringLiteral("https://bw.example"));

        const QByteArray encrypted = R"({"encrypted":true,"items":[]})";
        QVERIFY(!parse(encrypted, Format::Auto).error.isEmpty());
    }

    void cxfPasswordsTotpAndNotes()
    {
        auto field = [](const QString &v, const QString &type) {
            return QJsonObject{{QStringLiteral("fieldType"), type}, {QStringLiteral("value"), v}};
        };
        const QJsonObject item{
            {QStringLiteral("id"), QStringLiteral("aXRlbQ")},
            {QStringLiteral("title"), QStringLiteral("Example")},
            {QStringLiteral("scope"), QJsonObject{{QStringLiteral("urls"), QJsonArray{QStringLiteral("https://cxf.example/login")}},
                                                  {QStringLiteral("androidApps"), QJsonArray{}}}},
            {QStringLiteral("credentials"), QJsonArray{
                QJsonObject{{QStringLiteral("type"), QStringLiteral("basic-auth")},
                            {QStringLiteral("username"), field(QStringLiteral("cu"), QStringLiteral("string"))},
                            {QStringLiteral("password"), field(QStringLiteral("cp"), QStringLiteral("concealed-string"))}},
                QJsonObject{{QStringLiteral("type"), QStringLiteral("totp")}, {QStringLiteral("secret"), QStringLiteral("JBSWY3DPEHPK3PXP")},
                            {QStringLiteral("period"), 30}, {QStringLiteral("digits"), 6}, {QStringLiteral("algorithm"), QStringLiteral("sha1")},
                            {QStringLiteral("issuer"), QStringLiteral("Example")}},
                QJsonObject{{QStringLiteral("type"), QStringLiteral("note")}, {QStringLiteral("content"), field(QStringLiteral("hello"), QStringLiteral("string"))}},
                QJsonObject{{QStringLiteral("type"), QStringLiteral("passkey")}},
                QJsonObject{{QStringLiteral("type"), QStringLiteral("totp")}, {QStringLiteral("secret"), QStringLiteral("X")},
                            {QStringLiteral("period"), 30}, {QStringLiteral("digits"), 6}, {QStringLiteral("algorithm"), QStringLiteral("md5")}},
            }}};
        const QJsonObject root{
            {QStringLiteral("version"), QJsonObject{{QStringLiteral("major"), 1}, {QStringLiteral("minor"), 0}}},
            {QStringLiteral("exporterRpId"), QStringLiteral("x.example")}, {QStringLiteral("exporterDisplayName"), QStringLiteral("X")},
            {QStringLiteral("timestamp"), 1}, {QStringLiteral("accounts"), QJsonArray{QJsonObject{{QStringLiteral("items"), QJsonArray{item}}}}}};
        const QByteArray json = QJsonDocument(root).toJson();
        QCOMPARE(detect(json), Format::Cxf);
        const ParseResult r = parse(json, Format::Auto);
        QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
        QCOMPARE(r.entries.size(), 1);
        const Entry &e = r.entries.first();
        QCOMPARE(e.username, QStringLiteral("cu"));
        QCOMPARE(e.password, QStringLiteral("cp"));
        QCOMPARE(e.url, QStringLiteral("https://cxf.example/login"));
        QCOMPARE(e.notes, QStringLiteral("hello"));
        QVERIFY(e.otpAuth.contains(QStringLiteral("secret=JBSWY3DPEHPK3PXP"))); // the md5 TOTP was ignored
        QVERIFY(e.otpAuth.contains(QStringLiteral("algorithm=SHA1")));
        QCOMPARE(r.skippedPasskeys, 1);
    }

    void pruneKeepsOtherSourcesAndAcceptsLegacyMarker()
    {
        FakeFolder folder;
        folder.entries.insert(QStringLiteral("old.example (u)"),
                              EntryMap{{QStringLiteral("Source"), QStringLiteral("Apple Passwords (kwallet-icloud-import)")}});
        folder.entries.insert(QStringLiteral("ff.example (u)"), EntryMap{{QStringLiteral("Source"), sourceMarker(QStringLiteral("Firefox"))}});
        const Plan p = makePlan({}, folder, Apple);
        QCOMPARE(p.stale, QStringList{QStringLiteral("old.example (u)")}); // legacy Apple marker yes, Firefox no
    }

    void writeFailuresAreReported()
    {
        FakeFolder folder;
        folder.failWrites = true;
        const ApplyResult r = apply(makePlan({{QStringLiteral("A"), QStringLiteral("https://a.example"), QStringLiteral("u"), QStringLiteral("pw")}}, folder, Apple),
                                    folder, false);
        QCOMPARE(r.failed, 1);
        QCOMPARE(r.written, 0);
    }
};

QTEST_GUILESS_MAIN(TestImport)
#include "test_import.moc"
