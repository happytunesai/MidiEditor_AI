#ifndef MIDIPILOTWIDGET_H
#define MIDIPILOTWIDGET_H

#include <QWidget>
#include <QJsonArray>
#include <QJsonObject>
#include <QDateTime>

class QVBoxLayout;
class QHBoxLayout;
class QLabel;
class QTextEdit;
class QPushButton;
class QScrollArea;
class QComboBox;
class MidiFile;
class AiClient;
class AgentRunner;
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
     * \brief Executes an action silently (no chat bubbles). Used by Agent Mode tool calls.
     */
    QJsonObject executeAction(const QJsonObject &actionObj);
public slots:
    /**
     * \brief Called when a new file is loaded or file changes.
     */
    void onFileChanged(MidiFile *f);

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
    void onModelComboChanged(int index);
    void onEffortComboChanged(int index);

private:
    struct ConversationEntry {
        QString role;          // "user" or "assistant"
        QString message;
        QJsonObject context;
        QDateTime timestamp;
    };

    void setupUi();
    void setupSetupPrompt();
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

    // Footer (status, model, settings)
    QLabel *_statusLabel;
    QComboBox *_modelCombo;
    QComboBox *_effortCombo;

    // Setup prompt (shown when no API key)
    QWidget *_setupWidget;

    // Conversation
    QJsonArray _conversationHistory;
    QList<ConversationEntry> _entries;
};

#endif // MIDIPILOTWIDGET_H
