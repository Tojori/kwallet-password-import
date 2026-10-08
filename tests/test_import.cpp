// SPDX-License-Identifier: LGPL-2.1-or-later
#include "apple_csv.h"
#include "importer.h"

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
        const ParseResult r = parseAppleExport(csv);
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
        const ParseResult r = parseAppleExport("Password,Username,URL\nsecret,bob,https://a.example\n");
        QVERIFY(r.error.isEmpty());
        QCOMPARE(r.entries.first().password, QStringLiteral("secret"));
        QCOMPARE(r.entries.first().username, QStringLiteral("bob"));
    }

    void rejectsForeignFiles()
    {
        QVERIFY(!parseAppleExport("name,value\na,b\n").error.isEmpty());
        QVERIFY(!parseAppleExport("").error.isEmpty());
        QVERIFY(!parseAppleExport("Title,URL,Password\n\xff\xfe broken\n").error.isEmpty()); // not UTF-8
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
        const QList<AppleEntry> entries{{QStringLiteral("A"), QStringLiteral("https://a.example"), QStringLiteral("u"), QStringLiteral("pw")}};
        Plan p = makePlan(entries, folder);
        QCOMPARE(p.add.size(), 1);
        QCOMPARE(apply(p, folder, false).written, 1);
        QCOMPARE(folder.entries.value(QStringLiteral("a.example (u)")).value(QStringLiteral("Password")), QStringLiteral("pw"));

        p = makePlan(entries, folder);
        QCOMPARE(p.unchanged.size(), 1);
        QVERIFY(p.add.isEmpty() && p.update.isEmpty());
    }

    void changedPasswordIsUpdated()
    {
        FakeFolder folder;
        apply(makePlan({{QStringLiteral("A"), QStringLiteral("https://a.example"), QStringLiteral("u"), QStringLiteral("old")}}, folder), folder, false);
        const Plan p = makePlan({{QStringLiteral("A"), QStringLiteral("https://a.example"), QStringLiteral("u"), QStringLiteral("new")}}, folder);
        QCOMPARE(p.update, QStringList{QStringLiteral("a.example (u)")});
        apply(p, folder, false);
        QCOMPARE(folder.entries.value(QStringLiteral("a.example (u)")).value(QStringLiteral("Password")), QStringLiteral("new"));
    }

    void duplicatesAreNumberedStably()
    {
        FakeFolder folder;
        const QList<AppleEntry> entries{
            {QStringLiteral("A1"), QStringLiteral("https://a.example"), QStringLiteral("u"), QStringLiteral("one")},
            {QStringLiteral("A2"), QStringLiteral("https://a.example"), QStringLiteral("u"), QStringLiteral("two")},
        };
        apply(makePlan(entries, folder), folder, false);
        QCOMPARE(folder.entries.size(), 2);
        QVERIFY(folder.entries.contains(QStringLiteral("a.example (u) [2]")));
        QCOMPARE(makePlan(entries, folder).unchanged.size(), 2); // same keys on re-import
    }

    void pruneOnlyTouchesOwnEntries()
    {
        FakeFolder folder;
        folder.entries.insert(QStringLiteral("manual entry"), EntryMap{{QStringLiteral("Password"), QStringLiteral("x")}});
        apply(makePlan({{QStringLiteral("A"), QStringLiteral("https://a.example"), QStringLiteral("u"), QStringLiteral("pw")},
                        {QStringLiteral("B"), QStringLiteral("https://b.example"), QStringLiteral("u"), QStringLiteral("pw")}},
                       folder),
              folder, false);
        // B was deleted in iCloud
        const Plan p = makePlan({{QStringLiteral("A"), QStringLiteral("https://a.example"), QStringLiteral("u"), QStringLiteral("pw")}}, folder);
        QCOMPARE(p.stale, QStringList{QStringLiteral("b.example (u)")}); // not "manual entry"
        QCOMPARE(apply(p, folder, false).removed, 0);                      // without --prune nothing is deleted
        QCOMPARE(apply(p, folder, true).removed, 1);
        QVERIFY(folder.entries.contains(QStringLiteral("manual entry")));
    }

    void writeFailuresAreReported()
    {
        FakeFolder folder;
        folder.failWrites = true;
        const ApplyResult r = apply(makePlan({{QStringLiteral("A"), QStringLiteral("https://a.example"), QStringLiteral("u"), QStringLiteral("pw")}}, folder),
                                    folder, false);
        QCOMPARE(r.failed, 1);
        QCOMPARE(r.written, 0);
    }
};

QTEST_GUILESS_MAIN(TestImport)
#include "test_import.moc"
