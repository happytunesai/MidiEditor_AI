#ifndef AGENTRUNNER_H
#define AGENTRUNNER_H

#include <QObject>
#include <QJsonArray>
#include <QJsonObject>

class AiClient;
class MidiFile;
class MidiPilotWidget;

/**
 * \class AgentRunner
 *
 * \brief Manages the Agent Mode tool-calling loop for MidiPilot.
 *
 * When Agent Mode is active, AgentRunner replaces the single-shot
 * request/response flow with an iterative loop where the LLM can
 * call tools (get_editor_state, insert_events, etc.) and receive
 * results before producing a final text response.
 */
class AgentRunner : public QObject {
    Q_OBJECT

public:
    explicit AgentRunner(AiClient *client, QObject *parent = nullptr);

    /**
     * \brief Starts the agent loop.
     * \param systemPrompt The agent system prompt
     * \param conversationHistory Previous conversation messages
     * \param userMessage The current user request
     * \param file The current MIDI file
     * \param widget The MidiPilotWidget for tool execution
     */
    void run(const QString &systemPrompt,
             const QJsonArray &conversationHistory,
             const QString &userMessage,
             MidiFile *file,
             MidiPilotWidget *widget);

    void cancel();
    bool isRunning() const;

    /**
     * \brief Extends the step limit and continues the agent loop.
     * Called when the user chooses to continue after hitting the limit.
     */
    void continueRunning(int additionalSteps);

    /**
     * \brief Stops the agent at the current step limit (user chose not to continue).
     */
    void stopAtLimit();

signals:
    /**
     * \brief Emitted with all tool names from a batch before any are executed.
     */
    void stepsPlanned(int firstStep, const QStringList &toolNames);

    /**
     * \brief Emitted when beginning to execute a tool call.
     */
    void stepStarted(int stepNumber, const QString &toolName);

    /**
     * \brief Emitted after a tool call finishes.
     */
    void stepCompleted(int stepNumber, const QString &toolName, const QJsonObject &result);

    /**
     * \brief Emitted when the agent produces its final text response.
     */
    void finished(const QString &finalMessage);

    /**
     * \brief Emitted when the step limit is reached, allowing the user to continue or stop.
     */
    void stepLimitReached(int currentStep, int maxSteps);

    /**
     * \brief Emitted on error (API or tool execution).
     */
    void errorOccurred(const QString &error);

private slots:
    void onApiResponse(const QString &content, const QJsonObject &fullResponse);
    void onApiError(const QString &error);

private:
    void sendNextRequest();
    void processToolCalls(const QJsonObject &assistantMessage);
    static QString buildStepLabel(const QString &toolName, const QJsonObject &args);
    void cleanup();

    AiClient *_client;
    MidiFile *_file;
    MidiPilotWidget *_widget;

    QJsonArray _messages;
    QJsonArray _tools;

    int _maxSteps;
    int _currentStep;
    bool _running;
    bool _cancelled;

    QMetaObject::Connection _responseConn;
    QMetaObject::Connection _errorConn;
};

#endif // AGENTRUNNER_H
