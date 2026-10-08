// SPDX-License-Identifier: LGPL-2.1-or-later
// kwallet-icloud-import: one-way import of an Apple Passwords CSV export into
// a KDE wallet folder. Repeatable: changed entries are updated, unchanged ones
// left alone. Never prints passwords.
#include "apple_csv.h"
#include "importer.h"
#include "kwallet_folder.h"

#include <KWallet>

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFile>
#include <QTextStream>

#include <cstdio>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace kwimport;

namespace {

QTextStream &out()
{
    static QTextStream s(stdout);
    return s;
}

QTextStream &err()
{
    static QTextStream s(stderr);
    return s;
}

void printKeys(const QString &label, const QStringList &keys, bool verbose)
{
    out() << label << ": " << keys.size() << Qt::endl;
    if (verbose) {
        for (const QString &k : keys) {
            out() << "    " << k << Qt::endl;
        }
    }
}

bool confirm(const QString &question)
{
    out() << question << " [yes/NO] " << Qt::flush;
    QTextStream in(stdin);
    return in.readLine().trimmed().compare(QLatin1String("yes"), Qt::CaseInsensitive) == 0;
}

// Overwrites the file with zeros, syncs and unlinks it. On copy-on-write file
// systems (btrfs, ZFS) and SSDs old blocks may survive; see README.
bool shredFile(const QString &path)
{
    const QByteArray p = QFile::encodeName(path);
    const int fd = ::open(p.constData(), O_WRONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) {
        return false;
    }
    struct stat st {};
    bool ok = ::fstat(fd, &st) == 0 && S_ISREG(st.st_mode);
    if (ok) {
        static const char zeros[4096] = {};
        off_t left = st.st_size;
        while (ok && left > 0) {
            const ssize_t n = ::write(fd, zeros, size_t(std::min<off_t>(left, sizeof(zeros))));
            ok = n > 0;
            left -= n;
        }
        ok = ok && ::fsync(fd) == 0;
    }
    ::close(fd);
    return ok && ::unlink(p.constData()) == 0;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("kwallet-icloud-import"));
    app.setApplicationVersion(QStringLiteral(KWIMPORT_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral(
        "Imports an Apple Passwords CSV export (macOS: Passwords > File > Export All Passwords to File) "
        "into a folder of your KDE wallet. Re-running updates changed entries."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("csv"), QStringLiteral("The exported CSV file."));
    QCommandLineOption folderOpt(QStringLiteral("folder"), QStringLiteral("Wallet folder (default: \"iCloud Passwords\")."),
                                 QStringLiteral("name"), QStringLiteral("iCloud Passwords"));
    QCommandLineOption walletOpt(QStringLiteral("wallet"), QStringLiteral("Wallet name (default: your local wallet)."),
                                 QStringLiteral("name"));
    QCommandLineOption dryRunOpt(QStringLiteral("dry-run"), QStringLiteral("Only show what would change."));
    QCommandLineOption pruneOpt(QStringLiteral("prune"),
                                QStringLiteral("Delete entries imported earlier that are no longer in the export."));
    QCommandLineOption yesOpt(QStringLiteral("yes"), QStringLiteral("Do not ask before --prune deletes entries."));
    QCommandLineOption deleteOpt(QStringLiteral("delete-csv"),
                                 QStringLiteral("Overwrite and delete the CSV after a successful import."));
    QCommandLineOption verboseOpt({QStringLiteral("l"), QStringLiteral("list")},
                                  QStringLiteral("List entry names (never passwords)."));
    parser.addOptions({folderOpt, walletOpt, dryRunOpt, pruneOpt, yesOpt, deleteOpt, verboseOpt});
    parser.process(app);

    if (parser.positionalArguments().size() != 1) {
        parser.showHelp(2);
    }
    const QString csvPath = parser.positionalArguments().first();
    QFile f(csvPath);
    if (!f.open(QIODevice::ReadOnly)) {
        err() << "cannot read " << csvPath << ": " << f.errorString() << Qt::endl;
        return 2;
    }
    if (f.size() > 64 * 1024 * 1024) {
        err() << "file too large" << Qt::endl;
        return 2;
    }
    const ParseResult parsed = parseAppleExport(f.readAll());
    f.close();
    if (!parsed.error.isEmpty()) {
        err() << csvPath << ": " << parsed.error << Qt::endl;
        return 2;
    }
    out() << "read " << parsed.entries.size() << " entries from the export";
    if (parsed.skipped) {
        out() << " (" << parsed.skipped << " empty rows ignored)";
    }
    out() << Qt::endl;

    const bool dryRun = parser.isSet(dryRunOpt);
    const QString wallet = parser.isSet(walletOpt) ? parser.value(walletOpt) : KWallet::Wallet::LocalWallet();
    KWalletFolder folder;
    QString error;
    if (!folder.open(wallet, parser.value(folderOpt), !dryRun, &error)) {
        err() << error << Qt::endl;
        return 3;
    }

    const Plan plan = makePlan(parsed.entries, folder);
    const bool verbose = parser.isSet(verboseOpt);
    out() << "wallet \"" << wallet << "\", folder \"" << parser.value(folderOpt) << "\":" << Qt::endl;
    printKeys(QStringLiteral("  new"), plan.add, verbose);
    printKeys(QStringLiteral("  changed"), plan.update, verbose);
    printKeys(QStringLiteral("  unchanged"), plan.unchanged, false);
    printKeys(QStringLiteral("  no longer in the export"), plan.stale, verbose || parser.isSet(pruneOpt));
    if (dryRun) {
        out() << "dry run: nothing was written" << Qt::endl;
        return 0;
    }

    bool prune = parser.isSet(pruneOpt) && !plan.stale.isEmpty();
    if (prune && !parser.isSet(yesOpt)) {
        prune = confirm(QStringLiteral("Delete the %1 entries listed above from the wallet?").arg(plan.stale.size()));
    }
    const ApplyResult result = apply(plan, folder, prune);
    out() << "written: " << result.written << ", removed: " << result.removed << ", failed: " << result.failed << Qt::endl;
    if (result.failed) {
        err() << "some entries could not be written; the CSV was kept" << Qt::endl;
        return 4;
    }

    if (parser.isSet(deleteOpt)) {
        if (shredFile(csvPath)) {
            out() << "deleted " << csvPath << Qt::endl;
        } else {
            err() << "could not delete " << csvPath << " - delete it yourself!" << Qt::endl;
            return 5;
        }
    } else {
        out() << "Reminder: the CSV contains all passwords in plain text. Delete it now (or use --delete-csv)." << Qt::endl;
    }
    return 0;
}
