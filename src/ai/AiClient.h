#ifndef AICLIENT_H
#define AICLIENT_H

#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSettings>

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
     * \brief Emitted when streaming is complete.
     * \param fullContent The accumulated full response text
     * \param fullResponse A synthetic response object with usage info
     */
    void streamFinished(const QString &fullContent, const QJsonObject &fullResponse);

private slots:
    void onReplyFinished(QNetworkReply *reply);
    void onStreamDataAvailable();

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
    QByteArray _streamBuffer;
    QString _streamContent;

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
