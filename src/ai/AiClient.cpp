#include "AiClient.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QNetworkRequest>
#include <QUrl>

const QString AiClient::API_URL = QStringLiteral("https://api.openai.com/v1/chat/completions");
const QString AiClient::DEFAULT_MODEL = QStringLiteral("gpt-4o-mini");
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
      _thinkingEnabled(false),
      _reasoningEffort(QStringLiteral("medium"))
{
    _model = _settings.value(SETTINGS_KEY_MODEL, DEFAULT_MODEL).toString();
    _thinkingEnabled = _settings.value(SETTINGS_KEY_THINKING, false).toBool();
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
    _thinkingEnabled = _settings.value(SETTINGS_KEY_THINKING, false).toBool();
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

    // Build request body
    QJsonObject body;
    body[QStringLiteral("model")] = _model;
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

    QJsonDocument doc(body);
    QByteArray data = doc.toJson(QJsonDocument::Compact);

    // Create HTTP request
    QNetworkRequest request{QUrl(API_URL)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(apiKey()).toUtf8());
    request.setTransferTimeout(reasoning ? 600000 : 60000);

    logApi(QStringLiteral("[REQUEST] model=%1 reasoning=%2 tools=%3 body=%4")
           .arg(_model,
                reasoning ? QStringLiteral("yes") : QStringLiteral("no"),
                tools.isEmpty() ? QStringLiteral("none") : QString::number(tools.size()),
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

    logApi(QStringLiteral("[RESPONSE] HTTP %1 body=%2").arg(statusCode).arg(QString::fromUtf8(responseData.left(2000))));

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

    // Extract assistant message content
    QJsonArray choices = responseObj[QStringLiteral("choices")].toArray();
    if (choices.isEmpty()) {
        emit errorOccurred(tr("Empty response from API."));
        reply->deleteLater();
        return;
    }

    QJsonObject firstChoice = choices[0].toObject();
    QJsonObject message = firstChoice[QStringLiteral("message")].toObject();
    QString content = message[QStringLiteral("content")].toString();

    emit responseReceived(content, responseObj);
    reply->deleteLater();
}
