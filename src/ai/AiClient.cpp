#include "AiClient.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QNetworkRequest>
#include <QUrl>

const QString AiClient::API_URL = QStringLiteral("https://api.openai.com/v1/chat/completions");
const QString AiClient::RESPONSES_API_URL = QStringLiteral("https://api.openai.com/v1/responses");
const QString AiClient::DEFAULT_MODEL = QStringLiteral("gpt-5.4");
const QString AiClient::SETTINGS_KEY_API_KEY = QStringLiteral("AI/api_key");
const QString AiClient::SETTINGS_KEY_MODEL = QStringLiteral("AI/model");
const QString AiClient::SETTINGS_KEY_THINKING = QStringLiteral("AI/thinking_enabled");
const QString AiClient::SETTINGS_KEY_REASONING_EFFORT = QStringLiteral("AI/reasoning_effort");

AiClient::AiClient(QObject *parent)
    : QObject(parent),
      _manager(new QNetworkAccessManager(this)),
      _currentReply(nullptr),
      _settings(QStringLiteral("MidiEditor"), QStringLiteral("NONE")),
      _isTestRequest(false),
      _useResponsesApi(false),
      _thinkingEnabled(false),
      _reasoningEffort(QStringLiteral("medium"))
{
    _model = _settings.value(SETTINGS_KEY_MODEL, DEFAULT_MODEL).toString();
    _thinkingEnabled = _settings.value(SETTINGS_KEY_THINKING, true).toBool();
    _reasoningEffort = _settings.value(SETTINGS_KEY_REASONING_EFFORT, QStringLiteral("medium")).toString();
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

bool AiClient::isBusy() const
{
    return _currentReply != nullptr;
}

void AiClient::cancelRequest()
{
    if (_currentReply) {
        _currentReply->abort();
        _currentReply = nullptr;
    }
}

void AiClient::sendRequest(const QString &systemPrompt,
                            const QJsonArray &conversationHistory,
                            const QString &userMessage)
{
    // Build messages array
    QJsonArray messages;

    // System prompt: reasoning models require "developer" role, standard models use "system"
    bool reasoning = isReasoningModel();
    QJsonObject systemMsg;
    systemMsg[QStringLiteral("role")] = reasoning
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
        emit errorOccurred(tr("No API key configured. Please set your OpenAI API key in Settings."));
        return;
    }

    if (_currentReply) {
        emit errorOccurred(tr("A request is already in progress."));
        return;
    }

    _isTestRequest = false;

    bool reasoning = isReasoningModel();

    // gpt-5.4 does not support reasoning_effort + tools on /v1/chat/completions;
    // use /v1/responses for that combination.
    _useResponsesApi = !tools.isEmpty() && _model.toLower().startsWith(QStringLiteral("gpt-5.4"));

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
        } else {
            body[QStringLiteral("temperature")] = 0.3;
        }
    }

    QJsonDocument doc(body);
    QByteArray data = doc.toJson(QJsonDocument::Compact);

    // Create HTTP request — use Responses API URL when needed
    QNetworkRequest request{QUrl(_useResponsesApi ? RESPONSES_API_URL : API_URL)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(apiKey()).toUtf8());
    request.setTransferTimeout(reasoning ? 600000 : 60000);

    logApi(QStringLiteral("[REQUEST] model=%1 reasoning=%2 tools=%3 api=%4 body=%5")
           .arg(_model,
                reasoning ? QStringLiteral("yes") : QStringLiteral("no"),
                tools.isEmpty() ? QStringLiteral("none") : QString::number(tools.size()),
                _useResponsesApi ? QStringLiteral("responses") : QStringLiteral("completions"),
                QString::fromUtf8(data)));

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

    QNetworkRequest request{QUrl(API_URL)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(apiKey()).toUtf8());
    request.setTransferTimeout(15000);

    logApi(QStringLiteral("[TEST-REQ] model=%1 body=%2").arg(_model, QString::fromUtf8(data)));

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
    return normalized;
}

void AiClient::onReplyFinished(QNetworkReply *reply)
{
    _currentReply = nullptr;

    if (reply->error() == QNetworkReply::OperationCanceledError) {
        if (!_isTestRequest) {
            emit errorOccurred(tr("Request was cancelled or timed out. Try a lower reasoning effort."));
        }
        reply->deleteLater();
        return;
    }

    QByteArray responseData = reply->readAll();
    int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    logApi(QStringLiteral("[RESPONSE] HTTP %1 body=%2").arg(statusCode).arg(QString::fromUtf8(responseData.left(8000))));

    if (reply->error() != QNetworkReply::NoError) {
        QString errorMsg;

        if (statusCode == 401) {
            errorMsg = tr("Invalid API key. Please check your key in Settings.");
        } else if (statusCode == 429) {
            errorMsg = tr("Rate limit exceeded. Please wait a moment and try again.");
        } else if (statusCode == 500 || statusCode == 503) {
            errorMsg = tr("OpenAI service is temporarily unavailable. Please try again later.");
        } else if (reply->error() == QNetworkReply::HostNotFoundError ||
                   reply->error() == QNetworkReply::ConnectionRefusedError) {
            errorMsg = tr("Unable to connect to OpenAI. Please check your internet connection.");
        } else {
            errorMsg = tr("API error (HTTP %1): %2").arg(statusCode).arg(reply->errorString());
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
        emit errorOccurred(tr("Response was truncated (output token limit reached). "
                              "This request is too complex for Simple mode. "
                              "Switch to Agent mode for large compositions."));
        reply->deleteLater();
        return;
    }

    emit responseReceived(content, responseObj);
    reply->deleteLater();
}
