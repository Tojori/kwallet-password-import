// SPDX-License-Identifier: LGPL-2.1-or-later
#include "import_wizard.h"

#include "kwallet_folder.h"
#include "secure_delete.h"

#include <KLocalizedString>
#include <KMessageWidget>
#include <KWallet>

#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace kwimport {

namespace {

enum PageId { SourceId, TargetId, PreviewId, ResultId };

// Translated export instructions (the core library keeps English only).
QString howTo(Format f)
{
    switch (f) {
    case Format::Apple:
        return i18n("On a Mac: open <b>Passwords</b> › <b>File</b> › <b>Export All Passwords to File…</b>. "
                    "iPhone and iPad cannot export to a file.");
    case Format::Firefox:
        return i18n("In Firefox open <b>about:logins</b>, menu <b>…</b> › <b>Export Passwords…</b>.");
    case Format::Chromium:
        return i18n("In Chrome, Chromium, Edge or Brave: <b>Password Manager</b> › <b>Settings</b> › <b>Export passwords</b>.");
    case Format::BitwardenCsv:
    case Format::BitwardenJson:
        return i18n("In Bitwarden: <b>Tools</b> › <b>Export vault</b>, format <b>.csv</b> or <b>.json</b> "
                    "(not the encrypted variant).");
    case Format::KeePassXC:
        return i18n("In KeePassXC: <b>Database</b> › <b>Export</b> › <b>CSV File…</b>.");
    case Format::OnePassword:
        return i18n("In 1Password 8: <b>File</b> › <b>Export</b> › choose the account › <b>CSV</b>. "
                    "Please check the preview carefully; this format is supported on a best-effort basis.");
    case Format::ProtonPass:
        return i18n("In Proton Pass: <b>Settings</b> › <b>Export</b> › <b>CSV</b>.");
    case Format::Cxf:
        return i18n("A file in the FIDO <b>Credential Exchange Format</b> (CXF 1.0, JSON).");
    case Format::Auto:
        break;
    }
    return i18n("Choose the file you exported from your password manager or browser. "
                "The format is recognised automatically.");
}

QString parseError(const QString &coreError)
{
    // Core errors are English; give the most common ones a translated text.
    if (coreError == QLatin1String("unknown file format")) {
        return i18n("The file format was not recognised. Choose the format manually or check the file.");
    }
    if (coreError.contains(QLatin1String("encrypted"))) {
        return i18n("This export is encrypted. Export it again without encryption (and delete it after the import).");
    }
    return i18n("The file could not be read: %1", coreError);
}

} // namespace

void ImportState::clearSecrets()
{
    for (Entry &e : parsed.entries) {
        e.password.fill(QLatin1Char('\0'));
        e.otpAuth.fill(QLatin1Char('\0'));
    }
    parsed = ParseResult{};
    for (auto &v : plan.values) {
        v[QStringLiteral("Password")].fill(QLatin1Char('\0'));
    }
    plan = Plan{};
}

ImportWizard::ImportWizard(const QString &initialFile, QWidget *parent)
    : QWizard(parent)
{
    setWindowIcon(QIcon::fromTheme(QStringLiteral("kwalletmanager")));
    setWizardStyle(QWizard::ModernStyle);
    setOption(QWizard::NoBackButtonOnLastPage);
    setOption(QWizard::NoCancelButtonOnLastPage);
    setPage(SourceId, new SourcePage(&m_state, initialFile));
    setPage(TargetId, new TargetPage(&m_state));
    setPage(PreviewId, new PreviewPage(&m_state));
    setPage(ResultId, new ResultPage(&m_state));
    resize(760, 560);
}

ImportWizard::~ImportWizard()
{
    m_state.clearSecrets();
}

// --- 1. source -----------------------------------------------------------------

SourcePage::SourcePage(ImportState *state, const QString &initialFile)
    : m_state(state)
{
    setTitle(i18n("Choose the export file"));
    setSubTitle(i18n("Passwords are imported once; run the import again later to bring over changes."));

    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    auto *row = new QHBoxLayout;
    m_path = new QLineEdit(this);
    m_path->setPlaceholderText(i18n("Path to the CSV or JSON file"));
    auto *browse = new QPushButton(QIcon::fromTheme(QStringLiteral("document-open")), i18n("Browse…"), this);
    row->addWidget(m_path);
    row->addWidget(browse);
    form->addRow(i18n("File:"), row);

    m_format = new QComboBox(this);
    m_format->addItem(i18n("Detect automatically"), int(Format::Auto));
    for (const FormatInfo &f : formats()) {
        m_format->addItem(f.displayName, int(f.format));
    }
    form->addRow(i18n("Format:"), m_format);
    layout->addLayout(form);

    m_howTo = new QLabel(this);
    m_howTo->setWordWrap(true);
    m_howTo->setTextFormat(Qt::RichText); // static, translated text only
    layout->addWidget(m_howTo);

    m_summary = new QLabel(this);
    m_summary->setWordWrap(true);
    m_summary->setTextFormat(Qt::PlainText);
    layout->addWidget(m_summary);

    m_message = new KMessageWidget(this);
    m_message->setWordWrap(true);
    m_message->setCloseButtonVisible(false);
    m_message->hide();
    layout->addWidget(m_message);

    auto *warning = new KMessageWidget(i18n("The export file contains all passwords in plain text. "
                                            "Keep it only as long as needed; this wizard can delete it after the import."),
                                       this);
    warning->setMessageType(KMessageWidget::Warning);
    warning->setWordWrap(true);
    warning->setCloseButtonVisible(false);
    layout->addStretch();
    layout->addWidget(warning);

    connect(browse, &QPushButton::clicked, this, [this] {
        const QString dir = m_path->text().isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)
                                                     : QFileInfo(m_path->text()).absolutePath();
        const QString file = QFileDialog::getOpenFileName(this, i18n("Choose the export file"), dir,
                                                          i18n("Password exports (*.csv *.json);;All files (*)"));
        if (!file.isEmpty()) {
            m_path->setText(file);
        }
    });
    connect(m_path, &QLineEdit::textChanged, this, &SourcePage::reload);
    connect(m_format, &QComboBox::currentIndexChanged, this, &SourcePage::reload);
    m_path->setText(initialFile);
    reload();
}

void SourcePage::reload()
{
    m_state->clearSecrets();
    m_ok = false;
    m_message->hide();
    m_summary->clear();
    const auto requested = Format(m_format->currentData().toInt());
    m_howTo->setText(howTo(requested));

    const QString path = m_path->text().trimmed();
    if (!path.isEmpty()) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            m_message->setMessageType(KMessageWidget::Error);
            m_message->setText(i18n("The file cannot be opened: %1", f.errorString()));
            m_message->show();
        } else if (f.size() > 64 * 1024 * 1024) {
            m_message->setMessageType(KMessageWidget::Error);
            m_message->setText(i18n("The file is too large."));
            m_message->show();
        } else {
            Format used = Format::Auto;
            QByteArray content = f.readAll();
            m_state->parsed = parse(content, requested, &used);
            content.fill('\0');
            if (!m_state->parsed.error.isEmpty()) {
                m_message->setMessageType(KMessageWidget::Error);
                m_message->setText(parseError(m_state->parsed.error));
                m_message->show();
            } else {
                m_ok = true;
                m_state->path = path;
                m_state->format = used;
                if (requested == Format::Auto) {
                    m_howTo->setText(howTo(used));
                }
                QString text = i18np("Recognised as %2: 1 entry.", "Recognised as %2: %1 entries.",
                                     m_state->parsed.entries.size(), info(used).displayName);
                if (m_state->parsed.skipped) {
                    text += QLatin1Char(' ') + i18np("1 empty or unsupported item is skipped.",
                                                     "%1 empty or unsupported items are skipped.", m_state->parsed.skipped);
                }
                if (m_state->parsed.skippedPasskeys) {
                    text += QLatin1Char(' ') + i18np("1 passkey is not imported (passkeys are handled by kpasskey).",
                                                     "%1 passkeys are not imported (passkeys are handled by kpasskey).",
                                                     m_state->parsed.skippedPasskeys);
                }
                m_summary->setText(text);
            }
        }
    }
    Q_EMIT completeChanged();
}

bool SourcePage::isComplete() const
{
    return m_ok;
}

// --- 2. target -----------------------------------------------------------------

TargetPage::TargetPage(ImportState *state)
    : m_state(state)
{
    setTitle(i18n("Choose where to store the passwords"));
    setSubTitle(i18n("Entries are stored as \"host (user name)\" with user name, password, URL, notes and one-time code secret."));
    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    m_wallet = new QComboBox(this);
    form->addRow(i18n("Wallet:"), m_wallet);
    m_folder = new QLineEdit(this);
    form->addRow(i18n("Folder:"), m_folder);
    layout->addLayout(form);

    m_prune = new QCheckBox(i18n("Remove entries that were imported earlier from this source but no longer exist there"), this);
    m_prune->setToolTip(i18n("Only entries created by this import tool from the same source are removed. "
                             "Entries you created yourself are never touched."));
    layout->addWidget(m_prune);
    m_delete = new QCheckBox(i18n("Overwrite and delete the export file after a successful import"), this);
    m_delete->setChecked(true);
    layout->addWidget(m_delete);

    m_message = new KMessageWidget(this);
    m_message->setWordWrap(true);
    m_message->setCloseButtonVisible(false);
    m_message->setMessageType(KMessageWidget::Error);
    m_message->hide();
    layout->addWidget(m_message);
    layout->addStretch();
}

void TargetPage::initializePage()
{
    m_message->hide();
    if (m_wallet->count() == 0) {
        const QStringList wallets = KWallet::Wallet::walletList();
        m_wallet->addItems(wallets.isEmpty() ? QStringList{KWallet::Wallet::LocalWallet()} : wallets);
        m_wallet->setCurrentText(KWallet::Wallet::LocalWallet());
    }
    m_folder->setText(info(m_state->format).defaultFolder);
}

bool TargetPage::validatePage()
{
    m_message->hide();
    if (m_folder->text().trimmed().isEmpty()) {
        m_message->setText(i18n("Enter a folder name."));
        m_message->show();
        return false;
    }
    m_state->wallet = m_wallet->currentText();
    m_state->folder = m_folder->text().trimmed();
    m_state->prune = m_prune->isChecked();
    m_state->deleteFile = m_delete->isChecked();

    // Read-only preview: the folder is not created yet.
    KWalletFolder folder;
    QString error;
    if (!folder.open(m_state->wallet, m_state->folder, false, &error)) {
        m_message->setText(i18n("The wallet could not be opened: %1", error));
        m_message->show();
        return false;
    }
    m_state->plan = makePlan(m_state->parsed.entries, folder, info(m_state->format).sourceName);
    return true;
}

// --- 3. preview ----------------------------------------------------------------

PreviewPage::PreviewPage(ImportState *state)
    : m_state(state)
{
    setTitle(i18n("Check the changes"));
    setSubTitle(i18n("Nothing has been written yet. Passwords are not shown."));
    setButtonText(QWizard::NextButton, i18n("Import"));
    auto *layout = new QVBoxLayout(this);
    m_summary = new QLabel(this);
    m_summary->setWordWrap(true);
    layout->addWidget(m_summary);
    m_tree = new QTreeWidget(this);
    m_tree->setHeaderLabels({i18n("Entry"), i18n("Change")});
    m_tree->setRootIsDecorated(false);
    m_tree->setSortingEnabled(true);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    layout->addWidget(m_tree);
    m_message = new KMessageWidget(this);
    m_message->setWordWrap(true);
    m_message->setCloseButtonVisible(false);
    m_message->setMessageType(KMessageWidget::Error);
    m_message->hide();
    layout->addWidget(m_message);
}

void PreviewPage::initializePage()
{
    const Plan &p = m_state->plan;
    m_tree->clear();
    auto add = [this](const QStringList &keys, const QString &label, const QString &icon) {
        for (const QString &k : keys) {
            auto *item = new QTreeWidgetItem(m_tree, {k, label});
            item->setIcon(1, QIcon::fromTheme(icon));
        }
    };
    add(p.add, i18n("new"), QStringLiteral("list-add"));
    add(p.update, i18n("changed"), QStringLiteral("document-edit"));
    add(p.unchanged, i18n("unchanged"), QStringLiteral("dialog-ok"));
    add(p.stale, m_state->prune ? i18n("will be removed") : i18n("no longer in the export (kept)"),
        m_state->prune ? QStringLiteral("list-remove") : QStringLiteral("dialog-information"));
    m_tree->sortItems(1, Qt::AscendingOrder);
    m_summary->setText(i18n("Wallet <b>%1</b>, folder <b>%2</b>: %3 new, %4 changed, %5 unchanged, %6 no longer in the export.",
                            m_state->wallet.toHtmlEscaped(), m_state->folder.toHtmlEscaped(), p.add.size(), p.update.size(),
                            p.unchanged.size(), p.stale.size()));
}

bool PreviewPage::validatePage()
{
    KWalletFolder folder;
    QString error;
    if (!folder.open(m_state->wallet, m_state->folder, true, &error)) {
        m_message->setText(i18n("The wallet could not be opened: %1", error));
        m_message->show();
        return false;
    }
    const ApplyResult r = apply(m_state->plan, folder, m_state->prune);
    QString text = i18n("%1 entries written, %2 removed.", r.written, r.removed);
    m_state->resultOk = r.failed == 0;
    if (r.failed) {
        text += QLatin1Char('\n') + i18np("1 entry could not be written. The export file was kept.",
                                          "%1 entries could not be written. The export file was kept.", r.failed);
    } else if (m_state->deleteFile) {
        if (shredFile(m_state->path)) {
            text += QLatin1Char('\n') + i18n("The export file was overwritten and deleted.");
        } else {
            m_state->resultOk = false;
            text += QLatin1Char('\n') + i18n("The export file could not be deleted. Please delete it yourself: %1", m_state->path);
        }
    } else {
        text += QLatin1Char('\n') + i18n("Remember to delete the export file; it contains all passwords in plain text.");
    }
    m_state->resultText = text;
    m_state->clearSecrets();
    return true;
}

// --- 4. result -----------------------------------------------------------------

ResultPage::ResultPage(ImportState *state)
    : m_state(state)
{
    setTitle(i18n("Import finished"));
    setFinalPage(true);
    auto *layout = new QVBoxLayout(this);
    m_message = new KMessageWidget(this);
    m_message->setWordWrap(true);
    m_message->setCloseButtonVisible(false);
    layout->addWidget(m_message);
    auto *hint = new QLabel(i18n("You can view the entries in KWalletManager."), this);
    layout->addWidget(hint);
    auto *open = new QPushButton(QIcon::fromTheme(QStringLiteral("kwalletmanager")), i18n("Open KWalletManager"), this);
    connect(open, &QPushButton::clicked, this, [] {
        QProcess::startDetached(QStringLiteral("kwalletmanager5"), {});
    });
    layout->addWidget(open, 0, Qt::AlignLeft);
    layout->addStretch();
}

void ResultPage::initializePage()
{
    m_message->setMessageType(m_state->resultOk ? KMessageWidget::Positive : KMessageWidget::Warning);
    m_message->setText(m_state->resultText);
}

} // namespace kwimport
