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
    : SettingsWidget("MidiPilot AI", parent), _settings(settings), _keyVisible(false), _lastProvider() {

    QGridLayout *layout = new QGridLayout(this);
    setLayout(layout);
    setMinimumSize(400, 300);

    int row = 0;

    // Info box
    layout->addWidget(createInfoBox("Configure the AI API connection for MidiPilot. "
                                    "Select your provider, enter an API key (if required), and choose a model."),
                      row++, 0, 1, 3);

    layout->addWidget(separator(), row++, 0, 1, 3);

    // Provider selection
    layout->addWidget(new QLabel("Provider:"), row, 0);
    _providerCombo = new QComboBox(this);
    _providerCombo->addItem("OpenAI", "openai");
    _providerCombo->addItem("OpenRouter", "openrouter");
    _providerCombo->addItem("Google Gemini", "gemini");
    _providerCombo->addItem("Custom", "custom");
    QString currentProvider = _settings->value("AI/provider", "openai").toString();
    int provIdx = _providerCombo->findData(currentProvider);
    if (provIdx >= 0) _providerCombo->setCurrentIndex(provIdx);
    layout->addWidget(_providerCombo, row, 1, 1, 2);
    row++;

    // Base URL
    layout->addWidget(new QLabel("Base URL:"), row, 0);
    _baseUrlEdit = new QLineEdit(this);
    _baseUrlEdit->setPlaceholderText("https://api.openai.com/v1");
    _baseUrlEdit->setText(_settings->value("AI/api_base_url", "https://api.openai.com/v1").toString());
    layout->addWidget(_baseUrlEdit, row, 1, 1, 2);
    row++;

    // API Key
    _apiKeyLabel = new QLabel("API Key:", this);
    layout->addWidget(_apiKeyLabel, row, 0);
    _apiKeyEdit = new QLineEdit(this);
    _apiKeyEdit->setEchoMode(QLineEdit::Password);
    _apiKeyEdit->setPlaceholderText("sk-...");
    // Load per-provider key (fall back to legacy shared key)
    QString providerKey = _settings->value(QString("AI/api_key/%1").arg(currentProvider)).toString();
    if (providerKey.isEmpty())
        providerKey = _settings->value("AI/api_key").toString();
    _apiKeyEdit->setText(providerKey);
    layout->addWidget(_apiKeyEdit, row, 1);

    _toggleKeyButton = new QPushButton("Show", this);
    _toggleKeyButton->setFixedWidth(60);
    connect(_toggleKeyButton, &QPushButton::clicked, this, &AiSettingsWidget::onToggleKeyVisibility);
    layout->addWidget(_toggleKeyButton, row, 2);
    row++;

    // Model selection
    layout->addWidget(new QLabel("Model:"), row, 0);
    _modelCombo = new QComboBox(this);
    _modelCombo->setEditable(true); // Allow custom model names

    // Populate models for current provider
    populateModelsForProvider(currentProvider);

    QString currentModel = _settings->value("AI/model", "gpt-5.4").toString();
    int idx = _modelCombo->findData(currentModel);
    if (idx >= 0) {
        _modelCombo->setCurrentIndex(idx);
    } else {
        _modelCombo->setEditText(currentModel);
    }
    layout->addWidget(_modelCombo, row, 1, 1, 2);
    row++;

    // Connect provider changes (after _modelCombo is created)
    connect(_providerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AiSettingsWidget::onProviderChanged);
    // Apply current provider state (hides API key for local, sets URL)
    onProviderChanged(_providerCombo->currentIndex());

    // Thinking / Reasoning toggle
    layout->addWidget(new QLabel("Thinking:"), row, 0);
    _thinkingCheck = new QCheckBox("Enable reasoning (for o-series and GPT-5.x models)", this);
    _thinkingCheck->setChecked(_settings->value("AI/thinking_enabled", true).toBool());
    layout->addWidget(_thinkingCheck, row, 1, 1, 2);
    row++;

    // Reasoning effort
    _effortLabel = new QLabel("Reasoning Effort:", this);
    layout->addWidget(_effortLabel, row, 0);
    _effortCombo = new QComboBox(this);
    _effortCombo->addItem("None (no reasoning, fastest)", "none");
    _effortCombo->addItem("Low (fast, less thorough)", "low");
    _effortCombo->addItem("Medium (balanced)", "medium");
    _effortCombo->addItem("High (thorough, slower)", "high");
    _effortCombo->addItem("Extra High (most thorough)", "xhigh");
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

    // FFXIV Bard Performance mode
    layout->addWidget(new QLabel("FFXIV Mode:"), row, 0);
    _ffxivCheck = new QCheckBox("Enable FFXIV Bard Performance mode", this);
    _ffxivCheck->setChecked(_settings->value("AI/ffxiv_mode", false).toBool());
    _ffxivCheck->setToolTip("When enabled, MidiPilot constrains output to FFXIV Bard Performance rules:\n"
                            "- All notes in C3-C6 (MIDI 48-84), player auto-transposes\n"
                            "- Monophonic per track (one note at a time)\n"
                            "- Track names must match valid instrument names\n"
                            "- Drums are separate tonal tracks (no GM drum kit)\n"
                            "- Each track needs its own channel with program_change at tick 0");
    layout->addWidget(_ffxivCheck, row, 1, 1, 2);
    row++;

    layout->addWidget(separator(), row++, 0, 1, 3);

    // Agent max steps
    layout->addWidget(new QLabel("Agent Max Steps:"), row, 0);
    _agentMaxStepsSpin = new QSpinBox(this);
    _agentMaxStepsSpin->setRange(5, 100);
    _agentMaxStepsSpin->setValue(_settings->value("AI/agent_max_steps", 50).toInt());
    _agentMaxStepsSpin->setToolTip("Maximum number of tool calls the Agent can make per request.\n"
                                   "Higher values allow more complex tasks but take longer.");
    layout->addWidget(_agentMaxStepsSpin, row, 1);
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
    QString provider = _providerCombo->currentData().toString();
    _settings->setValue("AI/provider", provider);
    _settings->setValue("AI/api_base_url", _baseUrlEdit->text().trimmed());
    // Save API key per-provider and as active key
    QString key = _apiKeyEdit->text().trimmed();
    _settings->setValue(QString("AI/api_key/%1").arg(provider), key);
    _settings->setValue("AI/api_key", key);
    QString model = _modelCombo->currentData().toString();
    if (model.isEmpty()) model = _modelCombo->currentText().trimmed();
    _settings->setValue("AI/model", model);
    _settings->setValue("AI/thinking_enabled", _thinkingCheck->isChecked());
    _settings->setValue("AI/reasoning_effort", _effortCombo->currentData().toString());
    _settings->setValue("AI/context_measures", _contextMeasuresSpin->value());
    _settings->setValue("AI/agent_max_steps", _agentMaxStepsSpin->value());
    _settings->setValue("AI/ffxiv_mode", _ffxivCheck->isChecked());
    return true;
}

QIcon AiSettingsWidget::icon() {
    return QIcon(":/run_environment/graphics/tool/ai_settings.png");
}

void AiSettingsWidget::onTestConnection() {
    QString provider = _providerCombo->currentData().toString();
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
    client->setProvider(provider);
    client->setApiBaseUrl(_baseUrlEdit->text().trimmed());
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

void AiSettingsWidget::onProviderChanged(int /*index*/) {
    QString provider = _providerCombo->currentData().toString();

    // Save current key for the previous provider before switching
    if (!_lastProvider.isEmpty() && _lastProvider != provider) {
        _settings->setValue(QString("AI/api_key/%1").arg(_lastProvider),
                            _apiKeyEdit->text().trimmed());
    }
    _lastProvider = provider;

    // Load key for the new provider
    QString storedKey = _settings->value(QString("AI/api_key/%1").arg(provider)).toString();
    _apiKeyEdit->setText(storedKey);

    // Set default base URL based on provider
    static const QMap<QString, QString> defaultUrls = {
        {"openai",     "https://api.openai.com/v1"},
        {"openrouter", "https://openrouter.ai/api/v1"},
        {"gemini",     "https://generativelanguage.googleapis.com/v1beta/openai"},
    };

    if (provider != "custom") {
        _baseUrlEdit->setText(defaultUrls.value(provider, "https://api.openai.com/v1"));
        _baseUrlEdit->setReadOnly(true);
    } else {
        _baseUrlEdit->setReadOnly(false);
    }

    // Update model list (guard: _modelCombo may not exist during initial call)
    if (_modelCombo) {
        populateModelsForProvider(provider);
        // Select first model for the new provider
        if (_modelCombo->count() > 0)
            _modelCombo->setCurrentIndex(0);
    }
}

void AiSettingsWidget::populateModelsForProvider(const QString &provider) {
    _modelCombo->clear();

    if (provider == "openai") {
        _modelCombo->addItem("gpt-4o-mini", "gpt-4o-mini");
        _modelCombo->addItem("gpt-4o", "gpt-4o");
        _modelCombo->addItem("gpt-4.1-nano", "gpt-4.1-nano");
        _modelCombo->addItem("gpt-4.1-mini", "gpt-4.1-mini");
        _modelCombo->addItem("gpt-4.1", "gpt-4.1");
        _modelCombo->addItem("gpt-5", "gpt-5");
        _modelCombo->addItem("gpt-5-mini", "gpt-5-mini");
        _modelCombo->addItem("gpt-5.4", "gpt-5.4");
        _modelCombo->addItem("gpt-5.4-mini", "gpt-5.4-mini");
        _modelCombo->addItem("gpt-5.4-nano", "gpt-5.4-nano");
        _modelCombo->addItem("o4-mini", "o4-mini");
    } else if (provider == "openrouter") {
        _modelCombo->addItem("openai/gpt-5.4", "openai/gpt-5.4");
        _modelCombo->addItem("openai/gpt-4.1", "openai/gpt-4.1");
        _modelCombo->addItem("anthropic/claude-sonnet-4", "anthropic/claude-sonnet-4");
        _modelCombo->addItem("anthropic/claude-3.5-sonnet", "anthropic/claude-3.5-sonnet");
        _modelCombo->addItem("google/gemini-2.5-pro", "google/gemini-2.5-pro");
        _modelCombo->addItem("google/gemini-2.5-flash", "google/gemini-2.5-flash");
        _modelCombo->addItem("meta-llama/llama-4-maverick", "meta-llama/llama-4-maverick");
    } else if (provider == "gemini") {
        _modelCombo->addItem("gemini-2.5-flash", "gemini-2.5-flash");
        _modelCombo->addItem("gemini-2.5-flash-lite", "gemini-2.5-flash-lite");
        _modelCombo->addItem("gemini-2.5-pro", "gemini-2.5-pro");
        _modelCombo->addItem("gemini-3-flash", "gemini-3-flash-preview");
        _modelCombo->addItem("gemini-3.1-flash-lite", "gemini-3.1-flash-lite-preview");
        _modelCombo->addItem("gemini-3.1-pro", "gemini-3.1-pro-preview");
    } else {
        // Custom provider — no presets, user types the model
        _modelCombo->addItem("(enter model name)", "");
    }
}
