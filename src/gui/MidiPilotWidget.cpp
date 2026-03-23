#include "MidiPilotWidget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTextEdit>
#include <QFrame>
#include <QTime>
#include <QTimer>
#include <QSet>
#include <QSettings>
#include <QKeyEvent>
#include <QComboBox>
#include <QApplication>
#include <QRandomGenerator>
#include <QCheckBox>
#include <QMessageBox>

#include <cmath>
#include <algorithm>

#include "MainWindow.h"
#include "../ai/AiClient.h"
#include "../ai/AgentRunner.h"
#include "../ai/EditorContext.h"
#include "../ai/MidiEventSerializer.h"
#include "../tool/Selection.h"
#include "../tool/NewNoteTool.h"
#include "../midi/MidiFile.h"
#include "../midi/MidiChannel.h"
#include "../midi/MidiTrack.h"
#include "../MidiEvent/MidiEvent.h"
#include "../MidiEvent/NoteOnEvent.h"
#include "../MidiEvent/OffEvent.h"
#include "../MidiEvent/TempoChangeEvent.h"
#include "../MidiEvent/TimeSignatureEvent.h"
#include "../protocol/Protocol.h"
#include "../MidiEvent/ProgChangeEvent.h"
#include "../MidiEvent/ControlChangeEvent.h"

// Build a concise musical summary of created events so the model
// can compose coherent follow-up tracks (key, register, rhythm).
static QJsonObject buildMusicalSummary(const QList<MidiEvent *> &events) {
    QJsonObject summary;
    int noteCount = 0, ccCount = 0, progChange = -1;
    int minNote = 128, maxNote = -1;
    int minTick = INT_MAX, maxTick = 0;
    QSet<int> pitchClasses;

    for (MidiEvent *ev : events) {
        int t = ev->midiTime();
        if (t < minTick) minTick = t;
        if (t > maxTick) maxTick = t;

        if (auto *on = dynamic_cast<NoteOnEvent *>(ev)) {
            noteCount++;
            int n = on->note();
            if (n < minNote) minNote = n;
            if (n > maxNote) maxNote = n;
            pitchClasses.insert(n % 12);
        } else if (dynamic_cast<ControlChangeEvent *>(ev)) {
            ccCount++;
        } else if (auto *pc = dynamic_cast<ProgChangeEvent *>(ev)) {
            progChange = pc->program();
        }
    }

    static const char *noteNames[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    auto noteName = [](int midi) -> QString {
        return QString("%1%2").arg(noteNames[midi % 12]).arg(midi / 12 - 1);
    };

    summary["noteCount"] = noteCount;
    if (noteCount > 0) {
        summary["noteRange"] = noteName(minNote) + "-" + noteName(maxNote);
        summary["tickRange"] = QString("%1-%2").arg(minTick).arg(maxTick);
        QList<int> pcList(pitchClasses.begin(), pitchClasses.end());
        std::sort(pcList.begin(), pcList.end());
        QJsonArray pcArr;
        for (int pc : pcList) pcArr.append(QString(noteNames[pc]));
        summary["pitchClasses"] = pcArr;
    }
    if (ccCount > 0) summary["ccCount"] = ccCount;
    if (progChange >= 0) summary["gmProgram"] = progChange;

    return summary;
}

// ============================================================
// Collapsible Agent Steps Widget (displayed in chat area)
// ============================================================
class AgentStepsWidget : public QWidget {
public:
    AgentStepsWidget(QWidget *parent = nullptr) : QWidget(parent) {
        QVBoxLayout *layout = new QVBoxLayout(this);
        layout->setContentsMargins(8, 6, 8, 6);
        layout->setSpacing(0);

        // Header with collapse/expand toggle
        _headerBtn = new QPushButton(this);
        _headerBtn->setFlat(true);
        _headerBtn->setCursor(Qt::PointingHandCursor);
        _headerBtn->setStyleSheet(
            "text-align: left; font-weight: bold; font-size: 12px; "
            "padding: 4px 2px; color: #555;");
        connect(_headerBtn, &QPushButton::clicked, this, [this]() {
            _collapsed = !_collapsed;
            _stepsContainer->setVisible(!_collapsed);
            updateHeader();
        });
        layout->addWidget(_headerBtn);

        // Steps container
        _stepsContainer = new QWidget(this);
        _stepsLayout = new QVBoxLayout(_stepsContainer);
        _stepsLayout->setContentsMargins(4, 2, 0, 4);
        _stepsLayout->setSpacing(1);
        layout->addWidget(_stepsContainer);

        setStyleSheet(
            "AgentStepsWidget { background-color: #F5F5F0; "
            "border-radius: 8px; margin-right: 40px; }");

        updateHeader();
    }

    void addStep(int step, const QString &label) {
        if (_stepLabels.contains(step)) return;  // Already planned
        QLabel *lbl = new QLabel(
            QString("\xE2\x8F\xB3 %1").arg(label), _stepsContainer);  // ⏳
        lbl->setStyleSheet("color: #CC7700; font-size: 11px; padding: 1px 2px;");
        _stepsLayout->addWidget(lbl);
        _stepLabels[step] = lbl;
        _stepNames[step] = label;
        _totalSteps++;
        updateHeader();
    }

    void planSteps(int firstStep, const QStringList &labels) {
        for (int i = 0; i < labels.size(); ++i) {
            addStep(firstStep + i, labels[i]);
        }
    }

    void markActive(int step) {
        if (!_stepLabels.contains(step)) return;
        QLabel *label = _stepLabels[step];
        QString name = _stepNames[step];
        label->setText(QString("\xF0\x9F\x94\x84 %1").arg(name));  // 🔄
        label->setStyleSheet("color: #0066CC; font-weight: bold; font-size: 11px; padding: 1px 2px;");
    }

    void completeStep(int step, bool success, bool recoverable = false) {
        if (!_stepLabels.contains(step)) return;
        QLabel *label = _stepLabels[step];
        QString name = _stepNames[step];
        if (success) {
            label->setText(QString("\xE2\x9C\x85 %1").arg(name));  // ✅
            label->setStyleSheet("color: #2D8C3C; font-size: 11px; padding: 1px 2px;");
        } else if (recoverable) {
            label->setText(QString("\xE2\x9A\xA0 %1 - retrying").arg(name));  // ⚠
            label->setStyleSheet("color: #CC7700; font-size: 11px; padding: 1px 2px;");
        } else {
            label->setText(QString("\xE2\x9D\x8C %1 - failed").arg(name));  // ❌
            label->setStyleSheet("color: #CC3333; font-size: 11px; padding: 1px 2px;");
        }
        _completedSteps++;
        updateHeader();
    }

    void setFinished(bool success) {
        _finished = true;
        _finishSuccess = success;
        updateHeader();
    }

private:
    void updateHeader() {
        QString arrow = _collapsed
            ? QString("\xE2\x96\xB6")    // ▶
            : QString("\xE2\x96\xBC");   // ▼
        QString status;
        if (_finished) {
            status = _finishSuccess
                ? QString("\xE2\x9C\x85")   // ✅
                : QString("\xE2\x9A\xA0");  // ⚠
        }
        if (_totalSteps == 0) {
            _headerBtn->setText(QString("%1 Agent Steps").arg(arrow));
        } else {
            _headerBtn->setText(QString("%1 %4 Steps (%2/%3)")
                .arg(arrow)
                .arg(_completedSteps)
                .arg(_totalSteps)
                .arg(status));
        }
    }

    QPushButton *_headerBtn;
    QWidget *_stepsContainer;
    QVBoxLayout *_stepsLayout;
    QMap<int, QLabel*> _stepLabels;
    QMap<int, QString> _stepNames;
    int _totalSteps = 0;
    int _completedSteps = 0;
    bool _collapsed = false;
    bool _finished = false;
    bool _finishSuccess = false;
};

MidiPilotWidget::MidiPilotWidget(MainWindow *mainWindow, QWidget *parent)
    : QWidget(parent), _mainWindow(mainWindow), _file(nullptr),
      _isAgentRunning(false), _agentStepsWidget(nullptr) {

    _client = new AiClient(this);
    _agentRunner = new AgentRunner(_client, this);

    setupUi();

    connect(_client, &AiClient::responseReceived, this, &MidiPilotWidget::onResponseReceived);
    connect(_client, &AiClient::errorOccurred, this, &MidiPilotWidget::onErrorOccurred);

    connect(_agentRunner, &AgentRunner::stepsPlanned, this, &MidiPilotWidget::onAgentStepsPlanned);
    connect(_agentRunner, &AgentRunner::stepStarted, this, &MidiPilotWidget::onAgentStepStarted);
    connect(_agentRunner, &AgentRunner::stepCompleted, this, &MidiPilotWidget::onAgentStepCompleted);
    connect(_agentRunner, &AgentRunner::finished, this, &MidiPilotWidget::onAgentFinished);
    connect(_agentRunner, &AgentRunner::errorOccurred, this, &MidiPilotWidget::onAgentError);
    connect(_agentRunner, &AgentRunner::stepLimitReached, this, &MidiPilotWidget::onAgentStepLimitReached);
}

// Helper: QTextEdit that sends on Enter, newline on Shift+Enter
class ChatInputEdit : public QTextEdit {
public:
    ChatInputEdit(QWidget *parent = nullptr) : QTextEdit(parent) {}
    std::function<void()> onSend;
protected:
    void keyPressEvent(QKeyEvent *e) override {
        if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
            if (e->modifiers() & Qt::ShiftModifier) {
                QTextEdit::keyPressEvent(e);
            } else {
                e->accept();
                if (onSend) onSend();
            }
        } else {
            QTextEdit::keyPressEvent(e);
        }
    }
};

void MidiPilotWidget::setupUi() {
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(6, 6, 6, 6);
    mainLayout->setSpacing(4);

    // === Context Bar (top, compact) ===
    _contextLabel = new QLabel("No file loaded", this);
    _contextLabel->setStyleSheet("font-size: 11px; color: gray; padding: 2px 4px;");
    _contextLabel->setWordWrap(true);
    mainLayout->addWidget(_contextLabel);

    // === Chat Area (takes most space) ===
    _chatScroll = new QScrollArea(this);
    _chatScroll->setWidgetResizable(true);
    _chatScroll->setFrameShape(QFrame::NoFrame);

    _chatContainer = new QWidget(_chatScroll);
    _chatLayout = new QVBoxLayout(_chatContainer);
    _chatLayout->setContentsMargins(4, 4, 4, 4);
    _chatLayout->setSpacing(8);
    _chatLayout->addStretch();

    _chatScroll->setWidget(_chatContainer);
    mainLayout->addWidget(_chatScroll, 1);

    // === Setup Prompt (shown when no API key, overlaps chat area) ===
    _setupWidget = new QWidget(this);
    QVBoxLayout *setupLayout = new QVBoxLayout(_setupWidget);
    setupLayout->setAlignment(Qt::AlignCenter);

    QLabel *setupTitle = new QLabel("Welcome to MidiPilot", _setupWidget);
    setupTitle->setStyleSheet("font-size: 14px; font-weight: bold;");
    setupTitle->setAlignment(Qt::AlignCenter);
    setupLayout->addWidget(setupTitle);

    QLabel *setupDesc = new QLabel(
        "Enter your OpenAI API key in Settings to get started.\n"
        "MidiPilot can analyze, transform, and generate MIDI events.",
        _setupWidget);
    setupDesc->setWordWrap(true);
    setupDesc->setAlignment(Qt::AlignCenter);
    setupDesc->setStyleSheet("color: gray;");
    setupLayout->addWidget(setupDesc);

    QPushButton *openSettings = new QPushButton("Open Settings", _setupWidget);
    openSettings->setFixedWidth(150);
    connect(openSettings, &QPushButton::clicked, this, &MidiPilotWidget::onSettingsClicked);
    setupLayout->addWidget(openSettings, 0, Qt::AlignCenter);

    mainLayout->addWidget(_setupWidget);

    // === Input Area (multi-line, with buttons) ===
    QFrame *inputFrame = new QFrame(this);
    inputFrame->setFrameShape(QFrame::StyledPanel);
    QVBoxLayout *inputOuterLayout = new QVBoxLayout(inputFrame);
    inputOuterLayout->setContentsMargins(4, 4, 4, 4);
    inputOuterLayout->setSpacing(4);

    ChatInputEdit *inputEdit = new ChatInputEdit(this);
    inputEdit->setPlaceholderText("Ask MidiPilot... (Enter to send, Shift+Enter for newline)");
    inputEdit->setMinimumHeight(60);
    inputEdit->setMaximumHeight(120);
    inputEdit->setAcceptRichText(false);
    inputEdit->setTabChangesFocus(true);
    _inputField = inputEdit;
    inputOuterLayout->addWidget(_inputField);

    // Buttons row inside input frame
    QHBoxLayout *inputBtnLayout = new QHBoxLayout();
    inputBtnLayout->setContentsMargins(0, 0, 0, 0);
    inputBtnLayout->setSpacing(4);

    QPushButton *newChatBtn = new QPushButton(QChar(0x2795), this); // ➕
    newChatBtn->setFixedSize(28, 28);
    newChatBtn->setToolTip("New Chat");
    newChatBtn->setFlat(true);
    connect(newChatBtn, &QPushButton::clicked, this, &MidiPilotWidget::onNewChat);
    inputBtnLayout->addWidget(newChatBtn);

    // Mode selector (Simple / Agent)
    _modeCombo = new QComboBox(this);
    _modeCombo->addItem("Simple", "simple");
    _modeCombo->addItem("Agent", "agent");
    _modeCombo->setToolTip("Simple: single-shot edits\nAgent: multi-step autonomous editing");
    _modeCombo->setFixedHeight(28);
    QSettings modeSettings;
    int modeIdx = _modeCombo->findData(modeSettings.value("AI/mode", "simple").toString());
    if (modeIdx >= 0) _modeCombo->setCurrentIndex(modeIdx);
    connect(_modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MidiPilotWidget::onModeChanged);
    inputBtnLayout->addWidget(_modeCombo);

    inputBtnLayout->addStretch();

    _sendButton = new QPushButton(QChar(0x27A4), this); // ➤
    _sendButton->setFixedSize(28, 28);
    _sendButton->setToolTip("Send");
    connect(_sendButton, &QPushButton::clicked, this, &MidiPilotWidget::onSendMessage);
    inputBtnLayout->addWidget(_sendButton);

    _stopButton = new QPushButton(QChar(0x25A0), this); // ■ (stop)
    _stopButton->setFixedSize(28, 28);
    _stopButton->setToolTip("Stop Agent");
    _stopButton->setStyleSheet("color: #CC3333; font-weight: bold;");
    _stopButton->setVisible(false);
    connect(_stopButton, &QPushButton::clicked, this, [this]() {
        if (_isAgentRunning && _agentRunner) {
            _agentRunner->cancel();
        }
    });
    inputBtnLayout->addWidget(_stopButton);

    inputOuterLayout->addLayout(inputBtnLayout);

    // Wire up Enter-to-send
    inputEdit->onSend = [this]() { onSendMessage(); };

    mainLayout->addWidget(inputFrame);

    // === Status Bar (above footer, prominent) ===
    _statusBar = new QFrame(this);
    _statusBar->setFrameShape(QFrame::NoFrame);
    _statusBar->setStyleSheet(
        "QFrame { background-color: #E8F5E9; border-radius: 6px; }");
    QHBoxLayout *statusLayout = new QHBoxLayout(_statusBar);
    statusLayout->setContentsMargins(10, 5, 10, 5);
    statusLayout->setSpacing(6);

    _statusDots = new QLabel(this);
    _statusDots->setFixedWidth(28);
    _statusDots->setStyleSheet("font-size: 13px; color: #4CAF50;");
    _statusDots->setText(QString::fromUtf8("\u25CF")); // ●
    statusLayout->addWidget(_statusDots);

    _statusLabel = new QLabel("Ready", this);
    _statusLabel->setStyleSheet(
        "font-weight: bold; font-size: 12px; color: #2E7D32;");
    statusLayout->addWidget(_statusLabel);
    statusLayout->addStretch();

    _dotPhase = 0;
    _msgPhase = 0;
    _statusTimer = new QTimer(this);
    _statusTimer->setInterval(400);
    connect(_statusTimer, &QTimer::timeout, this, [this]() {
        static const char *dotFrames[] = {
            "\u25CF \u25CB \u25CB",  // ● ○ ○
            "\u25CB \u25CF \u25CB",  // ○ ● ○
            "\u25CB \u25CB \u25CF",  // ○ ○ ●
            "\u25CB \u25CF \u25CB"   // ○ ● ○
        };
        _dotPhase = (_dotPhase + 1) % 4;
        _statusDots->setText(QString::fromUtf8(dotFrames[_dotPhase]));

        // Cycle fun status messages every ~10 seconds (every 25th tick at 400ms)
        if (_dotPhase == 0 && (_msgPhase % 6) == 0) {
            static const char *msgs[] = {
                // Classic music production
                "Counting notes...",
                "Analyzing chord progressions...",
                "Tuning virtual instruments...",
                "Checking harmony...",
                "Crunching MIDI data...",
                "Polishing melodies...",
                "Transposing stuff...",
                "Quantizing to the grid...",
                "Calculating optimal velocity...",
                "Arranging voices...",
                "Warming up the synths...",
                "Double-checking the time signature...",
                "Choosing the perfect octave...",
                "Resolving dissonances...",
                "Fine-tuning dynamics...",
                "Mixing channels...",
                "Balancing frequencies...",
                "Aligning staccatos...",
                "Calibrating the sustain pedal...",
                "Stacking harmonics...",
                // Vibes & attitude
                "Adding musical rizz...",
                "Making it slap...",
                "Adding groove...",
                "Vibing with the tempo...",
                "Generating fresh bars...",
                "Dropping the beat...",
                "Cooking up a masterpiece...",
                "Sprinkling reverb...",
                "Rolling out the red carpet for notes...",
                "Turning it up to eleven...",
                "Making the bass go brrrr...",
                "Yeeting bad notes...",
                "Adding extra sauce...",
                "Putting the lit in polyphonic literature...",
                "Achieving maximum bop...",
                // Historical & classical
                "Composing like it's 1791...",
                "Channeling inner Mozart...",
                "Consulting the music theory gods...",
                "Summoning the ghost of Beethoven...",
                "Asking Bach for permission...",
                "Borrowing Chopin's left hand...",
                "Debussy would be proud...",
                "Vivaldi called, he wants his tempo back...",
                // Nerdy & meta
                "Defragmenting the MIDI bus...",
                "Reticulating musical splines...",
                "Converting feelings to hex...",
                "Compiling emotions into bytecode...",
                "Running notes through the algorithm...",
                "Optimizing semibreve throughput...",
                "Garbage-collecting unused rests...",
                "Allocating memory for vibes...",
                // Absurd & funny
                "Teaching the AI to feel rhythm...",
                "Bribing the metronome...",
                "Negotiating with middle C...",
                "Convincing the sharps to be natural...",
                "Apologizing to the neighbors...",
                "Hiding the wrong notes...",
                "Pretending to know music theory...",
                "Googling what a fermata is...",
                "Telling the rests to be patient...",
                "Feeding the MIDI hamster..."
            };
            constexpr int msgCount = sizeof(msgs) / sizeof(msgs[0]);
            // Pick a random message instead of sequential
            int idx = QRandomGenerator::global()->bounded(msgCount);
            _statusLabel->setText(QString::fromUtf8(msgs[idx]));
        }
        if (_dotPhase == 0) _msgPhase++;
    });

    mainLayout->addWidget(_statusBar);

    // === Footer (model, settings — below status) ===
    QFrame *footerFrame = new QFrame(this);
    footerFrame->setFrameShape(QFrame::NoFrame);
    QHBoxLayout *footerLayout = new QHBoxLayout(footerFrame);
    footerLayout->setContentsMargins(4, 2, 4, 2);
    footerLayout->setSpacing(6);

    _ffxivCheck = new QCheckBox("FFXIV", this);
    _ffxivCheck->setToolTip("Enable FFXIV Bard Performance mode — constrains output to game rules\n"
                            "(C3-C6 range, monophonic, max 8 tracks, FFXIV instrument names)");
    _ffxivCheck->setStyleSheet("font-size: 11px;");
    _ffxivCheck->setChecked(QSettings().value("AI/ffxiv_mode", false).toBool());
    connect(_ffxivCheck, &QCheckBox::toggled, this, [](bool checked) {
        QSettings().setValue("AI/ffxiv_mode", checked);
    });
    footerLayout->addWidget(_ffxivCheck);

    footerLayout->addStretch();

    _modelCombo = new QComboBox(this);
    populateFooterModels();
    _modelCombo->setEditable(true);
    _modelCombo->setFixedHeight(20);
    _modelCombo->setStyleSheet("font-size: 11px;");
    int modelIdx = _modelCombo->findData(_client->model());
    if (modelIdx >= 0) _modelCombo->setCurrentIndex(modelIdx);
    else _modelCombo->setEditText(_client->model());
    connect(_modelCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MidiPilotWidget::onModelComboChanged);
    footerLayout->addWidget(_modelCombo);

    _effortCombo = new QComboBox(this);
    _effortCombo->addItem(QString::fromUtf8("\xF0\x9F\x92\xAD off"), "none");
    _effortCombo->addItem(QString::fromUtf8("\xF0\x9F\x92\xAD low"), "low");
    _effortCombo->addItem(QString::fromUtf8("\xF0\x9F\x92\xAD med"), "medium");
    _effortCombo->addItem(QString::fromUtf8("\xF0\x9F\x92\xAD high"), "high");
    _effortCombo->addItem(QString::fromUtf8("\xF0\x9F\x92\xAD xhigh"), "xhigh");
    _effortCombo->setFixedHeight(20);
    _effortCombo->setStyleSheet("font-size: 11px;");
    int effortIdx = _effortCombo->findData(_client->reasoningEffort());
    if (effortIdx >= 0) _effortCombo->setCurrentIndex(effortIdx);
    _effortCombo->setVisible(_client->isReasoningModel());
    connect(_effortCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MidiPilotWidget::onEffortComboChanged);
    footerLayout->addWidget(_effortCombo);

    QPushButton *settingsBtn = new QPushButton(QChar(0x2699), this); // ⚙
    settingsBtn->setFixedSize(20, 20);
    settingsBtn->setToolTip("Open AI Settings");
    settingsBtn->setFlat(true);
    settingsBtn->setStyleSheet("font-size: 14px;");
    connect(settingsBtn, &QPushButton::clicked, this, &MidiPilotWidget::onSettingsClicked);
    footerLayout->addWidget(settingsBtn);

    mainLayout->addWidget(footerFrame);

    // Initial state
    setupSetupPrompt();
}

void MidiPilotWidget::setupSetupPrompt() {
    bool configured = _client->isConfigured();
    _setupWidget->setVisible(!configured);
    _chatScroll->setVisible(configured);
    _inputField->setEnabled(configured);
    _sendButton->setEnabled(configured);

    if (configured) {
        setStatus("Ready", "green");
        // Re-populate model list for current provider
        populateFooterModels();
        // Sync model combo
        int modelIdx = _modelCombo->findData(_client->model());
        if (modelIdx >= 0)
            _modelCombo->setCurrentIndex(modelIdx);
        else
            _modelCombo->setEditText(_client->model());
        // Show effort combo only for reasoning models
        _effortCombo->setVisible(_client->isReasoningModel());
        int effortIdx = _effortCombo->findData(_client->reasoningEffort());
        if (effortIdx >= 0) _effortCombo->setCurrentIndex(effortIdx);
        // Sync FFXIV checkbox with settings
        _ffxivCheck->setChecked(QSettings().value("AI/ffxiv_mode", false).toBool());
    } else {
        setStatus("Not configured", "orange");
    }
}

void MidiPilotWidget::populateFooterModels() {
    _modelCombo->clear();
    QString provider = _client->provider();

    if (provider == "openrouter") {
        _modelCombo->addItem("openai/gpt-5.4", "openai/gpt-5.4");
        _modelCombo->addItem("openai/gpt-4.1", "openai/gpt-4.1");
        _modelCombo->addItem("anthropic/claude-sonnet-4", "anthropic/claude-sonnet-4");
        _modelCombo->addItem("google/gemini-2.5-pro", "google/gemini-2.5-pro");
        _modelCombo->addItem("google/gemini-2.5-flash", "google/gemini-2.5-flash");
    } else if (provider == "gemini") {
        _modelCombo->addItem("gemini-2.5-flash", "gemini-2.5-flash");
        _modelCombo->addItem("gemini-2.5-flash-lite", "gemini-2.5-flash-lite");
        _modelCombo->addItem("gemini-2.5-pro", "gemini-2.5-pro");
        _modelCombo->addItem("gemini-3-flash", "gemini-3-flash-preview");
        _modelCombo->addItem("gemini-3.1-flash-lite", "gemini-3.1-flash-lite-preview");
        _modelCombo->addItem("gemini-3.1-pro", "gemini-3.1-pro-preview");
    } else if (provider == "groq") {
        _modelCombo->addItem("llama-3.3-70b", "llama-3.3-70b-versatile");
        _modelCombo->addItem("llama-3.1-8b", "llama-3.1-8b-instant");
        _modelCombo->addItem("mixtral-8x7b", "mixtral-8x7b-32768");
    } else if (provider == "ollama" || provider == "lmstudio") {
        _modelCombo->addItem("llama3.1", "llama3.1");
        _modelCombo->addItem("codellama", "codellama");
        _modelCombo->addItem("mistral", "mistral");
        _modelCombo->addItem("qwen2.5-coder", "qwen2.5-coder");
    } else {
        // OpenAI or custom — show OpenAI models
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
    }
}

void MidiPilotWidget::refreshContext() {
    if (!_file) {
        _contextLabel->setText("No file loaded");
        return;
    }

    QJsonObject state = EditorContext::captureState(_file, _mainWindow->matrixWidget());

    int track = state["activeTrack"].toObject()["index"].toInt(-1);
    QString trackName = state["activeTrack"].toObject()["name"].toString();
    int channel = state["activeChannel"].toInt(-1);
    int measure = state["currentMeasure"].toObject()["number"].toInt(0);

    QList<MidiEvent *> sel = Selection::instance()->selectedEvents();

    QStringList parts;
    if (track >= 0) {
        parts << QString("Track %1").arg(track);
        if (!trackName.isEmpty())
            parts << QString("(%1)").arg(trackName);
    }
    if (channel >= 0)
        parts << QString("Ch %1").arg(channel);
    if (measure > 0)
        parts << QString("Measure %1").arg(measure);
    if (!sel.isEmpty())
        parts << QString("%1 selected").arg(sel.size());

    _contextLabel->setText(parts.join(" | "));
}

void MidiPilotWidget::focusInput() {
    _inputField->setFocus();
}

void MidiPilotWidget::onFileChanged(MidiFile *f) {
    _file = f;
    refreshContext();
    AiClient::clearLog();
}

void MidiPilotWidget::onNewChat() {
    _conversationHistory = QJsonArray();
    _entries.clear();
    AiClient::clearLog();

    // Remove all chat bubbles (keep the stretch at index 0)
    while (_chatLayout->count() > 1) {
        QLayoutItem *item = _chatLayout->takeAt(1);
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }
}

void MidiPilotWidget::onSendMessage() {
    QString text = _inputField->toPlainText().trimmed();
    if (text.isEmpty())
        return;

    if (!_client->isConfigured()) {
        setupSetupPrompt();
        return;
    }

    if (_client->isBusy()) {
        setStatus("Processing...", "orange");
        return;
    }

    _inputField->clear();

    // Add user bubble
    addChatBubble("user", text);

    // Capture context and selected events
    QJsonObject editorState;
    QJsonArray selectedEvents;
    QJsonObject surroundingEvents;
    if (_file) {
        editorState = EditorContext::captureState(_file, _mainWindow->matrixWidget());
        QList<MidiEvent *> sel = Selection::instance()->selectedEvents();
        if (!sel.isEmpty()) {
            selectedEvents = MidiEventSerializer::serialize(sel, _file);
        }

        // Capture surrounding events (±N measures)
        QSettings settings;
        int contextMeasures = settings.value("AI/context_measures", 5).toInt();
        if (contextMeasures > 0) {
            surroundingEvents = EditorContext::captureSurroundingEvents(
                _file, _file->cursorTick(), contextMeasures);

            // Token budget warning: rough estimate ~15 tokens per event
            int totalEvents = surroundingEvents["totalEventCount"].toInt(0);
            if (totalEvents > 500) {
                addChatBubble("system",
                    QString("Note: Sending %1 surrounding events (~%2 tokens). "
                            "Consider reducing the context range in Settings if responses are slow.")
                    .arg(totalEvents).arg(totalEvents * 15));
            }
        }
    }

    // Build the user message with context
    QJsonObject userPayload;
    userPayload["instruction"] = text;
    if (!editorState.isEmpty())
        userPayload["editorState"] = editorState;
    if (!selectedEvents.isEmpty())
        userPayload["selectedEvents"] = selectedEvents;
    if (!surroundingEvents.isEmpty())
        userPayload["surroundingEvents"] = surroundingEvents;

    QString fullMessage = QJsonDocument(userPayload).toJson(QJsonDocument::Compact);

    // Store in conversation history
    ConversationEntry entry;
    entry.role = "user";
    entry.message = text;
    entry.context = editorState;
    entry.timestamp = QDateTime::currentDateTime();
    _entries.append(entry);

    // Add "thinking" indicator (Simple mode only; Agent uses steps widget)

    setStatus("Processing...", "orange");
    _inputField->setEnabled(false);
    _sendButton->setEnabled(false);

    if (currentMode() == "agent") {
        // Agent Mode: use AgentRunner with tool-calling loop
        // Add user message to history (AgentRunner reads it from there)
        QJsonObject userMsg;
        userMsg["role"] = "user";
        userMsg["content"] = fullMessage;
        _conversationHistory.append(userMsg);

        // Wrap all tool calls in a single undo action
        _isAgentRunning = true;
        _sendButton->setVisible(false);
        _stopButton->setVisible(true);

        // Create collapsible steps widget in chat area
        AgentStepsWidget *stepsWidget = new AgentStepsWidget(_chatContainer);
        _agentStepsWidget = stepsWidget;
        _chatLayout->addWidget(stepsWidget);

        if (_file)
            _file->protocol()->startNewAction("MidiPilot Agent: " + text);
        QString agentPrompt = EditorContext::agentSystemPrompt();
        if (ffxivMode())
            agentPrompt += EditorContext::ffxivContext();
        _agentRunner->run(agentPrompt,
                          _conversationHistory, fullMessage, _file, this);
    } else {
        // Simple Mode: single-shot request (sendRequest appends userMessage itself)
        addChatBubble("system", "Thinking...");
        QString simplePrompt = EditorContext::systemPrompt();
        if (ffxivMode())
            simplePrompt += EditorContext::ffxivContext();
        _client->sendRequest(simplePrompt,
                             _conversationHistory, fullMessage);
        // Add user message to history AFTER constructing the request
        QJsonObject userMsg;
        userMsg["role"] = "user";
        userMsg["content"] = fullMessage;
        _conversationHistory.append(userMsg);
    }
}

void MidiPilotWidget::onResponseReceived(const QString &content, const QJsonObject &fullResponse) {
    // Ignore AiClient signals during Agent Mode (AgentRunner handles them)
    if (_isAgentRunning) return;

    // Remove "thinking" indicator
    if (_chatLayout->count() > 1) {
        QLayoutItem *last = _chatLayout->takeAt(_chatLayout->count() - 1);
        if (last->widget())
            last->widget()->deleteLater();
        delete last;
    }

    setStatus("Ready", "green");
    _inputField->setEnabled(true);
    _sendButton->setEnabled(true);

    // Append assistant message to conversation history
    QJsonObject assistantMsg;
    assistantMsg["role"] = "assistant";
    assistantMsg["content"] = content;
    _conversationHistory.append(assistantMsg);

    // Strip markdown JSON fencing if present (```json ... ```)
    QString cleaned = content.trimmed();
    if (cleaned.startsWith(QStringLiteral("```"))) {
        int firstNewline = cleaned.indexOf('\n');
        int lastFence = cleaned.lastIndexOf(QStringLiteral("```"));
        if (firstNewline >= 0 && lastFence > firstNewline) {
            cleaned = cleaned.mid(firstNewline + 1, lastFence - firstNewline - 1).trimmed();
        }
    }

    // Try to parse as JSON action response
    QJsonDocument doc = QJsonDocument::fromJson(cleaned.toUtf8());
    if (doc.isObject()) {
        QJsonObject response = doc.object();

        // Multi-action support: { "actions": [...], "explanation": "..." }
        if (response.contains("actions") && response["actions"].isArray()) {
            QJsonArray actions = response["actions"].toArray();
            QString overallExplanation = response["explanation"].toString();
            QStringList stepResults;

            for (int i = 0; i < actions.size(); i++) {
                QJsonObject actionObj = actions[i].toObject();
                QString stepExplanation = actionObj["explanation"].toString();
                dispatchAction(actionObj, true);
                if (!stepExplanation.isEmpty()) {
                    stepResults << stepExplanation;
                }
            }

            // Show combined explanation
            QString displayText = overallExplanation;
            if (displayText.isEmpty() && !stepResults.isEmpty()) {
                displayText = stepResults.join("\n");
            } else if (!stepResults.isEmpty()) {
                displayText += "\n" + stepResults.join("\n");
            }
            addChatBubble("assistant", displayText.isEmpty() ? "Actions applied." : displayText);

        } else if (response.contains("action")) {
            // Single action (existing behavior)
            QString action = response["action"].toString();
            QString explanation = response["explanation"].toString();
            QJsonObject result = dispatchAction(response, true);
            if (!result.isEmpty()) {
                if (action == "info") {
                    addChatBubble("assistant", explanation);
                } else if (action == "error") {
                    addChatBubble("assistant", "Error: " + explanation);
                } else {
                    addChatBubble("assistant", explanation.isEmpty() ? "Done." : explanation);
                }
            } else {
                addChatBubble("assistant", content);
            }
        } else {
            // Fallback: treat as plain text
            addChatBubble("assistant", content);
        }
    } else {
        // JSON parse failed — might be truncated output
        if (content.length() > 500 && !content.trimmed().endsWith('}')) {
            addChatBubble("assistant",
                "\xe2\x9a\xa0 Response was truncated (output too large for Simple mode). "
                "Switch to **Agent** mode for complex compositions — it handles "
                "multi-step work like creating 8-track FFXIV songs.");
        } else {
            addChatBubble("assistant", content);
        }
    }

    // Store entry
    ConversationEntry entry;
    entry.role = "assistant";
    entry.message = content;
    entry.timestamp = QDateTime::currentDateTime();
    _entries.append(entry);

    refreshContext();
}

void MidiPilotWidget::onErrorOccurred(const QString &errorMessage) {
    // Ignore AiClient signals during Agent Mode (AgentRunner handles them)
    if (_isAgentRunning) return;

    // Remove "thinking" indicator
    if (_chatLayout->count() > 1) {
        QLayoutItem *last = _chatLayout->takeAt(_chatLayout->count() - 1);
        if (last->widget())
            last->widget()->deleteLater();
        delete last;
    }

    setStatus("Error", "red");
    _inputField->setEnabled(true);
    _sendButton->setEnabled(true);

    addChatBubble("system", "Error: " + errorMessage);
}

void MidiPilotWidget::onSettingsClicked() {
    _mainWindow->openConfig();
}

void MidiPilotWidget::onSettingsChanged() {
    _client->reloadSettings();
    setupSetupPrompt();
}

QString MidiPilotWidget::currentMode() const {
    return _modeCombo->currentData().toString();
}

bool MidiPilotWidget::ffxivMode() const {
    return _ffxivCheck->isChecked();
}

QJsonObject MidiPilotWidget::executeAction(const QJsonObject &actionObj) {
    return dispatchAction(actionObj, false);
}

void MidiPilotWidget::onModeChanged(int index) {
    Q_UNUSED(index);
    QSettings settings;
    settings.setValue("AI/mode", currentMode());

    // If there's an active conversation, start a new chat
    if (!_conversationHistory.isEmpty()) {
        onNewChat();
    }
}

void MidiPilotWidget::onModelComboChanged(int index) {
    Q_UNUSED(index);
    QString model = _modelCombo->currentData().toString();
    if (model.isEmpty()) model = _modelCombo->currentText().trimmed();
    if (!model.isEmpty()) {
        _client->setModel(model);
        _effortCombo->setVisible(_client->isReasoningModel());
    }
}

void MidiPilotWidget::onEffortComboChanged(int index) {
    Q_UNUSED(index);
    QString effort = _effortCombo->currentData().toString();
    if (!effort.isEmpty()) {
        _client->setReasoningEffort(effort);
        _client->setThinkingEnabled(true);
    }
}

void MidiPilotWidget::onAgentStepsPlanned(int firstStep, const QStringList &toolNames) {
    if (_agentStepsWidget) {
        AgentStepsWidget *sw = static_cast<AgentStepsWidget *>(_agentStepsWidget);
        sw->planSteps(firstStep, toolNames);
    }

    // Scroll to bottom to show all planned steps
    QTimer::singleShot(50, this, [this]() {
        _chatScroll->verticalScrollBar()->setValue(
            _chatScroll->verticalScrollBar()->maximum());
    });
}

void MidiPilotWidget::onAgentStepStarted(int step, const QString &toolName) {
    setStatus(QString("Agent step %1: %2...").arg(step).arg(toolName), "orange");

    // Mark step as active (🔄) — it was already added by stepsPlanned
    if (_agentStepsWidget) {
        AgentStepsWidget *sw = static_cast<AgentStepsWidget *>(_agentStepsWidget);
        sw->addStep(step, toolName);  // No-op if already planned
        sw->markActive(step);
    }
}

void MidiPilotWidget::onAgentStepCompleted(int step, const QString &toolName, const QJsonObject &result) {
    Q_UNUSED(toolName);
    bool success = result["success"].toBool(true);
    bool recoverable = result["recoverable"].toBool(false);
    setStatus(QString("Step %1: %2").arg(step).arg(success ? "OK" : (recoverable ? "retrying" : "failed")), "orange");

    // Check off the step in the checklist
    if (_agentStepsWidget) {
        AgentStepsWidget *sw = static_cast<AgentStepsWidget *>(_agentStepsWidget);
        sw->completeStep(step, success, recoverable);
    }
}

void MidiPilotWidget::onAgentFinished(const QString &finalMessage) {
    _isAgentRunning = false;

    // End the compound undo action
    if (_file)
        _file->protocol()->endAction();

    // Mark steps widget as finished
    if (_agentStepsWidget) {
        AgentStepsWidget *sw = static_cast<AgentStepsWidget *>(_agentStepsWidget);
        sw->setFinished(true);
        _agentStepsWidget = nullptr;
    }

    // Restore send/stop buttons
    _stopButton->setVisible(false);
    _sendButton->setVisible(true);

    setStatus("Ready", "green");
    _inputField->setEnabled(true);
    _sendButton->setEnabled(true);

    addChatBubble("assistant", finalMessage);

    // Store in conversation history
    QJsonObject assistantMsg;
    assistantMsg["role"] = "assistant";
    assistantMsg["content"] = finalMessage;
    _conversationHistory.append(assistantMsg);

    ConversationEntry entry;
    entry.role = "assistant";
    entry.message = finalMessage;
    entry.timestamp = QDateTime::currentDateTime();
    _entries.append(entry);

    refreshContext();
}

void MidiPilotWidget::onAgentError(const QString &error) {
    _isAgentRunning = false;

    // End the compound undo action (will undo any partial changes on Ctrl+Z)
    if (_file)
        _file->protocol()->endAction();

    // Mark steps widget as failed
    if (_agentStepsWidget) {
        AgentStepsWidget *sw = static_cast<AgentStepsWidget *>(_agentStepsWidget);
        sw->setFinished(false);
        _agentStepsWidget = nullptr;
    }

    // Restore send/stop buttons
    _stopButton->setVisible(false);
    _sendButton->setVisible(true);

    setStatus("Error", "red");
    _inputField->setEnabled(true);
    _sendButton->setEnabled(true);

    addChatBubble("system", "Agent error: " + error);
}

void MidiPilotWidget::onAgentStepLimitReached(int currentStep, int maxSteps) {
    Q_UNUSED(maxSteps);

    QMessageBox msgBox(this);
    msgBox.setWindowTitle("MidiPilot Agent");
    msgBox.setText(QString("The agent has reached the step limit (%1 steps).\n\n"
                           "The task may not be complete yet. "
                           "Would you like to continue?").arg(currentStep));
    msgBox.setIcon(QMessageBox::Question);
    QPushButton *continueBtn = msgBox.addButton("Continue", QMessageBox::AcceptRole);
    msgBox.addButton("Stop", QMessageBox::RejectRole);
    msgBox.setDefaultButton(continueBtn);
    msgBox.exec();

    if (msgBox.clickedButton() == continueBtn) {
        addChatBubble("system", QString("Step limit reached (%1 steps). Continuing...").arg(currentStep));
        _agentRunner->continueRunning(maxSteps);  // Double the budget
    } else {
        _agentRunner->stopAtLimit();
    }
}

void MidiPilotWidget::addChatBubble(const QString &role, const QString &text) {
    // Add timestamp prefix
    QString timestamp = QTime::currentTime().toString("HH:mm:ss");
    QString displayText = QString("[%1] %2").arg(timestamp, text);

    QLabel *bubble = new QLabel(displayText, _chatContainer);
    bubble->setWordWrap(true);
    bubble->setTextInteractionFlags(Qt::TextSelectableByMouse);
    bubble->setContentsMargins(10, 6, 10, 6);

    if (role == "user") {
        bubble->setStyleSheet(
            "background-color: #0078D4; color: white; "
            "border-radius: 8px; padding: 8px; margin-left: 40px;");
    } else if (role == "assistant") {
        bubble->setStyleSheet(
            "background-color: #E8E8E8; color: black; "
            "border-radius: 8px; padding: 8px; margin-right: 40px;");
    } else {
        // system messages
        bubble->setStyleSheet(
            "color: gray; font-style: italic; padding: 4px;");
        bubble->setAlignment(Qt::AlignCenter);
    }

    // Insert before the stretch
    _chatLayout->addWidget(bubble);

    // Scroll to bottom
    QTimer::singleShot(50, this, [this]() {
        _chatScroll->verticalScrollBar()->setValue(
            _chatScroll->verticalScrollBar()->maximum());
    });
}

void MidiPilotWidget::setStatus(const QString &text, const QString &color) {
    _statusLabel->setText(text);

    bool isProcessing = (color == "orange");
    bool isError = (color == "red");

    QString bgColor, textColor, dotColor;
    if (isProcessing) {
        bgColor = "#FFF3E0"; textColor = "#E65100"; dotColor = "#FF9800";
    } else if (isError) {
        bgColor = "#FFEBEE"; textColor = "#C62828"; dotColor = "#EF5350";
    } else {
        bgColor = "#E8F5E9"; textColor = "#2E7D32"; dotColor = "#4CAF50";
    }

    _statusBar->setStyleSheet(
        QString("QFrame { background-color: %1; border-radius: 6px; }").arg(bgColor));
    _statusLabel->setStyleSheet(
        QString("font-weight: bold; font-size: 12px; color: %1;").arg(textColor));
    _statusDots->setStyleSheet(
        QString("font-size: 13px; color: %1;").arg(dotColor));

    if (isProcessing) {
        if (!_statusTimer->isActive()) {
            _dotPhase = 0;
            _msgPhase = 0;
            _statusTimer->start();
        }
    } else {
        _statusTimer->stop();
        _statusDots->setText(QString::fromUtf8("\u25CF"));
    }
}

QJsonObject MidiPilotWidget::dispatchAction(const QJsonObject &response, bool showBubbles) {
    QString action = response["action"].toString();

    if (action == "edit" && response.contains("events")) {
        return applyAiEdits(response, showBubbles);
    } else if (action == "delete" && response.contains("deleteIndices")) {
        return applyAiDeletes(response, showBubbles);
    } else if (action == "move_to_track") {
        return applyMoveToTrack(response, showBubbles);
    } else if (action == "create_track" || action == "rename_track" || action == "set_channel") {
        return applyTrackAction(response, showBubbles);
    } else if (action == "set_tempo") {
        return applyTempoAction(response, showBubbles);
    } else if (action == "set_time_signature") {
        return applyTimeSignatureAction(response, showBubbles);
    } else if (action == "select_and_edit") {
        return applySelectAndEdit(response, showBubbles);
    } else if (action == "select_and_delete") {
        return applySelectAndDelete(response, showBubbles);
    } else if (action == "info" || action == "error") {
        QJsonObject result;
        result["success"] = true;
        result["action"] = action;
        return result;
    }
    return QJsonObject(); // unknown action
}

QJsonObject MidiPilotWidget::applyAiEdits(const QJsonObject &response, bool showBubbles) {
    QJsonObject result;
    result["action"] = QString("edit");

    if (!_file) {
        result["success"] = false;
        result["error"] = QString("No file loaded.");
        return result;
    }

    QJsonArray events = response["events"].toArray();
    if (events.isEmpty()) {
        result["success"] = false;
        result["error"] = QString("No events provided.");
        return result;
    }

    QString explanation = response["explanation"].toString("MidiPilot edit");

    // Determine target track: prefer AI-specified track, fallback to editor state
    int trackIndex = -1;
    if (response.contains("track")) {
        trackIndex = response["track"].toInt(-1);
    }
    if (trackIndex < 0 || trackIndex >= _file->numTracks()) {
        trackIndex = NewNoteTool::editTrack();
    }

    // Determine channel: prefer AI-specified, then track's assigned, then UI active
    int channel = -1;
    if (response.contains("channel")) {
        channel = response["channel"].toInt(-1);
    }
    if (channel < 0 && trackIndex >= 0 && trackIndex < _file->numTracks()) {
        channel = _file->track(trackIndex)->assignedChannel();
    }
    if (channel < 0) {
        channel = NewNoteTool::editChannel();
    }

    MidiTrack *track = nullptr;
    if (trackIndex >= 0 && trackIndex < _file->numTracks()) {
        track = _file->track(trackIndex);
    } else if (_file->numTracks() > 0) {
        track = _file->track(0);
    }

    if (!track) {
        result["success"] = false;
        result["error"] = QString("No valid track found.");
        return result;
    }

    // Start protocol action for undo support (skip in agent mode — compound action is active)
    if (showBubbles) _file->protocol()->startNewAction("MidiPilot: " + explanation);

    // In simple mode, remove currently selected events (they will be replaced by AI output).
    // In agent mode (showBubbles=false), skip this — each tool call is an independent insert.
    if (showBubbles) {
        QList<MidiEvent *> selected = Selection::instance()->selectedEvents();
        for (MidiEvent *ev : selected) {
            MidiChannel *ch = _file->channel(ev->channel());
            if (ch) ch->removeEvent(ev);
        }
        Selection::instance()->clearSelection();
    }

    // Deserialize and insert new events
    QList<MidiEvent *> created;
    bool ok = MidiEventSerializer::deserialize(events, _file, track, channel, created);

    if (!ok) {
        if (showBubbles) {
            _file->protocol()->endAction();
            addChatBubble("system", "Warning: Some events could not be applied.");
        }
        result["success"] = false;
        result["error"] = QString("Some events could not be applied.");
        return result;
    }

    // In simple mode, select the newly created events for visual feedback.
    // In agent mode, skip selection to prevent next tool call from deleting these events.
    if (showBubbles) {
        Selection::instance()->setSelection(created);
        _file->protocol()->endAction();
    }

    emit requestRepaint();

    result["success"] = true;
    result["eventsCreated"] = created.size();
    result["summary"] = buildMusicalSummary(created);
    return result;
}

QJsonObject MidiPilotWidget::applyAiDeletes(const QJsonObject &response, bool showBubbles) {
    Q_UNUSED(showBubbles);
    QJsonObject result;
    result["action"] = QString("delete");

    if (!_file) {
        result["success"] = false;
        result["error"] = QString("No file loaded.");
        return result;
    }

    QJsonArray indices = response["deleteIndices"].toArray();
    if (indices.isEmpty()) {
        result["success"] = false;
        result["error"] = QString("No delete indices provided.");
        return result;
    }

    QString explanation = response["explanation"].toString("MidiPilot delete");

    QList<MidiEvent *> selected = Selection::instance()->selectedEvents();
    if (selected.isEmpty()) {
        result["success"] = false;
        result["error"] = QString("No events selected.");
        return result;
    }

    // Collect indices to delete (validate bounds)
    QSet<int> toDelete;
    for (const QJsonValue &v : indices) {
        int idx = v.toInt(-1);
        if (idx >= 0 && idx < selected.size()) {
            toDelete.insert(idx);
        }
    }

    if (toDelete.isEmpty()) {
        result["success"] = false;
        result["error"] = QString("No valid indices to delete.");
        return result;
    }

    if (showBubbles) _file->protocol()->startNewAction("MidiPilot: " + explanation);

    // Remove the specified events
    QList<MidiEvent *> remaining;
    for (int i = 0; i < selected.size(); i++) {
        if (toDelete.contains(i)) {
            MidiChannel *ch = _file->channel(selected[i]->channel());
            if (ch) ch->removeEvent(selected[i]);
        } else {
            remaining.append(selected[i]);
        }
    }

    Selection::instance()->clearSelection();
    if (!remaining.isEmpty()) {
        Selection::instance()->setSelection(remaining);
    }

    if (showBubbles) _file->protocol()->endAction();

    emit requestRepaint();

    result["success"] = true;
    result["eventsDeleted"] = toDelete.size();
    return result;
}

QJsonObject MidiPilotWidget::applyTrackAction(const QJsonObject &response, bool showBubbles) {
    QJsonObject result;
    result["action"] = response["action"].toString();

    if (!_file) {
        result["success"] = false;
        result["error"] = QString("No file loaded.");
        return result;
    }

    QString action = response["action"].toString();
    QString explanation = response["explanation"].toString("MidiPilot track action");

    if (action == "create_track") {
        QString trackName = response["trackName"].toString("New Track");
        int channel = response["channel"].toInt(-1);

        if (showBubbles) _file->protocol()->startNewAction("MidiPilot: " + explanation);
        _file->addTrack();
        MidiTrack *newTrack = _file->tracks()->at(_file->numTracks() - 1);
        newTrack->setName(trackName);
        if (channel >= 0 && channel <= 15) {
            newTrack->assignChannel(channel);
        }
        if (showBubbles) _file->protocol()->endAction();

        result["success"] = true;
        result["trackIndex"] = _file->numTracks() - 1;

    } else if (action == "rename_track") {
        int trackIndex = response["trackIndex"].toInt(-1);
        QString newName = response["newName"].toString();

        if (trackIndex < 0 || trackIndex >= _file->numTracks() || newName.isEmpty()) {
            if (showBubbles) addChatBubble("system", "Invalid track index or name for rename.");
            result["success"] = false;
            result["error"] = QString("Invalid track index or name for rename.");
            return result;
        }

        if (showBubbles) _file->protocol()->startNewAction("MidiPilot: " + explanation);
        _file->track(trackIndex)->setName(newName);
        if (showBubbles) _file->protocol()->endAction();

        result["success"] = true;

    } else if (action == "set_channel") {
        int trackIndex = response["trackIndex"].toInt(-1);
        int channel = response["channel"].toInt(-1);

        if (trackIndex < 0 || trackIndex >= _file->numTracks()) {
            if (showBubbles) addChatBubble("system", "Invalid track index for channel assignment.");
            result["success"] = false;
            result["error"] = QString("Invalid track index for channel assignment.");
            return result;
        }
        if (channel < 0 || channel > 15) {
            if (showBubbles) addChatBubble("system", "Channel must be 0-15.");
            result["success"] = false;
            result["error"] = QString("Channel must be 0-15.");
            return result;
        }

        if (showBubbles) _file->protocol()->startNewAction("MidiPilot: " + explanation);
        _file->track(trackIndex)->assignChannel(channel);
        if (showBubbles) _file->protocol()->endAction();

        result["success"] = true;

    } else {
        result["success"] = false;
        result["error"] = QString("Unknown track action: ") + action;
        return result;
    }

    _mainWindow->updateTrackMenu();

    emit requestRepaint();
    return result;
}

QJsonObject MidiPilotWidget::applyMoveToTrack(const QJsonObject &response, bool showBubbles) {
    QJsonObject result;
    result["action"] = QString("move_to_track");

    if (!_file) {
        result["success"] = false;
        result["error"] = QString("No file loaded.");
        return result;
    }

    int targetTrackIndex = response["trackIndex"].toInt(-1);
    if (targetTrackIndex < 0 || targetTrackIndex >= _file->numTracks()) {
        if (showBubbles) addChatBubble("system", "Invalid target track index.");
        result["success"] = false;
        result["error"] = QString("Invalid target track index.");
        return result;
    }

    // Collect events to move: use sourceTrackIndex + tick range if provided,
    // otherwise fall back to the current UI selection.
    QList<MidiEvent *> toMove;

    int sourceTrackIndex = response["sourceTrackIndex"].toInt(-1);
    if (sourceTrackIndex >= 0 && sourceTrackIndex < _file->numTracks()) {
        MidiTrack *srcTrack = _file->track(sourceTrackIndex);
        int startTick = response["startTick"].toInt(0);
        int endTick   = response["endTick"].toInt(INT_MAX);
        // Gather all events on the source track within the tick range
        for (int ch = 0; ch < 19; ch++) {
            MidiChannel *channel = _file->channel(ch);
            if (!channel) continue;
            QMultiMap<int, MidiEvent *> *map = channel->eventMap();
            for (auto it = map->begin(); it != map->end(); ++it) {
                int tick = it.key();
                if (tick < startTick) continue;
                if (tick > endTick) break;
                MidiEvent *ev = it.value();
                if (ev->track() == srcTrack) {
                    toMove.append(ev);
                }
            }
        }
    } else {
        toMove = Selection::instance()->selectedEvents();
    }

    if (toMove.isEmpty()) {
        if (showBubbles) addChatBubble("system", "No events found to move.");
        result["success"] = false;
        result["error"] = QString("No events found to move.");
        return result;
    }

    MidiTrack *targetTrack = _file->track(targetTrackIndex);
    QString explanation = response["explanation"].toString("MidiPilot move to track");

    if (showBubbles) _file->protocol()->startNewAction("MidiPilot: " + explanation);

    for (MidiEvent *ev : toMove) {
        ev->setTrack(targetTrack, false);
    }

    if (showBubbles) _file->protocol()->endAction();

    emit requestRepaint();

    result["success"] = true;
    result["eventsMoved"] = toMove.size();
    return result;
}

QJsonObject MidiPilotWidget::applyTempoAction(const QJsonObject &response, bool showBubbles) {
    QJsonObject result;
    result["action"] = QString("set_tempo");

    if (!_file) {
        result["success"] = false;
        result["error"] = QString("No file loaded.");
        return result;
    }

    int bpm = response["bpm"].toInt(-1);
    if (bpm <= 0 || bpm > 999) {
        if (showBubbles) addChatBubble("system", "Invalid BPM value (must be 1-999).");
        result["success"] = false;
        result["error"] = QString("Invalid BPM value (must be 1-999).");
        return result;
    }

    int tick = response["tick"].toInt(0);
    if (tick < 0) tick = 0;

    QString explanation = response["explanation"].toString("MidiPilot set tempo");

    if (showBubbles) _file->protocol()->startNewAction("MidiPilot: " + explanation);

    // Check if there's already a tempo event at this tick
    QMap<int, MidiEvent *> *tempoMap = _file->tempoEvents();
    TempoChangeEvent *existing = nullptr;
    if (tempoMap) {
        auto it = tempoMap->find(tick);
        if (it != tempoMap->end()) {
            existing = dynamic_cast<TempoChangeEvent *>(it.value());
        }
    }

    if (existing) {
        // Modify existing tempo event
        existing->setBeats(bpm);
    } else {
        // Create new tempo event
        int microsPerQuarter = 60000000 / bpm;
        MidiTrack *track = _file->track(0);
        TempoChangeEvent *ev = new TempoChangeEvent(17, microsPerQuarter, track);
        _file->channel(17)->insertEvent(ev, tick);
    }

    if (showBubbles) _file->protocol()->endAction();
    _file->calcMaxTime();

    emit requestRepaint();

    result["success"] = true;
    result["bpm"] = bpm;
    return result;
}

QJsonObject MidiPilotWidget::applyTimeSignatureAction(const QJsonObject &response, bool showBubbles) {
    QJsonObject result;
    result["action"] = QString("set_time_signature");

    if (!_file) {
        result["success"] = false;
        result["error"] = QString("No file loaded.");
        return result;
    }

    int num = response["numerator"].toInt(-1);
    int denomActual = response["denominator"].toInt(-1);

    if (num <= 0 || num > 32) {
        if (showBubbles) addChatBubble("system", "Invalid numerator (must be 1-32).");
        result["success"] = false;
        result["error"] = QString("Invalid numerator (must be 1-32).");
        return result;
    }

    // Convert musical denominator (4=quarter, 8=eighth) to MIDI power-of-2
    int denomMidi = -1;
    if (denomActual == 1) denomMidi = 0;
    else if (denomActual == 2) denomMidi = 1;
    else if (denomActual == 4) denomMidi = 2;
    else if (denomActual == 8) denomMidi = 3;
    else if (denomActual == 16) denomMidi = 4;
    else if (denomActual == 32) denomMidi = 5;
    else {
        if (showBubbles) addChatBubble("system", "Invalid denominator (must be 1, 2, 4, 8, 16, or 32).");
        result["success"] = false;
        result["error"] = QString("Invalid denominator (must be 1, 2, 4, 8, 16, or 32).");
        return result;
    }

    int tick = response["tick"].toInt(0);
    if (tick < 0) tick = 0;

    QString explanation = response["explanation"].toString("MidiPilot set time signature");

    if (showBubbles) _file->protocol()->startNewAction("MidiPilot: " + explanation);

    // Check if there's already a time signature event at this tick
    QMap<int, MidiEvent *> *tsMap = _file->timeSignatureEvents();
    TimeSignatureEvent *existing = nullptr;
    if (tsMap) {
        auto it = tsMap->find(tick);
        if (it != tsMap->end()) {
            existing = dynamic_cast<TimeSignatureEvent *>(it.value());
        }
    }

    if (existing) {
        // Modify existing time signature
        existing->setNumerator(num);
        existing->setDenominator(denomMidi);
    } else {
        // Create new time signature event
        MidiTrack *track = _file->track(0);
        TimeSignatureEvent *ev = new TimeSignatureEvent(18, num, denomMidi, 24, 8, track);
        _file->channel(18)->insertEvent(ev, tick);
    }

    if (showBubbles) _file->protocol()->endAction();

    emit requestRepaint();

    result["success"] = true;
    result["numerator"] = num;
    result["denominator"] = denomActual;
    return result;
}

QJsonObject MidiPilotWidget::applySelectAndEdit(const QJsonObject &response, bool showBubbles) {
    QJsonObject result;
    result["action"] = QString("select_and_edit");

    if (!_file) {
        result["success"] = false;
        result["error"] = QString("No file loaded.");
        return result;
    }

    int trackIndex = response["trackIndex"].toInt(-1);
    int startTick = response["startTick"].toInt(-1);
    int endTick = response["endTick"].toInt(-1);
    QJsonArray events = response["events"].toArray();

    if (trackIndex < 0 || trackIndex >= _file->numTracks()) {
        if (showBubbles) addChatBubble("system", "Invalid track index for select_and_edit.");
        result["success"] = false;
        result["error"] = QString("Invalid track index for select_and_edit.");
        return result;
    }
    if (startTick < 0 || endTick < startTick) {
        if (showBubbles) addChatBubble("system", "Invalid tick range for select_and_edit.");
        result["success"] = false;
        result["error"] = QString("Invalid tick range for select_and_edit.");
        return result;
    }
    if (events.isEmpty()) {
        if (showBubbles) addChatBubble("system", "No events provided for select_and_edit.");
        result["success"] = false;
        result["error"] = QString("No events provided for select_and_edit.");
        return result;
    }

    MidiTrack *targetTrack = _file->track(trackIndex);
    int channel = targetTrack->assignedChannel();
    if (channel < 0) channel = NewNoteTool::editChannel();

    QString explanation = response["explanation"].toString("MidiPilot select and edit");

    if (showBubbles) _file->protocol()->startNewAction("MidiPilot: " + explanation);

    // Find and remove existing events in the tick range on this track
    QList<MidiEvent *> *allEvents = _file->eventsBetween(startTick, endTick);
    if (allEvents) {
        for (MidiEvent *ev : *allEvents) {
            // Skip OffEvents, tempo/time sig/meta channels
            if (dynamic_cast<OffEvent *>(ev)) continue;
            if (ev->channel() >= 16) continue;
            // Only remove events on the target track
            if (ev->track() != targetTrack) continue;

            MidiChannel *ch = _file->channel(ev->channel());
            if (ch) ch->removeEvent(ev);
        }
        delete allEvents;
    }

    // Deserialize and insert new events
    Selection::instance()->clearSelection();
    QList<MidiEvent *> created;
    MidiEventSerializer::deserialize(events, _file, targetTrack, channel, created);

    if (!created.isEmpty()) {
        Selection::instance()->setSelection(created);
    }

    if (showBubbles) _file->protocol()->endAction();

    emit requestRepaint();

    result["success"] = true;
    result["eventsCreated"] = created.size();
    result["summary"] = buildMusicalSummary(created);
    return result;
}

QJsonObject MidiPilotWidget::applySelectAndDelete(const QJsonObject &response, bool showBubbles) {
    QJsonObject result;
    result["action"] = QString("select_and_delete");

    if (!_file) {
        result["success"] = false;
        result["error"] = QString("No file loaded.");
        return result;
    }

    int trackIndex = response["trackIndex"].toInt(-1);
    int startTick = response["startTick"].toInt(-1);
    int endTick = response["endTick"].toInt(-1);

    if (trackIndex < 0 || trackIndex >= _file->numTracks()) {
        if (showBubbles) addChatBubble("system", "Invalid track index for select_and_delete.");
        result["success"] = false;
        result["error"] = QString("Invalid track index for select_and_delete.");
        return result;
    }
    if (startTick < 0 || endTick < startTick) {
        if (showBubbles) addChatBubble("system", "Invalid tick range for select_and_delete.");
        result["success"] = false;
        result["error"] = QString("Invalid tick range for select_and_delete.");
        return result;
    }

    MidiTrack *targetTrack = _file->track(trackIndex);
    QString explanation = response["explanation"].toString("MidiPilot select and delete");

    if (showBubbles) _file->protocol()->startNewAction("MidiPilot: " + explanation);

    // Find and remove events in the tick range on this track
    QList<MidiEvent *> *allEvents = _file->eventsBetween(startTick, endTick);
    int deletedCount = 0;
    if (allEvents) {
        for (MidiEvent *ev : *allEvents) {
            if (dynamic_cast<OffEvent *>(ev)) continue;
            if (ev->channel() >= 16) continue;
            if (ev->track() != targetTrack) continue;

            MidiChannel *ch = _file->channel(ev->channel());
            if (ch) {
                ch->removeEvent(ev);
                deletedCount++;
            }
        }
        delete allEvents;
    }

    Selection::instance()->clearSelection();

    if (showBubbles) _file->protocol()->endAction();

    if (deletedCount == 0) {
        if (showBubbles) addChatBubble("system", "No events found in the specified range.");
        result["success"] = false;
        result["error"] = QString("No events found in the specified range.");
    } else {
        result["success"] = true;
        result["eventsDeleted"] = deletedCount;
    }

    emit requestRepaint();
    return result;
}
