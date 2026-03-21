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
#include <QTimer>
#include <QSet>
#include <QSettings>
#include <QKeyEvent>

#include <cmath>

#include "MainWindow.h"
#include "../ai/AiClient.h"
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

MidiPilotWidget::MidiPilotWidget(MainWindow *mainWindow, QWidget *parent)
    : QWidget(parent), _mainWindow(mainWindow), _file(nullptr) {

    _client = new AiClient(this);

    setupUi();

    connect(_client, &AiClient::responseReceived, this, &MidiPilotWidget::onResponseReceived);
    connect(_client, &AiClient::errorOccurred, this, &MidiPilotWidget::onErrorOccurred);
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

    inputBtnLayout->addStretch();

    _sendButton = new QPushButton(QChar(0x27A4), this); // ➤
    _sendButton->setFixedSize(28, 28);
    _sendButton->setToolTip("Send");
    connect(_sendButton, &QPushButton::clicked, this, &MidiPilotWidget::onSendMessage);
    inputBtnLayout->addWidget(_sendButton);

    inputOuterLayout->addLayout(inputBtnLayout);

    // Wire up Enter-to-send
    inputEdit->onSend = [this]() { onSendMessage(); };

    mainLayout->addWidget(inputFrame);

    // === Footer (status, model, settings — below input) ===
    QFrame *footerFrame = new QFrame(this);
    footerFrame->setFrameShape(QFrame::NoFrame);
    QHBoxLayout *footerLayout = new QHBoxLayout(footerFrame);
    footerLayout->setContentsMargins(4, 2, 4, 2);
    footerLayout->setSpacing(6);

    _statusLabel = new QLabel("Disconnected", this);
    _statusLabel->setStyleSheet("font-weight: bold; font-size: 11px;");
    footerLayout->addWidget(_statusLabel);

    footerLayout->addStretch();

    _modelLabel = new QLabel(_client->model(), this);
    _modelLabel->setStyleSheet("color: gray; font-size: 11px;");
    footerLayout->addWidget(_modelLabel);

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
        QString modelText = _client->model();
        if (_client->thinkingEnabled()) {
            modelText += QString(" \xF0\x9F\x92\xAD %1").arg(_client->reasoningEffort());
        } else if (_client->isReasoningModel()) {
            modelText += QString(" \xF0\x9F\x92\xAD auto");
        }
        _modelLabel->setText(modelText);
    } else {
        setStatus("Not configured", "orange");
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

    // Add "thinking" indicator
    addChatBubble("system", "Thinking...");

    setStatus("Processing...", "orange");
    _inputField->setEnabled(false);
    _sendButton->setEnabled(false);

    _client->sendRequest(EditorContext::systemPrompt(), _conversationHistory, fullMessage);

    // Update conversation history for future requests
    QJsonObject userMsg;
    userMsg["role"] = "user";
    userMsg["content"] = fullMessage;
    _conversationHistory.append(userMsg);
}

void MidiPilotWidget::onResponseReceived(const QString &content, const QJsonObject &fullResponse) {
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
                dispatchAction(actionObj);
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
            bool handled = dispatchAction(response);
            if (handled) {
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
        // Plain text response (analysis, explanation)
        addChatBubble("assistant", content);
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

    // Re-check configuration after dialog closes
    QTimer::singleShot(500, this, [this]() {
        _client->reloadSettings();
        setupSetupPrompt();
    });
}

void MidiPilotWidget::addChatBubble(const QString &role, const QString &text) {
    QLabel *bubble = new QLabel(text, _chatContainer);
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
    _statusLabel->setStyleSheet(
        QString("font-weight: bold; font-size: 11px; color: %1;").arg(color));
}

bool MidiPilotWidget::dispatchAction(const QJsonObject &response) {
    QString action = response["action"].toString();

    if (action == "edit" && response.contains("events")) {
        applyAiEdits(response);
    } else if (action == "delete" && response.contains("deleteIndices")) {
        applyAiDeletes(response);
    } else if (action == "move_to_track") {
        applyMoveToTrack(response);
    } else if (action == "create_track" || action == "rename_track" || action == "set_channel") {
        applyTrackAction(response);
    } else if (action == "set_tempo") {
        applyTempoAction(response);
    } else if (action == "set_time_signature") {
        applyTimeSignatureAction(response);
    } else if (action == "select_and_edit") {
        applySelectAndEdit(response);
    } else if (action == "select_and_delete") {
        applySelectAndDelete(response);
    } else if (action == "info" || action == "error") {
        // No-op here; caller handles display
    } else {
        return false;
    }
    return true;
}

void MidiPilotWidget::applyAiEdits(const QJsonObject &response) {
    if (!_file)
        return;

    QJsonArray events = response["events"].toArray();
    if (events.isEmpty())
        return;

    QString explanation = response["explanation"].toString("MidiPilot edit");

    // Determine target track: prefer AI-specified track, fallback to editor state
    int trackIndex = -1;
    if (response.contains("track")) {
        trackIndex = response["track"].toInt(-1);
    }
    if (trackIndex < 0 || trackIndex >= _file->numTracks()) {
        trackIndex = NewNoteTool::editTrack();
    }
    int channel = NewNoteTool::editChannel();

    MidiTrack *track = nullptr;
    if (trackIndex >= 0 && trackIndex < _file->numTracks()) {
        track = _file->track(trackIndex);
    } else if (_file->numTracks() > 0) {
        track = _file->track(0);
    }

    if (!track)
        return;

    // Start protocol action for undo support
    _file->protocol()->startNewAction("MidiPilot: " + explanation);

    // Remove currently selected events (they will be replaced by AI output)
    QList<MidiEvent *> selected = Selection::instance()->selectedEvents();
    for (MidiEvent *ev : selected) {
        MidiChannel *ch = _file->channel(ev->channel());
        if (ch) ch->removeEvent(ev);
    }
    Selection::instance()->clearSelection();

    // Deserialize and insert new events
    QList<MidiEvent *> created;
    bool ok = MidiEventSerializer::deserialize(events, _file, track, channel, created);

    if (!ok) {
        _file->protocol()->endAction();
        addChatBubble("system", "Warning: Some events could not be applied.");
        return;
    }

    // Select the newly created events
    Selection::instance()->setSelection(created);

    _file->protocol()->endAction();

    emit requestRepaint();
}

void MidiPilotWidget::applyAiDeletes(const QJsonObject &response) {
    if (!_file)
        return;

    QJsonArray indices = response["deleteIndices"].toArray();
    if (indices.isEmpty())
        return;

    QString explanation = response["explanation"].toString("MidiPilot delete");

    QList<MidiEvent *> selected = Selection::instance()->selectedEvents();
    if (selected.isEmpty())
        return;

    // Collect indices to delete (validate bounds)
    QSet<int> toDelete;
    for (const QJsonValue &v : indices) {
        int idx = v.toInt(-1);
        if (idx >= 0 && idx < selected.size()) {
            toDelete.insert(idx);
        }
    }

    if (toDelete.isEmpty())
        return;

    _file->protocol()->startNewAction("MidiPilot: " + explanation);

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
}

void MidiPilotWidget::applyTrackAction(const QJsonObject &response) {
    if (!_file)
        return;

    QString action = response["action"].toString();
    QString explanation = response["explanation"].toString("MidiPilot track action");

    if (action == "create_track") {
        QString trackName = response["trackName"].toString("New Track");
        int channel = response["channel"].toInt(-1);

        _file->protocol()->startNewAction("MidiPilot: " + explanation);
        _file->addTrack();
        MidiTrack *newTrack = _file->tracks()->at(_file->numTracks() - 1);
        newTrack->setName(trackName);
        if (channel >= 0 && channel <= 15) {
            newTrack->assignChannel(channel);
        }
        _file->protocol()->endAction();

    } else if (action == "rename_track") {
        int trackIndex = response["trackIndex"].toInt(-1);
        QString newName = response["newName"].toString();

        if (trackIndex < 0 || trackIndex >= _file->numTracks() || newName.isEmpty()) {
            addChatBubble("system", "Invalid track index or name for rename.");
            return;
        }

        _file->protocol()->startNewAction("MidiPilot: " + explanation);
        _file->track(trackIndex)->setName(newName);
        _file->protocol()->endAction();

    } else if (action == "set_channel") {
        int trackIndex = response["trackIndex"].toInt(-1);
        int channel = response["channel"].toInt(-1);

        if (trackIndex < 0 || trackIndex >= _file->numTracks()) {
            addChatBubble("system", "Invalid track index for channel assignment.");
            return;
        }
        if (channel < 0 || channel > 15) {
            addChatBubble("system", "Channel must be 0-15.");
            return;
        }

        _file->protocol()->startNewAction("MidiPilot: " + explanation);
        _file->track(trackIndex)->assignChannel(channel);
        _file->protocol()->endAction();

    } else {
        return;
    }

    _mainWindow->updateTrackMenu();

    emit requestRepaint();
}

void MidiPilotWidget::applyMoveToTrack(const QJsonObject &response) {
    if (!_file)
        return;

    int targetTrackIndex = response["trackIndex"].toInt(-1);
    if (targetTrackIndex < 0 || targetTrackIndex >= _file->numTracks()) {
        addChatBubble("system", "Invalid target track index.");
        return;
    }

    QList<MidiEvent *> selected = Selection::instance()->selectedEvents();
    if (selected.isEmpty()) {
        addChatBubble("system", "No events selected to move.");
        return;
    }

    MidiTrack *targetTrack = _file->track(targetTrackIndex);
    QString explanation = response["explanation"].toString("MidiPilot move to track");

    _file->protocol()->startNewAction("MidiPilot: " + explanation);

    for (MidiEvent *ev : selected) {
        ev->setTrack(targetTrack, false);
    }

    _file->protocol()->endAction();

    emit requestRepaint();
}

void MidiPilotWidget::applyTempoAction(const QJsonObject &response) {
    if (!_file)
        return;

    int bpm = response["bpm"].toInt(-1);
    if (bpm <= 0 || bpm > 999) {
        addChatBubble("system", "Invalid BPM value (must be 1-999).");
        return;
    }

    int tick = response["tick"].toInt(0);
    if (tick < 0) tick = 0;

    QString explanation = response["explanation"].toString("MidiPilot set tempo");

    _file->protocol()->startNewAction("MidiPilot: " + explanation);

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

    _file->protocol()->endAction();
    _file->calcMaxTime();

    emit requestRepaint();
}

void MidiPilotWidget::applyTimeSignatureAction(const QJsonObject &response) {
    if (!_file)
        return;

    int num = response["numerator"].toInt(-1);
    int denomActual = response["denominator"].toInt(-1);

    if (num <= 0 || num > 32) {
        addChatBubble("system", "Invalid numerator (must be 1-32).");
        return;
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
        addChatBubble("system", "Invalid denominator (must be 1, 2, 4, 8, 16, or 32).");
        return;
    }

    int tick = response["tick"].toInt(0);
    if (tick < 0) tick = 0;

    QString explanation = response["explanation"].toString("MidiPilot set time signature");

    _file->protocol()->startNewAction("MidiPilot: " + explanation);

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

    _file->protocol()->endAction();

    emit requestRepaint();
}

void MidiPilotWidget::applySelectAndEdit(const QJsonObject &response) {
    if (!_file)
        return;

    int trackIndex = response["trackIndex"].toInt(-1);
    int startTick = response["startTick"].toInt(-1);
    int endTick = response["endTick"].toInt(-1);
    QJsonArray events = response["events"].toArray();

    if (trackIndex < 0 || trackIndex >= _file->numTracks()) {
        addChatBubble("system", "Invalid track index for select_and_edit.");
        return;
    }
    if (startTick < 0 || endTick < startTick) {
        addChatBubble("system", "Invalid tick range for select_and_edit.");
        return;
    }
    if (events.isEmpty()) {
        addChatBubble("system", "No events provided for select_and_edit.");
        return;
    }

    MidiTrack *targetTrack = _file->track(trackIndex);
    int channel = targetTrack->assignedChannel();
    if (channel < 0) channel = NewNoteTool::editChannel();

    QString explanation = response["explanation"].toString("MidiPilot select and edit");

    _file->protocol()->startNewAction("MidiPilot: " + explanation);

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

    _file->protocol()->endAction();

    emit requestRepaint();
}

void MidiPilotWidget::applySelectAndDelete(const QJsonObject &response) {
    if (!_file)
        return;

    int trackIndex = response["trackIndex"].toInt(-1);
    int startTick = response["startTick"].toInt(-1);
    int endTick = response["endTick"].toInt(-1);

    if (trackIndex < 0 || trackIndex >= _file->numTracks()) {
        addChatBubble("system", "Invalid track index for select_and_delete.");
        return;
    }
    if (startTick < 0 || endTick < startTick) {
        addChatBubble("system", "Invalid tick range for select_and_delete.");
        return;
    }

    MidiTrack *targetTrack = _file->track(trackIndex);
    QString explanation = response["explanation"].toString("MidiPilot select and delete");

    _file->protocol()->startNewAction("MidiPilot: " + explanation);

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
        addChatBubble("system", "No events found in the specified range.");
    }

    emit requestRepaint();
}
