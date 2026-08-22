#include "AiSettingsWidget.h"
#include "SystemPromptDialog.h"
#include "ModelFavoritesDialog.h"
#include "PromptProfilesDialog.h"
#include "../ai/PromptProfileStore.h"
#include "../ai/ProviderProfileStore.h"
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QScopedValueRollback>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QStyle>
#include <QCheckBox>
#include <QSpinBox>
#include <QMessageBox>
#include <QClipboard>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

#include "../ai/AiClient.h"
#include "../ai/EditorContext.h"
#include "../ai/McpServer.h"
#include "../ai/ModelFavorites.h"
#include "../ai/ModelListCache.h"
#include "../ai/ModelListFetcher.h"

namespace {
// Qt::UserRole holds the PROVIDER ID on every provider-combo entry, including
// the stored-profile entries (which carry "custom"). The profile name lives
// here instead, so it can never leak into AI/provider or into a request.
constexpr int kProviderProfileRole = Qt::UserRole + 1;
// The five fixed entries at the top of the provider combo; everything after
// them is the separator plus the stored custom profiles.
constexpr int kFixedProviderCount = 5;
} // namespace

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

    // Phase 50: Provider profile - a named bundle of provider + base URL +
    // key + model. Lives directly above the fields it fills so the relation
    // is obvious. Deliberately NOT "Prompt Profiles" (further down): those
    // bind system prompts to models and never touch the connection.
    layout->addWidget(new QLabel("Provider profile:"), row, 0);
    {
        QHBoxLayout *profileRow = new QHBoxLayout();
        profileRow->setContentsMargins(0, 0, 0, 0);
        profileRow->setSpacing(4);

        _providerProfileCombo = new QComboBox(this);
        _providerProfileCombo->setToolTip(
            tr("Named endpoint configurations: provider, base URL, API key and model.\n"
               "Selecting one fills the fields below - nothing is stored until you\n"
               "press OK. Editing a field afterwards switches back to (No profile).\n"
               "Not to be confused with Prompt Profiles, which bind system prompts\n"
               "to models."));
        _providerProfileCombo->setSizeAdjustPolicy(
            QComboBox::AdjustToMinimumContentsLengthWithIcon);
        _providerProfileCombo->setMinimumContentsLength(14);
        profileRow->addWidget(_providerProfileCombo, 1);

        _saveProfileButton = new QPushButton(tr("Save as…"), this);
        _saveProfileButton->setToolTip(
            tr("Store the current provider, base URL, API key and model under a name."));
        connect(_saveProfileButton, &QPushButton::clicked,
                this, &AiSettingsWidget::onSaveProviderProfile);
        profileRow->addWidget(_saveProfileButton);

        _deleteProfileButton = new QPushButton(tr("Delete"), this);
        _deleteProfileButton->setToolTip(
            tr("Delete the selected provider profile. The fields keep their values."));
        _deleteProfileButton->setEnabled(false);
        connect(_deleteProfileButton, &QPushButton::clicked,
                this, &AiSettingsWidget::onDeleteProviderProfile);
        profileRow->addWidget(_deleteProfileButton);

        layout->addLayout(profileRow, row, 1, 1, 2);
    }
    row++;

    // Provider selection
    layout->addWidget(new QLabel("Provider:"), row, 0);
    _providerCombo = new QComboBox(this);
    _providerCombo->addItem("OpenAI", "openai");
    _providerCombo->addItem("OpenRouter", "openrouter");
    _providerCombo->addItem("Google Gemini", "gemini");
    _providerCombo->addItem("Ollama (local)", "ollama");
    _providerCombo->addItem("Custom", "custom");
    _providerCombo->setToolTip(
        tr("Where requests go. Below the separator, every saved Custom provider\n"
           "profile appears as its own entry - picking one fills base URL, API\n"
           "key and model from that profile."));
    // Saved custom endpoints are first-class entries here, below a separator.
    populateProviderComboProfiles();
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
    // The ACTIVE key wins on the way in - see storedKeyForProvider(). The
    // per-provider memory is only consulted once the page really switches
    // provider.
    _apiKeyEdit->setText(storedKeyForProvider(currentProvider, /*initialLoad*/ true));
    // Dirty tracking for the per-provider key memory: textEdited() fires ONLY
    // on a user edit, so clearing the field by hand is distinguishable from the
    // setText() that a provider switch or a keyless profile performs. Without
    // that distinction an emptied key cannot be represented in the memory at
    // all - every writer skips the empty value - and it silently returns on the
    // next provider round-trip.
    connect(_apiKeyEdit, &QLineEdit::textEdited, this, [this](const QString &) {
        const QString shown = _lastProvider.isEmpty() && _providerCombo
                                  ? _providerCombo->currentData().toString()
                                  : _lastProvider;
        if (!shown.isEmpty())
            _keyFieldEditedFor.insert(shown);
    });
    layout->addWidget(_apiKeyEdit, row, 1);

    _toggleKeyButton = new QPushButton("Show", this);
    _toggleKeyButton->setMinimumWidth(60);
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
    layout->addWidget(_modelCombo, row, 1);

    _refreshModelsButton = new QPushButton(this);
    _refreshModelsButton->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    _refreshModelsButton->setToolTip(tr("Refresh model list from provider"));
    _refreshModelsButton->setFixedWidth(32);
    connect(_refreshModelsButton, &QPushButton::clicked, this, &AiSettingsWidget::onRefreshModels);
    layout->addWidget(_refreshModelsButton, row, 2);
    row++;

        _forceStreamingButton = new QPushButton(tr("Streaming OK"), this);
        _forceStreamingButton->setToolTip(tr("Clears the temporary streaming fallback block for the selected model and tries live streaming again on the next request."));
        _forceStreamingButton->setEnabled(false);
        connect(_forceStreamingButton, &QPushButton::clicked,
            this, &AiSettingsWidget::onForceStreamingForCurrentModel);
        layout->addWidget(_forceStreamingButton, row, 1, 1, 2);
        row++;

    // ⭐ Favourites button
    auto *favBtn = new QPushButton(tr("Manage favourites\u2026"), this);
    favBtn->setToolTip(tr("Pick the models that should appear in the dropdowns.\n"
                          "Image / audio / video / embedding models are filtered out automatically."));
    connect(favBtn, &QPushButton::clicked, this, [this]() {
        ModelFavoritesDialog dlg(this);
        if (dlg.exec() == QDialog::Accepted) {
            const QString p = _providerCombo->currentData().toString();
            // Remember the model ID, not the displayed label: findData()
            // below matches ids, so a favourite whose display name differs
            // from its id used to fall through to "first entry" and quietly
            // change the configured model.
            const QString keepModel = currentModelId();
            populateModelsForProvider(p);
            int idx = _modelCombo->findData(keepModel);
            if (idx >= 0) _modelCombo->setCurrentIndex(idx);
            else if (!keepModel.isEmpty()) _modelCombo->setEditText(keepModel);
            else if (_modelCombo->count() > 0) _modelCombo->setCurrentIndex(0);
            updateStreamingBlockStatus();
        }
    });
    layout->addWidget(favBtn, row, 1, 1, 2);
    row++;

    // Phase 29: Prompt profiles button.
    auto *promptBtn = new QPushButton(tr("Prompt Profiles\u2026"), this);
    promptBtn->setToolTip(tr("Bind a custom system prompt to specific models.\n"
                             "Useful when one model needs different rules than others\n"
                             "(e.g. the shipped 'GPT-5.5 Decisive' built-in)."));
    connect(promptBtn, &QPushButton::clicked, this, [this]() {
        PromptProfileStore store;
        PromptProfilesDialog dlg(&store, this);
        dlg.exec();
    });
    layout->addWidget(promptBtn, row, 1, 1, 2);
    row++;

    // Models status label (e.g. "updated 2 days ago" / "never refreshed")
    _modelsStatusLabel = new QLabel(this);
    _modelsStatusLabel->setStyleSheet("color: gray; font-size: 11px;");
    updateModelsStatusLabel(currentProvider);
    layout->addWidget(_modelsStatusLabel, row, 1, 1, 2);
    row++;

    // Output token limit
    layout->addWidget(new QLabel("Token Limit:"), row, 0);
    _tokenLimitCheck = new QCheckBox("Limit output tokens", this);
    _tokenLimitCheck->setChecked(_settings->value("AI/max_token_enabled", false).toBool());
    _tokenLimitCheck->setToolTip("When enabled, limits the maximum number of output tokens per API request.\n"
                                  "By default, models use their full output capacity.\n"
                                  "Enable this if you want to control costs or if your provider\n"
                                  "charges based on max_tokens rather than actual usage.");
    layout->addWidget(_tokenLimitCheck, row, 1);
    _tokenLimitSpin = new QSpinBox(this);
    _tokenLimitSpin->setRange(1024, 131072);
    _tokenLimitSpin->setSingleStep(1024);
    _tokenLimitSpin->setValue(_settings->value("AI/max_token_limit", 16384).toInt());
    _tokenLimitSpin->setSuffix(" tokens");
    _tokenLimitSpin->setEnabled(_tokenLimitCheck->isChecked());
    connect(_tokenLimitCheck, &QCheckBox::toggled, _tokenLimitSpin, &QSpinBox::setEnabled);
    layout->addWidget(_tokenLimitSpin, row, 2);
    row++;

    // Connect provider changes (after _modelCombo is created)
    connect(_providerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AiSettingsWidget::onProviderChanged);
        connect(_modelCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AiSettingsWidget::updateStreamingBlockStatus);
    // Apply current provider state (hides API key for local, sets URL)
    onProviderChanged(_providerCombo->currentIndex());
    // ... and put the model from the settings back: the call above repopulates
    // the list and lands on entry 0, which would show (and on OK save) a model
    // the user never picked - and would make every stored provider profile
    // look edited the moment the page opens.
    {
        int savedIdx = _modelCombo->findData(currentModel);
        if (savedIdx >= 0)
            _modelCombo->setCurrentIndex(savedIdx);
        else if (!currentModel.isEmpty())
            _modelCombo->setEditText(currentModel);
    }
        updateStreamingBlockStatus();

    // Phase 50: keep the provider-profile combo in sync with the fields. The
    // selection is DERIVED (see updateProviderProfileSelection), so any edit
    // after applying a profile honestly falls back to "(No profile)".
    connect(_providerProfileCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AiSettingsWidget::onProviderProfileSelected);
    connect(_providerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AiSettingsWidget::updateProviderProfileSelection);
    connect(_baseUrlEdit, &QLineEdit::textChanged,
            this, &AiSettingsWidget::updateProviderProfileSelection);
    // Typing a local URL under "Custom" makes the endpoint keyless - say so
    // while it is being typed, not only after a provider switch.
    connect(_baseUrlEdit, &QLineEdit::textChanged,
            this, &AiSettingsWidget::updateKeyFieldHint);
    connect(_apiKeyEdit, &QLineEdit::textChanged,
            this, &AiSettingsWidget::updateProviderProfileSelection);
    connect(_modelCombo, &QComboBox::currentTextChanged,
            this, &AiSettingsWidget::updateProviderProfileSelection);
    populateProviderProfiles();

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

    // Live streaming for the agent loop (Phase 25.1)
    layout->addWidget(new QLabel("Live Streaming:"), row, 0);
    _streamingCheck = new QCheckBox(
        "Stream agent responses live (text + tool-call arguments)", this);
    _streamingCheck->setToolTip(
        "When enabled, MidiPilot streams the AI's response token-by-token "
        "instead of waiting for the full reply. Recommended; turn off only "
        "if your network is flaky or your provider's SSE implementation is "
        "buggy.");
    QString streamMode = _settings->value("AI/streaming_mode", "on").toString();
    _streamingCheck->setChecked(streamMode != "off");
    layout->addWidget(_streamingCheck, row, 1, 1, 2);
    row++;

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

    // System Prompts
    layout->addWidget(new QLabel("System Prompts:"), row, 0);
    _editPromptsButton = new QPushButton("Edit System Prompts...", this);
    _editPromptsButton->setToolTip("Open the system prompt editor to customize AI behavior");
    connect(_editPromptsButton, &QPushButton::clicked, this, &AiSettingsWidget::onEditSystemPrompts);
    layout->addWidget(_editPromptsButton, row, 1);
    _promptsStatusLabel = new QLabel(this);
    if (EditorContext::hasCustomPrompts()) {
        _promptsStatusLabel->setText(QString::fromUtf8("\xe2\x97\x8f Custom"));
        _promptsStatusLabel->setStyleSheet("color: green;");
    } else {
        _promptsStatusLabel->setText(QString::fromUtf8("\xe2\x97\x8b Default"));
        _promptsStatusLabel->setStyleSheet("color: gray;");
    }
    layout->addWidget(_promptsStatusLabel, row, 2);
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

    // --- MCP Server section ---
    layout->addWidget(new QLabel("<b>MCP Server</b>"), row++, 0, 1, 3);

    layout->addWidget(new QLabel("Enable:"), row, 0);
    _mcpEnableCheck = new QCheckBox("Start MCP server on launch", this);
    _mcpEnableCheck->setChecked(_settings->value("MCP/enabled", false).toBool());
    _mcpEnableCheck->setToolTip("When enabled, MidiEditor exposes its MIDI editing tools via the\n"
                                "Model Context Protocol (MCP). Any MCP-compatible AI client\n"
                                "(Claude Desktop, VS Code Copilot, Cursor, etc.) can connect\n"
                                "and use the tools to edit MIDI files.");
    connect(_mcpEnableCheck, &QCheckBox::toggled, this, [this](bool checked) {
        // Live start/stop the MCP server when checkbox changes
        if (!_mcpServer) return;
        if (checked) {
            quint16 port = static_cast<quint16>(_mcpPortSpin->value());
            QString token = _mcpTokenEdit->text().trimmed();
            _mcpServer->setAuthToken(token.isEmpty() ? QString() : token);
            if (!_mcpServer->isRunning() || _mcpServer->port() != port) {
                _mcpServer->stop();
                _mcpServer->start(port);
            }
        } else {
            _mcpServer->stop();
        }
        updateMcpStatus();
    });
    layout->addWidget(_mcpEnableCheck, row, 1, 1, 2);
    row++;

    layout->addWidget(new QLabel("Port:"), row, 0);
    _mcpPortSpin = new QSpinBox(this);
    _mcpPortSpin->setRange(1024, 65535);
    _mcpPortSpin->setValue(_settings->value("MCP/port", 9420).toInt());
    _mcpPortSpin->setToolTip("TCP port for the MCP server (localhost only).");
    layout->addWidget(_mcpPortSpin, row, 1);

    _mcpStatusLabel = new QLabel(this);
    _mcpStatusLabel->setStyleSheet("color: gray;");
    _mcpStatusLabel->setText("Stopped");
    layout->addWidget(_mcpStatusLabel, row, 2);
    row++;

    layout->addWidget(new QLabel("Auth Token:"), row, 0);
    _mcpTokenEdit = new QLineEdit(this);
    _mcpTokenEdit->setEchoMode(QLineEdit::Password);
    _mcpTokenEdit->setPlaceholderText("(optional - leave empty for no auth)");
    _mcpTokenEdit->setText(_settings->value("MCP/auth_token").toString());
    _mcpTokenEdit->setToolTip("Optional Bearer token for authentication.\n"
                              "If set, MCP clients must include it in the Authorization header.");
    layout->addWidget(_mcpTokenEdit, row, 1);

    QPushButton *generateTokenBtn = new QPushButton("Generate", this);
    generateTokenBtn->setMinimumWidth(80);
    connect(generateTokenBtn, &QPushButton::clicked, this, [this]() {
        _mcpTokenEdit->setText(McpServer::generateToken());
        _mcpTokenEdit->setEchoMode(QLineEdit::Normal);
    });
    layout->addWidget(generateTokenBtn, row, 2);
    row++;

    // Copy config button
    layout->addWidget(new QLabel("Client Config:"), row, 0);
    _mcpCopyConfigButton = new QPushButton("Copy MCP Config to Clipboard", this);
    _mcpCopyConfigButton->setToolTip("Copies the JSON config snippet that MCP clients need.\n"
                                     "Paste it into your client's MCP configuration file.");
    connect(_mcpCopyConfigButton, &QPushButton::clicked, this, [this]() {
        int port = _mcpPortSpin->value();
        QString token = _mcpTokenEdit->text().trimmed();

        QJsonObject config;
        config["url"] = QString("http://localhost:%1/mcp").arg(port);
        if (!token.isEmpty()) {
            QJsonObject headers;
            headers["Authorization"] = QString("Bearer %1").arg(token);
            config["headers"] = headers;
        }
        QJsonObject wrapper;
        wrapper["midieditor"] = config;
        QString json = QJsonDocument(wrapper).toJson(QJsonDocument::Indented);

        QGuiApplication::clipboard()->setText(json);
        _mcpCopyConfigButton->setText("Copied!");
        QTimer::singleShot(2000, this, [this]() {
            _mcpCopyConfigButton->setText("Copy MCP Config to Clipboard");
        });
    });
    layout->addWidget(_mcpCopyConfigButton, row, 1, 1, 2);
    row++;

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

void AiSettingsWidget::setMcpServer(McpServer *server) {
    _mcpServer = server;
    if (_mcpServer) {
        connect(_mcpServer, &McpServer::started, this, [this]() {
            _mcpEnableCheck->blockSignals(true);
            _mcpEnableCheck->setChecked(true);
            _mcpEnableCheck->blockSignals(false);
            updateMcpStatus();
        });
        connect(_mcpServer, &McpServer::stopped, this, [this]() {
            _mcpEnableCheck->blockSignals(true);
            _mcpEnableCheck->setChecked(false);
            _mcpEnableCheck->blockSignals(false);
            updateMcpStatus();
        });
    }
    updateMcpStatus();
}

void AiSettingsWidget::updateMcpStatus() {
    bool running = _mcpServer && _mcpServer->isRunning();

    // Sync checkbox with actual server state
    _mcpEnableCheck->blockSignals(true);
    _mcpEnableCheck->setChecked(running);
    _mcpEnableCheck->blockSignals(false);

    if (running) {
        _mcpStatusLabel->setText(QString("Running on port %1").arg(_mcpServer->port()));
        _mcpStatusLabel->setStyleSheet("color: green;");
    } else {
        _mcpStatusLabel->setText("Stopped");
        _mcpStatusLabel->setStyleSheet("color: gray;");
    }
}

bool AiSettingsWidget::accept() {
    QString provider = _providerCombo->currentData().toString();
    _settings->setValue("AI/provider", provider);
    _settings->setValue("AI/api_base_url", _baseUrlEdit->text().trimmed());
    // Save API key per-provider and as active key. The ACTIVE key follows the
    // field exactly (an empty field really does mean "send no key"); the
    // per-provider MEMORY follows it only where the user was really on this
    // provider - same rule as the provider switch (\ref rememberKeyFieldFor),
    // so closing this page while a keyless local profile is applied cannot
    // destroy the remembered key of that provider, while a key the user cleared
    // here stays cleared instead of coming back on the next switch.
    QString key = _apiKeyEdit->text().trimmed();
    rememberKeyFieldFor(provider);
    _settings->setValue("AI/api_key", key);
    QString model = currentModelId();
    _settings->setValue("AI/model", model);
    // Phase 50: remember which provider profile these four values came from
    // (empty when the user edited them - the hint is only a hint, every reader
    // re-checks the values themselves).
    ProviderProfileStore::setActiveProfileHint(
        ProviderProfileStore::nameMatching(provider, _baseUrlEdit->text().trimmed(),
                                           model, key));
    _settings->setValue("AI/thinking_enabled", _thinkingCheck->isChecked());
    _settings->setValue("AI/reasoning_effort", _effortCombo->currentData().toString());
    _settings->setValue("AI/streaming_mode",
        _streamingCheck->isChecked() ? "on" : "off");
    _settings->setValue("AI/context_measures", _contextMeasuresSpin->value());
    _settings->setValue("AI/agent_max_steps", _agentMaxStepsSpin->value());
    _settings->setValue("AI/ffxiv_mode", _ffxivCheck->isChecked());
    _settings->setValue("AI/max_token_enabled", _tokenLimitCheck->isChecked());
    _settings->setValue("AI/max_token_limit", _tokenLimitSpin->value());
    _settings->setValue("MCP/enabled", _mcpEnableCheck->isChecked());
    _settings->setValue("MCP/port", _mcpPortSpin->value());
    _settings->setValue("MCP/auth_token", _mcpTokenEdit->text().trimmed());
    return true;
}

QIcon AiSettingsWidget::icon() {
    return QIcon(":/run_environment/graphics/tool/ai_settings.png");
}

void AiSettingsWidget::onTestConnection() {
    QString provider = _providerCombo->currentData().toString();
    QString key = _apiKeyEdit->text().trimmed();

    // Local endpoints (Ollama, a Custom server on this machine) need no API
    // key; everyone else does. One rule, defined by the client itself, so a
    // keyless local Custom profile is testable here too.
    const bool keyRequired =
        AiClient::providerRequiresKey(provider, _baseUrlEdit->text().trimmed());
    if (keyRequired && key.isEmpty()) {
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
    // While a profile is being poured into the fields we drive this combo
    // ourselves; applyProviderProfileToFields() runs applyProviderSwitch()
    // directly, so there is no nested signal to service here.
    if (_applyingProviderProfile)
        return;

    // A stored-profile entry is not a new provider id - resolve it to provider
    // "custom" plus that profile's URL, key and model, through the very same
    // code path the profile combo uses.
    const QString profileName = currentProviderComboProfile();
    if (!profileName.isEmpty()) {
        applyProviderProfileToFields(profileName);
        return;
    }

    const QString provider = _providerCombo->currentData().toString();
    if (provider.isEmpty()) {
        // The separator between the fixed providers and the profiles. It is not
        // a selectable configuration; put the selection back where it was.
        selectProviderComboEntry(_lastProvider, QString());
        return;
    }
    applyProviderSwitch(provider);
}

void AiSettingsWidget::applyProviderSwitch(const QString &provider) {
    // The very first call comes from the constructor and does NOT switch
    // provider - it just paints the configured one. Only then may the active
    // AI/api_key outrank the per-provider memory.
    const bool initialLoad = _lastProvider.isEmpty();

    // Save current key for the previous provider before switching. A keyless
    // profile leaves AI/api_key/<provider> alone on purpose
    // (ProviderProfileStore::apply); wiping it here would lose the key the
    // store just protected - but a key the USER cleared has to disappear, or it
    // resurrects the moment the provider is selected again.
    if (!_lastProvider.isEmpty() && _lastProvider != provider)
        rememberKeyFieldFor(_lastProvider);
    _lastProvider = provider;

    // Load key for the new provider
    _apiKeyEdit->setText(storedKeyForProvider(provider, initialLoad));

    // Set default base URL based on provider
    static const QMap<QString, QString> defaultUrls = {
        {"openai",     "https://api.openai.com/v1"},
        {"openrouter", "https://openrouter.ai/api/v1"},
        {"gemini",     "https://generativelanguage.googleapis.com/v1beta/openai"},
        {"ollama",     "http://localhost:11434/v1"},
    };

    if (provider == "custom") {
        // Fully user-defined.
        _baseUrlEdit->setReadOnly(false);
    } else if (provider == "ollama") {
        // Local server: default to the standard endpoint but keep it editable
        // (the host/port can differ). Only auto-fill when the field is empty or
        // still holds a cloud default, so a user-set local URL is preserved.
        const QString cur = _baseUrlEdit->text().trimmed();
        if (cur.isEmpty() || defaultUrls.value("openai") == cur ||
            defaultUrls.value("openrouter") == cur || defaultUrls.value("gemini") == cur)
            _baseUrlEdit->setText(defaultUrls.value("ollama"));
        _baseUrlEdit->setReadOnly(false);
    } else {
        _baseUrlEdit->setText(defaultUrls.value(provider, "https://api.openai.com/v1"));
        _baseUrlEdit->setReadOnly(true);
    }

    // Ollama - and a Custom endpoint on this machine - needs no API key; make
    // that obvious in the field.
    updateKeyFieldHint();

    // A "Connection successful" from the endpoint we just left must not stand
    // next to the new one (parity: the profile paths overwrite this label too).
    if (_statusLabel) {
        _statusLabel->setStyleSheet("color: gray;");
        _statusLabel->clear();
    }

    // Update model list (guard: _modelCombo may not exist during initial call)
    if (_modelCombo) {
        populateModelsForProvider(provider);
        // Select first model for the new provider
        if (_modelCombo->count() > 0)
            _modelCombo->setCurrentIndex(0);
        updateModelsStatusLabel(provider);
    }
}

void AiSettingsWidget::populateModelsForProvider(const QString &provider) {
    // Phase 50: the combo emits currentTextChanged for every entry added, and
    // each one would re-derive the provider-profile selection from the stored
    // settings. Suppress that here; the caller settles the selection once.
    QScopedValueRollback<bool> noProfileChurn(_applyingProviderProfile, true);
    _modelCombo->clear();

    auto addModel = [this, &provider](const QString &label, const QString &id) {
        QString itemLabel = label.isEmpty() ? id : label;
        const bool simpleBlocked = AiClient::streamingBlockedForSession(provider, id, false);
        const bool agentBlocked  = AiClient::streamingBlockedForSession(provider, id, true);
        if (simpleBlocked || agentBlocked) {
            QString suffix;
            QString tip;
            if (simpleBlocked && agentBlocked) {
                suffix = tr(" (Simple+Agent)");
                tip = tr("Live streaming failed for this model in both Simple and Agent mode this session. Select it and click Force Streaming to try again.");
            } else if (simpleBlocked) {
                suffix = tr(" (Simple)");
                tip = tr("Live streaming failed in Simple Mode this session. Agent Mode still streams. Select it and click Force Streaming to retry Simple Mode.");
            } else {
                suffix = tr(" (Agent)");
                tip = tr("Live streaming failed in Agent Mode this session. Simple Mode still streams. Select it and click Force Streaming to retry Agent Mode.");
            }
            itemLabel = tr("⚠ %1%2").arg(itemLabel, suffix);
            _modelCombo->addItem(itemLabel, id);
            _modelCombo->setItemData(_modelCombo->count() - 1, tip, Qt::ToolTipRole);
        } else {
            _modelCombo->addItem(itemLabel, id);
        }
    };

    // Phase 26: prefer cached entries from <userdata>/midipilot_models.json
    // Phase 26.1: filter via ModelFavorites (drops non-LLM models, restricts
    // to favourites if any are set).
    // Phase 50 follow-up: both are keyed by scope, so a custom profile shows
    // its own endpoint's models and its own favourites.
    const QString scope = modelScopeFor(provider);
    QJsonArray cached = ModelListCache::models(scope);
    QJsonArray visible = ModelFavorites::visibleModels(scope, cached);
    if (!visible.isEmpty()) {
        for (const QJsonValue &v : visible) {
            QJsonObject m = v.toObject();
            QString id = m.value(QStringLiteral("id")).toString();
            QString display = m.value(QStringLiteral("displayName")).toString();
            if (id.isEmpty())
                continue;
            addModel(display, id);
        }
        updateStreamingBlockStatus();
        return;
    }

    // Fallback: built-in hardcoded list (kept as last-resort safety net so a
    // first-run/offline user still gets a usable model picker).
    if (provider == "openai") {
        addModel("gpt-4o-mini", "gpt-4o-mini");
        addModel("gpt-4o", "gpt-4o");
        addModel("gpt-4.1-nano", "gpt-4.1-nano");
        addModel("gpt-4.1-mini", "gpt-4.1-mini");
        addModel("gpt-4.1", "gpt-4.1");
        addModel("gpt-5", "gpt-5");
        addModel("gpt-5-mini", "gpt-5-mini");
        addModel("gpt-5.4", "gpt-5.4");
        addModel("gpt-5.4-mini", "gpt-5.4-mini");
        addModel("gpt-5.4-nano", "gpt-5.4-nano");
        addModel("o4-mini", "o4-mini");
    } else if (provider == "openrouter") {
        addModel("openai/gpt-5.4", "openai/gpt-5.4");
        addModel("openai/gpt-4.1", "openai/gpt-4.1");
        addModel("anthropic/claude-sonnet-4", "anthropic/claude-sonnet-4");
        addModel("anthropic/claude-3.5-sonnet", "anthropic/claude-3.5-sonnet");
        addModel("google/gemini-2.5-pro", "google/gemini-2.5-pro");
        addModel("google/gemini-2.5-flash", "google/gemini-2.5-flash");
        addModel("meta-llama/llama-4-maverick", "meta-llama/llama-4-maverick");
    } else if (provider == "gemini") {
        addModel("gemini-2.5-flash", "gemini-2.5-flash");
        addModel("gemini-2.5-flash-lite", "gemini-2.5-flash-lite");
        addModel("gemini-2.5-pro", "gemini-2.5-pro");
        addModel("gemini-3-flash", "gemini-3-flash-preview");
        addModel("gemini-3.1-flash-lite", "gemini-3.1-flash-lite-preview");
        addModel("gemini-3.1-pro", "gemini-3.1-pro-preview");
    } else {
        // Custom provider — no presets, user types the model
        addModel("(enter model name)", "");
    }
    updateStreamingBlockStatus();
}

void AiSettingsWidget::updateStreamingBlockStatus()
{
    if (!_forceStreamingButton || !_modelCombo || !_providerCombo)
        return;
    QString provider = _providerCombo->currentData().toString();
    QString model = _modelCombo->currentData().toString();
    if (model.isEmpty())
        model = _modelCombo->currentText().trimmed();
    const bool simpleBlocked = AiClient::streamingBlockedForSession(provider, model, false);
    const bool agentBlocked  = AiClient::streamingBlockedForSession(provider, model, true);
    const bool blocked = simpleBlocked || agentBlocked;
    _forceStreamingButton->setEnabled(blocked);
    if (!blocked) {
        _forceStreamingButton->setText(tr("Streaming OK"));
        _forceStreamingButton->setToolTip(QString());
    } else if (simpleBlocked && agentBlocked) {
        _forceStreamingButton->setText(tr("Force Streaming (Simple + Agent blocked)"));
        _forceStreamingButton->setToolTip(tr("Streaming was disabled for this model in both Simple and Agent mode this session."));
    } else if (simpleBlocked) {
        _forceStreamingButton->setText(tr("Force Streaming (Simple Mode blocked)"));
        _forceStreamingButton->setToolTip(tr("Streaming was disabled for Simple Mode this session. Agent Mode still streams."));
    } else {
        _forceStreamingButton->setText(tr("Force Streaming (Agent Mode blocked)"));
        _forceStreamingButton->setToolTip(tr("Streaming was disabled for Agent Mode this session. Simple Mode still streams."));
    }
}

void AiSettingsWidget::onForceStreamingForCurrentModel()
{
    QString provider = _providerCombo->currentData().toString();
    QString model = _modelCombo->currentData().toString();
    if (model.isEmpty())
        model = _modelCombo->currentText().trimmed();
    if (model.isEmpty())
        return;

    AiClient::clearStreamingBlockForSession(provider, model);
    populateModelsForProvider(provider);
    int idx = _modelCombo->findData(model);
    if (idx >= 0)
        _modelCombo->setCurrentIndex(idx);
    else
        _modelCombo->setEditText(model);
    updateStreamingBlockStatus();
}

void AiSettingsWidget::updateModelsStatusLabel(const QString &provider)
{
    if (!_modelsStatusLabel)
        return;
    const QString scope = modelScopeFor(provider);
    QDateTime ts = ModelListCache::lastFetched(scope);
    if (!ts.isValid()) {
        _modelsStatusLabel->setText(tr("Models: built-in list (click \xF0\x9F\x94\x84 to fetch from provider)"));
        return;
    }
    qint64 days = ts.daysTo(QDateTime::currentDateTimeUtc());
    QString rel;
    if (days <= 0) rel = tr("today");
    else if (days == 1) rel = tr("yesterday");
    else rel = tr("%1 days ago").arg(days);
    QString staleHint = ModelListCache::isStale(scope) ? tr(" — refresh recommended") : QString();
    _modelsStatusLabel->setText(tr("Models updated %1%2").arg(rel, staleHint));
}

void AiSettingsWidget::onRefreshModels()
{
    QString provider = _providerCombo->currentData().toString();
    QString apiKey = _apiKeyEdit->text().trimmed();
    QString baseUrl = _baseUrlEdit->text().trimmed();

    _refreshModelsButton->setEnabled(false);
    _modelsStatusLabel->setText(tr("Fetching models from %1\xE2\x80\xA6").arg(provider));

    auto *fetcher = new ModelListFetcher(this);
    connect(fetcher, &ModelListFetcher::finished,
            this, &AiSettingsWidget::onModelsFetched);
    connect(fetcher, &ModelListFetcher::failed,
            this, &AiSettingsWidget::onModelsFetchFailed);
    // The result is filed under the scope of the endpoint in the fields, not
    // under the bare provider - two custom endpoints must not overwrite each
    // other's cached list.
    fetcher->fetch(provider, apiKey, baseUrl, modelScopeFor(provider));
}

void AiSettingsWidget::onModelsFetched(const QString &scope, const QJsonArray &models)
{
    ModelListCache::store(scope, models);
    _refreshModelsButton->setEnabled(true);

    const QString provider = _providerCombo->currentData().toString();
    // Only refill when the fields still describe the endpoint we fetched for.
    if (modelScopeFor(provider) == scope) {
        QString currentText = _modelCombo->currentText();
        populateModelsForProvider(provider);
        int idx = _modelCombo->findData(currentText);
        if (idx >= 0)
            _modelCombo->setCurrentIndex(idx);
        else
            _modelCombo->setEditText(currentText);
        updateModelsStatusLabel(provider);
    }
}

void AiSettingsWidget::onModelsFetchFailed(const QString &scope, const QString &error)
{
    Q_UNUSED(scope);
    _refreshModelsButton->setEnabled(true);
    _modelsStatusLabel->setText(tr("Refresh failed: %1").arg(error));
}

QString AiSettingsWidget::currentModelId() const
{
    // The combo is editable, and several paths restore a model with
    // setEditText() while the current INDEX still points at entry 0 (a model
    // may be missing from the list, or only cached under another scope).
    // currentData() would then report entry 0's id and Close would silently
    // save a model the user never picked - so the line edit wins whenever it
    // no longer shows the current item.
    const QString typed = _modelCombo->currentText().trimmed();
    const int idx = _modelCombo->currentIndex();
    if (idx >= 0 && typed == _modelCombo->itemText(idx).trimmed()) {
        const QString id = _modelCombo->itemData(idx).toString();
        if (!id.isEmpty())
            return id;  // display name != id (favourites carry both)
    }
    return typed;
}

QString AiSettingsWidget::storedKeyForProvider(const QString &provider,
                                              bool initialLoad) const
{
    if (provider.isEmpty())
        return QString();
    if (initialLoad
        && _settings->value(QStringLiteral("AI/provider"),
                            QStringLiteral("openai")).toString() == provider
        && _settings->contains(QStringLiteral("AI/api_key"))) {
        // The active pair belongs together: accept() and
        // ProviderProfileStore::apply() always write AI/provider and
        // AI/api_key in one go. An EMPTY active key is a real answer here
        // ("this endpoint is keyless"), never a reason to fall back.
        return _settings->value(QStringLiteral("AI/api_key")).toString();
    }
    const QString remembered =
        _settings->value(QStringLiteral("AI/api_key/%1").arg(provider)).toString();
    // The URL that will be in force for the provider being entered: for Custom
    // the field is left exactly as it is, so what stands here IS that endpoint.
    const QString url = _baseUrlEdit ? _baseUrlEdit->text().trimmed() : QString();
    return ProviderProfileStore::keyMemoryOnEnter(
        provider, remembered, AiClient::providerRequiresKey(provider, url));
}

void AiSettingsWidget::rememberKeyFieldFor(const QString &provider)
{
    if (provider.isEmpty() || !_apiKeyEdit)
        return;
    const QString memoryKey = QStringLiteral("AI/api_key/%1").arg(provider);
    const QString fieldKey = _apiKeyEdit->text().trimmed();
    switch (ProviderProfileStore::keyMemoryActionOnLeave(
        fieldKey, _keyFieldEditedFor.contains(provider))) {
    case ProviderProfileStore::KeyMemoryAction::Store:
        _settings->setValue(memoryKey, fieldKey);
        break;
    case ProviderProfileStore::KeyMemoryAction::Erase:
        _settings->remove(memoryKey);
        break;
    case ProviderProfileStore::KeyMemoryAction::Keep:
        break;
    }
}

void AiSettingsWidget::updateKeyFieldHint()
{
    if (!_apiKeyEdit || !_baseUrlEdit)
        return;
    QString provider = _providerCombo ? _providerCombo->currentData().toString()
                                      : QString();
    if (provider.isEmpty())
        provider = _lastProvider;
    if (AiClient::providerRequiresKey(provider, _baseUrlEdit->text().trimmed())) {
        _apiKeyEdit->setPlaceholderText(QStringLiteral("sk-..."));
    } else if (provider == QStringLiteral("ollama")) {
        _apiKeyEdit->setPlaceholderText(tr("(not required for Ollama)"));
    } else {
        _apiKeyEdit->setPlaceholderText(tr("(not required for a local endpoint)"));
    }
}

QString AiSettingsWidget::modelScopeFor(const QString &provider) const
{
    // Deliberately endpoint-based (provider + base URL + key) and independent
    // of the selected model: picking another model in the combo must not move
    // the user to a different favourites/cache scope mid-edit.
    return ProviderProfileStore::modelScopeId(provider,
                                              _baseUrlEdit->text().trimmed(),
                                              _apiKeyEdit->text().trimmed());
}

void AiSettingsWidget::populateProviderComboProfiles()
{
    if (!_providerCombo)
        return;

    // Remember what is selected: removeItem() below shifts the current index.
    const QString keepProvider = _providerCombo->currentData().toString();
    const QString keepProfile = currentProviderComboProfile();

    const bool blocked = _providerCombo->blockSignals(true);
    while (_providerCombo->count() > kFixedProviderCount)
        _providerCombo->removeItem(_providerCombo->count() - 1);

    bool separatorAdded = false;
    const QStringList names = ProviderProfileStore::profileNames();
    for (const QString &n : names) {
        bool ok = false;
        const ProviderProfileStore::Profile p = ProviderProfileStore::load(n, &ok);
        // Only CUSTOM profiles get an entry: they are the ones that change
        // which server answers. A profile of a built-in provider would be a
        // second "OpenAI" line that means the same endpoint.
        if (!ok || p.provider.compare(QStringLiteral("custom"), Qt::CaseInsensitive) != 0)
            continue;
        if (!separatorAdded) {
            _providerCombo->insertSeparator(_providerCombo->count());
            separatorAdded = true;
        }
        _providerCombo->addItem(p.name, QStringLiteral("custom"));
        _providerCombo->setItemData(_providerCombo->count() - 1, p.name,
                                    kProviderProfileRole);
    }
    _providerCombo->blockSignals(blocked);

    selectProviderComboEntry(keepProvider, keepProfile);
}

QString AiSettingsWidget::currentProviderComboProfile() const
{
    if (!_providerCombo)
        return QString();
    return _providerCombo->itemData(_providerCombo->currentIndex(),
                                    kProviderProfileRole).toString();
}

void AiSettingsWidget::selectProviderComboEntry(const QString &provider,
                                                const QString &profileName)
{
    if (!_providerCombo)
        return;
    int idx = -1;
    if (!profileName.isEmpty())
        idx = _providerCombo->findData(profileName, kProviderProfileRole);
    if (idx < 0 && !provider.isEmpty())
        idx = _providerCombo->findData(provider);  // the fixed entry wins
    if (idx < 0 || idx == _providerCombo->currentIndex())
        return;
    const bool blocked = _providerCombo->blockSignals(true);
    _providerCombo->setCurrentIndex(idx);
    _providerCombo->blockSignals(blocked);
}

void AiSettingsWidget::updateProviderComboSelection()
{
    if (!_providerCombo || !_baseUrlEdit || !_apiKeyEdit)
        return;
    const QString provider = _providerCombo->currentData().toString();
    if (provider.isEmpty())
        return;
    // Endpoint identity, not the exact saved configuration: picking another
    // model keeps you on the same server, so the provider entry stays - only
    // the profile combo below falls back to "(No profile)".
    // The profile combo's live selection outranks the stored hint: with two
    // profiles on one endpoint the hint may name the other one, and then the
    // two pickers would show different names for the same connection.
    const QString preferred = _providerProfileCombo
                                  ? _providerProfileCombo->currentData().toString()
                                  : QString();
    const QString name = ProviderProfileStore::nameMatchingEndpoint(
        provider, _baseUrlEdit->text().trimmed(), _apiKeyEdit->text().trimmed(),
        preferred);
    selectProviderComboEntry(provider, name);
}

void AiSettingsWidget::populateProviderProfiles(const QString &selectName)
{
    // One spot, both pickers: the provider combo lists the same stored custom
    // profiles, so adding or deleting one updates them together.
    populateProviderComboProfiles();
    if (!_providerProfileCombo)
        return;
    const bool blocked = _providerProfileCombo->blockSignals(true);
    _providerProfileCombo->clear();
    _providerProfileCombo->addItem(tr("(No profile)"), QString());
    const QStringList names = ProviderProfileStore::profileNames();
    for (const QString &n : names)
        _providerProfileCombo->addItem(n, n);
    _providerProfileCombo->blockSignals(blocked);

    if (!selectName.isEmpty()) {
        int idx = _providerProfileCombo->findData(selectName);
        if (idx >= 0) {
            const bool b = _providerProfileCombo->blockSignals(true);
            _providerProfileCombo->setCurrentIndex(idx);
            _providerProfileCombo->blockSignals(b);
        }
        if (_deleteProfileButton)
            _deleteProfileButton->setEnabled(idx > 0);
        return;
    }
    updateProviderProfileSelection();
}

void AiSettingsWidget::updateProviderProfileSelection()
{
    if (!_providerProfileCombo || _applyingProviderProfile)
        return;

    const QString provider = _providerCombo->currentData().toString();
    const QString url = _baseUrlEdit->text().trimmed();
    const QString key = _apiKeyEdit->text().trimmed();
    const QString model = currentModelId();

    // Keep the current selection when it still describes the fields (two
    // profiles may hold identical settings); otherwise ask the store.
    QString match = _providerProfileCombo->currentData().toString();
    if (match.isEmpty() || !ProviderProfileStore::matches(match, provider, url, model, key))
        match = ProviderProfileStore::nameMatching(provider, url, model, key);

    int idx = match.isEmpty() ? 0 : _providerProfileCombo->findData(match);
    if (idx < 0)
        idx = 0;
    const bool blocked = _providerProfileCombo->blockSignals(true);
    _providerProfileCombo->setCurrentIndex(idx);
    _providerProfileCombo->blockSignals(blocked);
    if (_deleteProfileButton)
        _deleteProfileButton->setEnabled(idx > 0);

    // Keep the two pickers mutually in sync. Both setters block signals, so
    // neither can re-enter the other's handler.
    updateProviderComboSelection();
}

void AiSettingsWidget::onProviderProfileSelected(int /*index*/)
{
    const QString name = _providerProfileCombo->currentData().toString();
    if (name.isEmpty()) {
        // "(No profile)" keeps whatever is in the fields - picking it is not
        // a command to change anything, it just says "these are ad-hoc".
        if (_deleteProfileButton)
            _deleteProfileButton->setEnabled(false);
        return;
    }
    applyProviderProfileToFields(name);
}

void AiSettingsWidget::applyProviderProfileToFields(const QString &name)
{
    bool ok = false;
    const ProviderProfileStore::Profile p = ProviderProfileStore::load(name, &ok);
    if (!ok) {
        // Vanished behind our back (portable copy, hand-edited settings).
        populateProviderProfiles();
        return;
    }

    // Pour the profile into the visible fields. Nothing is persisted here -
    // accept() writes the four active keys as it always did, so Cancel keeps
    // the previous configuration.
    {
        QScopedValueRollback<bool> applying(_applyingProviderProfile, true);
        // Show the profile's own entry while we work, so every currentData()
        // read below already sees this endpoint's provider id.
        selectProviderComboEntry(p.provider, name);
        // Exactly the steps a fixed provider entry runs (remember the old key,
        // load the new provider's key, default URL, placeholder, model list) -
        // called directly instead of through the combo, so nothing re-enters.
        applyProviderSwitch(p.provider);
        _baseUrlEdit->setText(p.baseUrl);
        _apiKeyEdit->setText(ProviderProfileStore::apiKeyFor(name));
        // The provider switch above filled the model list from the OLD endpoint -
        // the URL and key only became this profile's a line ago. Refill now that
        // all three fields agree, so the combo shows this endpoint's cached models
        // and this endpoint's favourites.
        populateModelsForProvider(p.provider);
        if (!p.model.isEmpty()) {
            int mIdx = _modelCombo->findData(p.model);
            if (mIdx >= 0)
                _modelCombo->setCurrentIndex(mIdx);
            else
                _modelCombo->setEditText(p.model);
        } else if (_modelCombo->count() > 0) {
            _modelCombo->setCurrentIndex(0);
        }
    }

    updateStreamingBlockStatus();
    updateModelsStatusLabel(p.provider);
    updateProviderProfileSelection();  // also re-derives the provider combo
    if (_statusLabel) {
        _statusLabel->setStyleSheet("color: gray;");
        _statusLabel->setText(
            tr("Provider profile \"%1\" loaded into the fields.").arg(p.name));
    }
}

void AiSettingsWidget::onSaveProviderProfile()
{
    const QString provider = _providerCombo->currentData().toString();
    if (provider.isEmpty())
        return;

    bool ok = false;
    QString suggestion = _providerProfileCombo->currentData().toString();
    if (suggestion.isEmpty())
        suggestion = _providerCombo->currentText();
    QString name = QInputDialog::getText(
        this, tr("Save provider profile"),
        tr("Name for this endpoint (provider, base URL, API key and model):"),
        QLineEdit::Normal, suggestion, &ok);
    if (!ok)
        return;

    name = ProviderProfileStore::normalizeName(name);
    if (name.isEmpty()) {
        QMessageBox::warning(this, tr("Save provider profile"),
                             tr("Please enter a name for the profile."));
        return;
    }
    if (ProviderProfileStore::exists(name)
        && QMessageBox::question(
               this, tr("Save provider profile"),
               tr("A provider profile named \"%1\" already exists. Overwrite it?").arg(name))
               != QMessageBox::Yes) {
        return;
    }

    ProviderProfileStore::Profile p;
    p.name = name;
    p.provider = provider;
    p.baseUrl = _baseUrlEdit->text().trimmed();
    p.model = currentModelId();
    const QString apiKey = _apiKeyEdit->text().trimmed();
    // Judged BEFORE the overwrite: does this name change which server it
    // points at? Its cached model list and favourites are keyed by NAME, so
    // they would otherwise survive the move and describe the old endpoint.
    const bool endpointChanged =
        ProviderProfileStore::exists(name)
        && !ProviderProfileStore::matchesEndpoint(name, provider, p.baseUrl, apiKey);
    // The scope the VISIBLE fields belong to right now - the model list and
    // the favourites the user is looking at while pressing Save.
    const QString scopeBefore = modelScopeFor(provider);
    if (!ProviderProfileStore::save(p, apiKey)) {
        QMessageBox::warning(this, tr("Save provider profile"),
                             tr("Could not store the profile."));
        return;
    }
    // Hand the endpoint's model list and favourites over to the scope that is
    // in force from here on. Without this, re-pointing a profile at another
    // server keeps the OLD server's list and favourites, and naming a
    // previously ad-hoc Custom endpoint starts from an empty scope, collapsing
    // the model dropdown to the placeholder. A profile whose endpoint did NOT
    // change keeps whatever it already had.
    const QString scopeAfter = ProviderProfileStore::modelScopeIdForProfile(name);
    if (!scopeAfter.isEmpty() && scopeAfter != scopeBefore) {
        const QJsonArray fieldsModels = ModelListCache::models(scopeBefore);
        const bool targetHasCache = !ModelListCache::models(scopeAfter).isEmpty();
        // Take over when the target list is stale (endpoint moved) or absent -
        // and only write at all when that actually changes something, so an
        // untouched scope keeps its honest "never fetched" state.
        if ((endpointChanged || !targetHasCache)
            && (!fieldsModels.isEmpty() || targetHasCache))
            ModelListCache::store(scopeAfter, fieldsModels);
        if (endpointChanged || !ModelFavorites::hasFavorites(scopeAfter)) {
            const QSet<QString> favs = ModelFavorites::favorites(scopeBefore);
            // An empty list REMOVES the entry - exactly right for a stale
            // favourite set of the endpoint this name used to point at.
            ModelFavorites::setFavorites(scopeAfter,
                                         QStringList(favs.cbegin(), favs.cend()));
        }
    }
    // Parity with the footer's "Save connection as provider profile...": the
    // endpoint you just named IS the active one, so say so. Without the hint
    // two profiles sharing an endpoint would resolve to whichever sorts first.
    ProviderProfileStore::setActiveProfileHint(name);
    populateProviderProfiles(name);
    // A custom endpoint that had no profile lived in the shared ad-hoc "custom"
    // model-list / favourites scope; it now owns "custom:profile:<id>". Refill
    // so the combo and the status line show the scope that is in force from
    // here on, instead of waiting for the next edit to re-derive it.
    {
        const QString keepModel = currentModelId();
        QScopedValueRollback<bool> applying(_applyingProviderProfile, true);
        populateModelsForProvider(p.provider);
        int idx = _modelCombo->findData(keepModel);
        if (idx >= 0)
            _modelCombo->setCurrentIndex(idx);
        else if (!keepModel.isEmpty())
            _modelCombo->setEditText(keepModel);
    }
    updateModelsStatusLabel(p.provider);
    updateStreamingBlockStatus();
    updateProviderComboSelection();
    _statusLabel->setStyleSheet("color: green;");
    _statusLabel->setText(tr("Provider profile \"%1\" saved.").arg(name));
}

void AiSettingsWidget::onDeleteProviderProfile()
{
    const QString name = _providerProfileCombo->currentData().toString();
    if (name.isEmpty())
        return;
    if (QMessageBox::question(
            this, tr("Delete provider profile"),
            tr("Delete the provider profile \"%1\"? The fields keep their current values.")
                .arg(name))
        != QMessageBox::Yes) {
        return;
    }
    ProviderProfileStore::remove(name);
    populateProviderProfiles();
    // Mirror of the save path: the endpoint just fell back to the shared ad-hoc
    // "custom" scope, so its model list and favourites changed under us.
    {
        const QString provider = _providerCombo->currentData().toString();
        const QString keepModel = currentModelId();
        QScopedValueRollback<bool> applying(_applyingProviderProfile, true);
        populateModelsForProvider(provider);
        int idx = _modelCombo->findData(keepModel);
        if (idx >= 0)
            _modelCombo->setCurrentIndex(idx);
        else if (!keepModel.isEmpty())
            _modelCombo->setEditText(keepModel);
        updateModelsStatusLabel(provider);
    }
    updateStreamingBlockStatus();
    _statusLabel->setStyleSheet("color: gray;");
    _statusLabel->setText(tr("Provider profile \"%1\" deleted.").arg(name));
}

void AiSettingsWidget::onEditSystemPrompts()
{
    SystemPromptDialog dialog(this);
    dialog.exec();

    // Update status indicator after dialog closes
    if (EditorContext::hasCustomPrompts()) {
        _promptsStatusLabel->setText(QString::fromUtf8("\xe2\x97\x8f Custom"));
        _promptsStatusLabel->setStyleSheet("color: green;");
    } else {
        _promptsStatusLabel->setText(QString::fromUtf8("\xe2\x97\x8b Default"));
        _promptsStatusLabel->setStyleSheet("color: gray;");
    }
}
