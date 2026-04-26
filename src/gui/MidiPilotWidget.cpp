#include "MidiPilotWidget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QScrollArea>
#include <QScrollBar>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTextEdit>
#include <QFrame>
#include <QTime>
#include <QTimer>
#include <QPointer>
#include <QSet>
#include <QSettings>
#include <QKeyEvent>
#include <QComboBox>
#include <QApplication>
#include <QRandomGenerator>
#include <QCheckBox>
#include <QMessageBox>
#include <QMenu>
#include <QDialog>
#include <QLineEdit>
#include <QScreen>
#include <QDateTime>
#include <QClipboard>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>

#include <cmath>
#include <algorithm>

#include "MainWindow.h"
#include "Appearance.h"
#include "../ai/AiClient.h"
#include "../ai/AgentRunner.h"
#include "../ai/EditorContext.h"
#include "../ai/MidiEventSerializer.h"
#include "../ai/ConversationStore.h"
#include "../ai/ModelFavorites.h"
#include "../ai/ModelListCache.h"
#include "../ai/ModelListFetcher.h"
#include "../ai/PromptProfileStore.h"
#include "PromptProfilesDialog.h"
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
    int minDur = INT_MAX, maxDur = 0;
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
            if (OffEvent *off = on->offEvent()) {
                int dur = off->midiTime() - t;
                if (dur > 0) {
                    if (dur < minDur) minDur = dur;
                    if (dur > maxDur) maxDur = dur;
                }
            }
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
        if (maxDur > 0)
            summary["durationRange"] = QString("%1-%2 ticks").arg(minDur).arg(maxDur);
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

        bool dark = Appearance::shouldUseDarkMode();

        // Header with collapse/expand toggle
        _headerBtn = new QPushButton(this);
        _headerBtn->setFlat(true);
        _headerBtn->setCursor(Qt::PointingHandCursor);
        _headerBtn->setStyleSheet(
            QString("text-align: left; font-weight: bold; font-size: 12px; "
                    "padding: 4px 2px; color: %1;")
                .arg(dark ? "#BBB" : "#555"));
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
            QString("AgentStepsWidget { background-color: %1; "
                    "border-radius: 8px; margin-right: 40px; }")
                .arg(dark ? "#2A2A2A" : "#F5F5F0"));

        updateHeader();
    }

    void addStep(int step, const QString &label) {
        if (_stepLabels.contains(step)) return;  // Already planned
        bool dark = Appearance::shouldUseDarkMode();
        QLabel *lbl = new QLabel(
            QString("\xE2\x8F\xB3 %1").arg(label), _stepsContainer);  // ⏳
        lbl->setStyleSheet(
            QString("color: %1; font-size: 11px; padding: 1px 2px;")
                .arg(dark ? "#CC9944" : "#CC7700"));
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
        bool dark = Appearance::shouldUseDarkMode();
        QLabel *label = _stepLabels[step];
        QString name = _stepNames[step];
        label->setText(QString("\xF0\x9F\x94\x84 %1").arg(name));  // 🔄
        label->setStyleSheet(
            QString("color: %1; font-weight: bold; font-size: 11px; padding: 1px 2px;")
                .arg(dark ? "#55AAFF" : "#0066CC"));
    }

    void completeStep(int step, bool success, bool recoverable = false) {
        if (!_stepLabels.contains(step)) return;
        bool dark = Appearance::shouldUseDarkMode();
        QLabel *label = _stepLabels[step];
        QString name = _stepNames[step];
        if (success) {
            label->setText(QString("\xE2\x9C\x85 %1").arg(name));  // ✅
            label->setStyleSheet(
                QString("color: %1; font-size: 11px; padding: 1px 2px;")
                    .arg(dark ? "#66CC77" : "#2D8C3C"));
        } else if (recoverable) {
            label->setText(QString("\xE2\x9A\xA0 %1 - retrying").arg(name));  // ⚠
            label->setStyleSheet(
                QString("color: %1; font-size: 11px; padding: 1px 2px;")
                    .arg(dark ? "#CC9944" : "#CC7700"));
        } else {
            label->setText(QString("\xE2\x9D\x8C %1 - failed").arg(name));  // ❌
            label->setStyleSheet(
                QString("color: %1; font-size: 11px; padding: 1px 2px;")
                    .arg(dark ? "#FF6666" : "#CC3333"));
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
      _isAgentRunning(false), _agentStepsWidget(nullptr),
      _agentDockArea(nullptr), _streamBubble(nullptr),
      _streamIsJson(false),
      _simpleRetryCount(0), _simpleMaxRetries(3),
      _thoughtLabel(nullptr), _thoughtCursorTimer(nullptr),
      _thoughtCursorOn(true), _thoughtCursorFrame(0),
      _turnStartMs(0), _turnStreamed(false) {

    _client = new AiClient(this);
    _agentRunner = new AgentRunner(_client, this);
    _profileStore = new PromptProfileStore(this);

    _saveTimer = new QTimer(this);
    _saveTimer->setSingleShot(true);
    _saveTimer->setInterval(2000);
    connect(_saveTimer, &QTimer::timeout, this, &MidiPilotWidget::doSaveConversation);

    setupUi();

    connect(_client, &AiClient::responseReceived, this, &MidiPilotWidget::onResponseReceived);
    connect(_client, &AiClient::errorOccurred, this, &MidiPilotWidget::onErrorOccurred);
    connect(_client, &AiClient::retrying, this, [this](const QString &msg) {
        addChatBubble("system", msg);
    });
    connect(_client, &AiClient::streamDelta, this, &MidiPilotWidget::onStreamDelta);
    connect(_client, &AiClient::streamFinished, this, &MidiPilotWidget::onStreamFinished);

    // Agent-mode live text streaming. Reuses _streamBubble — most agent
    // round-trips return tool_calls only (no text), so in practice the
    // bubble only fills up during the final summarising round. The bubble
    // is removed in onAgentFinished / onAgentError before the proper final
    // assistant bubble is rendered.
    connect(_client, &AiClient::streamAssistantTextDelta, this,
            [this](const QString &text) {
        if (!_isAgentRunning) return;
        if (!_streamBubble) {
            bool dark = Appearance::shouldUseDarkMode();
            _streamBubble = new QLabel(_chatContainer);
            _streamBubble->setWordWrap(true);
            // Render assistant streaming as Markdown so **bold**, *italics*,
            // `code`, fenced blocks and bullet lists appear formatted instead
            // of as raw asterisks. QLabel re-parses on every setText() call so
            // incremental streaming updates render correctly.
            _streamBubble->setTextFormat(Qt::MarkdownText);
            _streamBubble->setStyleSheet(
                QString("background-color: %1; color: %2; padding: 10px 14px; "
                        "border-radius: 12px; font-size: 13px; margin-right: 40px;")
                    .arg(dark ? "#2A2A2A" : "#F5F5F0",
                         dark ? "#DDD" : "#333"));
            _chatLayout->addWidget(_streamBubble);
        }
        _streamBubble->setText(_streamBubble->text() + text);
        _turnStreamed = true;
        QTimer::singleShot(10, this, [this]() {
            _chatScroll->verticalScrollBar()->setValue(
                _chatScroll->verticalScrollBar()->maximum());
        });
    });

    // Live "thought" / reasoning streaming. Rendered as gray italic text
    // inline in the chat (NOT a speech bubble) with a rotating spinner
    // ("thinking" animation) while new chunks keep arriving. Persists in
    // chat history after the agent finishes — only the pointer is reset
    // on the next user send so the next request gets a fresh label.
    _thoughtCursorTimer = new QTimer(this);
    _thoughtCursorTimer->setInterval(120); // ~8 fps for smooth rotation
    _thoughtCursorFrame = 0;
    connect(_thoughtCursorTimer, &QTimer::timeout, this, [this]() {
        if (!_thoughtLabel) return;
        // Braille spinner: smoother than ASCII | / - \ and renders well in Qt.
        static const QStringList kFrames = {
            QStringLiteral("⠋"), QStringLiteral("⠙"), QStringLiteral("⠹"), QStringLiteral("⠸"),
            QStringLiteral("⠼"), QStringLiteral("⠴"), QStringLiteral("⠦"), QStringLiteral("⠧"),
            QStringLiteral("⠇"), QStringLiteral("⠏")
        };
        _thoughtCursorFrame = (_thoughtCursorFrame + 1) % kFrames.size();
        _thoughtLabel->setText(_thoughtBaseText
                               + QStringLiteral(" ")
                               + kFrames.at(_thoughtCursorFrame));
    });

    connect(_client, &AiClient::streamReasoningDelta, this,
            [this](const QString &text) {
        if (text.isEmpty()) return;
        if (!_thoughtLabel) {
            bool dark = Appearance::shouldUseDarkMode();
            _thoughtLabel = new QLabel(_chatContainer);
            _thoughtLabel->setWordWrap(true);
            // Render reasoning text as Markdown so the model's section
            // headers like **Planning chord measures** do not leak as raw
            // asterisks. The trailing braille spinner glyph is harmless to
            // the Markdown parser.
            _thoughtLabel->setTextFormat(Qt::MarkdownText);
            // Dark mode used to be #888 on near-black which was barely
            // readable. Bump to #C8C8C8 text + #707070 border for a clear
            // "secondary text" contrast that still reads as italic/quiet.
            _thoughtLabel->setStyleSheet(
                QString("background: transparent; color: %1; "
                        "font-style: italic; font-size: 12px; "
                        "padding: 4px 8px; margin: 2px 40px 2px 8px; "
                        "border-left: 2px solid %2;")
                    .arg(dark ? "#C8C8C8" : "#666",
                         dark ? "#707070" : "#CCC"));
            _thoughtBaseText = QStringLiteral("💭 ");
            _chatLayout->addWidget(_thoughtLabel);
            _thoughtCursorOn = true;
            _thoughtCursorFrame = 0;
            _thoughtCursorTimer->start();
        }
        _thoughtBaseText += text;
        _turnReasoning += text;
        _turnStreamed = true;
        _thoughtCursorOn = true;
        _thoughtLabel->setText(_thoughtBaseText + QStringLiteral(" ⠋"));
        QTimer::singleShot(10, this, [this]() {
            _chatScroll->verticalScrollBar()->setValue(
                _chatScroll->verticalScrollBar()->maximum());
        });
    });
    connect(_agentRunner, &AgentRunner::stepsPlanned, this, &MidiPilotWidget::onAgentStepsPlanned);
    connect(_agentRunner, &AgentRunner::stepStarted, this, &MidiPilotWidget::onAgentStepStarted);
    connect(_agentRunner, &AgentRunner::stepCompleted, this, &MidiPilotWidget::onAgentStepCompleted);
    connect(_agentRunner, &AgentRunner::finished, this, &MidiPilotWidget::onAgentFinished);
    connect(_agentRunner, &AgentRunner::errorOccurred, this, &MidiPilotWidget::onAgentError);
    connect(_agentRunner, &AgentRunner::stepLimitReached, this, &MidiPilotWidget::onAgentStepLimitReached);
    connect(_agentRunner, &AgentRunner::agentRetrying, this,
            [this](int attempt, int maxAttempts, const QString &reason) {
        // Surface self-healing retries to the user as a system bubble so
        // the chat doesn't look frozen while the agent recovers.
        QString shortReason = reason;
        if (shortReason.length() > 160) shortReason = shortReason.left(157) + QStringLiteral("...");
        addChatBubble(QStringLiteral("system"),
                      QStringLiteral("\u26A0 Agent self-healing retry %1/%2: %3")
                          .arg(attempt).arg(maxAttempts).arg(shortReason));
        setStatus(QString("Retrying (%1/%2)...").arg(attempt).arg(maxAttempts), "orange");
    });
    connect(_agentRunner, &AgentRunner::tokenUsageUpdated, this, [this](int pt, int ct, int /*tt*/) {
        _lastPromptTokens = pt;
        _lastCompletionTokens = ct;
        _totalPromptTokens += pt;
        _totalCompletionTokens += ct;
        updateTokenLabel();
    });
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

    // === Anchored "Agent Steps" dock (between chat scroll and input) ===
    // The AgentStepsWidget is placed here instead of inside _chatLayout so it
    // stays pinned at the bottom of the chat area while the thoughts/messages
    // scroll above it.
    _agentDockArea = new QWidget(this);
    QVBoxLayout *dockLayout = new QVBoxLayout(_agentDockArea);
    dockLayout->setContentsMargins(4, 0, 4, 0);
    dockLayout->setSpacing(0);
    _agentDockArea->setVisible(false);
    mainLayout->addWidget(_agentDockArea);

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

    QPushButton *newChatBtn = new QPushButton(this);
    newChatBtn->setIcon(Appearance::adjustIconForDarkMode(":/run_environment/graphics/tool/new_chat.png"));
    newChatBtn->setIconSize(QSize(18, 18));
    newChatBtn->setFixedSize(28, 28);
    newChatBtn->setToolTip("New Chat — clear current conversation");
    newChatBtn->setFlat(true);
    connect(newChatBtn, &QPushButton::clicked, this, &MidiPilotWidget::onNewChat);
    inputBtnLayout->addWidget(newChatBtn);

    QPushButton *historyBtn = new QPushButton(this);
    historyBtn->setIcon(Appearance::adjustIconForDarkMode(":/run_environment/graphics/tool/history.png"));
    historyBtn->setIconSize(QSize(18, 18));
    historyBtn->setFixedSize(28, 28);
    historyBtn->setToolTip("Conversation history");
    historyBtn->setFlat(true);
    connect(historyBtn, &QPushButton::clicked, this, &MidiPilotWidget::showHistoryMenu);
    inputBtnLayout->addWidget(historyBtn);

    // Mode selector (Simple / Agent)
    _modeCombo = new QComboBox(this);
    _modeCombo->addItem("Simple", "simple");
    _modeCombo->addItem("Agent", "agent");
    _modeCombo->setToolTip("Simple: single-shot edits\nAgent: multi-step autonomous editing");
    _modeCombo->setFixedHeight(28);
    QSettings modeSettings("MidiEditor", "NONE");
    int modeIdx = _modeCombo->findData(modeSettings.value("AI/mode", "simple").toString());
    if (modeIdx >= 0) _modeCombo->setCurrentIndex(modeIdx);
    connect(_modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MidiPilotWidget::onModeChanged);
    inputBtnLayout->addWidget(_modeCombo);

    _tokenLabel = new QLabel(this);
    _tokenLabel->setStyleSheet("font-size: 10px; color: #888;");
    _tokenLabel->setToolTip("Token usage: last request | session total\n"
                            "Scissors icon (✂) = output token limit active");
    _lastPromptTokens = 0;
    _lastCompletionTokens = 0;
    _totalPromptTokens = 0;
    _totalCompletionTokens = 0;
    inputBtnLayout->addWidget(_tokenLabel);

    inputBtnLayout->addStretch();

    _sendButton = new QPushButton(this);
    _sendButton->setIcon(Appearance::adjustIconForDarkMode(":/run_environment/graphics/tool/play.png"));
    _sendButton->setIconSize(QSize(18, 18));
    _sendButton->setFixedSize(28, 28);
    _sendButton->setToolTip("Send");
    _sendButton->setFlat(true);
    connect(_sendButton, &QPushButton::clicked, this, &MidiPilotWidget::onSendMessage);
    inputBtnLayout->addWidget(_sendButton);

    _stopButton = new QPushButton(this);
    _stopButton->setIcon(Appearance::adjustIconForDarkMode(":/run_environment/graphics/tool/stop.png"));
    _stopButton->setIconSize(QSize(18, 18));
    _stopButton->setFixedSize(28, 28);
    _stopButton->setToolTip("Stop Agent");
    _stopButton->setFlat(true);
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
    _ffxivCheck->setChecked(QSettings("MidiEditor", "NONE").value("AI/ffxiv_mode", false).toBool());
    connect(_ffxivCheck, &QCheckBox::toggled, this, [](bool checked) {
        QSettings("MidiEditor", "NONE").setValue("AI/ffxiv_mode", checked);
    });
    footerLayout->addWidget(_ffxivCheck);

    footerLayout->addStretch();

    _providerCombo = new QComboBox(this);
    _providerCombo->addItem("OpenAI", "openai");
    _providerCombo->addItem("OpenRouter", "openrouter");
    _providerCombo->addItem("Gemini", "gemini");
    _providerCombo->addItem("Custom", "custom");
    _providerCombo->setFixedHeight(20);
    _providerCombo->setStyleSheet("font-size: 11px;");
    int provIdx = _providerCombo->findData(_client->provider());
    if (provIdx >= 0) _providerCombo->setCurrentIndex(provIdx);
    connect(_providerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MidiPilotWidget::onProviderComboChanged);
    footerLayout->addWidget(_providerCombo);

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

    _refreshModelsButton = new QPushButton(this);
    _refreshModelsButton->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    _refreshModelsButton->setIconSize(QSize(14, 14));
    _refreshModelsButton->setToolTip(tr("Refresh model list from provider"));
    _refreshModelsButton->setFixedSize(22, 20);
    connect(_refreshModelsButton, &QPushButton::clicked, this, &MidiPilotWidget::onRefreshModels);
    footerLayout->addWidget(_refreshModelsButton);

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
    settingsBtn->setToolTip("Settings");
    settingsBtn->setFlat(true);
    settingsBtn->setStyleSheet("font-size: 14px;");
    QMenu *settingsMenu = new QMenu(settingsBtn);
    settingsMenu->addAction("MidiPilot Settings...", this, &MidiPilotWidget::onSettingsClicked);
    settingsMenu->addSeparator();
    settingsMenu->addAction(tr("Prompt Profiles\u2026"), this, [this]() {
        PromptProfilesDialog dlg(_profileStore, this);
        dlg.exec();
    });
    settingsMenu->addSeparator();
    settingsMenu->addAction("Save AI preset for this file...", this, &MidiPilotWidget::savePresetForFile);
    connect(settingsBtn, &QPushButton::clicked, settingsBtn, [settingsBtn, settingsMenu]() {
        settingsMenu->exec(settingsBtn->mapToGlobal(QPoint(0, -settingsMenu->sizeHint().height())));
    });
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
        // Sync provider combo with current settings
        _providerCombo->blockSignals(true);
        int provIdx = _providerCombo->findData(_client->provider());
        if (provIdx >= 0) _providerCombo->setCurrentIndex(provIdx);
        _providerCombo->blockSignals(false);
        // Re-populate model list for current provider (block signals to avoid
        // onModelComboChanged firing during clear/addItem and overwriting
        // the model the user just chose in Settings)
        _modelCombo->blockSignals(true);
        populateFooterModels();
        // Sync model combo with the model from settings
        int modelIdx = _modelCombo->findData(_client->model());
        if (modelIdx >= 0)
            _modelCombo->setCurrentIndex(modelIdx);
        else
            _modelCombo->setEditText(_client->model());
        _modelCombo->blockSignals(false);
        // Show effort combo only for reasoning models
        _effortCombo->setVisible(_client->isReasoningModel());
        int effortIdx = _effortCombo->findData(_client->reasoningEffort());
        if (effortIdx >= 0) _effortCombo->setCurrentIndex(effortIdx);
        // Sync FFXIV checkbox with settings
        _ffxivCheck->setChecked(QSettings("MidiEditor", "NONE").value("AI/ffxiv_mode", false).toBool());
    } else {
        setStatus("Not configured", "orange");
    }
}

void MidiPilotWidget::populateFooterModels() {
    _modelCombo->clear();
    QString provider = _client->provider();

    auto addModel = [this, &provider](const QString &label, const QString &id) {
        QString itemLabel = label.isEmpty() ? id : label;
        const bool simpleBlocked = AiClient::streamingBlockedForSession(provider, id, false);
        const bool agentBlocked  = AiClient::streamingBlockedForSession(provider, id, true);
        if (simpleBlocked || agentBlocked) {
            QString suffix;
            QString tip;
            if (simpleBlocked && agentBlocked) {
                suffix = tr(" (Simple+Agent)");
                tip = tr("Live streaming failed for this model in both Simple and Agent mode this session. Open MidiPilot Settings and click Force Streaming to try again.");
            } else if (simpleBlocked) {
                suffix = tr(" (Simple)");
                tip = tr("Live streaming failed in Simple Mode this session. Agent Mode still streams. Open MidiPilot Settings and click Force Streaming to retry.");
            } else {
                suffix = tr(" (Agent)");
                tip = tr("Live streaming failed in Agent Mode this session. Simple Mode still streams. Open MidiPilot Settings and click Force Streaming to retry.");
            }
            itemLabel = tr("⚠ %1%2").arg(itemLabel, suffix);
            _modelCombo->addItem(itemLabel, id);
            _modelCombo->setItemData(_modelCombo->count() - 1, tip, Qt::ToolTipRole);
        } else {
            _modelCombo->addItem(itemLabel, id);
        }
    };

    // Phase 26: prefer cached entries from <userdata>/midipilot_models.json
    // Phase 26.1: ModelFavorites filters non-LLM models out and, if the user
    // has selected favourites, restricts the visible set to those.
    QJsonArray cached = ModelListCache::models(provider);
    QJsonArray visible = ModelFavorites::visibleModels(provider, cached);
    if (!visible.isEmpty()) {
        for (const QJsonValue &v : visible) {
            QJsonObject m = v.toObject();
            QString id = m.value(QStringLiteral("id")).toString();
            QString display = m.value(QStringLiteral("displayName")).toString();
            if (id.isEmpty())
                continue;
            addModel(display, id);
        }
        return;
    }

    // Fallback: built-in hardcoded list
    if (provider == "openrouter") {
        addModel("openai/gpt-5.4", "openai/gpt-5.4");
        addModel("openai/gpt-4.1", "openai/gpt-4.1");
        addModel("anthropic/claude-sonnet-4", "anthropic/claude-sonnet-4");
        addModel("google/gemini-2.5-pro", "google/gemini-2.5-pro");
        addModel("google/gemini-2.5-flash", "google/gemini-2.5-flash");
    } else if (provider == "gemini") {
        addModel("gemini-2.5-flash", "gemini-2.5-flash");
        addModel("gemini-2.5-flash-lite", "gemini-2.5-flash-lite");
        addModel("gemini-2.5-pro", "gemini-2.5-pro");
        addModel("gemini-3-flash", "gemini-3-flash-preview");
        addModel("gemini-3.1-flash-lite", "gemini-3.1-flash-lite-preview");
        addModel("gemini-3.1-pro", "gemini-3.1-pro-preview");
    } else {
        // OpenAI or custom — show OpenAI models
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
    }
}

void MidiPilotWidget::onRefreshModels()
{
    QString provider = _client->provider();
    QString apiKey = _client->apiKey();
    QString baseUrl = _client->apiBaseUrl();

    if (_refreshModelsButton)
        _refreshModelsButton->setEnabled(false);
    setStatus(tr("Fetching models from %1\xE2\x80\xA6").arg(provider), "gray");

    auto *fetcher = new ModelListFetcher(this);
    connect(fetcher, &ModelListFetcher::finished,
            this, &MidiPilotWidget::onModelsFetched);
    connect(fetcher, &ModelListFetcher::failed,
            this, &MidiPilotWidget::onModelsFetchFailed);
    fetcher->fetch(provider, apiKey, baseUrl);
}

void MidiPilotWidget::onModelsFetched(const QString &provider, const QJsonArray &models)
{
    ModelListCache::store(provider, models);
    if (_refreshModelsButton)
        _refreshModelsButton->setEnabled(true);

    if (_client->provider() == provider) {
        QString currentText = _modelCombo->currentText();
        populateFooterModels();
        int idx = _modelCombo->findData(currentText);
        if (idx >= 0)
            _modelCombo->setCurrentIndex(idx);
        else
            _modelCombo->setEditText(currentText);
    }
    setStatus(tr("Models updated (%1 entries)").arg(models.size()), "green");
}

void MidiPilotWidget::onModelsFetchFailed(const QString &provider, const QString &error)
{
    Q_UNUSED(provider);
    if (_refreshModelsButton)
        _refreshModelsButton->setEnabled(true);
    setStatus(tr("Model refresh failed: %1").arg(error), "red");
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
    if (_file) {
        loadPresetForFile(_file->path());
    } else {
        _customFileInstructions.clear();
    }
}

void MidiPilotWidget::onNewChat() {
    // Only ask for confirmation if there are messages in the chat
    if (!_conversationHistory.isEmpty()) {
        QMessageBox msgBox(this);
        msgBox.setIcon(QMessageBox::Question);
        msgBox.setWindowTitle("New Chat");
        msgBox.setText("Start a new conversation?");
        msgBox.setInformativeText("This will clear the current chat history and token counters.");
        msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
        msgBox.setDefaultButton(QMessageBox::Cancel);
        if (msgBox.exec() != QMessageBox::Yes)
            return;
    }

    _conversationHistory = QJsonArray();
    _entries.clear();
    // Flush any pending save for the old conversation before clearing the ID
    _saveTimer->stop();
    doSaveConversation();
    _conversationId.clear();
    _turns = QJsonArray();
    AiClient::clearLog();

    // Reset token counters
    _lastPromptTokens = 0;
    _lastCompletionTokens = 0;
    _totalPromptTokens = 0;
    _totalCompletionTokens = 0;
    updateTokenLabel();

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
        QSettings settings("MidiEditor", "NONE");
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

    // Begin a fresh per-turn metadata record. resetTurnState() captures
    // the start timestamp, current model/provider/effort and clears the
    // reasoning + steps accumulators. The record is sealed by
    // finalizeTurn() in onAgentFinished/onAgentError or onResponseReceived.
    resetTurnState();

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
        // Pre-flight capability check — if we previously observed that
        // the chosen model has no tool-calling support, don't even try
        // (would just return HTTP 404 again). Surface a friendly bubble
        // and bail out cleanly.
        if (_client->toolsIncapableForCurrentModel()) {
            setStatus("Model does not support tools", "red");
            _inputField->setEnabled(true);
            _sendButton->setEnabled(true);
            addChatBubble(QStringLiteral("system"),
                tr("⚠ The selected model does not support tool calling and "
                   "cannot be used in Agent mode.\n\n"
                   "Pick a different model in Settings → AI (look for "
                   "tool/function-calling support), or switch this chat "
                   "to Simple mode."));
            return;
        }

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

        // Reset the thought label pointer so the next streamReasoningDelta
        // creates a fresh label for THIS request. The previous request's
        // thought label stays in the chat layout as part of history.
        _thoughtLabel = nullptr;
        _thoughtBaseText.clear();
        _thoughtCursorOn = true;

        // Create collapsible steps widget — anchored at the bottom of the
        // chat area (in _agentDockArea) so it stays visible while thoughts
        // and messages scroll above it.
        AgentStepsWidget *stepsWidget = new AgentStepsWidget(_agentDockArea);
        _agentStepsWidget = stepsWidget;
        _agentDockArea->layout()->addWidget(stepsWidget);
        _agentDockArea->setVisible(true);

        QString agentPrompt = EditorContext::agentSystemPrompt();
        // Phase 29: layer per-model prompt profile (e.g. GPT-5.5 Decisive)
        // on top. resolveForModel returns an empty profile when nothing
        // matches, in which case agentPrompt stays unchanged. We pass the
        // already-customised editor prompt as BOTH default and userCustom
        // so the resolve helper preserves the user's custom prompt as the
        // base for append-mode profiles.
        {
            const PromptProfile pp = _profileStore->resolveForModel(
                _client->provider(), _client->model());
            if (!pp.id.isEmpty()) {
                agentPrompt = pp.appendToDefault
                                  ? agentPrompt + QStringLiteral("\n\n") + pp.system
                                  : pp.system;
            }
        }
        if (ffxivMode()) {
            // Detect which optional sections the file needs
            bool hasDrums = false, hasGuitar = false;
            if (_file) {
                static const QStringList drumNames = {"Timpani","Bongo","Bass Drum","Snare Drum","Cymbal"};
                for (int i = 0; i < _file->numTracks(); i++) {
                    QString tn = _file->track(i)->name();
                    if (drumNames.contains(tn)) hasDrums = true;
                    if (tn.startsWith("ElectricGuitar")) hasGuitar = true;
                }
                // Also include if user message mentions them
                if (fullMessage.contains("drum", Qt::CaseInsensitive) ||
                    fullMessage.contains("percussion", Qt::CaseInsensitive))
                    hasDrums = true;
                if (fullMessage.contains("guitar", Qt::CaseInsensitive))
                    hasGuitar = true;
                // New/empty file: include everything
                if (_file->numTracks() <= 1) { hasDrums = true; hasGuitar = true; }
            }
            agentPrompt += EditorContext::ffxivContext(hasDrums, hasGuitar);
        }
        if (!_customFileInstructions.isEmpty())
            agentPrompt += QStringLiteral("\n\n## Per-File Instructions\n") + _customFileInstructions;

        // Truncate history if approaching context window limit
        QJsonArray historyForApi = truncateHistory(_conversationHistory,
                                                    _client->contextWindowForModel(),
                                                    agentPrompt.length());

        _agentRunner->run(agentPrompt,
                          historyForApi, fullMessage, _file, this);
    } else {
        // Simple Mode: use streaming for incremental text display
        QString simplePrompt = EditorContext::systemPrompt();
        // Phase 29: per-model prompt profile layered on top.
        {
            const PromptProfile pp = _profileStore->resolveForModel(
                _client->provider(), _client->model());
            if (!pp.id.isEmpty()) {
                simplePrompt = pp.appendToDefault
                                  ? simplePrompt + QStringLiteral("\n\n") + pp.system
                                  : pp.system;
            }
        }
        if (ffxivMode()) {
            // Low effort → compact prompt to save tokens
            if (_client->reasoningEffort() == QStringLiteral("low")) {
                simplePrompt += EditorContext::ffxivContextCompact();
            } else {
                bool hasDrums = false, hasGuitar = false;
                if (_file) {
                    static const QStringList drumNames = {"Timpani","Bongo","Bass Drum","Snare Drum","Cymbal"};
                    for (int i = 0; i < _file->numTracks(); i++) {
                        QString tn = _file->track(i)->name();
                        if (drumNames.contains(tn)) hasDrums = true;
                        if (tn.startsWith("ElectricGuitar")) hasGuitar = true;
                    }
                    if (fullMessage.contains("drum", Qt::CaseInsensitive) ||
                        fullMessage.contains("percussion", Qt::CaseInsensitive))
                        hasDrums = true;
                    if (fullMessage.contains("guitar", Qt::CaseInsensitive))
                        hasGuitar = true;
                    if (_file->numTracks() <= 1) { hasDrums = true; hasGuitar = true; }
                }
                simplePrompt += EditorContext::ffxivContext(hasDrums, hasGuitar);
            }
        }
        if (!_customFileInstructions.isEmpty())
            simplePrompt += QStringLiteral("\n\n## Per-File Instructions\n") + _customFileInstructions;

        // Truncate history if approaching context window limit
        QJsonArray historyForApi = truncateHistory(_conversationHistory,
                                                    _client->contextWindowForModel(),
                                                    simplePrompt.length());

        _client->sendStreamingRequest(simplePrompt,
                                       historyForApi, fullMessage);
        // Stash for self-healing retry on transient errors.
        _lastSimpleSystemPrompt = simplePrompt;
        _lastSimpleHistory = historyForApi;
        _lastSimpleMessage = fullMessage;
        _simpleRetryCount = 0;
        QSettings _retrySettings(QStringLiteral("MidiEditor"), QStringLiteral("NONE"));
        _simpleMaxRetries = _retrySettings.value(QStringLiteral("AI/simple_max_retries"), 3).toInt();
        if (_simpleMaxRetries < 0) _simpleMaxRetries = 0;
        if (_simpleMaxRetries > 10) _simpleMaxRetries = 10;
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

    // Successful response — reset the simple-mode self-healing retry counter.
    _simpleRetryCount = 0;
    _lastSimpleMessage.clear();

    // Extract token usage
    QJsonObject usage = fullResponse["usage"].toObject();
    if (!usage.isEmpty()) {
        _lastPromptTokens = usage["prompt_tokens"].toInt();
        _lastCompletionTokens = usage["completion_tokens"].toInt();
        _totalPromptTokens += _lastPromptTokens;
        _totalCompletionTokens += _lastCompletionTokens;
        updateTokenLabel();
    }

    // Remove "thinking" indicator if present (check text before removing)
    if (_chatLayout->count() > 1) {
        QLayoutItem *last = _chatLayout->itemAt(_chatLayout->count() - 1);
        if (last && last->widget()) {
            QLabel *lbl = qobject_cast<QLabel *>(last->widget());
            if (lbl && lbl->text().contains(QStringLiteral("Thinking"))) {
                _chatLayout->takeAt(_chatLayout->count() - 1);
                lbl->deleteLater();
                delete last;
            }
        }
    }

    setStatus("Ready", "green");
    _inputField->setEnabled(true);
    _sendButton->setEnabled(true);

    // Append assistant message to conversation history
    QJsonObject assistantMsg;
    assistantMsg["role"] = "assistant";
    assistantMsg["content"] = content;
    _conversationHistory.append(assistantMsg);

    finalizeTurn(content, QStringLiteral("ok"));

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

    scheduleSave();
    refreshContext();
}

void MidiPilotWidget::onStreamDelta(const QString &text) {
    // Ignore during Agent mode
    if (_isAgentRunning) return;

    // On first delta, detect if the response is a JSON action so we can
    // render it in a subdued monospace "composing" bubble (vs. a regular
    // text bubble for natural-language replies). Either way we now show
    // live streaming feedback — matching agent-mode parity.
    if (!_streamBubble) {
        QString trimmed = text.trimmed();
        bool looksJson = trimmed.startsWith('{')
                      || trimmed.startsWith('[')
                      || trimmed.startsWith("```");
        _streamIsJson = looksJson;

        // Remove "Thinking..." indicator if present (text + JSON paths)
        if (_chatLayout->count() > 1) {
            QLayoutItem *lastItem = _chatLayout->itemAt(_chatLayout->count() - 1);
            if (lastItem && lastItem->widget()) {
                QLabel *lbl = qobject_cast<QLabel *>(lastItem->widget());
                if (lbl && lbl->text().contains("Thinking")) {
                    _chatLayout->removeItem(lastItem);
                    lbl->deleteLater();
                    delete lastItem;
                }
            }
        }

        bool dark = Appearance::shouldUseDarkMode();
        _streamBubble = new QLabel(_chatContainer);
        _streamBubble->setWordWrap(true);
        // For non-JSON streamed text we want Markdown rendering. The JSON
        // branch below replaces the styling but keeps the format flag so the
        // raw payload is still shown verbatim if needed (it is hidden via
        // styling anyway and replaced on completion).
        _streamBubble->setTextFormat(looksJson ? Qt::PlainText : Qt::MarkdownText);
        if (looksJson) {
            // Subdued "preparing action" indicator — JSON payload is NOT
            // shown to the user; the parsed action result replaces this
            // bubble in onStreamFinished.
            _streamBubble->setStyleSheet(
                QString("background-color: %1; color: %2; padding: 6px 10px; "
                        "border-radius: 8px; font-size: 12px; margin-right: 40px; "
                        "font-style: italic; border-left: 2px solid %3;")
                    .arg(dark ? "#1E1E1E" : "#F0F0F0",
                         dark ? "#888"    : "#666",
                         dark ? "#444"    : "#CCC"));
            _streamBubble->setText(QStringLiteral("⚙ Composing actions…"));
        } else {
            _streamBubble->setStyleSheet(
                QString("background-color: %1; color: %2; padding: 10px 14px; "
                        "border-radius: 12px; font-size: 13px; margin-right: 40px;")
                    .arg(dark ? "#2A2A2A" : "#F5F5F0",
                         dark ? "#DDD" : "#333"));
        }
        _chatLayout->addWidget(_streamBubble);
    }

    if (!_streamBubble) return;

    if (_streamIsJson) {
        // JSON action mode — keep static "Composing actions…" indicator,
        // do NOT leak raw payload into the chat.
    } else {
        _streamBubble->setText(_streamBubble->text() + text);
    }

    // Auto-scroll to bottom
    QTimer::singleShot(10, this, [this]() {
        _chatScroll->verticalScrollBar()->setValue(
            _chatScroll->verticalScrollBar()->maximum());
    });
}

void MidiPilotWidget::onStreamFinished(const QString &fullContent, const QJsonObject &fullResponse) {
    if (_isAgentRunning) return;

    // Reset streaming state
    bool wasJson = _streamIsJson;
    _streamIsJson = false;

    // Remove the streaming bubble — we'll replace it with a proper chat bubble
    if (_streamBubble) {
        _chatLayout->removeWidget(_streamBubble);
        _streamBubble->deleteLater();
        _streamBubble = nullptr;
    }

    // Remove "Thinking..." indicator if still present (JSON path skipped the removal)
    if (wasJson && _chatLayout->count() > 1) {
        QLayoutItem *lastItem = _chatLayout->itemAt(_chatLayout->count() - 1);
        if (lastItem && lastItem->widget()) {
            QLabel *lbl = qobject_cast<QLabel *>(lastItem->widget());
            if (lbl && lbl->text().contains("Thinking")) {
                _chatLayout->removeItem(lastItem);
                lbl->deleteLater();
                delete lastItem;
            }
        }
    }

    // Delegate to the same response-processing logic as non-streamed responses
    onResponseReceived(fullContent, fullResponse);
}

void MidiPilotWidget::onErrorOccurred(const QString &errorMessage) {
    // Ignore AiClient signals during Agent Mode (AgentRunner handles them)
    if (_isAgentRunning) return;

    // Reset streaming state
    _streamIsJson = false;

    // Clean up streaming bubble if active
    if (_streamBubble) {
        _chatLayout->removeWidget(_streamBubble);
        _streamBubble->deleteLater();
        _streamBubble = nullptr;
    }

    // Self-healing retry for simple mode — mirror agent-mode classification.
    // Recoverable categories: malformed/empty/MAX_TOKENS/network. We retry
    // by replaying the stashed request with exponential backoff.
    auto isRetriable = [](const QString &err) {
        QString l = err.toLower();
        return l.contains(QStringLiteral("malformed"))
            || l.contains(QStringLiteral("max_tokens"))
            || l.contains(QStringLiteral("max tokens"))
            || l.contains(QStringLiteral("cut off"))
            || l.contains(QStringLiteral("truncated"))
            || l.contains(QStringLiteral("empty response"))
            || l.contains(QStringLiteral("ended without output"))
            || l.contains(QStringLiteral("no text and no tool call"))
            || l.contains(QStringLiteral("timeout"))
            || l.contains(QStringLiteral("timed out"))
            || l.contains(QStringLiteral("connection"))
            || l.contains(QStringLiteral("network"))
            || l.contains(QStringLiteral("http 5"))
            || l.contains(QStringLiteral("500"))
            || l.contains(QStringLiteral("502"))
            || l.contains(QStringLiteral("503"))
            || l.contains(QStringLiteral("504"))
            || l.contains(QStringLiteral("429"))
            // OpenRouter routes to a different provider on retry — its
            // "Provider returned error" / provider_name HTTP 400s are
            // effectively transient.
            || l.contains(QStringLiteral("provider returned error"))
            || l.contains(QStringLiteral("provider_name"))
            || (l.contains(QStringLiteral("http 400"))
                && l.contains(QStringLiteral("openrouter")));
    };

    if (!_lastSimpleMessage.isEmpty()
        && _simpleRetryCount < _simpleMaxRetries
        && isRetriable(errorMessage)) {
        _simpleRetryCount++;
        QString shortReason = errorMessage;
        if (shortReason.length() > 160) shortReason = shortReason.left(157) + QStringLiteral("...");
        addChatBubble(QStringLiteral("system"),
                      QStringLiteral("\u26A0 Self-healing retry %1/%2: %3")
                          .arg(_simpleRetryCount).arg(_simpleMaxRetries).arg(shortReason));
        setStatus(QString("Retrying (%1/%2)...").arg(_simpleRetryCount).arg(_simpleMaxRetries), "orange");

        // Remove the user message we appended on the original send so
        // the replay doesn't double-append it.
        if (!_conversationHistory.isEmpty()) {
            QJsonObject last = _conversationHistory.last().toObject();
            if (last.value(QStringLiteral("role")).toString() == QStringLiteral("user")
                && last.value(QStringLiteral("content")).toString() == _lastSimpleMessage) {
                _conversationHistory.removeLast();
            }
        }

        int delayMs = qMin(4000, 500 * (1 << (_simpleRetryCount - 1)));
        QString sysP = _lastSimpleSystemPrompt;
        QJsonArray hist = _lastSimpleHistory;
        QString msg = _lastSimpleMessage;
        QTimer::singleShot(delayMs, this, [this, sysP, hist, msg]() {
            _client->sendStreamingRequest(sysP, hist, msg);
            QJsonObject userMsg;
            userMsg["role"] = "user";
            userMsg["content"] = msg;
            _conversationHistory.append(userMsg);
        });
        return;
    }

    // Remove "thinking" indicator if present (check text before removing)
    if (_chatLayout->count() > 1) {
        QLayoutItem *last = _chatLayout->itemAt(_chatLayout->count() - 1);
        if (last && last->widget()) {
            QLabel *lbl = qobject_cast<QLabel *>(last->widget());
            if (lbl && lbl->text().contains(QStringLiteral("Thinking"))) {
                _chatLayout->takeAt(_chatLayout->count() - 1);
                lbl->deleteLater();
                delete last;
            }
        }
    }

    setStatus("Error", "red");
    _inputField->setEnabled(true);
    _sendButton->setEnabled(true);

    QString surfaced = errorMessage;
    if (_simpleRetryCount > 0)
        surfaced = QStringLiteral("%1 (after %2 retry attempts)").arg(errorMessage).arg(_simpleRetryCount);
    addChatBubble("system", "Error: " + surfaced);
    _simpleRetryCount = 0;
    _lastSimpleMessage.clear();
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

void MidiPilotWidget::updateTokenLabel() {
    int lastTotal = _lastPromptTokens + _lastCompletionTokens;
    int sessionTotal = _totalPromptTokens + _totalCompletionTokens;
    if (sessionTotal == 0) {
        _tokenLabel->clear();
    } else {
        QString limitStr;
        if (_client->maxTokensEnabled()) {
            int limit = _client->maxTokensLimit();
            limitStr = QString(" [%1%2]")
                .arg(limit >= 1000 ? QString::number(limit / 1000.0, 'f', 1) + "k"
                                   : QString::number(limit))
                .arg(QString::fromUtf8(" \xE2\x9C\x82")); // ✂
        }

        // Show context window usage
        int ctxWindow = _client->contextWindowForModel();
        QString ctxStr;
        if (ctxWindow > 0 && _lastPromptTokens > 0) {
            QString ctxLabel = ctxWindow >= 1000000
                ? QString::number(ctxWindow / 1000000.0, 'f', 0) + "M"
                : (ctxWindow >= 1000
                    ? QString::number(ctxWindow / 1000.0, 'f', 0) + "k"
                    : QString::number(ctxWindow));
            ctxStr = QStringLiteral(" / ") + ctxLabel;
        }

        _tokenLabel->setText(QString("%1 | %2%3%4%5")
            .arg(lastTotal)
            .arg(sessionTotal >= 1000 ? QString::number(sessionTotal / 1000.0, 'f', 1) + "k"
                                      : QString::number(sessionTotal))
            .arg(QString::fromUtf8(" \xF0\x9F\x94\xA5")) // 🔥
            .arg(ctxStr)
            .arg(limitStr));

        // Yellow warning when approaching context limit
        double ratio = static_cast<double>(_lastPromptTokens) / ctxWindow;
        if (ratio > 0.8)
            _tokenLabel->setStyleSheet("font-size: 10px; color: #CC9900; font-weight: bold;");
        else
            _tokenLabel->setStyleSheet("font-size: 10px; color: #888;");
    }
}

QJsonObject MidiPilotWidget::executeAction(const QJsonObject &actionObj) {
    return dispatchAction(actionObj, false);
}

void MidiPilotWidget::onModeChanged(int index) {
    Q_UNUSED(index);
    QSettings settings("MidiEditor", "NONE");
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

void MidiPilotWidget::onProviderComboChanged(int index) {
    Q_UNUSED(index);
    QString provider = _providerCombo->currentData().toString();
    if (provider.isEmpty()) return;

    // Save current API key for the old provider
    QString oldProvider = _client->provider();
    if (!oldProvider.isEmpty() && oldProvider != provider) {
        QSettings settings("MidiEditor", "NONE");
        QString currentKey = settings.value("AI/api_key").toString();
        if (!currentKey.isEmpty())
            settings.setValue(QString("AI/api_key/%1").arg(oldProvider), currentKey);
    }

    // Set new provider and its default base URL
    _client->setProvider(provider);
    static const QMap<QString, QString> defaultUrls = {
        {"openai",     "https://api.openai.com/v1"},
        {"openrouter", "https://openrouter.ai/api/v1"},
        {"gemini",     "https://generativelanguage.googleapis.com/v1beta/openai"},
    };
    _client->setApiBaseUrl(defaultUrls.value(provider, "https://api.openai.com/v1"));

    // Load API key for the new provider
    QSettings settings("MidiEditor", "NONE");
    QString newKey = settings.value(QString("AI/api_key/%1").arg(provider)).toString();
    settings.setValue("AI/api_key", newKey);

    // Repopulate model list and select first model
    _modelCombo->blockSignals(true);
    populateFooterModels();
    if (_modelCombo->count() > 0) {
        _modelCombo->setCurrentIndex(0);
        QString model = _modelCombo->currentData().toString();
        if (!model.isEmpty()) _client->setModel(model);
    }
    _modelCombo->blockSignals(false);
    _effortCombo->setVisible(_client->isReasoningModel());

    // Update setup prompt (checks if API key is present)
    setupSetupPrompt();
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

    // "Seal" the current thought block (if any) by appending a footer that
    // links it to the tool call it triggered, then drop the pointer so the
    // NEXT reasoning delta starts a fresh, separately framed thought block.
    // This makes it visually obvious which thoughts led to which step.
    if (_thoughtLabel) {
        if (_thoughtCursorTimer) _thoughtCursorTimer->stop();
        QString footer = QStringLiteral("\n\n→ Step %1: %2").arg(step).arg(toolName);
        _thoughtBaseText += footer;
        _thoughtLabel->setText(_thoughtBaseText);
        // Re-style the now-finalized thought block to highlight it.
        // Dark mode: tint matches the green Steps panel below for visual
        // grouping ("this thinking → these steps") and uses a bright
        // off-white text colour so the italic prose stays readable.
        // Light mode keeps the warm amber tint that worked there.
        bool dark = Appearance::shouldUseDarkMode();
        _thoughtLabel->setStyleSheet(
            QString("background: %1; color: %2; "
                    "font-style: italic; font-size: 12px; "
                    "padding: 4px 8px; margin: 2px 40px 2px 8px; "
                    "border-left: 3px solid %3;")
                .arg(dark ? "rgba(76,175,80,0.10)" : "rgba(255,165,0,0.12)",
                     dark ? "#ECECEC" : "#444",
                     dark ? "#4CAF50" : "#E68A00"));
        _thoughtLabel = nullptr;
        _thoughtBaseText.clear();
    }

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
    // Color-code the footer status to match the step outcome:
    //   OK       → green   (matches the green check in the Steps widget)
    //   retrying → orange  (still in progress)
    //   failed   → red     (terminal failure)
    QString label;
    QString color;
    if (success) {
        label = QStringLiteral("OK");
        color = QStringLiteral("green");
    } else if (recoverable) {
        label = QStringLiteral("retrying");
        color = QStringLiteral("orange");
    } else {
        label = QStringLiteral("failed");
        color = QStringLiteral("red");
    }
    setStatus(QString("Step %1: %2").arg(step).arg(label), color);

    // For a successful step, the green flash is informational — if the
    // agent loop is still in flight, return to the orange "Thinking…"
    // status after a short delay so the user sees the cycling messages
    // resume immediately and knows the loop is still progressing.
    if (success) {
        QPointer<MidiPilotWidget> self(this);
        QTimer::singleShot(700, this, [self]() {
            if (!self || !self->_isAgentRunning) return;
            self->setStatus(QStringLiteral("Thinking\xE2\x80\xA6"), QStringLiteral("orange"));
        });
    }

    // Record step in the per-turn metadata so it is persisted with the
    // conversation. We only keep a compact summary (no large arg/result
    // payloads) to keep the history file small.
    QJsonObject stepEntry;
    stepEntry[QStringLiteral("step")] = step;
    stepEntry[QStringLiteral("tool")] = toolName;
    stepEntry[QStringLiteral("success")] = success;
    if (recoverable) stepEntry[QStringLiteral("recoverable")] = true;
    _turnSteps.append(stepEntry);

    // Check off the step in the checklist
    if (_agentStepsWidget) {
        AgentStepsWidget *sw = static_cast<AgentStepsWidget *>(_agentStepsWidget);
        sw->completeStep(step, success, recoverable);
    }
}

void MidiPilotWidget::onAgentFinished(const QString &finalMessage) {
    _isAgentRunning = false;

    // Stop the thought-cursor pulse and freeze the thought label at its
    // final accumulated text (drop the trailing blinking cursor glyph).
    if (_thoughtCursorTimer) _thoughtCursorTimer->stop();
    if (_thoughtLabel) _thoughtLabel->setText(_thoughtBaseText);

    // Remove the live-streaming bubble — about to be replaced by the proper
    // final assistant bubble.
    if (_streamBubble) {
        _chatLayout->removeWidget(_streamBubble);
        _streamBubble->deleteLater();
        _streamBubble = nullptr;
    }

    // Mark steps widget as finished, then move it from the anchored dock
    // back into the scrolling chat history. We insert it AFTER the final
    // assistant bubble (below) so the reading order in the history is:
    //   [thoughts]  →  [final response]  →  [steps]
    // which matches the visual flow the user expects.
    AgentStepsWidget *swToMove = nullptr;
    if (_agentStepsWidget) {
        swToMove = static_cast<AgentStepsWidget *>(_agentStepsWidget);
        swToMove->setFinished(true);
        _agentDockArea->layout()->removeWidget(swToMove);
        swToMove->setParent(_chatContainer);
        _agentDockArea->setVisible(false);
        _agentStepsWidget = nullptr;
    }

    // Restore send/stop buttons
    _stopButton->setVisible(false);
    _sendButton->setVisible(true);

    setStatus("Ready", "green");
    _inputField->setEnabled(true);
    _sendButton->setEnabled(true);

    addChatBubble("assistant", finalMessage);

    // Now drop the steps widget in below the freshly added assistant bubble,
    // still before the trailing stretch so it sticks to the bottom of history.
    if (swToMove) {
        int insertAt = _chatLayout->count() > 0 ? _chatLayout->count() - 1 : 0;
        _chatLayout->insertWidget(insertAt, swToMove);
    }

    // Store in conversation history
    QJsonObject assistantMsg;
    assistantMsg["role"] = "assistant";
    assistantMsg["content"] = finalMessage;
    _conversationHistory.append(assistantMsg);

    finalizeTurn(finalMessage, QStringLiteral("ok"));

    ConversationEntry entry;
    entry.role = "assistant";
    entry.message = finalMessage;
    entry.timestamp = QDateTime::currentDateTime();
    _entries.append(entry);

    scheduleSave();
    refreshContext();
}

void MidiPilotWidget::onAgentError(const QString &error) {
    _isAgentRunning = false;

    // Stop the thought-cursor pulse and freeze the thought label at its
    // final accumulated text.
    if (_thoughtCursorTimer) _thoughtCursorTimer->stop();
    if (_thoughtLabel) _thoughtLabel->setText(_thoughtBaseText);

    // Drop the in-flight live-streaming bubble (if any) before the error bubble.
    if (_streamBubble) {
        _chatLayout->removeWidget(_streamBubble);
        _streamBubble->deleteLater();
        _streamBubble = nullptr;
    }

    // Mark steps widget as failed, then move it from the anchored dock
    // back into the scrolling chat history. Insert AFTER the error bubble
    // so the reading order is [thoughts] → [error] → [steps].
    AgentStepsWidget *swToMove = nullptr;
    if (_agentStepsWidget) {
        swToMove = static_cast<AgentStepsWidget *>(_agentStepsWidget);
        swToMove->setFinished(false);
        _agentDockArea->layout()->removeWidget(swToMove);
        swToMove->setParent(_chatContainer);
        _agentDockArea->setVisible(false);
        _agentStepsWidget = nullptr;
    }

    // Restore send/stop buttons
    _stopButton->setVisible(false);
    _sendButton->setVisible(true);

    setStatus("Error", "red");
    _inputField->setEnabled(true);
    _sendButton->setEnabled(true);

    addChatBubble("system", "Agent error: " + error);

    finalizeTurn(error, QStringLiteral("error"));
    scheduleSave();

    if (swToMove) {
        int insertAt = _chatLayout->count() > 0 ? _chatLayout->count() - 1 : 0;
        _chatLayout->insertWidget(insertAt, swToMove);
    }
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
    bool dark = Appearance::shouldUseDarkMode();

    // Timestamp label — small outlined pill above the bubble
    QString timestamp = QTime::currentTime().toString("HH:mm:ss");
    QLabel *timeLabel = new QLabel(timestamp, _chatContainer);
    QString timeColor = dark ? "#CCC" : "#888";
    timeLabel->setStyleSheet(
        QString("color: %1; font-size: 10px; padding: 0px 4px; margin: 0px; "
                "background: transparent;")
            .arg(timeColor));
    timeLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    if (role == "user")
        timeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    else
        timeLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    // Wrap in a widget to align left/right
    QWidget *timeWidget = new QWidget(_chatContainer);
    QHBoxLayout *timeRow = new QHBoxLayout(timeWidget);
    timeRow->setContentsMargins(12, 2, 12, 0);
    timeRow->setSpacing(0);
    if (role == "user")
        timeRow->addStretch();
    timeRow->addWidget(timeLabel);
    if (role != "user")
        timeRow->addStretch();
    _chatLayout->addWidget(timeWidget);

    QLabel *bubble = new QLabel(text, _chatContainer);
    bubble->setWordWrap(true);
    bubble->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    bubble->setContentsMargins(10, 6, 10, 6);
    // Assistant replies often contain Markdown (bold, italics, bullet
    // lists, inline `code`). Render them as Markdown so the visual output
    // matches the rest of the app instead of leaking raw `**` and `*`
    // tokens. User and system bubbles stay plain text — the user typed
    // it themselves and system messages are short status strings.
    if (role == QStringLiteral("assistant")) {
        bubble->setTextFormat(Qt::MarkdownText);
        bubble->setOpenExternalLinks(true);
    } else {
        bubble->setTextFormat(Qt::PlainText);
    }

    // Custom styled context menu (Copy / Select All)
    bubble->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(bubble, &QLabel::customContextMenuRequested, this, [bubble](const QPoint &pos) {
        QMenu menu(bubble);
        menu.setStyleSheet(
            "QMenu { background-color: #2D2D30; border: 1px solid #3F3F46; "
            "        padding: 0px; margin: 0px; font-size: 12px; }"
            "QMenu::item { color: #E0E0E0; padding: 3px 14px; margin: 0px; }"
            "QMenu::item:selected { background-color: #094771; }"
            "QMenu::separator { height: 1px; background: #3F3F46; margin: 0px 6px; }");
        QAction *copyAct = menu.addAction("Copy");
        copyAct->setShortcut(QKeySequence::Copy);
        copyAct->setEnabled(bubble->hasSelectedText());
        QAction *selectAllAct = menu.addAction("Select All");
        QAction *chosen = menu.exec(bubble->mapToGlobal(pos));
        if (chosen == copyAct) {
            QApplication::clipboard()->setText(bubble->selectedText());
        } else if (chosen == selectAllAct) {
            // Select the full bubble text — QLabel doesn't have selectAll(),
            // so we copy the entire text directly to clipboard
            QApplication::clipboard()->setText(bubble->text());
        }
    });

    if (role == "user") {
        bubble->setStyleSheet(
            QString("background-color: %1; color: white; "
                    "border-radius: 12px; padding: 10px 14px; margin-left: 40px;")
                .arg(dark ? "#0063B1" : "#0078D4"));
    } else if (role == "assistant") {
        bubble->setStyleSheet(
            QString("background-color: %1; color: %2; "
                    "border-radius: 12px; padding: 10px 14px; margin-right: 40px;")
                .arg(dark ? "#2A2A2A" : "#F0F0F0",
                     dark ? "#DDD" : "#222"));
    } else {
        // system messages — no separate timestamp
        timeWidget->hide();
        bubble->setStyleSheet(
            QString("color: %1; font-style: italic; padding: 4px;")
                .arg(dark ? "#888" : "gray"));
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

    // Keep the animated dots + cycling fun-messages running while either an
    // explicit "processing" status is set OR the agent loop is still in
    // flight (so a green "Step N: OK" flash does not freeze the bar between
    // steps).
    if (isProcessing || _isAgentRunning) {
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

static QString protoPrefix(const QJsonObject &response) {
    QString src = response["_source"].toString();
    if (!src.startsWith(QLatin1String("mcp")))
        return QStringLiteral("MidiPilot");

    // "mcp" -> "MidiPilotMCP", "mcp:ClientName" -> "MidiPilotMCP (ClientName)"
    int colon = src.indexOf(':');
    if (colon > 0 && colon + 1 < src.length())
        return QStringLiteral("MidiPilotMCP (%1)").arg(src.mid(colon + 1));
    return QStringLiteral("MidiPilotMCP");
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

    // Start protocol action for undo support (each tool call gets its own undo step)
    QString protoMsg = QStringLiteral("%1: Agent insert events - %2 (%3)")
                           .arg(protoPrefix(response))
                           .arg(track->name())
                           .arg(events.size());
    _file->protocol()->startNewAction(protoMsg);

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
    QStringList skippedErrors;
    bool ok = MidiEventSerializer::deserialize(events, _file, track, channel, created, &skippedErrors);

    if (!ok) {
        _file->protocol()->endAction();
        if (showBubbles) {
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
    }
    _file->protocol()->endAction();

    emit requestRepaint();

    result["success"] = true;
    result["eventsCreated"] = created.size();
    if (!skippedErrors.isEmpty()) {
        result["skipped"] = skippedErrors.size();
        QJsonArray errArr;
        for (const QString &e : skippedErrors)
            errArr.append(e);
        result["skippedErrors"] = errArr;
    }
    result["summary"] = buildMusicalSummary(created);
    return result;
}

QJsonObject MidiPilotWidget::applyAiDeletes(const QJsonObject &response, bool showBubbles) {
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

    _file->protocol()->startNewAction(
        QStringLiteral("%1: Agent delete events (%2)").arg(protoPrefix(response)).arg(toDelete.size()));

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

    _file->protocol()->endAction();

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

        _file->protocol()->startNewAction(
            QStringLiteral("%1: Agent create track - %2").arg(protoPrefix(response)).arg(trackName));
        _file->addTrack();
        MidiTrack *newTrack = _file->tracks()->at(_file->numTracks() - 1);
        newTrack->setName(trackName);
        if (channel >= 0 && channel <= 15) {
            newTrack->assignChannel(channel);
        }
        _file->protocol()->endAction();

        result["success"] = true;
        result["trackIndex"] = _file->numTracks() - 1;
        result["hint"] = QString("Remember to insert a program_change at tick 0 to set the instrument sound.");

    } else if (action == "rename_track") {
        int trackIndex = response["trackIndex"].toInt(-1);
        QString newName = response["newName"].toString();

        if (trackIndex < 0 || trackIndex >= _file->numTracks() || newName.isEmpty()) {
            if (showBubbles) addChatBubble("system", "Invalid track index or name for rename.");
            result["success"] = false;
            result["error"] = QString("Invalid track index or name for rename.");
            return result;
        }

        _file->protocol()->startNewAction(
            QStringLiteral("%1: Agent rename track %2 - %3").arg(protoPrefix(response)).arg(trackIndex).arg(newName));
        _file->track(trackIndex)->setName(newName);
        _file->protocol()->endAction();

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

        _file->protocol()->startNewAction(
            QStringLiteral("%1: Agent set channel - Track %2 - Ch %3").arg(protoPrefix(response)).arg(trackIndex).arg(channel));
        _file->track(trackIndex)->assignChannel(channel);
        _file->protocol()->endAction();

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

    _file->protocol()->startNewAction(
        QStringLiteral("%1: Agent move events - %2 (%3)")
            .arg(protoPrefix(response)).arg(targetTrack->name()).arg(toMove.size()));

    for (MidiEvent *ev : toMove) {
        ev->setTrack(targetTrack, false);
    }

    _file->protocol()->endAction();

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

    _file->protocol()->startNewAction(
        QStringLiteral("%1: Agent set tempo - %2 BPM").arg(protoPrefix(response)).arg(bpm));

    // Check if there's already a tempo event at this tick
    QMultiMap<int, MidiEvent *> *tempoMap = _file->tempoEvents();
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

    _file->protocol()->endAction();
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

    _file->protocol()->startNewAction(
        QStringLiteral("%1: Agent set time sig - %2/%3").arg(protoPrefix(response)).arg(num).arg(denomActual));

    // Check if there's already a time signature event at this tick
    QMultiMap<int, MidiEvent *> *tsMap = _file->timeSignatureEvents();
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

    _file->protocol()->endAction();

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

    _file->protocol()->startNewAction(
        QStringLiteral("%1: Agent edit events - %2 (%3)")
            .arg(protoPrefix(response)).arg(targetTrack->name()).arg(events.size()));

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
    QStringList skippedErrors;
    MidiEventSerializer::deserialize(events, _file, targetTrack, channel, created, &skippedErrors);

    if (!created.isEmpty()) {
        Selection::instance()->setSelection(created);
    }

    _file->protocol()->endAction();

    emit requestRepaint();

    result["success"] = true;
    result["eventsCreated"] = created.size();
    if (!skippedErrors.isEmpty()) {
        result["skipped"] = skippedErrors.size();
        QJsonArray errArr;
        for (const QString &e : skippedErrors)
            errArr.append(e);
        result["skippedErrors"] = errArr;
    }
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

    _file->protocol()->startNewAction(
        QStringLiteral("%1: Agent delete events - %2")
            .arg(protoPrefix(response)).arg(targetTrack->name()));

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

    _file->protocol()->endAction();

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

// === Context Window Management ===

QJsonArray MidiPilotWidget::truncateHistory(const QJsonArray &history, int contextWindow,
                                            int systemPromptChars) const
{
    if (history.isEmpty())
        return history;

    // Estimate total chars, then tokens (~4 chars per token)
    int totalChars = 0;
    for (const QJsonValue &v : history) {
        QJsonObject msg = v.toObject();
        totalChars += msg[QStringLiteral("content")].toString().length();
        // Tool calls and tool results can be large
        if (msg.contains(QStringLiteral("tool_calls")))
            totalChars += QJsonDocument(msg[QStringLiteral("tool_calls")].toArray()).toJson().size();
    }

    int estimatedTokens = totalChars / 4;
    // Subtract system prompt tokens from budget, then reserve 25% for new request + response
    int sysPromptTokens = systemPromptChars / 4;
    int availableTokens = contextWindow - sysPromptTokens;
    int maxHistoryTokens = static_cast<int>(availableTokens * 0.75);

    if (estimatedTokens <= maxHistoryTokens)
        return history; // fits fine

    // Sliding window: keep first 2 messages (task context) + as many recent messages as fit
    QJsonArray truncated;
    int keepFront = qMin(2, history.size());

    // Calculate chars for front messages
    int frontChars = 0;
    for (int i = 0; i < keepFront; i++) {
        frontChars += history[i].toObject()[QStringLiteral("content")].toString().length();
    }

    // Fill from the back with remaining budget
    int budgetChars = maxHistoryTokens * 4 - frontChars;
    QList<int> backIndices;
    int backChars = 0;
    for (int i = history.size() - 1; i >= keepFront; i--) {
        int msgChars = history[i].toObject()[QStringLiteral("content")].toString().length();
        if (backChars + msgChars > budgetChars)
            break;
        backChars += msgChars;
        backIndices.prepend(i);
    }

    // Build truncated array
    for (int i = 0; i < keepFront; i++)
        truncated.append(history[i]);

    if (!backIndices.isEmpty() && backIndices.first() > keepFront) {
        // Insert marker for dropped messages
        QJsonObject marker;
        marker[QStringLiteral("role")] = QStringLiteral("system");
        int dropped = backIndices.first() - keepFront;
        marker[QStringLiteral("content")] = QStringLiteral("[Context truncated — %1 older messages removed to fit context window]")
                                                .arg(dropped);
        truncated.append(marker);
    }

    for (int idx : backIndices)
        truncated.append(history[idx]);

    return truncated;
}

// === Persistent Conversation History ===

void MidiPilotWidget::resetTurnState()
{
    _turnReasoning.clear();
    _turnSteps = QJsonArray();
    _turnStartMs = QDateTime::currentMSecsSinceEpoch();
    _turnStreamed = false;
    _turnEffort = _client ? _client->reasoningEffort() : QString();
    _turnProvider = _client ? _client->provider() : QString();
    _turnModel = _client ? _client->model() : QString();
}

void MidiPilotWidget::finalizeTurn(const QString &finalText, const QString &status)
{
    Q_UNUSED(finalText);
    // Anchor the turn to the just-appended assistant message so that on
    // reload we know which message in `messages[]` it belongs to.
    int assistantIndex = _conversationHistory.size() - 1;
    if (assistantIndex < 0)
        return;

    QJsonObject turn;
    turn[QStringLiteral("assistantIndex")] = assistantIndex;
    turn[QStringLiteral("status")] = status;
    if (!_turnReasoning.isEmpty())
        turn[QStringLiteral("reasoning")] = _turnReasoning;
    if (!_turnSteps.isEmpty())
        turn[QStringLiteral("steps")] = _turnSteps;
    turn[QStringLiteral("streamed")] = _turnStreamed;
    if (_turnStartMs > 0) {
        qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - _turnStartMs;
        if (elapsed >= 0)
            turn[QStringLiteral("latencyMs")] = static_cast<qint64>(elapsed);
    }
    if (!_turnEffort.isEmpty()) turn[QStringLiteral("effort")] = _turnEffort;
    if (!_turnProvider.isEmpty()) turn[QStringLiteral("provider")] = _turnProvider;
    if (!_turnModel.isEmpty()) turn[QStringLiteral("model")] = _turnModel;
    turn[QStringLiteral("promptTokens")] = _lastPromptTokens;
    turn[QStringLiteral("completionTokens")] = _lastCompletionTokens;
    _turns.append(turn);

    // Reset accumulators so a stray late callback can't pollute the next turn.
    _turnReasoning.clear();
    _turnSteps = QJsonArray();
    _turnStartMs = 0;
    _turnStreamed = false;
}

void MidiPilotWidget::scheduleSave()
{
    if (_conversationHistory.isEmpty())
        return;
    if (_conversationId.isEmpty())
        _conversationId = ConversationStore::generateId();
    _saveTimer->start();
}

void MidiPilotWidget::doSaveConversation()
{
    if (_conversationId.isEmpty() || _conversationHistory.isEmpty())
        return;

    QJsonObject data;
    data[QStringLiteral("id")] = _conversationId;
    data[QStringLiteral("updated")] = QDateTime::currentDateTime().toString(Qt::ISODate);

    QString title;
    for (int i = 0; i < _conversationHistory.size(); i++) {
        QJsonObject msg = _conversationHistory[i].toObject();
        if (msg[QStringLiteral("role")].toString() == QStringLiteral("user")) {
            title = ConversationStore::titleFromMessage(msg[QStringLiteral("content")].toString());
            break;
        }
    }
    data[QStringLiteral("title")] = title;

    QJsonObject existing = ConversationStore::loadConversation(_conversationId);
    if (existing.contains(QStringLiteral("created")))
        data[QStringLiteral("created")] = existing[QStringLiteral("created")];
    else
        data[QStringLiteral("created")] = QDateTime::currentDateTime().toString(Qt::ISODate);

    data[QStringLiteral("midiFile")] = _file ? _file->path() : QString();
    data[QStringLiteral("model")] = _client->model();
    data[QStringLiteral("provider")] = _client->provider();
    data[QStringLiteral("messages")] = _conversationHistory;

    QJsonObject usage;
    usage[QStringLiteral("prompt")] = _totalPromptTokens;
    usage[QStringLiteral("completion")] = _totalCompletionTokens;
    data[QStringLiteral("tokenUsage")] = usage;

    // Persist per-turn metadata (reasoning text, agent steps, latency,
    // streaming flag, effort, provider/model) so reopening a conversation
    // shows the live thoughts and tool steps that produced each answer.
    if (!_turns.isEmpty())
        data[QStringLiteral("turns")] = _turns;

    ConversationStore::saveConversation(data);
}

// Lightweight scrollable, date-grouped popup for conversation history.
// Uses a frameless QDialog anchored near the triggering button so it
// behaves like a popup but is fully scrollable and supports search.
// Conversations are grouped by human-readable date buckets (Today /
// Yesterday / weekday / Month Year). Each row has a title, metadata line,
// Load button, and a trash icon. A search box filters rows in real time.
void MidiPilotWidget::showHistoryMenu()
{
    QList<ConversationStore::ConversationMeta> conversations = ConversationStore::listConversations();

    bool dark = Appearance::shouldUseDarkMode();
    const QString bgCol      = dark ? QStringLiteral("#2D2D30") : QStringLiteral("#FFFFFF");
    const QString fgCol      = dark ? QStringLiteral("#E0E0E0") : QStringLiteral("#222222");
    const QString dimCol     = dark ? QStringLiteral("#999999") : QStringLiteral("#666666");
    const QString borderCol  = dark ? QStringLiteral("#3F3F46") : QStringLiteral("#CFCFCF");
    const QString hoverCol   = dark ? QStringLiteral("#3E3E42") : QStringLiteral("#F0F0F0");
    const QString accentCol  = dark ? QStringLiteral("#4EC9B0") : QStringLiteral("#0B6E5C");
    const QString headerBg   = dark ? QStringLiteral("#252526") : QStringLiteral("#F6F6F6");

    QDialog *dlg = new QDialog(this, Qt::Popup);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setFixedSize(420, 480);
    dlg->setStyleSheet(QString("QDialog { background-color: %1; border: 1px solid %2; }")
                       .arg(bgCol, borderCol));

    QVBoxLayout *rootLayout = new QVBoxLayout(dlg);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    // ---- Header with title + search -------------------------------------
    QWidget *header = new QWidget(dlg);
    header->setStyleSheet(QString("background-color: %1; border-bottom: 1px solid %2;")
                          .arg(headerBg, borderCol));
    QVBoxLayout *headerLayout = new QVBoxLayout(header);
    headerLayout->setContentsMargins(10, 8, 10, 8);
    headerLayout->setSpacing(6);

    QLabel *title = new QLabel(tr("Conversation history"), header);
    title->setStyleSheet(QString("color: %1; font-weight: bold; font-size: 13px;").arg(fgCol));
    headerLayout->addWidget(title);

    QLineEdit *search = new QLineEdit(header);
    search->setPlaceholderText(tr("Search…"));
    search->setStyleSheet(QString(
        "QLineEdit { background-color: %1; color: %2; border: 1px solid %3; "
        "            border-radius: 3px; padding: 4px 6px; font-size: 12px; }"
        "QLineEdit:focus { border: 1px solid %4; }")
        .arg(bgCol, fgCol, borderCol, accentCol));
    headerLayout->addWidget(search);
    rootLayout->addWidget(header);

    // ---- Scrollable list -------------------------------------------------
    QScrollArea *scroll = new QScrollArea(dlg);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet(QString("QScrollArea { background-color: %1; }").arg(bgCol));
    QWidget *listContainer = new QWidget(scroll);
    QVBoxLayout *listLayout = new QVBoxLayout(listContainer);
    listLayout->setContentsMargins(0, 0, 0, 0);
    listLayout->setSpacing(0);

    if (conversations.isEmpty()) {
        QLabel *empty = new QLabel(tr("No saved conversations"), listContainer);
        empty->setAlignment(Qt::AlignCenter);
        empty->setStyleSheet(QString("color: %1; padding: 40px 10px; font-style: italic;").arg(dimCol));
        listLayout->addWidget(empty);
        listLayout->addStretch();
    } else {
        // Group conversations by date bucket. Keep insertion order by using
        // a vector of (bucket, list). Buckets are produced sequentially as
        // we walk the (already newest-first) conversation list.
        auto bucketForDate = [](const QDateTime &dt) -> QString {
            QDate today = QDate::currentDate();
            QDate d = dt.date();
            if (d == today) return QObject::tr("Today");
            if (d == today.addDays(-1)) return QObject::tr("Yesterday");
            if (d >= today.addDays(-6)) return dt.toString(QStringLiteral("dddd"));
            if (d.year() == today.year()) return dt.toString(QStringLiteral("MMMM"));
            return dt.toString(QStringLiteral("MMMM yyyy"));
        };

        struct Bucket {
            QString name;
            QList<ConversationStore::ConversationMeta> items;
        };
        QList<Bucket> buckets;
        for (const auto &conv : conversations) {
            QString b = bucketForDate(conv.updated);
            if (buckets.isEmpty() || buckets.last().name != b)
                buckets.append({ b, {} });
            buckets.last().items.append(conv);
        }

        // Store rows so the search filter can show/hide them. Each row also
        // tracks its parent header so empty headers can be hidden.
        struct Row { QWidget *widget; QLabel *headerLabel; QString hay; };
        QList<Row> rows;
        QList<QLabel *> allHeaders;

        for (const Bucket &b : buckets) {
            QLabel *hdr = new QLabel(b.name.toUpper(), listContainer);
            hdr->setStyleSheet(QString(
                "color: %1; background-color: %2; font-size: 10px; "
                "font-weight: bold; letter-spacing: 1px; "
                "padding: 8px 12px 4px 12px;")
                .arg(dimCol, headerBg));
            listLayout->addWidget(hdr);
            allHeaders.append(hdr);

            for (const auto &conv : b.items) {
                QString convId = conv.id;
                QString convTitle = conv.title.isEmpty()
                    ? QStringLiteral("(untitled)") : conv.title;

                QWidget *row = new QWidget(listContainer);
                row->setStyleSheet(QString(
                    "QWidget#histRow { border-bottom: 1px solid %1; }"
                    "QWidget#histRow:hover { background-color: %2; }")
                    .arg(borderCol, hoverCol));
                row->setObjectName(QStringLiteral("histRow"));

                QHBoxLayout *rowLayout = new QHBoxLayout(row);
                rowLayout->setContentsMargins(12, 6, 8, 6);
                rowLayout->setSpacing(8);

                QVBoxLayout *textLayout = new QVBoxLayout();
                textLayout->setContentsMargins(0, 0, 0, 0);
                textLayout->setSpacing(1);

                QLabel *titleLbl = new QLabel(convTitle, row);
                titleLbl->setStyleSheet(QString("color: %1; font-size: 12px;").arg(fgCol));
                titleLbl->setToolTip(convTitle);
                // Truncate very long titles so the row doesn't blow up.
                QFontMetrics fm(titleLbl->font());
                QString elided = fm.elidedText(convTitle, Qt::ElideRight, 300);
                titleLbl->setText(elided);
                textLayout->addWidget(titleLbl);

                QString meta = conv.updated.toString(QStringLiteral("HH:mm"));
                if (!conv.model.isEmpty())
                    meta += QStringLiteral(" · ") + conv.model;
                meta += QStringLiteral(" · ") + tr("%1 msgs").arg(conv.messageCount);
                QLabel *metaLbl = new QLabel(meta, row);
                metaLbl->setStyleSheet(QString("color: %1; font-size: 10px;").arg(dimCol));
                textLayout->addWidget(metaLbl);
                rowLayout->addLayout(textLayout, 1);

                // Primary action — load conversation. Styled as a subtle
                // text button so the row stays lightweight.
                QPushButton *loadBtn = new QPushButton(tr("Open"), row);
                loadBtn->setCursor(Qt::PointingHandCursor);
                loadBtn->setFlat(true);
                loadBtn->setFixedHeight(24);
                loadBtn->setStyleSheet(QString(
                    "QPushButton { background: transparent; color: %1; "
                    "              border: 1px solid %2; border-radius: 3px; "
                    "              padding: 2px 10px; font-size: 11px; }"
                    "QPushButton:hover { background-color: %1; color: %3; }")
                    .arg(accentCol, borderCol, bgCol));
                rowLayout->addWidget(loadBtn);

                QPushButton *deleteBtn = new QPushButton(row);
                deleteBtn->setText(QStringLiteral("🗑"));
                deleteBtn->setFixedSize(24, 24);
                deleteBtn->setCursor(Qt::PointingHandCursor);
                deleteBtn->setToolTip(tr("Delete conversation"));
                deleteBtn->setFlat(true);
                deleteBtn->setStyleSheet(QString(
                    "QPushButton { background: transparent; color: %1; border: none; "
                    "              font-size: 13px; border-radius: 3px; }"
                    "QPushButton:hover { background-color: #C94545; color: white; }")
                    .arg(dimCol));
                rowLayout->addWidget(deleteBtn);

                QObject::connect(loadBtn, &QPushButton::clicked, dlg,
                                 [this, convId, dlg]() {
                    loadConversation(convId);
                    dlg->close();
                });
                QObject::connect(deleteBtn, &QPushButton::clicked, dlg,
                                 [convId, row]() {
                    ConversationStore::deleteConversation(convId);
                    row->hide();
                    row->deleteLater();
                });

                listLayout->addWidget(row);
                rows.append({ row, hdr,
                              (convTitle + QStringLiteral(" ") + meta).toLower() });
            }
        }

        listLayout->addStretch();

        // Live filtering: hide non-matching rows; hide headers whose rows
        // are all filtered out.
        QObject::connect(search, &QLineEdit::textChanged, dlg,
                         [rows, allHeaders](const QString &q) {
            QString needle = q.trimmed().toLower();
            QHash<QLabel *, int> visibleCount;
            for (QLabel *h : allHeaders) visibleCount[h] = 0;
            for (const auto &r : rows) {
                bool match = needle.isEmpty() || r.hay.contains(needle);
                r.widget->setVisible(match);
                if (match) visibleCount[r.headerLabel]++;
            }
            for (QLabel *h : allHeaders)
                h->setVisible(visibleCount.value(h, 0) > 0);
        });
    }

    scroll->setWidget(listContainer);
    rootLayout->addWidget(scroll, 1);

    // ---- Footer with "Clear all" ----------------------------------------
    if (!conversations.isEmpty()) {
        QWidget *footer = new QWidget(dlg);
        footer->setStyleSheet(QString("background-color: %1; border-top: 1px solid %2;")
                              .arg(headerBg, borderCol));
        QHBoxLayout *footerLayout = new QHBoxLayout(footer);
        footerLayout->setContentsMargins(10, 6, 10, 6);
        footerLayout->addStretch();
        QPushButton *clearAll = new QPushButton(tr("Clear all history"), footer);
        clearAll->setCursor(Qt::PointingHandCursor);
        clearAll->setStyleSheet(QString(
            "QPushButton { background: transparent; color: %1; border: none; "
            "              font-size: 11px; padding: 4px 8px; }"
            "QPushButton:hover { color: #C94545; text-decoration: underline; }")
            .arg(dimCol));
        footerLayout->addWidget(clearAll);
        rootLayout->addWidget(footer);

        QObject::connect(clearAll, &QPushButton::clicked, dlg, [this, dlg]() {
            QMessageBox msgBox(this);
            msgBox.setIcon(QMessageBox::Warning);
            msgBox.setWindowTitle(tr("Clear History"));
            msgBox.setText(tr("Delete all saved conversations?"));
            msgBox.setInformativeText(tr("This cannot be undone."));
            msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
            msgBox.setDefaultButton(QMessageBox::Cancel);
            if (msgBox.exec() == QMessageBox::Yes) {
                ConversationStore::deleteAll();
                dlg->close();
            }
        });
    }

    // Position the popup near the cursor (which is at the button).
    QPoint pos = QCursor::pos();
    // Anchor so the popup opens upward if near the bottom of the screen.
    QRect screen = QApplication::primaryScreen()->availableGeometry();
    int x = qBound(screen.left() + 4, pos.x() - dlg->width() + 20,
                   screen.right() - dlg->width() - 4);
    int y = pos.y() + 8;
    if (y + dlg->height() > screen.bottom())
        y = pos.y() - dlg->height() - 8;
    dlg->move(x, y);
    dlg->show();
    search->setFocus();
}

void MidiPilotWidget::loadConversation(const QString &id)
{
    QJsonObject data = ConversationStore::loadConversation(id);
    if (data.isEmpty())
        return;

    _conversationHistory = QJsonArray();
    _entries.clear();
    _lastPromptTokens = 0;
    _lastCompletionTokens = 0;
    _totalPromptTokens = 0;
    _totalCompletionTokens = 0;

    while (_chatLayout->count() > 1) {
        QLayoutItem *item = _chatLayout->takeAt(1);
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }

    _conversationId = id;
    _conversationHistory = data[QStringLiteral("messages")].toArray();
    _turns = data[QStringLiteral("turns")].toArray();

    QJsonObject tokUsage = data[QStringLiteral("tokenUsage")].toObject();
    _totalPromptTokens = tokUsage[QStringLiteral("prompt")].toInt();
    _totalCompletionTokens = tokUsage[QStringLiteral("completion")].toInt();
    updateTokenLabel();

    // Index turns by assistantIndex so we can render the persisted
    // reasoning/steps next to the assistant message they belong to.
    QHash<int, QJsonObject> turnByIdx;
    for (const QJsonValue &v : std::as_const(_turns)) {
        QJsonObject t = v.toObject();
        int idx = t.value(QStringLiteral("assistantIndex")).toInt(-1);
        if (idx >= 0) turnByIdx.insert(idx, t);
    }

    for (int i = 0; i < _conversationHistory.size(); i++) {
        QJsonObject msg = _conversationHistory[i].toObject();
        QString role = msg[QStringLiteral("role")].toString();
        QString content = msg[QStringLiteral("content")].toString();

        if (role == QStringLiteral("system") || role == QStringLiteral("tool"))
            continue;

        if (role == QStringLiteral("user") || role == QStringLiteral("assistant")) {
            // For user messages, extract just the instruction from the JSON payload
            QString displayText = content;
            if (role == QStringLiteral("user")) {
                QJsonDocument jd = QJsonDocument::fromJson(content.toUtf8());
                if (jd.isObject()) {
                    QString instr = jd.object()[QStringLiteral("instruction")].toString();
                    if (!instr.isEmpty())
                        displayText = instr;
                }
            }

            // Render the persisted reasoning block (if any) BEFORE the
            // assistant bubble so the reload visual matches the live order.
            if (role == QStringLiteral("assistant") && turnByIdx.contains(i)) {
                const QJsonObject t = turnByIdx.value(i);
                QString reasoning = t.value(QStringLiteral("reasoning")).toString();
                if (!reasoning.isEmpty()) {
                    bool dark = Appearance::shouldUseDarkMode();
                    QLabel *thoughts = new QLabel(_chatContainer);
                    thoughts->setWordWrap(true);
                    thoughts->setTextFormat(Qt::MarkdownText);
                    // Match the live-streaming dark-mode contrast bump.
                    thoughts->setStyleSheet(
                        QString("background: transparent; color: %1; "
                                "font-style: italic; font-size: 12px; "
                                "padding: 4px 8px; margin: 2px 40px 2px 8px; "
                                "border-left: 2px solid %2;")
                            .arg(dark ? "#C8C8C8" : "#666",
                                 dark ? "#707070" : "#CCC"));
                    thoughts->setText(QStringLiteral("💭 ") + reasoning);
                    _chatLayout->addWidget(thoughts);
                }
            }

            addChatBubble(role, displayText);

            // Steps + per-turn metadata footer go AFTER the assistant bubble.
            if (role == QStringLiteral("assistant") && turnByIdx.contains(i)) {
                const QJsonObject t = turnByIdx.value(i);
                QJsonArray steps = t.value(QStringLiteral("steps")).toArray();
                if (!steps.isEmpty()) {
                    QStringList parts;
                    for (const QJsonValue &sv : std::as_const(steps)) {
                        QJsonObject s = sv.toObject();
                        bool ok = s.value(QStringLiteral("success")).toBool(true);
                        parts << QString::fromUtf8(ok ? "\xe2\x9c\x93 " : "\xe2\x9c\x97 ")
                                + s.value(QStringLiteral("tool")).toString();
                    }
                    bool dark = Appearance::shouldUseDarkMode();
                    QLabel *stepsLbl = new QLabel(_chatContainer);
                    stepsLbl->setWordWrap(true);
                    stepsLbl->setTextFormat(Qt::PlainText);
                    stepsLbl->setStyleSheet(
                        QString("background: transparent; color: %1; "
                                "font-size: 11px; padding: 2px 8px; "
                                "margin: 0 40px 4px 8px;")
                            .arg(dark ? "#888" : "#666"));
                    stepsLbl->setText(QStringLiteral("🔧 Steps: ") + parts.join(QStringLiteral(", ")));
                    _chatLayout->addWidget(stepsLbl);
                }
            }

            ConversationEntry entry;
            entry.role = role;
            entry.message = displayText;
            _entries.append(entry);
        }
    }
}

void MidiPilotWidget::loadPresetForFile(const QString &midiPath) {
    _customFileInstructions.clear();
    if (midiPath.isEmpty())
        return;

    QString presetPath = midiPath + QStringLiteral(".midipilot.json");
    QFile f(presetPath);
    if (!f.open(QIODevice::ReadOnly))
        return;

    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (!doc.isObject())
        return;

    QJsonObject obj = doc.object();

    // Apply provider (before model, so model list is populated correctly)
    if (obj.contains(QStringLiteral("provider"))) {
        int idx = _providerCombo->findData(obj[QStringLiteral("provider")].toString());
        if (idx >= 0)
            _providerCombo->setCurrentIndex(idx);
    }

    // Apply model
    if (obj.contains(QStringLiteral("model"))) {
        QString model = obj[QStringLiteral("model")].toString();
        int idx = _modelCombo->findData(model);
        if (idx >= 0)
            _modelCombo->setCurrentIndex(idx);
        else
            _modelCombo->setEditText(model);
    }

    // Apply mode
    if (obj.contains(QStringLiteral("mode"))) {
        int idx = _modeCombo->findData(obj[QStringLiteral("mode")].toString());
        if (idx >= 0)
            _modeCombo->setCurrentIndex(idx);
    }

    // Apply FFXIV
    if (obj.contains(QStringLiteral("ffxiv")))
        _ffxivCheck->setChecked(obj[QStringLiteral("ffxiv")].toBool());

    // Apply effort
    if (obj.contains(QStringLiteral("effort"))) {
        int idx = _effortCombo->findData(obj[QStringLiteral("effort")].toString());
        if (idx >= 0)
            _effortCombo->setCurrentIndex(idx);
    }

    // Custom instructions
    _customFileInstructions = obj[QStringLiteral("instructions")].toString();

    if (!_customFileInstructions.isEmpty()) {
        addChatBubble("system", QString::fromUtf8("\xF0\x9F\x93\x8B Loaded AI preset for this file."));
    }
}

void MidiPilotWidget::savePresetForFile() {
    if (!_file) {
        QMessageBox::warning(this, "Save AI Preset", "No file is currently open.");
        return;
    }

    QString filePath = _file->path();
    if (filePath.isEmpty()) {
        filePath = QFileDialog::getSaveFileName(
            this, "Save AI Preset — choose MIDI file",
            QString(), "MIDI Files (*.mid *.midi *.kar);;All Files (*)");
        if (filePath.isEmpty())
            return;
    }

    // Ask for custom instructions
    bool ok = false;
    QString instructions = QInputDialog::getMultiLineText(
        this, "Save AI Preset",
        "Custom instructions for this file (optional):",
        _customFileInstructions, &ok);
    if (!ok)
        return;

    QJsonObject obj;
    obj[QStringLiteral("provider")] = _providerCombo->currentData().toString();
    obj[QStringLiteral("model")] = _modelCombo->currentData().toString().isEmpty()
        ? _modelCombo->currentText()
        : _modelCombo->currentData().toString();
    obj[QStringLiteral("mode")] = _modeCombo->currentData().toString();
    obj[QStringLiteral("ffxiv")] = _ffxivCheck->isChecked();
    obj[QStringLiteral("effort")] = _effortCombo->currentData().toString();
    if (!instructions.isEmpty())
        obj[QStringLiteral("instructions")] = instructions;

    QString presetPath = filePath + QStringLiteral(".midipilot.json");
    QFile f(presetPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, "Save AI Preset",
            QString("Could not write preset file:\n%1").arg(presetPath));
        return;
    }

    f.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    f.close();

    _customFileInstructions = instructions;
    addChatBubble("system", QString::fromUtf8("\xE2\x9C\x85 AI preset saved for this file."));
}
