#ifndef AICLIENT_H
#define AICLIENT_H

#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSettings>
#include <QHash>
#include <QSet>

/**
 * \class AiClient
 *
 * \brief Handles communication with AI APIs for MidiPilot.
 *
 * AiClient sends chat completion requests to OpenAI-compatible APIs and emits
 * signals when responses are received or errors occur. It manages the
 * conversation history and constructs properly formatted API requests.
 * Supports multiple providers via configurable base URL.
 */
class AiClient : public QObject {
    Q_OBJECT

public:
    explicit AiClient(QObject *parent = nullptr);

    /**
     * \brief Sends a chat completion request to the OpenAI API.
     * \param systemPrompt The system prompt defining the AI's behavior
     * \param conversationHistory Previous messages in the conversation
     * \param userMessage The current user message (appended to history)
     */
    void sendRequest(const QString &systemPrompt,
                     const QJsonArray &conversationHistory,
                     const QString &userMessage);

    /**
     * \brief Sends a raw messages array (with optional tools) to the API.
     * Used by AgentRunner for the tool-calling loop.
     * \param messages Complete messages array (system + user + assistant + tool)
     * \param tools Optional tools array in OpenAI format
     */
    void sendMessages(const QJsonArray &messages,
                      const QJsonArray &tools = QJsonArray());

    /**
     * \brief Checks whether an API key is configured.
     * \return true if an API key is set
     */
    bool isConfigured() const;

    /**
     * \brief Gets the currently configured model name.
     * \return Model identifier string (e.g. "gpt-4o-mini")
     */
    QString model() const;

    /**
     * \brief Sets the model to use for API requests.
     * \param model Model identifier string
     */
    void setModel(const QString &model);

    /**
     * \brief Gets the API key from settings.
     * \return The stored API key (may be empty)
     */
    QString apiKey() const;

    /**
     * \brief Stores the API key in settings.
     * \param key The OpenAI API key
     */
    void setApiKey(const QString &key);

    /**
     * \brief Gets the configured API base URL.
     * \return Base URL string (e.g. "https://api.openai.com/v1")
     */
    QString apiBaseUrl() const;

    /**
     * \brief Sets the API base URL.
     * \param url Base URL (e.g. "https://api.openai.com/v1")
     */
    void setApiBaseUrl(const QString &url);

    /**
     * \brief Gets the configured provider name.
     * \return Provider identifier (e.g. "openai", "gemini", "openrouter")
     */
    QString provider() const;

    /**
     * \brief Sets the provider name.
     * \param provider Provider identifier
     */
    void setProvider(const QString &provider);

    /**
     * \brief Gets whether thinking/reasoning is enabled.
     */
    bool thinkingEnabled() const;

    /**
     * \brief Sets whether thinking/reasoning is enabled.
     */
    void setThinkingEnabled(bool enabled);

    /**
     * \brief Gets the reasoning effort level.
     * \return "low", "medium", or "high"
     */
    QString reasoningEffort() const;

    /**
     * \brief Sets the reasoning effort level.
     * \param effort "low", "medium", or "high"
     */
    void setReasoningEffort(const QString &effort);

    /**
     * \brief Gets whether max output tokens limit is enabled.
     */
    bool maxTokensEnabled() const;

    /**
     * \brief Gets the max output tokens limit value.
     */
    int maxTokensLimit() const;

    /**
     * \brief Reloads all settings from QSettings (after settings dialog closes).
     */
    void reloadSettings();

    /**
     * \brief Checks whether the current model is a reasoning-native model.
     * \return true for o-series and GPT-5.x models
     */
    bool isReasoningModel() const;
    bool isGeminiThinkingModel() const;

    /**
     * \brief Tests the API connection with a simple request.
     */
    void testConnection();

    /**
     * \brief Clears the API debug log file.
     */
    static void clearLog();

    /**
     * \brief Cancels any pending request.
     */
    void cancelRequest();

    /**
     * \brief Returns the context window size for the current or given model.
     * \param model Model name (uses current model if empty)
     * \return Context window in tokens, or 128000 as default
     */
    int contextWindowForModel(const QString &model = QString()) const;

    /**
     * \brief Returns whether a request is currently in progress.
     */
    bool isBusy() const;

    /**
     * \brief Sends a streaming chat completion request. Content arrives incrementally
     *        via the streamDelta signal. Only for Simple mode text responses (no tools).
     * \param systemPrompt The system prompt
     * \param conversationHistory Previous messages
     * \param userMessage Current user message
     */
    void sendStreamingRequest(const QString &systemPrompt,
                              const QJsonArray &conversationHistory,
                              const QString &userMessage);

    /**
     * \brief Streaming variant of sendMessages — used by AgentRunner.
     *
     * Sends a Chat-Completions request with `stream: true` and incrementally
     * parses both text content deltas and `tool_calls[].function.arguments`
     * deltas from the SSE stream. Emits live signals while the response is
     * being composed (\ref streamAssistantTextDelta,
     * \ref streamToolCallStarted, \ref streamToolCallArgsDelta,
     * \ref streamToolCallArgsDone) and finally emits the normal
     * \ref responseReceived signal with a fully reconstructed assistant
     * message — so AgentRunner can stay on the existing onApiResponse path.
     *
      * Uses the dedicated Responses API SSE handler for OpenAI GPT-5 + tools.
      * Falls back to non-streaming \ref sendMessages only when streaming is
      * disabled or auto-blocklisted for the current provider/model combination.
     */
    void sendStreamingMessages(const QJsonArray &messages,
                               const QJsonArray &tools = QJsonArray());

    /**
     * \brief Phase 31: Apply a one-shot per-request override before sending
     *        the next streaming/non-streaming request. Both fields are
     *        consumed (and reset) after the next outgoing request, so this
     *        must be called immediately before each `sendStreamingMessages`
     *        / `sendMessages` call when the override is desired.
     *
     * \param forceSequentialTools When true, set
     *        `parallel_tool_calls: false` on the OpenAI Responses API
     *        body. No effect on other providers/transports.
     * \param reasoningEffortOverride One of `""`, `"low"`, `"medium"`,
     *        `"high"`. When non-empty AND the model is on a path that
     *        sends `reasoning_effort`, this value overrides the user's
     *        configured effort for this single request.
     */
    void setNextRequestPolicyOverride(bool forceSequentialTools,
                                      const QString &reasoningEffortOverride);

    /**
     * \brief Whether agent-loop streaming is enabled in settings.
     * \return true if `AI/streaming_mode` != "off" (default true).
     */
    bool agentStreamingEnabled() const;

    /**
     * \brief True when the current provider/model has been auto-blocklisted
     *        from streaming after a previous failure (HTTP 4xx/5xx or a
        *        silent empty-stream response). Kept in memory for the current
        *        application session so a fresh launch tries streaming once again.
     */
    bool streamingDisabledForCurrentModel() const;

    /**
     * \brief Mode-aware variant. When \a withTools is true the check
     *        targets the Agent-mode (tools-enabled) transport; when false
     *        it targets the Simple-mode (no-tools) transport. The two
     *        modes are blocked independently because OpenAI's gpt-5*-pro
     *        family streams cleanly through one endpoint but not the
     *        other (Phase 31.1).
     */
    bool streamingDisabledForCurrentModel(bool withTools) const;

        /**
        * \brief True when a provider/model was marked as streaming-broken in
        *        this application session (any mode). Kept for UI/legacy use.
        */
        static bool streamingBlockedForSession(const QString &provider,
                                       const QString &model);

        /**
        * \brief Mode-aware overload. \see streamingDisabledForCurrentModel(bool).
        */
        static bool streamingBlockedForSession(const QString &provider,
                                       const QString &model,
                                       bool withTools);

        /**
        * \brief Clears the in-memory streaming block for a provider/model.
        *        Empty provider+model clears all session blocks. Clears
        *        both Simple- and Agent-mode entries when both args are set.
        */
        static void clearStreamingBlockForSession(const QString &provider = QString(),
                                         const QString &model = QString());

    /**
     * \brief Mark the current provider/model as unable to stream so that
     *        future requests skip the streaming code path entirely.
     *        Uses the current request's tools state to scope the block.
     */
    void markStreamingUnsupportedForCurrentModel(const QString &reason);

    /**
     * \brief Clear the auto-streaming-blocklist for one provider/model pair
     *        (or all entries when both are empty). Exposed so a future
     *        Settings UI can offer a "reset" button.
     */
    void clearStreamingBlocklist(const QString &provider = QString(),
                                 const QString &model = QString());

    // === Tool-incapable model flag (Phase 28.2) ===
    //
    // OpenRouter (and other proxy providers) routes some models to upstream
    // backends that do not implement tool/function calling. Those return
    // HTTP 404 "No endpoints found that support tool use". Once observed
    // we remember the (provider, model) pair so that subsequent agent-mode
    // requests can short-circuit with a friendly message instead of
    // round-tripping the API.

    /**
     * \brief True when the current provider/model has been observed not to
     *        support tool calling. Persisted in QSettings under
     *        `AI/incapable_tools/<provider>:<model>`.
     */
    bool toolsIncapableForCurrentModel() const;

    /**
     * \brief Mark the current provider/model as not supporting tool calling.
     */
    void markToolsIncapableForCurrentModel(const QString &reason);

    /**
     * \brief Clear the tools-incapable flag for one provider/model pair (or
     *        all entries when both are empty).
     */
    void clearToolsIncapableFlag(const QString &provider = QString(),
                                 const QString &model = QString());

    /**
     * \brief Heuristic: returns true if `error` looks like the upstream
     *        rejected the request because the model does not support
     *        tool/function calling (i.e. permanent capability gap, NOT
     *        a retriable transient).
     */
    static bool errorIndicatesNoToolSupport(const QString &error);

private:
    /**
     * \brief Internal: streams the agent loop against Google's native
     *        `:streamGenerateContent?alt=sse` endpoint instead of the
     *        OpenAI-compat one. Used because Google's compat endpoint
     *        rejects `stream: true` + `tools` in the same request, and
     *        because the native endpoint also gives us live
     *        thought-summary streaming (`thinkingConfig.includeThoughts`).
     *
     *        Converts OpenAI-shape messages → Gemini `contents`,
     *        OpenAI-shape tools → `tools[0].functionDeclarations`,
     *        parses Gemini SSE chunks live, then reassembles a
     *        synthetic Chat-Completions response and emits the normal
     *        \ref responseReceived signal so AgentRunner stays
     *        unchanged.
     */
    /**
    * \brief Streaming variant for OpenAI's `/v1/responses` endpoint.
    * Used for GPT-5 reasoning models + tools. Parses typed SSE events
     * (`response.output_text.delta`, `response.reasoning_summary_text.delta`,
     * `response.function_call_arguments.delta`, `response.completed`, ...)
     * and forwards thought / text deltas live, then builds a synthetic
     * Chat-Completions response so AgentRunner is unchanged.
     */
    void sendStreamingMessagesResponses(const QJsonArray &messages,
                                        const QJsonArray &tools);

    void sendStreamingMessagesGemini(const QJsonArray &messages,
                                     const QJsonArray &tools);

public:

signals:
    /**
     * \brief Emitted when a successful response is received.
     * \param content The assistant's response text
     * \param fullResponse The complete API response JSON
     */
    void responseReceived(const QString &content, const QJsonObject &fullResponse);

    /**
     * \brief Emitted when an error occurs.
     * \param errorMessage Human-readable error description
     */
    void errorOccurred(const QString &errorMessage);

    /**
     * \brief Emitted when a connection test completes.
     * \param success Whether the test was successful
     * \param message Description of the result
     */
    void connectionTestResult(bool success, const QString &message);

    /**
     * \brief Emitted when a streaming text delta arrives.
     * \param text The incremental text chunk
     */
    void streamDelta(const QString &text);

    /**
     * \brief Emitted from the agent-loop streaming path when an
     *        assistant text-content delta arrives.
     */
    void streamAssistantTextDelta(const QString &text);

    /**
     * \brief Emitted when the model starts emitting a tool call.
     *        Fires once per call when its name first becomes known.
     * \param callId Provider-assigned call id (used to correlate later deltas)
     * \param toolName The tool / function name (e.g. "insert_events")
     */
    void streamToolCallStarted(const QString &callId, const QString &toolName);

    /**
     * \brief Emitted while the model streams the JSON arguments for a tool call.
     *        Multiple fragments per call; reassemble in order.
     */
    void streamToolCallArgsDelta(const QString &callId, const QString &jsonFragment);

    /**
     * \brief Emitted when a tool call's arguments are fully assembled.
     */
    void streamToolCallArgsDone(const QString &callId);

    /**
     * \brief Emitted when a reasoning / "thought" delta arrives.
     *
     * Currently surfaced from the native Gemini streaming path
     * (`thinkingConfig.includeThoughts: true` — chunks carry
     * `parts[*].thought == true` text parts). UI can render this in
     * a collapsed "Thinking…" disclosure separate from the assistant
     * bubble. Empty / no thoughts → signal never fires.
     */
    void streamReasoningDelta(const QString &text);

    /**
     * \brief Emitted when streaming is complete.
     * \param fullContent The accumulated full response text
     * \param fullResponse A synthetic response object with usage info
     */
    void streamFinished(const QString &fullContent, const QJsonObject &fullResponse);

    /**
     * \brief Emitted when a request is being retried after a transient error.
     * \param message Description (e.g. "Retrying... (1/1)")
     */
    void retrying(const QString &message);

private slots:
    void onReplyFinished(QNetworkReply *reply);
    void onStreamDataAvailable();
    /**
     * \brief SSE parser for Google's native
     *        `:streamGenerateContent?alt=sse` endpoint. Different schema
     *        than OpenAI Chat Completions — chunks carry
     *        `candidates[0].content.parts[*]` which can be `text`,
     *        `thought`+`text`, or `functionCall` (whole, not delta).
     */
    void onGeminiStreamDataAvailable();
    /**
     * \brief SSE parser for OpenAI's `/v1/responses` endpoint. Each
     *        event has both an `event:` line and a `data:` JSON payload;
     *        we dispatch by event type to forward live thought/text deltas
     *        and accumulate function-call arguments.
     */
    void onResponsesStreamDataAvailable();

private:
    QNetworkAccessManager *_manager;
    QNetworkReply *_currentReply;
    QSettings _settings;
    QString _model;
    QString _apiBaseUrl;
    QString _provider;
    bool _isTestRequest;
    bool _useResponsesApi;
    bool _thinkingEnabled;
    QString _reasoningEffort;
    bool _maxTokensEnabled;
    int _maxTokensLimit;
    bool _hasToolsInRequest;
    bool _isStreaming;
    bool _streamHasTools;
    QByteArray _streamBuffer;
    QString _streamContent;
    // Accumulated reasoning/thought stream from the current SSE response.
    // Logged once at stream completion so the API log mirrors what the UI
    // showed live (chat shows it, but it's gone after the message renders).
    QString _streamReasoning;
    // Per-tool-call accumulators for the agent-loop streaming path.
    // Keyed by SSE chunk index (Chat Completions uses delta.tool_calls[i].index)
    // and resolved to provider call_id once the first chunk for that index
    // arrives.
    struct StreamToolCall {
        QString id;          // provider-assigned id (resolved on first chunk)
        QString name;        // function name (resolved on first chunk)
        QString arguments;   // accumulated JSON fragments
        bool started;        // true once name+id were emitted via signal
        // Gemini 3.x emits a `thoughtSignature` on each functionCall part.
        // Follow-up requests MUST echo the signature back in the `parts`
        // array of the assistant turn, or the API rejects with HTTP 400.
        // Stored per tool call so it can be plumbed through tool_calls[]
        // and re-serialised in buildGeminiContents().
        QString thoughtSignature;
    };
    QHash<int, StreamToolCall> _streamToolCalls;
    // Per-output-index map for the OpenAI Responses-API streaming path.
    // The `output_index` field on each event identifies which item in the
    // response's `output[]` array a delta belongs to. Function-call items
    // arrive as a sequence of `function_call_arguments.delta` events; once
    // `output_item.done` fires we have the full call_id + name + arguments.
    QHash<int, StreamToolCall> _responsesStreamItems;
    QJsonObject _responsesStreamUsage;
    // Synthetic call-id counter for the native Gemini streaming path.
    // Gemini's streamGenerateContent does not assign call ids — it just
    // emits whole `functionCall` parts — so we mint our own as
    // "gemini_<n>" so AgentRunner can correlate tool result messages.
    int _streamGeminiCallCounter;
    // Diagnostics for the native Gemini streaming path: count of thought-text
    // characters seen (not added to _streamContent) and last finishReason
    // value. Used to log empty-stream diagnostics and to surface
    // MALFORMED_FUNCTION_CALL / MAX_TOKENS / SAFETY as proper errors.
    int _streamGeminiThoughtChars = 0;
    QString _streamGeminiFinishReason;
    int _retryCount;
    QByteArray _lastRequestData;
    QNetworkRequest _lastRequest;

    // === Streaming-fallback retry context ===
    // Saved at the entry of every streaming send so that we can transparently
    // retry the request via the non-streaming code path if the stream fails
    // (HTTP error or empty 200) for a model that doesn't actually support
    // SSE. Populated by sendStreamingMessages / sendStreamingRequest, cleared
    // once the request completes successfully or the retry has been issued.
    bool _streamRetryArmed = false;
    bool _streamRetrySimpleMode = false; // true → retry via sendRequest;
                                          // false → retry via sendMessages
    QJsonArray _streamRetryMessages;
    QJsonArray _streamRetryTools;
    QString _streamRetrySystemPrompt;
    QJsonArray _streamRetryHistory;
    QString _streamRetryUserMessage;
    QString _streamRetryProvider;
    QString _streamRetryModel;
    QString _streamRetryApiBaseUrl;

    // === Phase 31 — one-shot per-request policy overrides ===
    // Populated by AgentRunner via setNextRequestPolicyOverride() right
    // before each outbound request. Consumed and cleared inside
    // sendMessagesInternal(). Streaming-fallback retries replay the same
    // override (re-armed by setNextRequestPolicyOverride callers as needed).
    bool _nextForceSequentialTools = false;
    QString _nextReasoningEffortOverride;

    // Internal helpers for the streaming-fallback path.
    QString streamingBlocklistKey(const QString &provider, const QString &model) const;
    void markStreamingUnsupported(const QString &provider,
                                  const QString &model,
                                  const QString &reason);
    void markStreamingUnsupported(const QString &provider,
                                  const QString &model,
                                  bool withTools,
                                  const QString &reason);
    void sendMessagesInternal(const QJsonArray &messages,
                              const QJsonArray &tools,
                              bool allowGeminiNativeToolsPath);
    void sendMessagesWithRequestSnapshot(const QJsonArray &messages,
                                         const QJsonArray &tools,
                                         const QString &provider,
                                         const QString &model,
                                         const QString &apiBaseUrl,
                                         bool allowGeminiNativeToolsPath);
    void armStreamingRetryAgent(const QJsonArray &messages, const QJsonArray &tools);
    void armStreamingRetrySimple(const QString &systemPrompt,
                                  const QJsonArray &history,
                                  const QString &userMessage);
    void clearStreamingRetryContext();
    // Returns true when the just-finished stream looks broken enough to
    // justify a one-shot non-streaming retry. `httpStatus` is 0 when the
    // reply finished cleanly, otherwise the HTTP status code from the reply.
    bool shouldFallbackToNonStreaming(int httpStatus,
                                      QNetworkReply::NetworkError netError,
                                      bool gotContent,
                                      bool gotToolCalls) const;
    // Issues the non-streaming retry using the previously-saved context.
    // Emits a `retrying(...)` signal first. Returns true when a retry was
    // dispatched.
    bool tryStreamingFallback(const QString &reason);

    static const QString API_URL;
    static const QString RESPONSES_API_URL;
    static const QString DEFAULT_MODEL;
    static const QString DEFAULT_API_BASE_URL;
    static const QString SETTINGS_KEY_API_KEY;
    static const QString SETTINGS_KEY_MODEL;
    static const QString SETTINGS_KEY_THINKING;
    static const QString SETTINGS_KEY_REASONING_EFFORT;
    static const QString SETTINGS_KEY_API_BASE_URL;
    static const QString SETTINGS_KEY_PROVIDER;
};

#endif // AICLIENT_H
