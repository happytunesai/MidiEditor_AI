#ifndef MIDIPILOTWIDGET_H
#define MIDIPILOTWIDGET_H

#include <QWidget>
#include <QJsonArray>
#include <QJsonObject>
#include <QDateTime>

class QTimer;
class QCheckBox;

class QVBoxLayout;
class QHBoxLayout;
class QFrame;
class QLabel;
class QTextEdit;
class QPushButton;
class QScrollArea;
class QComboBox;
class MidiFile;
class AiClient;
class AgentRunner;
class PromptProfileStore;
class MainWindow;

/**
 * \class MidiPilotWidget
 *
 * \brief AI assistant sidebar panel for MidiEditor.
 *
 * Provides a chat-based interface for AI-assisted MIDI editing.
 * Three sections: status header, context bar, chat area.
 */
class MidiPilotWidget : public QWidget {
    Q_OBJECT

public:
    explicit MidiPilotWidget(MainWindow *mainWindow, QWidget *parent = nullptr);

    /**
     * \brief Updates the context bar with current editor state.
     */
    void refreshContext();

    /**
     * \brief Sets focus to the input field.
     */
    void focusInput();
    /**
     * \\brief Returns the current mode: \"simple\" or \"agent\".
     */
    QString currentMode() const;

    /**
     * \brief Returns whether FFXIV Bard Performance mode is enabled.
     */
    bool ffxivMode() const;

    /**
     * \brief Executes an action silently (no chat bubbles). Used by Agent Mode tool calls.
     */
    QJsonObject executeAction(const QJsonObject &actionObj);

private:
    void updateTokenLabel();

public slots:
    /**
     * \brief Called when a new file is loaded or file changes.
     */
    void onFileChanged(MidiFile *f);

    /**
     * \brief Called when application settings have changed (e.g. API key updated).
     */
    void onSettingsChanged();

    /**
     * \brief Clears the conversation history and chat display.
     */
    void onNewChat();

signals:
    /**
     * \brief Emitted when the matrix widget should be repainted.
     */
    void requestRepaint();


private slots:
    void onSendMessage();
    void onResponseReceived(const QString &content, const QJsonObject &fullResponse);
    void onErrorOccurred(const QString &errorMessage);
    void onSettingsClicked();
    void onModeChanged(int index);
    void onAgentStepStarted(int step, const QString &toolName);
    void onAgentStepCompleted(int step, const QString &toolName, const QJsonObject &result);
    void onAgentStepsPlanned(int firstStep, const QStringList &toolNames);
    void onAgentFinished(const QString &finalMessage);
    void onAgentError(const QString &error);
    void onAgentStepLimitReached(int currentStep, int maxSteps);
    void onModelComboChanged(int index);
    void onProviderComboChanged(int index);
    void onEffortComboChanged(int index);
    void onStreamDelta(const QString &text);
    void onStreamFinished(const QString &fullContent, const QJsonObject &fullResponse);
    void onRefreshModels();
    void onModelsFetched(const QString &provider, const QJsonArray &models);
    void onModelsFetchFailed(const QString &provider, const QString &error);

private:
    struct ConversationEntry {
        QString role;          // "user" or "assistant"
        QString message;
        QJsonObject context;
        QDateTime timestamp;
    };

    void setupUi();
    void setupSetupPrompt();
    void populateFooterModels();
    void addChatBubble(const QString &role, const QString &text);
    void setStatus(const QString &text, const QString &color);
    QJsonObject dispatchAction(const QJsonObject &actionObj, bool showBubbles = true);
    QJsonObject applyAiEdits(const QJsonObject &response, bool showBubbles = true);
    QJsonObject applyAiDeletes(const QJsonObject &response, bool showBubbles = true);
    QJsonObject applyTrackAction(const QJsonObject &response, bool showBubbles = true);
    QJsonObject applyMoveToTrack(const QJsonObject &response, bool showBubbles = true);
    QJsonObject applyTempoAction(const QJsonObject &response, bool showBubbles = true);
    QJsonObject applyTimeSignatureAction(const QJsonObject &response, bool showBubbles = true);
    QJsonObject applySelectAndEdit(const QJsonObject &response, bool showBubbles = true);
    QJsonObject applySelectAndDelete(const QJsonObject &response, bool showBubbles = true);

    MainWindow *_mainWindow;
    MidiFile *_file;
    AiClient *_client;
    AgentRunner *_agentRunner;
    bool _isAgentRunning;

    // Agent steps UI
    QWidget *_agentStepsWidget;  // Actually AgentStepsWidget*, stored as QWidget* to avoid header dep
    QWidget *_agentDockArea;     // Anchored container below the chat scroll, holds the steps widget

    // Streaming bubble for incremental display
    QLabel *_streamBubble;
    bool _streamIsJson;  // true = response is JSON action; render in subdued monospace preview style

    // Simple-mode self-healing retry state \u2014 stashed so we can replay
    // the same request after a transient failure (network blip, empty
    // stream, MAX_TOKENS, etc.). Reset on successful response.
    QString _lastSimpleSystemPrompt;
    QJsonArray _lastSimpleHistory;
    QString _lastSimpleMessage;
    int _simpleRetryCount;
    int _simpleMaxRetries;

    // Live reasoning / "thought" display. Rendered as plain gray italic
    // text inline in the chat (not a speech bubble). Lazy-created on first
    // streamReasoningDelta and reset to nullptr on each new send so a fresh
    // label is allocated per request — but the previous one stays in the
    // chat history.
    QLabel *_thoughtLabel;
    QString _thoughtBaseText;        // accumulated text without trailing cursor
    QTimer *_thoughtCursorTimer;     // blinks the trailing cursor while thinking
    bool _thoughtCursorOn;           // current visibility of the cursor glyph
    int _thoughtCursorFrame;         // current spinner frame index

    // Context bar
    QLabel *_contextLabel;

    // Chat area
    QWidget *_chatContainer;
    QVBoxLayout *_chatLayout;
    QScrollArea *_chatScroll;

    // Input area
    QTextEdit *_inputField;
    QPushButton *_sendButton;
    QPushButton *_stopButton;
    QComboBox *_modeCombo;
    QLabel *_tokenLabel;

    // Footer (status, model, settings)
    QFrame *_statusBar;
    QLabel *_statusLabel;
    QLabel *_statusDots;
    QTimer *_statusTimer;
    int _dotPhase;
    int _msgPhase;
    QComboBox *_providerCombo;
    QComboBox *_modelCombo;
    QPushButton *_refreshModelsButton = nullptr;
    QComboBox *_effortCombo;
    QCheckBox *_ffxivCheck;

    // Setup prompt (shown when no API key)
    QWidget *_setupWidget;

    // Conversation
    QJsonArray _conversationHistory;
    QList<ConversationEntry> _entries;

    // Token tracking
    int _lastPromptTokens;
    int _lastCompletionTokens;
    int _totalPromptTokens;
    int _totalCompletionTokens;

    // Persistent conversation history
    QString _conversationId;
    QTimer *_saveTimer;
    // Per-turn metadata accumulator. One entry is appended to _turns when
    // the assistant turn completes (agent finished/error or simple-mode
    // response received) and persisted as part of the conversation JSON
    // so the live thoughts, steps, latency, effort and provider/model used
    // for that turn survive a reload.
    QJsonArray _turns;
    QString _turnReasoning;
    QJsonArray _turnSteps;
    qint64 _turnStartMs;
    bool _turnStreamed;
    QString _turnEffort;
    QString _turnProvider;
    QString _turnModel;
    void resetTurnState();
    void finalizeTurn(const QString &finalText, const QString &status);
    void scheduleSave();
    void doSaveConversation();
    void showHistoryMenu();
    void loadConversation(const QString &id);
    QJsonArray truncateHistory(const QJsonArray &history, int contextWindow,
                               int systemPromptChars = 0) const;
    void loadPresetForFile(const QString &midiPath);
    void savePresetForFile();
    QString _customFileInstructions;  // Per-file custom instructions from preset

    // Phase 29: Per-model system prompt profiles. Resolves <provider:model>
    // → optional override / append to the default agent or simple prompt.
    // Owned by the widget; the dialog opens a temporary view on the same
    // QSettings-backed store.
    PromptProfileStore *_profileStore = nullptr;
};

#endif // MIDIPILOTWIDGET_H
