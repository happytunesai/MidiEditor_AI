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

    /**
     * \brief Phase 47 — lets the caller strip the `pitch_bend` branch from
     *        the tool schema for the upcoming run.
     *
     * Set from the active \ref PromptProfile's \c disallowPitchBend flag.
     * AgentRunner deliberately does not know PromptProfileStore: it derives
     * its policy from model + provider only, so the profile decision has to
     * be handed in from outside (MidiPilotWidget does it right before every
     * \ref run call).
     *
     * The flag is AND-ed into the model policy: it can only turn pitch_bend
     * OFF, never back on, so gpt-5.5*'s existing schema-light behaviour is
     * unaffected. It is NOT reset by \ref run — the caller sets it before
     * every run, which is what keeps a profile change effective from the
     * next message on.
     */
    void setProfileDisallowsPitchBend(bool disallow);

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

    /**
     * \brief v2.3.1 cross-tab: appends " [in <tab title>]" to a step label
     *        while the run is bound to a document OTHER than the one it was
     *        started on, so the steps dock says where each edit (and its undo
     *        step) landed. Empty-suffix (= no-op) on the origin document.
     */
    QString decorateStepLabel(const QString &label) const;

    /**
     * \brief v2.3.1 cross-tab: handles a `switch_document` tool call BEFORE
     *        generic dispatch (mirroring the MCP server's pre-dispatch
     *        intercept) and re-binds the run ATOMICALLY - `_file`, the
     *        widget-side closed-mid-run guard (`_runOriginFile`) and the
     *        chat-visible announcement all move in one synchronous step on
     *        the main thread, so there is no stale-bind window. Deliberately
     *        does NOT activate the tab in the UI: MidiPilot's chat stays
     *        visible on the current tab (the MCP variant keeps activating
     *        the tab - that difference is by design and documented).
     *        Selection context needs no explicit move: every tool call
     *        resolves Selection::forFile()/EditorContext from the per-call
     *        file, which is `_file`.
     *
     *        An invalid/closed index returns a structured error result and
     *        the run continues on its current document.
     */
    QJsonObject interceptSwitchDocument(const QJsonObject &args);

    void cleanup();

    /**
     * \brief Re-derives \c _tools from the current policy and settings.
     *
     * The single derivation site for the request tool list: run() calls it at
     * the start, \ref applyFfxivModeChange calls it again mid-run. Both go
     * through the same \c ToolSchemaOptions and the same live
     * \c AI/ffxiv_mode read that gates the FFXIV bundle.
     */
    void rebuildToolSchemas();

    /**
     * \brief Makes a mid-run \c set_ffxiv_mode call effective for the rest of
     *        the SAME run.
     *
     * Both halves of FFXIV mode used to be frozen at run start: the tool list
     * (built once in run()) and the FFXIV rules (composed into the system
     * prompt by the caller). A model that turned the mode on therefore never
     * saw the five gated tools nor the rules, although the panel checkbox and
     * MCP clients had already followed along. This re-derives the tool list and
     * carries the rules in as one runner-owned system-side overlay message,
     * which is dropped again when the mode is switched back off.
     */
    void applyFfxivModeChange(bool enabled);

public:
    // Retry helpers — classify a raw API error string into a recoverable
    // category and craft a corrective message to feed back to the model.
    // Pure and free of network/editor state; public so the classification can
    // be unit-tested directly (tests/test_agent_runner_state.cpp).
    enum class RetryKind { None, Malformed, MaxTokens, Empty, Network, ToolCallCutOff };
    static RetryKind classifyError(const QString &error);
    static QString hintForRetry(RetryKind kind, const QString &rawError);

private:
    AiClient *_client;
    MidiFile *_file;
    MidiPilotWidget *_widget;

    // v2.3.1 cross-tab: the document the run was STARTED on. `_file` is the
    // run's CURRENT bind and moves with switch_document; this one never moves
    // and anchors the "is the run away from home?" question behind
    // decorateStepLabel(). Used for IDENTITY COMPARISON ONLY - after the
    // origin tab is closed mid-run (which, post-switch, no longer aborts the
    // run) the pointer dangles and must never be dereferenced; a comparison
    // against a reused address could at worst drop the cosmetic label suffix.
    MidiFile *_originFile = nullptr;
    // Tab title of the CURRENT bind while it differs from the origin document,
    // empty while the run is on its origin. Feeds decorateStepLabel().
    QString _boundDocTitle;

    QJsonArray _messages;
    QJsonArray _tools;
    AgentWorkingState _workingState;

    int _maxSteps;
    int _currentStep;
    bool _running;
    bool _cancelled;
    // Monotonic id of the current run. Bumped by run() and cleanup() so a
    // queued retry timer can tell whether the run it belongs to is still the
    // live one (see onApiError's backoff).
    quint64 _runGeneration = 0;

    // Self-healing retry state — reset on every successful API response.
    int _retryCount;
    int _maxRetries;

    // Guard against models repeating the same write tool call indefinitely.
    QString _lastWriteToolSignature;
    int _repeatedWriteToolCalls = 0;

    // Phase 31 — model/task scoped policy, computed once per run() call.
    AgentToolPolicy _policy;
    // Phase 47 — profile-level "no pitch_bend" opt-in, supplied by the caller
    // via setProfileDisallowsPitchBend() before each run() and AND-ed into
    // `_policy.allowPitchBendEvents`.
    bool _profileDisallowsPitchBend = false;
    // Counter of consecutive write-tool calls that produced an "incomplete
    // payload" rejection (e.g. only program_change / cc, no notes). Used by
    // `policy.boundedIncompleteWriteStop` to terminate the run with a clear
    // message after two consecutive incomplete writes.
    int _consecutiveIncompleteWrites = 0;

    // Phase 46 follow-up — mid-run FFXIV-mode switching (see
    // applyFfxivModeChange). All four are reset by every run().
    /// FFXIV mode as it currently stands for this run. Starts from the setting
    /// and is updated by an effective set_ffxiv_mode call, so a redundant call
    /// ("enable" while already enabled) adds no message.
    bool _ffxivModeActiveInRun = false;
    /// True when the caller's system prompt already carries the FFXIV rules
    /// (the run started in FFXIV mode), so the overlay must not repeat them.
    bool _ffxivRulesInBasePrompt = false;
    /// Index of the runner-owned FFXIV overlay message in \c _messages, or -1.
    int _ffxivOverlayIndex = -1;
    /// Mode change requested by a tool call in the batch being processed:
    /// -1 = none, 0 = off, 1 = on. Applied only after the whole batch's tool
    /// results are appended.
    int _pendingFfxivMode = -1;

    QMetaObject::Connection _responseConn;
    QMetaObject::Connection _errorConn;
};

#endif // AGENTRUNNER_H
