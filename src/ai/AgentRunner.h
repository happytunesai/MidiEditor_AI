#ifndef AGENTRUNNER_H
#define AGENTRUNNER_H

#include <QObject>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

#include "AgentToolPolicy.h"

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
    enum class TaskType { Composition, Edit, Analysis, Repair };

    struct AgentWorkingState {
        QString goal;
        TaskType taskType = TaskType::Edit;
        QStringList confirmedFacts;
        QString lastToolResult;
        QString activeConstraints;
        QString nextStepHint;
        int repeatedFailureCount = 0;
    };

    explicit AgentRunner(AiClient *client, QObject *parent = nullptr);

    static TaskType classifyTask(const QString &userMessage, const QString &systemPrompt = QString());
    static QString taskTypeName(TaskType taskType);
    static AgentWorkingState initialWorkingState(const QString &userMessage,
                                                 const QString &systemPrompt = QString());
    static void updateWorkingStateFromToolResult(AgentWorkingState &state,
                                                 const QString &toolName,
                                                 const QJsonObject &args,
                                                 const QJsonObject &result);
    static QString stateLayerContent(const AgentWorkingState &state);
    static QJsonArray messagesForNextRequest(const QJsonArray &messages,
                                             const AgentWorkingState &state);

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

    /**
     * \brief Emitted with token usage from each API response during the agent loop.
     */
    void tokenUsageUpdated(int promptTokens, int completionTokens, int totalTokens);

    /**
     * \brief Emitted when the agent self-heals from a transient API error
     * (MALFORMED_FUNCTION_CALL, MAX_TOKENS, empty response, network blip)
     * by injecting a corrective hint and retrying the same step.
     */
    void agentRetrying(int attempt, int maxAttempts, const QString &reason);

private slots:
    void onApiResponse(const QString &content, const QJsonObject &fullResponse);
    void onApiError(const QString &error);

private:
    void sendNextRequest();
    QJsonArray messagesForNextRequest() const;
    void processToolCalls(const QJsonObject &assistantMessage);
    static QString buildStepLabel(const QString &toolName, const QJsonObject &args);
    void cleanup();

    // Retry helpers — classify a raw API error string into a recoverable
    // category and craft a corrective message to feed back to the model.
    enum class RetryKind { None, Malformed, MaxTokens, Empty, Network };
    static RetryKind classifyError(const QString &error);
    static QString hintForRetry(RetryKind kind, const QString &rawError);
    AiClient *_client;
    MidiFile *_file;
    MidiPilotWidget *_widget;

    QJsonArray _messages;
    QJsonArray _tools;
    AgentWorkingState _workingState;

    int _maxSteps;
    int _currentStep;
    bool _running;
    bool _cancelled;

    // Self-healing retry state — reset on every successful API response.
    int _retryCount;
    int _maxRetries;

    // Guard against models repeating the same write tool call indefinitely.
    QString _lastWriteToolSignature;
    int _repeatedWriteToolCalls = 0;

    // Phase 31 — model/task scoped policy, computed once per run() call.
    AgentToolPolicy _policy;
    // Counter of consecutive write-tool calls that produced an "incomplete
    // payload" rejection (e.g. only program_change / cc, no notes). Used by
    // `policy.boundedIncompleteWriteStop` to terminate the run with a clear
    // message after two consecutive incomplete writes.
    int _consecutiveIncompleteWrites = 0;

    QMetaObject::Connection _responseConn;
    QMetaObject::Connection _errorConn;
};

#endif // AGENTRUNNER_H
