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
class MidiFile;
class AiClient;
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
    bool dispatchAction(const QJsonObject &actionObj);
    void applyAiEdits(const QJsonObject &response);
    void applyAiDeletes(const QJsonObject &response);
    void applyTrackAction(const QJsonObject &response);
    void applyMoveToTrack(const QJsonObject &response);
    void applyTempoAction(const QJsonObject &response);
    void applyTimeSignatureAction(const QJsonObject &response);
    void applySelectAndEdit(const QJsonObject &response);
    void applySelectAndDelete(const QJsonObject &response);

    MainWindow *_mainWindow;
    MidiFile *_file;
    AiClient *_client;

    // Context bar
    QLabel *_contextLabel;

    // Chat area
    QWidget *_chatContainer;
    QVBoxLayout *_chatLayout;
    QScrollArea *_chatScroll;

    // Input area
    QTextEdit *_inputField;
    QPushButton *_sendButton;

    // Footer (status, model, settings)
    QLabel *_statusLabel;
    QLabel *_modelLabel;

    // Setup prompt (shown when no API key)
    QWidget *_setupWidget;

    // Conversation
    QJsonArray _conversationHistory;
    QList<ConversationEntry> _entries;
};

#endif // MIDIPILOTWIDGET_H
