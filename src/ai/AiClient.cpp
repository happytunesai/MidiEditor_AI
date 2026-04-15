#include "AiClient.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

const QString AiClient::API_URL = QStringLiteral("https://api.openai.com/v1/chat/completions");
const QString AiClient::RESPONSES_API_URL = QStringLiteral("https://api.openai.com/v1/responses");
const QString AiClient::DEFAULT_MODEL = QStringLiteral("gpt-5.4");
const QString AiClient::DEFAULT_API_BASE_URL = QStringLiteral("https://api.openai.com/v1");
const QString AiClient::SETTINGS_KEY_API_KEY = QStringLiteral("AI/api_key");
const QString AiClient::SETTINGS_KEY_MODEL = QStringLiteral("AI/model");
const QString AiClient::SETTINGS_KEY_THINKING = QStringLiteral("AI/thinking_enabled");
const QString AiClient::SETTINGS_KEY_REASONING_EFFORT = QStringLiteral("AI/reasoning_effort");
const QString AiClient::SETTINGS_KEY_API_BASE_URL = QStringLiteral("AI/api_base_url");
const QString AiClient::SETTINGS_KEY_PROVIDER = QStringLiteral("AI/provider");

AiClient::AiClient(QObject *parent)
    : QObject(parent),
      _manager(new QNetworkAccessManager(this)),
      _currentReply(nullptr),
      _settings(QStringLiteral("MidiEditor"), QStringLiteral("NONE")),
      _isTestRequest(false),
      _useResponsesApi(false),
      _thinkingEnabled(false),
      _reasoningEffort(QStringLiteral("medium")),
      _maxTokensEnabled(false),
      _maxTokensLimit(16384),
      _hasToolsInRequest(false),
      _isStreaming(false),
      _retryCount(0)
{
    _model = _settings.value(SETTINGS_KEY_MODEL, DEFAULT_MODEL).toString();
    _thinkingEnabled = _settings.value(SETTINGS_KEY_THINKING, true).toBool();
    _reasoningEffort = _settings.value(SETTINGS_KEY_REASONING_EFFORT, QStringLiteral("medium")).toString();
    _apiBaseUrl = _settings.value(SETTINGS_KEY_API_BASE_URL, DEFAULT_API_BASE_URL).toString();
    _provider = _settings.value(SETTINGS_KEY_PROVIDER, QStringLiteral("openai")).toString();
    _maxTokensEnabled = _settings.value(QStringLiteral("AI/max_token_enabled"), false).toBool();
    _maxTokensLimit = _settings.value(QStringLiteral("AI/max_token_limit"), 16384).toInt();
    connect(_manager, &QNetworkAccessManager::finished, this, &AiClient::onReplyFinished);
}

static QString logFilePath()
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/midipilot_api.log");
}

static void logApi(const QString &entry)
{
    QFile f(logFilePath());
    if (f.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream s(&f);
        s << QDateTime::currentDateTime().toString(Qt::ISODate) << " " << entry << "\n";
    }
}

void AiClient::clearLog()
{
    QFile f(logFilePath());
    if (f.exists()) {
        f.resize(0);
    }
}

bool AiClient::isConfigured() const
{
    return !apiKey().isEmpty();
}

QString AiClient::model() const
{
    return _model;
}

void AiClient::setModel(const QString &model)
{
    _model = model;
    _settings.setValue(SETTINGS_KEY_MODEL, model);
}

QString AiClient::apiKey() const
{
    return _settings.value(SETTINGS_KEY_API_KEY).toString();
}

void AiClient::setApiKey(const QString &key)
{
    _settings.setValue(SETTINGS_KEY_API_KEY, key);
}

QString AiClient::apiBaseUrl() const
{
    return _apiBaseUrl;
}

void AiClient::setApiBaseUrl(const QString &url)
{
    _apiBaseUrl = url;
    _settings.setValue(SETTINGS_KEY_API_BASE_URL, url);
}

QString AiClient::provider() const
{
    return _provider;
}

void AiClient::setProvider(const QString &provider)
{
    _provider = provider;
    _settings.setValue(SETTINGS_KEY_PROVIDER, provider);
}

bool AiClient::thinkingEnabled() const
{
    return _thinkingEnabled;
}

void AiClient::setThinkingEnabled(bool enabled)
{
    _thinkingEnabled = enabled;
    _settings.setValue(SETTINGS_KEY_THINKING, enabled);
}

QString AiClient::reasoningEffort() const
{
    return _reasoningEffort;
}

void AiClient::setReasoningEffort(const QString &effort)
{
    _reasoningEffort = effort;
    _settings.setValue(SETTINGS_KEY_REASONING_EFFORT, effort);
}

void AiClient::reloadSettings()
{
    _model = _settings.value(SETTINGS_KEY_MODEL, DEFAULT_MODEL).toString();
    _thinkingEnabled = _settings.value(SETTINGS_KEY_THINKING, true).toBool();
    _reasoningEffort = _settings.value(SETTINGS_KEY_REASONING_EFFORT, QStringLiteral("medium")).toString();
    _apiBaseUrl = _settings.value(SETTINGS_KEY_API_BASE_URL, DEFAULT_API_BASE_URL).toString();
    _provider = _settings.value(SETTINGS_KEY_PROVIDER, QStringLiteral("openai")).toString();
    _maxTokensEnabled = _settings.value(QStringLiteral("AI/max_token_enabled"), false).toBool();
    _maxTokensLimit = _settings.value(QStringLiteral("AI/max_token_limit"), 16384).toInt();
}

bool AiClient::maxTokensEnabled() const
{
    return _maxTokensEnabled;
}

int AiClient::maxTokensLimit() const
{
    return _maxTokensLimit;
}

bool AiClient::isReasoningModel() const
{
    // Models that are inherently reasoning-capable and don't support temperature.
    // They require "developer" role instead of "system".
    QString m = _model.toLower();
    return m.startsWith(QStringLiteral("o1"))
        || m.startsWith(QStringLiteral("o3"))
        || m.startsWith(QStringLiteral("o4"))
        || m.startsWith(QStringLiteral("gpt-5"));
}

bool AiClient::isGeminiThinkingModel() const
{
    // Gemini 2.5+ and 3.x models are "thinking" models that support
    // reasoning_effort but NOT temperature.  Sending temperature causes
    // the model to use its default (high) thinking level, leading to very
    // slow responses.  We detect these so we can send reasoning_effort: "low"
    // to keep responses fast.
    if (_provider != QStringLiteral("gemini"))
        return false;
    QString m = _model.toLower();
    return m.startsWith(QStringLiteral("gemini-2.5"))
        || m.startsWith(QStringLiteral("gemini-3"));
}

bool AiClient::isBusy() const
{
    return _currentReply != nullptr;
}

void AiClient::cancelRequest()
{
    if (_currentReply) {
        _currentReply->disconnect(this);
        _currentReply->abort();
        _currentReply = nullptr;
    }
}

int AiClient::contextWindowForModel(const QString &model) const
{
    QString m = (model.isEmpty() ? _model : model).toLower();

    // Match by prefix — handles version variants like gpt-4o-2024-08-06
    if (m.startsWith(QStringLiteral("gpt-5")))       return 1000000;
    if (m.startsWith(QStringLiteral("gpt-4o")))      return 128000;
    if (m.startsWith(QStringLiteral("gpt-4.1")))     return 1000000;
    if (m.startsWith(QStringLiteral("o4-mini")))      return 200000;
    if (m.startsWith(QStringLiteral("o3")))           return 200000;
    if (m.startsWith(QStringLiteral("o1")))           return 200000;
    if (m.startsWith(QStringLiteral("claude-4")))     return 200000;
    if (m.startsWith(QStringLiteral("claude-3")))     return 200000;
    if (m.startsWith(QStringLiteral("gemini-2.5")))   return 1000000;
    if (m.startsWith(QStringLiteral("gemini-2.0")))   return 1000000;
    if (m.startsWith(QStringLiteral("gemini-1.5")))   return 1000000;

    return 128000; // safe default
}

void AiClient::sendRequest(const QString &systemPrompt,
                            const QJsonArray &conversationHistory,
                            const QString &userMessage)
{
    // Build messages array
    QJsonArray messages;

    // System prompt: reasoning models require "developer" role, standard models use "system".
    // Gemini thinking models still use "system" per Google's docs.
    bool reasoning = isReasoningModel();
    QJsonObject systemMsg;
    systemMsg[QStringLiteral("role")] = (reasoning && _provider != QStringLiteral("gemini"))
        ? QStringLiteral("developer")
        : QStringLiteral("system");
    systemMsg[QStringLiteral("content")] = systemPrompt;
    messages.append(systemMsg);

    // Conversation history
    for (const QJsonValue &msg : conversationHistory) {
        messages.append(msg);
    }

    // Current user message
    QJsonObject userMsg;
    userMsg[QStringLiteral("role")] = QStringLiteral("user");
    userMsg[QStringLiteral("content")] = userMessage;
    messages.append(userMsg);

    sendMessages(messages);
}

void AiClient::sendMessages(const QJsonArray &messages, const QJsonArray &tools)
{
    if (!isConfigured()) {
        emit errorOccurred(tr("No API key configured. Please set your API key in Settings."));
        return;
    }

    if (_currentReply) {
        emit errorOccurred(tr("A request is already in progress."));
        return;
    }

    _isTestRequest = false;
    _hasToolsInRequest = !tools.isEmpty();
    _retryCount = 0;

    bool reasoning = isReasoningModel();
    bool geminiThinking = isGeminiThinkingModel();

    // gpt-5.4 does not support reasoning_effort + tools on /v1/chat/completions;
    // use /v1/responses for that combination (OpenAI only).
    _useResponsesApi = !tools.isEmpty()
                       && _model.toLower().startsWith(QStringLiteral("gpt-5.4"))
                       && (_provider.isEmpty() || _provider == QStringLiteral("openai"));

    QJsonObject body;
    body[QStringLiteral("model")] = _model;

    if (_useResponsesApi) {
        // --- Responses API format ---

        // Convert messages to Responses API input array
        QJsonArray input;
        for (const QJsonValue &msgVal : messages) {
            QJsonObject m = msgVal.toObject();
            QString role = m[QStringLiteral("role")].toString();

            if (role == QStringLiteral("system")) {
                QJsonObject item;
                item[QStringLiteral("role")] = QStringLiteral("developer");
                item[QStringLiteral("content")] = m[QStringLiteral("content")];
                input.append(item);
            } else if (role == QStringLiteral("assistant")) {
                if (m.contains(QStringLiteral("tool_calls"))) {
                    // Emit each tool_call as a function_call item
                    for (const QJsonValue &tcVal : m[QStringLiteral("tool_calls")].toArray()) {
                        QJsonObject tc = tcVal.toObject();
                        QJsonObject fn = tc[QStringLiteral("function")].toObject();
                        QJsonObject item;
                        item[QStringLiteral("type")] = QStringLiteral("function_call");
                        item[QStringLiteral("call_id")] = tc[QStringLiteral("id")];
                        item[QStringLiteral("name")] = fn[QStringLiteral("name")];
                        item[QStringLiteral("arguments")] = fn[QStringLiteral("arguments")];
                        input.append(item);
                    }
                } else {
                    input.append(m);
                }
            } else if (role == QStringLiteral("tool")) {
                QJsonObject item;
                item[QStringLiteral("type")] = QStringLiteral("function_call_output");
                item[QStringLiteral("call_id")] = m[QStringLiteral("tool_call_id")];
                item[QStringLiteral("output")] = m[QStringLiteral("content")];
                input.append(item);
            } else {
                // developer, user — pass through
                input.append(m);
            }
        }
        body[QStringLiteral("input")] = input;

        // Convert tools: flatten {type, function:{...}} → {type, name, ...}
        QJsonArray responsesTools;
        for (const QJsonValue &toolVal : tools) {
            QJsonObject fn = toolVal.toObject()[QStringLiteral("function")].toObject();
            QJsonObject item;
            item[QStringLiteral("type")] = QStringLiteral("function");
            item[QStringLiteral("name")] = fn[QStringLiteral("name")];
            item[QStringLiteral("description")] = fn[QStringLiteral("description")];
            item[QStringLiteral("parameters")] = fn[QStringLiteral("parameters")];
            if (fn.contains(QStringLiteral("strict")))
                item[QStringLiteral("strict")] = fn[QStringLiteral("strict")];
            responsesTools.append(item);
        }
        body[QStringLiteral("tools")] = responsesTools;

        // Reasoning effort as nested object
        QJsonObject reasoningObj;
        if (_thinkingEnabled) {
            reasoningObj[QStringLiteral("effort")] = _reasoningEffort;
        } else {
            reasoningObj[QStringLiteral("effort")] = QStringLiteral("medium");
        }
        body[QStringLiteral("reasoning")] = reasoningObj;

    } else {
        // --- Chat Completions API format ---
        body[QStringLiteral("messages")] = messages;

        if (!tools.isEmpty()) {
            body[QStringLiteral("tools")] = tools;
        }

        if (reasoning) {
            if (_thinkingEnabled) {
                body[QStringLiteral("reasoning_effort")] = _reasoningEffort;
            } else if (!tools.isEmpty()) {
                body[QStringLiteral("reasoning_effort")] = QStringLiteral("medium");
            } else {
                body[QStringLiteral("reasoning_effort")] = QStringLiteral("low");
            }
        } else if (geminiThinking) {
            // Gemini 2.5+ / 3.x are thinking models that support reasoning_effort
            // via OpenAI compat.  Without this, they default to high thinking
            // which makes responses extremely slow (40-200+ seconds).
            if (_thinkingEnabled) {
                body[QStringLiteral("reasoning_effort")] = _reasoningEffort;
            } else {
                body[QStringLiteral("reasoning_effort")] = QStringLiteral("low");
            }
        } else {
            body[QStringLiteral("temperature")] = 0.3;
        }

        // Only set max_tokens if the user explicitly enabled a token limit
        // in settings.  By default, let models use their full output capacity.
        if (_maxTokensEnabled && _maxTokensLimit > 0) {
            body[QStringLiteral("max_tokens")] = _maxTokensLimit;
        }
    }

    QJsonDocument doc(body);
    QByteArray data = doc.toJson(QJsonDocument::Compact);

    // Create HTTP request — use Responses API URL when needed
    QString url = _useResponsesApi
                  ? (_apiBaseUrl + QStringLiteral("/responses"))
                  : (_apiBaseUrl + QStringLiteral("/chat/completions"));
    QNetworkRequest request{QUrl(url)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(apiKey()).toUtf8());
    // Generous timeouts: reasoning/thinking models can take minutes,
    // non-reasoning models still need time for large structured outputs.
    request.setTransferTimeout((reasoning || geminiThinking) ? 600000 : 180000);

    logApi(QStringLiteral("[REQUEST] model=%1 reasoning=%2 tools=%3 api=%4 body=%5")
           .arg(_model,
                reasoning ? QStringLiteral("yes") : QStringLiteral("no"),
                tools.isEmpty() ? QStringLiteral("none") : QString::number(tools.size()),
                _useResponsesApi ? QStringLiteral("responses") : QStringLiteral("completions"),
                QString::fromUtf8(data.left(4000))));

    _lastRequest = request;
    _lastRequestData = data;
    _currentReply = _manager->post(request, data);
}

void AiClient::testConnection()
{
    if (!isConfigured()) {
        emit connectionTestResult(false, tr("No API key configured."));
        return;
    }

    if (_currentReply) {
        emit connectionTestResult(false, tr("A request is already in progress."));
        return;
    }

    _isTestRequest = true;

    bool reasoning = isReasoningModel();

    QJsonArray messages;
    QJsonObject msg;
    msg[QStringLiteral("role")] = QStringLiteral("user");
    msg[QStringLiteral("content")] = QStringLiteral("Reply with: OK");
    messages.append(msg);

    QJsonObject body;
    body[QStringLiteral("model")] = _model;
    body[QStringLiteral("messages")] = messages;

    if (reasoning) {
        // Reasoning models: use top-level reasoning_effort string
        body[QStringLiteral("reasoning_effort")] = QStringLiteral("low");
        body[QStringLiteral("max_completion_tokens")] = 64;
    } else {
        body[QStringLiteral("max_completion_tokens")] = 16;
    }

    QJsonDocument doc(body);
    QByteArray data = doc.toJson(QJsonDocument::Compact);

    QNetworkRequest request{QUrl(_apiBaseUrl + QStringLiteral("/chat/completions"))};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(apiKey()).toUtf8());
    request.setTransferTimeout(15000);

    logApi(QStringLiteral("[TEST-REQ] model=%1 body=%2").arg(_model, QString::fromUtf8(data.left(4000))));

    _currentReply = _manager->post(request, data);
}

// Converts a /v1/responses response into the same shape as /v1/chat/completions
// so the rest of the code (AgentRunner, Simple mode) can stay unchanged.
static QJsonObject normalizeResponsesApiResponse(const QJsonObject &respObj)
{
    QJsonObject message;
    message[QStringLiteral("role")] = QStringLiteral("assistant");

    QString textContent;
    QJsonArray toolCalls;

    for (const QJsonValue &item : respObj[QStringLiteral("output")].toArray()) {
        QJsonObject itemObj = item.toObject();
        QString type = itemObj[QStringLiteral("type")].toString();

        if (type == QStringLiteral("message")) {
            for (const QJsonValue &c : itemObj[QStringLiteral("content")].toArray()) {
                if (c.toObject()[QStringLiteral("type")].toString() == QStringLiteral("output_text")) {
                    textContent += c.toObject()[QStringLiteral("text")].toString();
                }
            }
        } else if (type == QStringLiteral("function_call")) {
            QJsonObject tc;
            tc[QStringLiteral("id")] = itemObj[QStringLiteral("call_id")];
            tc[QStringLiteral("type")] = QStringLiteral("function");
            QJsonObject fn;
            fn[QStringLiteral("name")] = itemObj[QStringLiteral("name")];
            fn[QStringLiteral("arguments")] = itemObj[QStringLiteral("arguments")];
            tc[QStringLiteral("function")] = fn;
            toolCalls.append(tc);
        }
    }

    message[QStringLiteral("content")] = textContent.isEmpty()
        ? QJsonValue(QJsonValue::Null) : QJsonValue(textContent);
    if (!toolCalls.isEmpty()) {
        message[QStringLiteral("tool_calls")] = toolCalls;
    }

    // Map Responses API status to Chat Completions finish_reason
    QString status = respObj[QStringLiteral("status")].toString();
    QString finishReason;
    if (!toolCalls.isEmpty())
        finishReason = QStringLiteral("tool_calls");
    else if (status == QStringLiteral("incomplete"))
        finishReason = QStringLiteral("length");
    else
        finishReason = QStringLiteral("stop");

    QJsonObject choice;
    choice[QStringLiteral("message")] = message;
    choice[QStringLiteral("finish_reason")] = finishReason;

    QJsonArray choices;
    choices.append(choice);

    QJsonObject normalized;
    normalized[QStringLiteral("choices")] = choices;

    // Copy and remap Responses API usage field
    // Responses API uses "input_tokens"/"output_tokens" instead of "prompt_tokens"/"completion_tokens"
    if (respObj.contains(QStringLiteral("usage"))) {
        QJsonObject rawUsage = respObj[QStringLiteral("usage")].toObject();
        QJsonObject usage;
        usage[QStringLiteral("prompt_tokens")] = rawUsage[QStringLiteral("input_tokens")];
        usage[QStringLiteral("completion_tokens")] = rawUsage[QStringLiteral("output_tokens")];
        usage[QStringLiteral("total_tokens")] = rawUsage[QStringLiteral("total_tokens")];
        normalized[QStringLiteral("usage")] = usage;
    }

    return normalized;
}

void AiClient::onReplyFinished(QNetworkReply *reply)
{
    // Streaming requests handle their own finish — skip the normal handler
    if (_isStreaming) {
        reply->deleteLater();
        return;
    }

    _currentReply = nullptr;

    if (reply->error() == QNetworkReply::OperationCanceledError) {
        logApi(QStringLiteral("[TIMEOUT] Request timed out or was cancelled"));
        if (!_isTestRequest) {
            emit errorOccurred(tr("Request timed out. The prompt may be too complex for this model, "
                                  "or the provider is slow. Try a simpler prompt or a different model."));
        }
        reply->deleteLater();
        return;
    }

    QByteArray responseData = reply->readAll();
    int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    logApi(QStringLiteral("[RESPONSE] HTTP %1 body=%2").arg(statusCode).arg(QString::fromUtf8(responseData.left(8000))));

    if (reply->error() != QNetworkReply::NoError) {
        QString errorMsg;

        bool retriable = false;
        int retryDelayMs = 0;

        if (statusCode == 401) {
            errorMsg = tr("Invalid API key. Please check your key in Settings.");
        } else if (statusCode == 429) {
            errorMsg = tr("Rate limit exceeded. Please wait a moment and try again.");
            retriable = true;
            retryDelayMs = 2000;
        } else if (statusCode == 500 || statusCode == 503) {
            errorMsg = tr("API service is temporarily unavailable. Please try again later.");
            retriable = true;
            retryDelayMs = 1000;
        } else if (reply->error() == QNetworkReply::HostNotFoundError ||
                   reply->error() == QNetworkReply::ConnectionRefusedError) {
            errorMsg = tr("Unable to connect to the API. Please check your internet connection.");
        } else {
            errorMsg = tr("API error (HTTP %1): %2").arg(statusCode).arg(reply->errorString());
        }

        // Retry once for transient errors (429 rate limit, 5xx server errors)
        if (retriable && !_isTestRequest && _retryCount < 1 && !_lastRequestData.isEmpty()) {
            _retryCount++;
            reply->deleteLater();
            logApi(QStringLiteral("[RETRY] Scheduling retry %1/1 in %2ms for HTTP %3")
                       .arg(_retryCount).arg(retryDelayMs).arg(statusCode));
            emit retrying(tr("Retrying... (%1/1)").arg(_retryCount));
            QTimer::singleShot(retryDelayMs, this, [this]() {
                _currentReply = _manager->post(_lastRequest, _lastRequestData);
            });
            return;
        }

        if (_isTestRequest) {
            emit connectionTestResult(false, errorMsg);
        } else {
            emit errorOccurred(errorMsg);
        }

        reply->deleteLater();
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(responseData);
    if (doc.isNull() || !doc.isObject()) {
        QString msg = tr("Invalid response from API.");
        if (_isTestRequest) {
            emit connectionTestResult(false, msg);
        } else {
            emit errorOccurred(msg);
        }
        reply->deleteLater();
        return;
    }

    QJsonObject responseObj = doc.object();

    if (_isTestRequest) {
        emit connectionTestResult(true, tr("Connection successful! Model: %1").arg(_model));
        reply->deleteLater();
        return;
    }

    // If Responses API was used, normalize to Chat Completions format
    // so AgentRunner and Simple mode don't need separate code paths.
    if (_useResponsesApi) {
        responseObj = normalizeResponsesApiResponse(responseObj);
    }

    // Normalize provider-specific usage fields to canonical OpenAI format
    // (prompt_tokens, completion_tokens, total_tokens)
    if (!responseObj.contains(QStringLiteral("usage"))
        || responseObj[QStringLiteral("usage")].toObject().isEmpty()
        || !responseObj[QStringLiteral("usage")].toObject().contains(QStringLiteral("prompt_tokens"))) {

        QJsonObject usage;
        bool found = false;

        // Check for existing usage with non-standard field names (Anthropic: input_tokens/output_tokens)
        QJsonObject rawUsage = responseObj[QStringLiteral("usage")].toObject();
        if (rawUsage.contains(QStringLiteral("input_tokens"))) {
            usage[QStringLiteral("prompt_tokens")] = rawUsage[QStringLiteral("input_tokens")];
            usage[QStringLiteral("completion_tokens")] = rawUsage[QStringLiteral("output_tokens")];
            int total = rawUsage[QStringLiteral("input_tokens")].toInt()
                      + rawUsage[QStringLiteral("output_tokens")].toInt();
            usage[QStringLiteral("total_tokens")] = total;
            found = true;
        }

        // Check for Gemini usageMetadata format
        if (!found && responseObj.contains(QStringLiteral("usageMetadata"))) {
            QJsonObject meta = responseObj[QStringLiteral("usageMetadata")].toObject();
            usage[QStringLiteral("prompt_tokens")] = meta[QStringLiteral("promptTokenCount")];
            usage[QStringLiteral("completion_tokens")] = meta[QStringLiteral("candidatesTokenCount")];
            usage[QStringLiteral("total_tokens")] = meta[QStringLiteral("totalTokenCount")];
            found = true;
        }

        if (found) {
            responseObj[QStringLiteral("usage")] = usage;
        }
    }

    // Extract assistant message content
    QJsonArray choices = responseObj[QStringLiteral("choices")].toArray();
    if (choices.isEmpty()) {
        emit errorOccurred(tr("Empty response from API."));
        reply->deleteLater();
        return;
    }

    QJsonObject firstChoice = choices[0].toObject();
    QString finishReason = firstChoice[QStringLiteral("finish_reason")].toString();
    QJsonObject message = firstChoice[QStringLiteral("message")].toObject();
    QString content = message[QStringLiteral("content")].toString();

    // Detect output truncation (model hit max output tokens)
    if (finishReason == QStringLiteral("length")) {
        // Log partial content for debugging (AI-004)
        logApi(QStringLiteral("[TRUNCATED] partial content (%1 chars): %2")
                   .arg(content.size())
                   .arg(content.left(500)));
        if (_hasToolsInRequest) {
            emit errorOccurred(tr("Response was truncated (output token limit reached). "
                                  "Try a simpler request, reduce the number of tracks/measures, "
                                  "or increase the token limit in Settings."));
        } else {
            emit errorOccurred(tr("Response was truncated (output token limit reached). "
                                  "This request may be too complex for Simple mode. "
                                  "Switch to Agent mode for large compositions."));
        }
        reply->deleteLater();
        return;
    }

    emit responseReceived(content, responseObj);
    reply->deleteLater();
}

// === Streaming (SSE) support — Simple mode only ===

void AiClient::sendStreamingRequest(const QString &systemPrompt,
                                     const QJsonArray &conversationHistory,
                                     const QString &userMessage)
{
    if (!isConfigured()) {
        emit errorOccurred(tr("No API key configured. Please set your API key in Settings."));
        return;
    }
    if (_currentReply) {
        emit errorOccurred(tr("A request is already in progress."));
        return;
    }

    _isTestRequest = false;
    _isStreaming = true;
    _streamBuffer.clear();
    _streamContent.clear();
    _hasToolsInRequest = false;
    _useResponsesApi = false;

    bool reasoning = isReasoningModel();
    bool geminiThinking = isGeminiThinkingModel();

    // Build messages array
    QJsonArray messages;
    QJsonObject systemMsg;
    systemMsg[QStringLiteral("role")] = (reasoning && _provider != QStringLiteral("gemini"))
        ? QStringLiteral("developer")
        : QStringLiteral("system");
    systemMsg[QStringLiteral("content")] = systemPrompt;
    messages.append(systemMsg);
    for (const QJsonValue &msg : conversationHistory)
        messages.append(msg);
    QJsonObject userMsg;
    userMsg[QStringLiteral("role")] = QStringLiteral("user");
    userMsg[QStringLiteral("content")] = userMessage;
    messages.append(userMsg);

    QJsonObject body;
    body[QStringLiteral("model")] = _model;
    body[QStringLiteral("messages")] = messages;
    body[QStringLiteral("stream")] = true;

    // Request usage in the final streaming chunk (OpenAI)
    QJsonObject streamOpts;
    streamOpts[QStringLiteral("include_usage")] = true;
    body[QStringLiteral("stream_options")] = streamOpts;

    if (reasoning) {
        body[QStringLiteral("reasoning_effort")] = _thinkingEnabled ? _reasoningEffort : QStringLiteral("low");
    } else if (geminiThinking) {
        body[QStringLiteral("reasoning_effort")] = _thinkingEnabled ? _reasoningEffort : QStringLiteral("low");
    } else {
        body[QStringLiteral("temperature")] = 0.3;
    }

    if (_maxTokensEnabled && _maxTokensLimit > 0)
        body[QStringLiteral("max_tokens")] = _maxTokensLimit;

    QByteArray data = QJsonDocument(body).toJson(QJsonDocument::Compact);

    QString url = _apiBaseUrl + QStringLiteral("/chat/completions");
    QNetworkRequest request{QUrl(url)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(apiKey()).toUtf8());
    request.setTransferTimeout((reasoning || geminiThinking) ? 600000 : 180000);

    logApi(QStringLiteral("[STREAM-REQ] model=%1 body=%2").arg(_model, QString::fromUtf8(data.left(2000))));

    _currentReply = _manager->post(request, data);
    // Don't use finished signal for streaming — use readyRead instead
    disconnect(_manager, &QNetworkAccessManager::finished, this, &AiClient::onReplyFinished);
    connect(_currentReply, &QNetworkReply::readyRead, this, &AiClient::onStreamDataAvailable);
    connect(_currentReply, &QNetworkReply::finished, this, [this]() {
        // Process any remaining buffer
        if (!_streamBuffer.isEmpty()) {
            onStreamDataAvailable();
        }
        QNetworkReply *reply = _currentReply;
        _currentReply = nullptr;
        _isStreaming = false;

        // Re-connect the normal finished handler
        connect(_manager, &QNetworkAccessManager::finished, this, &AiClient::onReplyFinished);

        if (reply->error() == QNetworkReply::OperationCanceledError) {
            logApi(QStringLiteral("[STREAM-CANCEL]"));
            reply->deleteLater();
            return;
        }

        if (reply->error() != QNetworkReply::NoError) {
            int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            emit errorOccurred(tr("Streaming error (HTTP %1): %2").arg(statusCode).arg(reply->errorString()));
            reply->deleteLater();
            return;
        }

        // Build a synthetic response object for token tracking
        QJsonObject responseObj;
        QJsonObject choice;
        QJsonObject message;
        message[QStringLiteral("role")] = QStringLiteral("assistant");
        message[QStringLiteral("content")] = _streamContent;
        choice[QStringLiteral("message")] = message;
        choice[QStringLiteral("finish_reason")] = QStringLiteral("stop");
        QJsonArray choices;
        choices.append(choice);
        responseObj[QStringLiteral("choices")] = choices;

        // Usage was captured from the final SSE chunk if available
        logApi(QStringLiteral("[STREAM-DONE] chars=%1").arg(_streamContent.size()));

        emit streamFinished(_streamContent, responseObj);
        reply->deleteLater();
    });
}

void AiClient::onStreamDataAvailable()
{
    if (!_currentReply) return;

    _streamBuffer += _currentReply->readAll();

    // Process complete SSE events (separated by \n\n)
    while (true) {
        int idx = _streamBuffer.indexOf("\n\n");
        if (idx < 0) break;

        QByteArray chunk = _streamBuffer.left(idx);
        _streamBuffer.remove(0, idx + 2);

        // Parse SSE lines — may have multiple "data:" lines per event
        for (const QByteArray &line : chunk.split('\n')) {
            QByteArray trimmed = line.trimmed();
            if (!trimmed.startsWith("data: ")) continue;

            QByteArray payload = trimmed.mid(6);
            if (payload == "[DONE]") {
                // Stream complete — finished handler will fire
                continue;
            }

            QJsonDocument doc = QJsonDocument::fromJson(payload);
            if (!doc.isObject()) continue;

            QJsonObject obj = doc.object();

            // Extract content delta
            QJsonArray choices = obj[QStringLiteral("choices")].toArray();
            if (!choices.isEmpty()) {
                QJsonObject delta = choices[0].toObject()[QStringLiteral("delta")].toObject();
                QString text = delta[QStringLiteral("content")].toString();
                if (!text.isEmpty()) {
                    _streamContent += text;
                    emit streamDelta(text);
                }
            }

            // Capture usage from the final chunk (OpenAI sends it with stream_options)
            if (obj.contains(QStringLiteral("usage"))) {
                // Store usage in _streamBuffer-adjacent member isn't practical,
                // so we re-parse in the finished handler. Just log it.
                logApi(QStringLiteral("[STREAM-USAGE] %1")
                    .arg(QString::fromUtf8(QJsonDocument(obj[QStringLiteral("usage")].toObject()).toJson(QJsonDocument::Compact))));
            }
        }
    }
}
