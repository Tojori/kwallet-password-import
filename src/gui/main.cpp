// SPDX-License-Identifier: LGPL-2.1-or-later
#include "import_wizard.h"

#include <KAboutData>
#include <KLocalizedString>

#include <QApplication>
#include <QCommandLineParser>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    KLocalizedString::setApplicationDomain("kwallet-password-import");

    KAboutData about(QStringLiteral("kwallet-password-import"), i18n("Password Import for KDE Wallet"),
                     QStringLiteral(KWIMPORT_VERSION),
                     i18n("Imports passwords exported from Apple Passwords, browsers and password managers into KDE Wallet."),
                     KAboutLicense::LGPL_V2_1, QString(), QString(), QStringLiteral("https://github.com/Tojori/kwallet-password-import"));
    about.setDesktopFileName(QStringLiteral("io.github.tojori.kwalletpasswordimport"));
    KAboutData::setApplicationData(about);
    QApplication::setWindowIcon(QIcon::fromTheme(QStringLiteral("kwalletmanager")));

    QCommandLineParser parser;
    about.setupCommandLine(&parser);
    parser.addPositionalArgument(QStringLiteral("file"), i18n("Export file to import (optional)."));
    parser.process(app);
    about.processCommandLine(&parser);

    const QStringList args = parser.positionalArguments();
    kwimport::ImportWizard wizard(args.isEmpty() ? QString() : args.first());
    wizard.show();
    return app.exec();
}
