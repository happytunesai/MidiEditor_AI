#include "SystemPromptDialog.h"
#include "../ai/EditorContext.h"

#include <QTabWidget>
#include <QPlainTextEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QFont>
#include <QFileDialog>
#include <QMessageBox>
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

SystemPromptDialog::SystemPromptDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("System Prompt Editor");
    resize(800, 600);

    auto *mainLayout = new QVBoxLayout(this);

    // Tab widget with 4 prompt editors
    _tabs = new QTabWidget(this);

    QFont monoFont("Consolas", 10);
    monoFont.setStyleHint(QFont::Monospace);

    _simpleEdit = new QPlainTextEdit(this);
    _simpleEdit->setFont(monoFont);
    _tabs->addTab(_simpleEdit, "Simple");

    _agentEdit = new QPlainTextEdit(this);
    _agentEdit->setFont(monoFont);
    _tabs->addTab(_agentEdit, "Agent");

    _ffxivEdit = new QPlainTextEdit(this);
    _ffxivEdit->setFont(monoFont);
    _tabs->addTab(_ffxivEdit, "FFXIV");

    _ffxivCompactEdit = new QPlainTextEdit(this);
    _ffxivCompactEdit->setFont(monoFont);
    _tabs->addTab(_ffxivCompactEdit, "FFXIV Compact");

    mainLayout->addWidget(_tabs);

    // Middle button row
    auto *midRow = new QHBoxLayout();
    auto *loadDefaultBtn = new QPushButton("Load Default", this);
    loadDefaultBtn->setToolTip("Restore the hardcoded default for the current tab");
    connect(loadDefaultBtn, &QPushButton::clicked, this, &SystemPromptDialog::onLoadDefault);
    midRow->addWidget(loadDefaultBtn);

    auto *exportBtn = new QPushButton("Export to JSON", this);
    exportBtn->setToolTip("Save all prompts to a JSON file");
    connect(exportBtn, &QPushButton::clicked, this, &SystemPromptDialog::onExportJson);
    midRow->addWidget(exportBtn);

    auto *importBtn = new QPushButton("Import from JSON", this);
    importBtn->setToolTip("Load prompts from a JSON file");
    connect(importBtn, &QPushButton::clicked, this, &SystemPromptDialog::onImportJson);
    midRow->addWidget(importBtn);

    midRow->addStretch();
    mainLayout->addLayout(midRow);

    // Info label
    _statusLabel = new QLabel(this);
    _statusLabel->setWordWrap(true);
    if (EditorContext::hasCustomPrompts()) {
        _statusLabel->setText(QString::fromUtf8("\u2139 Custom prompts active from: %1")
                              .arg(EditorContext::customPromptsPath()));
    } else {
        _statusLabel->setText(QString::fromUtf8("\u2139 Using hardcoded default prompts. "
                              "Edit and save to create a custom system_prompts.json."));
    }
    mainLayout->addWidget(_statusLabel);

    // Bottom button row
    auto *bottomRow = new QHBoxLayout();
    auto *resetAllBtn = new QPushButton("Reset All to Defaults", this);
    resetAllBtn->setToolTip("Restore all prompts to hardcoded defaults and delete the JSON file");
    connect(resetAllBtn, &QPushButton::clicked, this, &SystemPromptDialog::onResetAll);
    bottomRow->addWidget(resetAllBtn);

    bottomRow->addStretch();

    auto *cancelBtn = new QPushButton("Cancel", this);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    bottomRow->addWidget(cancelBtn);

    auto *saveBtn = new QPushButton("Save", this);
    saveBtn->setDefault(true);
    connect(saveBtn, &QPushButton::clicked, this, &SystemPromptDialog::onSave);
    bottomRow->addWidget(saveBtn);

    mainLayout->addLayout(bottomRow);

    loadCurrentPrompts();
}

void SystemPromptDialog::loadCurrentPrompts()
{
    _simpleEdit->setPlainText(EditorContext::systemPrompt());
    _agentEdit->setPlainText(EditorContext::agentSystemPrompt());
    _ffxivEdit->setPlainText(EditorContext::ffxivContext());
    _ffxivCompactEdit->setPlainText(EditorContext::ffxivContextCompact());
}

QString SystemPromptDialog::promptsFilePath() const
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/system_prompts.json");
}

void SystemPromptDialog::onLoadDefault()
{
    int idx = _tabs->currentIndex();
    switch (idx) {
    case 0: _simpleEdit->setPlainText(EditorContext::defaultPrompt("simple")); break;
    case 1: _agentEdit->setPlainText(EditorContext::defaultPrompt("agent")); break;
    case 2: _ffxivEdit->setPlainText(EditorContext::defaultPrompt("ffxiv")); break;
    case 3: _ffxivCompactEdit->setPlainText(EditorContext::defaultPrompt("ffxiv_compact")); break;
    }
}

void SystemPromptDialog::onResetAll()
{
    auto result = QMessageBox::question(this, "Reset to Defaults",
        "This will restore all prompts to hardcoded defaults and delete the custom JSON file.\n\nContinue?",
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (result != QMessageBox::Yes)
        return;

    EditorContext::resetToDefaults();

    // Delete the JSON file
    QString path = promptsFilePath();
    if (QFile::exists(path))
        QFile::remove(path);

    loadCurrentPrompts();
    _statusLabel->setText(QString::fromUtf8("\u2139 Reset to defaults. Custom prompts file removed."));
}

void SystemPromptDialog::onExportJson()
{
    QString path = QFileDialog::getSaveFileName(this, "Export System Prompts",
        promptsFilePath(), "JSON Files (*.json)");
    if (path.isEmpty())
        return;

    // Temporarily apply editor content so savePromptsToJson captures it
    // Save & restore approach not needed — we write directly
    QJsonObject prompts;
    prompts[QStringLiteral("simple")] = _simpleEdit->toPlainText();
    prompts[QStringLiteral("agent")] = _agentEdit->toPlainText();
    prompts[QStringLiteral("ffxiv")] = _ffxivEdit->toPlainText();
    prompts[QStringLiteral("ffxiv_compact")] = _ffxivCompactEdit->toPlainText();

    QJsonObject root;
    root[QStringLiteral("version")] = 1;
    root[QStringLiteral("prompts")] = prompts;

    QJsonDocument doc(root);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Export Failed",
            QString("Could not write to:\n%1").arg(path));
        return;
    }
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();

    _statusLabel->setText(QString::fromUtf8("\u2705 Exported to: %1").arg(path));
}

void SystemPromptDialog::onImportJson()
{
    QString path = QFileDialog::getOpenFileName(this, "Import System Prompts",
        QCoreApplication::applicationDirPath(), "JSON Files (*.json)");
    if (path.isEmpty())
        return;

    if (EditorContext::loadCustomPrompts(path)) {
        loadCurrentPrompts();
        _statusLabel->setText(QString::fromUtf8("\u2705 Imported from: %1").arg(path));
    } else {
        QMessageBox::warning(this, "Import Failed",
            "Could not load the JSON file. Make sure it has:\n"
            "- Valid JSON syntax\n"
            "- \"version\": 1\n"
            "- \"prompts\" object with prompt keys");
    }
}

void SystemPromptDialog::onSave()
{
    // Validate: all prompts must be non-empty
    if (_simpleEdit->toPlainText().trimmed().isEmpty() ||
        _agentEdit->toPlainText().trimmed().isEmpty() ||
        _ffxivEdit->toPlainText().trimmed().isEmpty() ||
        _ffxivCompactEdit->toPlainText().trimmed().isEmpty()) {
        QMessageBox::warning(this, "Validation Error",
            "All prompts must be non-empty. Use 'Load Default' to restore a default prompt.");
        return;
    }

    QString path = promptsFilePath();

    // Back up existing file
    if (QFile::exists(path)) {
        QString bakPath = path + QStringLiteral(".bak");
        QFile::remove(bakPath);
        QFile::rename(path, bakPath);
    }

    QJsonObject prompts;
    prompts[QStringLiteral("simple")] = _simpleEdit->toPlainText();
    prompts[QStringLiteral("agent")] = _agentEdit->toPlainText();
    prompts[QStringLiteral("ffxiv")] = _ffxivEdit->toPlainText();
    prompts[QStringLiteral("ffxiv_compact")] = _ffxivCompactEdit->toPlainText();

    QJsonObject root;
    root[QStringLiteral("version")] = 1;
    root[QStringLiteral("prompts")] = prompts;

    QJsonDocument doc(root);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Save Failed",
            QString("Could not write to:\n%1\n\nCheck file permissions.").arg(path));
        return;
    }
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();

    // Reload the saved prompts so they take effect immediately
    EditorContext::loadCustomPrompts(path);

    accept();
}
