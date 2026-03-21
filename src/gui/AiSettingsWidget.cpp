#include "AiSettingsWidget.h"

#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QCheckBox>
#include <QSpinBox>
#include <QMessageBox>

#include "../ai/AiClient.h"

AiSettingsWidget::AiSettingsWidget(QSettings *settings, QWidget *parent)
    : SettingsWidget("MidiPilot AI", parent), _settings(settings), _keyVisible(false) {

    QGridLayout *layout = new QGridLayout(this);
    setLayout(layout);
    setMinimumSize(400, 300);

    int row = 0;

    // Info box
    layout->addWidget(createInfoBox("Configure the OpenAI API connection for MidiPilot. "
                                    "An API key is required to use AI-assisted MIDI editing features."),
                      row++, 0, 1, 3);

    layout->addWidget(separator(), row++, 0, 1, 3);

    // API Key
    layout->addWidget(new QLabel("API Key:"), row, 0);
    _apiKeyEdit = new QLineEdit(this);
    _apiKeyEdit->setEchoMode(QLineEdit::Password);
    _apiKeyEdit->setPlaceholderText("sk-...");
    _apiKeyEdit->setText(_settings->value("AI/api_key").toString());
    layout->addWidget(_apiKeyEdit, row, 1);

    _toggleKeyButton = new QPushButton("Show", this);
    _toggleKeyButton->setFixedWidth(60);
    connect(_toggleKeyButton, &QPushButton::clicked, this, &AiSettingsWidget::onToggleKeyVisibility);
    layout->addWidget(_toggleKeyButton, row, 2);
    row++;

    // Model selection
    layout->addWidget(new QLabel("Model:"), row, 0);
    _modelCombo = new QComboBox(this);
    _modelCombo->addItem("gpt-4o-mini (fast, cheap)", "gpt-4o-mini");
    _modelCombo->addItem("gpt-4o (balanced)", "gpt-4o");
    _modelCombo->addItem("gpt-4.1-nano (fastest)", "gpt-4.1-nano");
    _modelCombo->addItem("gpt-4.1-mini (fast)", "gpt-4.1-mini");
    _modelCombo->addItem("gpt-4.1 (strong)", "gpt-4.1");
    _modelCombo->addItem("gpt-5 (GPT-5)", "gpt-5");
    _modelCombo->addItem("gpt-5-mini (GPT-5 mini)", "gpt-5-mini");
    _modelCombo->addItem("o4-mini (reasoning)", "o4-mini");
    _modelCombo->setEditable(true); // Allow custom model names

    QString currentModel = _settings->value("AI/model", "gpt-4o-mini").toString();
    int idx = _modelCombo->findData(currentModel);
    if (idx >= 0) {
        _modelCombo->setCurrentIndex(idx);
    }
    layout->addWidget(_modelCombo, row, 1, 1, 2);
    row++;

    // Thinking / Reasoning toggle
    layout->addWidget(new QLabel("Thinking:"), row, 0);
    _thinkingCheck = new QCheckBox("Enable reasoning (for o-series and GPT-5.x models)", this);
    _thinkingCheck->setChecked(_settings->value("AI/thinking_enabled", false).toBool());
    layout->addWidget(_thinkingCheck, row, 1, 1, 2);
    row++;

    // Reasoning effort
    _effortLabel = new QLabel("Reasoning Effort:", this);
    layout->addWidget(_effortLabel, row, 0);
    _effortCombo = new QComboBox(this);
    _effortCombo->addItem("Low (fast, less thorough)", "low");
    _effortCombo->addItem("Medium (balanced)", "medium");
    _effortCombo->addItem("High (thorough, slower)", "high");
    QString currentEffort = _settings->value("AI/reasoning_effort", "medium").toString();
    int effortIdx = _effortCombo->findData(currentEffort);
    if (effortIdx >= 0) _effortCombo->setCurrentIndex(effortIdx);
    layout->addWidget(_effortCombo, row, 1, 1, 2);
    row++;

    // Show/hide effort based on thinking toggle
    auto updateEffortVisibility = [this]() {
        bool on = _thinkingCheck->isChecked();
        _effortLabel->setVisible(on);
        _effortCombo->setVisible(on);
    };
    connect(_thinkingCheck, &QCheckBox::toggled, this, updateEffortVisibility);
    updateEffortVisibility();

    layout->addWidget(separator(), row++, 0, 1, 3);

    // Context range (surrounding measures)
    layout->addWidget(new QLabel("Context Range:"), row, 0);
    _contextMeasuresSpin = new QSpinBox(this);
    _contextMeasuresSpin->setRange(0, 50);
    _contextMeasuresSpin->setValue(_settings->value("AI/context_measures", 5).toInt());
    _contextMeasuresSpin->setSuffix(" measures");
    _contextMeasuresSpin->setSpecialValueText("Off (no surrounding context)");
    _contextMeasuresSpin->setToolTip("Number of measures before and after the cursor to include as context.\n"
                                     "Higher values give the AI more musical context but cost more tokens.\n"
                                     "Set to 0 to disable surrounding context.");
    layout->addWidget(_contextMeasuresSpin, row, 1);

    _contextEstimateLabel = new QLabel(this);
    _contextEstimateLabel->setStyleSheet("color: gray; font-size: 11px;");
    layout->addWidget(_contextEstimateLabel, row, 2);
    row++;

    auto updateEstimate = [this]() {
        int m = _contextMeasuresSpin->value();
        if (m == 0) {
            _contextEstimateLabel->setText("No extra context");
        } else {
            // Rough estimate: ~10-30 tokens per event, ~4-16 events per measure per track
            // Conservative: ~15 tokens/event * 8 events/measure * N tracks * 2*M measures
            _contextEstimateLabel->setText(QString::fromUtf8("\xc2\xb1%1 measures around cursor").arg(m));
        }
    };
    connect(_contextMeasuresSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, updateEstimate);
    updateEstimate();

    layout->addWidget(separator(), row++, 0, 1, 3);

    // Test connection button and status
    _testButton = new QPushButton("Test Connection", this);
    connect(_testButton, &QPushButton::clicked, this, &AiSettingsWidget::onTestConnection);
    layout->addWidget(_testButton, row, 0, 1, 1);

    _statusLabel = new QLabel("", this);
    _statusLabel->setWordWrap(true);
    layout->addWidget(_statusLabel, row, 1, 1, 2);
    row++;

    // Spacer
    layout->setRowStretch(row, 1);
}

bool AiSettingsWidget::accept() {
    _settings->setValue("AI/api_key", _apiKeyEdit->text().trimmed());
    QString model = _modelCombo->currentData().toString();
    if (model.isEmpty()) model = _modelCombo->currentText().trimmed();
    _settings->setValue("AI/model", model);
    _settings->setValue("AI/thinking_enabled", _thinkingCheck->isChecked());
    _settings->setValue("AI/reasoning_effort", _effortCombo->currentData().toString());
    _settings->setValue("AI/context_measures", _contextMeasuresSpin->value());
    return true;
}

QIcon AiSettingsWidget::icon() {
    return QIcon(":/run_environment/graphics/tool/ai_settings.png");
}

void AiSettingsWidget::onTestConnection() {
    QString key = _apiKeyEdit->text().trimmed();
    if (key.isEmpty()) {
        _statusLabel->setStyleSheet("color: red;");
        _statusLabel->setText("Please enter an API key first.");
        return;
    }

    _testButton->setEnabled(false);
    _statusLabel->setStyleSheet("color: gray;");
    _statusLabel->setText("Testing connection...");

    AiClient *client = new AiClient(this);
    client->setApiKey(key);
    client->setModel(_modelCombo->currentData().toString().isEmpty()
                      ? _modelCombo->currentText().trimmed()
                      : _modelCombo->currentData().toString());
    connect(client, &AiClient::connectionTestResult, this, &AiSettingsWidget::onTestResult);
    connect(client, &AiClient::connectionTestResult, client, &QObject::deleteLater);
    client->testConnection();
}

void AiSettingsWidget::onTestResult(bool success, const QString &message) {
    _testButton->setEnabled(true);
    if (success) {
        _statusLabel->setStyleSheet("color: green;");
    } else {
        _statusLabel->setStyleSheet("color: red;");
    }
    _statusLabel->setText(message);
}

void AiSettingsWidget::onToggleKeyVisibility() {
    _keyVisible = !_keyVisible;
    if (_keyVisible) {
        _apiKeyEdit->setEchoMode(QLineEdit::Normal);
        _toggleKeyButton->setText("Hide");
    } else {
        _apiKeyEdit->setEchoMode(QLineEdit::Password);
        _toggleKeyButton->setText("Show");
    }
}
