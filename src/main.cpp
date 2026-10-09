// SPDX-License-Identifier: LGPL-2.1-or-later
// kwallet-password-import: one-way import of password manager / browser
// exports into a KDE wallet folder. Repeatable: changed entries are updated,
// unchanged ones left alone. Never prints passwords.
#include "formats.h"
#include "importer.h"
#include "kwallet_folder.h"
#include "secure_delete.h"

#include <KWallet>

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFile>
#include <QTextStream>


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

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("kwallet-password-import"));
    app.setApplicationVersion(QStringLiteral(KWIMPORT_VERSION));

    QCommandLineParser parser;
    QString formatHelp;
    for (const FormatInfo &f : formats()) {
        formatHelp += QStringLiteral("\n  %1: %2\n      %3").arg(f.id, -15).arg(f.displayName, f.howToExport);
    }
    parser.setApplicationDescription(QStringLiteral(
        "Imports password exports into a folder of your KDE wallet. Re-running updates changed entries.\n"
        "Formats (recognised automatically):") + formatHelp);
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("file"), QStringLiteral("The exported file (CSV or JSON)."));
    QCommandLineOption formatOpt(QStringLiteral("format"), QStringLiteral("Format id (default: auto)."),
                                 QStringLiteral("id"), QStringLiteral("auto"));
    QCommandLineOption folderOpt(QStringLiteral("folder"), QStringLiteral("Wallet folder (default depends on the format)."),
                                 QStringLiteral("name"));
    QCommandLineOption walletOpt(QStringLiteral("wallet"), QStringLiteral("Wallet name (default: your local wallet)."),
                                 QStringLiteral("name"));
    QCommandLineOption dryRunOpt(QStringLiteral("dry-run"), QStringLiteral("Only show what would change."));
    QCommandLineOption pruneOpt(QStringLiteral("prune"),
                                QStringLiteral("Delete entries imported earlier that are no longer in the export."));
    QCommandLineOption yesOpt(QStringLiteral("yes"), QStringLiteral("Do not ask before --prune deletes entries."));
    QCommandLineOption deleteOpt({QStringLiteral("delete-file"), QStringLiteral("delete-csv")},
                                 QStringLiteral("Overwrite and delete the export file after a successful import."));
    QCommandLineOption verboseOpt({QStringLiteral("l"), QStringLiteral("list")},
                                  QStringLiteral("List entry names (never passwords)."));
    parser.addOptions({formatOpt, folderOpt, walletOpt, dryRunOpt, pruneOpt, yesOpt, deleteOpt, verboseOpt});
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
    const auto requested = formatFromId(parser.value(formatOpt));
    if (!requested) {
        err() << "unknown format " << parser.value(formatOpt) << Qt::endl;
        return 2;
    }
    Format format = Format::Auto;
    const ParseResult parsed = parse(f.readAll(), *requested, &format);
    f.close();
    if (!parsed.error.isEmpty()) {
        err() << csvPath << ": " << parsed.error << Qt::endl;
        return 2;
    }
    const FormatInfo &fmt = info(format);
    out() << "format: " << fmt.displayName << Qt::endl;
    out() << "read " << parsed.entries.size() << " entries";
    if (parsed.skipped) {
        out() << " (" << parsed.skipped << " empty or unsupported items ignored)";
    }
    out() << Qt::endl;
    if (parsed.skippedPasskeys) {
        out() << parsed.skippedPasskeys << " passkeys were not imported (use kpasskey for passkeys)" << Qt::endl;
    }
    const QString folderName = parser.isSet(folderOpt) ? parser.value(folderOpt) : fmt.defaultFolder;

    const bool dryRun = parser.isSet(dryRunOpt);
    const QString wallet = parser.isSet(walletOpt) ? parser.value(walletOpt) : KWallet::Wallet::LocalWallet();
    KWalletFolder folder;
    QString error;
    if (!folder.open(wallet, folderName, !dryRun, &error)) {
        err() << error << Qt::endl;
        return 3;
    }

    const Plan plan = makePlan(parsed.entries, folder, fmt.sourceName);
    const bool verbose = parser.isSet(verboseOpt);
    out() << "wallet \"" << wallet << "\", folder \"" << folderName << "\":" << Qt::endl;
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
        out() << "Reminder: the export contains all passwords in plain text. Delete it now (or use --delete-file)." << Qt::endl;
    }
    return 0;
}
