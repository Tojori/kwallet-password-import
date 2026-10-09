// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "formats.h"
#include "importer.h"

#include <QWizard>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QTreeWidget;
class KMessageWidget;

namespace kwimport {

// Shared state of the import wizard. Holds decrypted passwords only in
// memory and only as long as the wizard is open.
struct ImportState {
    QString path;
    Format format = Format::Auto;
    ParseResult parsed;
    QString wallet;
    QString folder;
    bool prune = false;
    bool deleteFile = false;
    Plan plan;
    QString resultText;
    bool resultOk = false;

    // Drops all secrets held by the wizard.
    void clearSecrets();
};

class ImportWizard : public QWizard
{
    Q_OBJECT
public:
    explicit ImportWizard(const QString &initialFile, QWidget *parent = nullptr);
    ~ImportWizard() override;
    ImportState &state() { return m_state; }

private:
    ImportState m_state;
};

class SourcePage : public QWizardPage
{
    Q_OBJECT
public:
    SourcePage(ImportState *state, const QString &initialFile);
    bool isComplete() const override;

private:
    void reload();
    ImportState *m_state;
    QLineEdit *m_path;
    QComboBox *m_format;
    QLabel *m_howTo;
    QLabel *m_summary;
    KMessageWidget *m_message;
    bool m_ok = false;
};

class TargetPage : public QWizardPage
{
    Q_OBJECT
public:
    explicit TargetPage(ImportState *state);
    void initializePage() override;
    bool validatePage() override;

private:
    ImportState *m_state;
    QComboBox *m_wallet;
    QLineEdit *m_folder;
    QCheckBox *m_prune;
    QCheckBox *m_delete;
    KMessageWidget *m_message;
};

class PreviewPage : public QWizardPage
{
    Q_OBJECT
public:
    explicit PreviewPage(ImportState *state);
    void initializePage() override;
    bool validatePage() override;

private:
    ImportState *m_state;
    QLabel *m_summary;
    QTreeWidget *m_tree;
    KMessageWidget *m_message;
};

class ResultPage : public QWizardPage
{
    Q_OBJECT
public:
    explicit ResultPage(ImportState *state);
    void initializePage() override;

private:
    ImportState *m_state;
    KMessageWidget *m_message;
};

} // namespace kwimport
