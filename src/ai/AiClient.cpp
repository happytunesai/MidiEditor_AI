#include "AiClient.h"
#include "ModelListCache.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QNetworkRequest>
#include <QSet>
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

// Phase 31.1: blocklist is keyed per (provider, model, mode) where mode is
// "tools=1" for Agent Mode and "tools=0" for Simple Mode. Some models
// (notably the gpt-5*-pro family) only accept one transport, so a streaming
// failure in one mode must not poison the other.
static QString sessionStreamingBlockKey(const QString &provider,
                                         const QString &model,
                                         bool withTools)
{
    QString p = provider.isEmpty() ? QStringLiteral("openai") : provider;
    return p + QStringLiteral(":") + model
             + QStringLiteral(":tools=") + (withTools ? QStringLiteral("1")
                                                       : QStringLiteral("0"));
}

static QSet<QString> &sessionStreamingBlocklist()
{
    static QSet<QString> blocked;
    return blocked;
}

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
      _streamHasTools(false),
      _streamGeminiCallCounter(0),
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

static void logInstructionProfileState(const QString &tag, const QString &model,
                                       const QJsonArray &messages)
{
    for (const QJsonValue &value : messages) {
        const QJsonObject msg = value.toObject();
        const QString role = msg.value(QStringLiteral("role")).toString();
        if (role != QStringLiteral("system") && role != QStringLiteral("developer"))
            continue;
        const QString content = msg.value(QStringLiteral("content")).toString();
        const bool hasGpt55Profile = content.contains(
            QStringLiteral("## GPT-5.5 MIDI TOOL MODE"));
        logApi(QStringLiteral("[%1-INSTRUCTIONS] model=%2 role=%3 chars=%4 gpt55_profile=%5 tail=%6")
            .arg(tag, model, role, QString::number(content.size()),
                 hasGpt55Profile ? QStringLiteral("yes") : QStringLiteral("no"),
                 content.right(1200)));
        return;
    }
}

static QString promptCacheKeyForRequest(const QString &model, bool hasTools)
{
    QString family = QStringLiteral("generic");
    const QString lower = model.toLower();
    if (lower.startsWith(QStringLiteral("gpt-5")))
        family = QStringLiteral("gpt-5");
    else if (lower.startsWith(QStringLiteral("gpt-4.1")))
        family = QStringLiteral("gpt-4.1");

    return QStringLiteral("midieditor-ai:%1:%2:v1")
        .arg(family, hasTools ? QStringLiteral("agent") : QStringLiteral("simple"));
}

static QJsonObject normalizeResponsesUsage(const QJsonObject &rawUsage)
{
    QJsonObject usage;
    usage[QStringLiteral("prompt_tokens")] = rawUsage[QStringLiteral("input_tokens")];
    usage[QStringLiteral("completion_tokens")] = rawUsage[QStringLiteral("output_tokens")];
    usage[QStringLiteral("total_tokens")] = rawUsage[QStringLiteral("total_tokens")];

    QJsonObject inputDetails = rawUsage[QStringLiteral("input_tokens_details")].toObject();
    if (!inputDetails.isEmpty()) {
        QJsonObject promptDetails;
        promptDetails[QStringLiteral("cached_tokens")] = inputDetails[QStringLiteral("cached_tokens")];
        usage[QStringLiteral("prompt_tokens_details")] = promptDetails;
    }

    return usage;
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

void AiClient::setNextRequestPolicyOverride(bool forceSequentialTools,
                                            const QString &reasoningEffortOverride)
{
    _nextForceSequentialTools = forceSequentialTools;
    _nextReasoningEffortOverride = reasoningEffortOverride;
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
    QString idRaw = (model.isEmpty() ? _model : model);

    // Phase 26: prefer the cached value from <userdata>/midipilot_models.json
    int cached = ModelListCache::contextWindowFor(idRaw);
    if (cached > 0)
        return cached;

    QString m = idRaw.toLower();

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
    sendMessagesInternal(messages, tools, true);
}

void AiClient::sendMessagesInternal(const QJsonArray &messages,
                                    const QJsonArray &tools,
                                    bool allowGeminiNativeToolsPath)
{
    if (!isConfigured()) {
        emit errorOccurred(tr("No API key configured. Please set your API key in Settings."));
        return;
    }

    if (_currentReply) {
        emit errorOccurred(tr("A request is already in progress."));
        return;
    }

    // Google's Gemini OpenAI-compat endpoint
    // (generativelanguage.googleapis.com/v1beta/openai/chat/completions)
    // mishandles multi-turn tool loops: after we echo back the assistant's
    // tool_calls plus our `role: tool` results, Gemini frequently responds
    // with `finish_reason: function_call_filter: MALFORMED_FUNCTION_CALL`
    // and an empty body. Routing the entire Gemini agent loop through the
    // native `:streamGenerateContent` endpoint instead — even when the user
    // has live streaming disabled — works around the compat-layer bug. The
    // native helper buffers everything and emits a single
    // responseReceived at the end, so callers can't tell the difference.
    if (allowGeminiNativeToolsPath && !tools.isEmpty() && _provider == QStringLiteral("gemini")) {
        sendStreamingMessagesGemini(messages, tools);
        return;
    }

    _isTestRequest = false;
    _hasToolsInRequest = !tools.isEmpty();
    _retryCount = 0;

    bool reasoning = isReasoningModel();
    bool geminiThinking = isGeminiThinkingModel();

    // GPT-5 reasoning models do not support reasoning_effort + tools on
    // /v1/chat/completions; use /v1/responses for that combination
    // (OpenAI-native only). Keep this family-wide so newly released
    // versions such as gpt-5.5 inherit the same transport automatically.
    _useResponsesApi = !tools.isEmpty()
                       && _model.toLower().startsWith(QStringLiteral("gpt-5"))
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

        // Phase 31: when AgentRunner asked for sequential tools (gpt-5.5*
        // composition/edit on OpenAI-native), suppress parallel function
        // calls so the model has to commit to one well-formed tool call
        // instead of fanning out into multiple minimal placeholders.
        if (_nextForceSequentialTools) {
            body[QStringLiteral("parallel_tool_calls")] = false;
        }

        // Prompt caching is automatic, but a stable cache key helps OpenAI
        // route repeated MidiPilot requests with the same static prefix
        // (developer prompt + tool schemas) to the same cache shard. Keep the
        // key coarse and content-free: no file names, user text, or song data.
        body[QStringLiteral("prompt_cache_key")] = promptCacheKeyForRequest(_model, !tools.isEmpty());

        // Reasoning effort as nested object
        QJsonObject reasoningObj;
        QString effortToSend;
        if (!_nextReasoningEffortOverride.isEmpty()) {
            // Phase 31 one-shot override (e.g. "low" for gpt-5.5*
            // composition/edit) — wins over both `_thinkingEnabled` and
            // the user's configured `_reasoningEffort`.
            effortToSend = _nextReasoningEffortOverride;
            qInfo().noquote() << QStringLiteral(
                "[POLICY] reasoning_effort overridden: %1 -> %2 (per-request override)")
                .arg(_thinkingEnabled ? _reasoningEffort : QStringLiteral("medium"),
                     effortToSend);
        } else if (_thinkingEnabled) {
            effortToSend = _reasoningEffort;
        } else {
            effortToSend = QStringLiteral("medium");
        }
        reasoningObj[QStringLiteral("effort")] = effortToSend;
        // Ask the model to produce human-readable summaries of its
        // reasoning. These come back as `output[].type == "reasoning"`
        // items with a `summary[]` array of `{type:"summary_text", text:"..."}`
        // entries — we forward them to the UI via `streamReasoningDelta`
        // (see normalizeResponsesApiResponse + onReplyFinished).
        reasoningObj[QStringLiteral("summary")] = QStringLiteral("auto");
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

    logInstructionProfileState(QStringLiteral("REQUEST"), _model, messages);
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

void AiClient::sendMessagesWithRequestSnapshot(const QJsonArray &messages,
                                               const QJsonArray &tools,
                                               const QString &provider,
                                               const QString &model,
                                               const QString &apiBaseUrl,
                                               bool allowGeminiNativeToolsPath)
{
    const QString currentProvider = _provider;
    const QString currentModel = _model;
    const QString currentApiBaseUrl = _apiBaseUrl;

    _provider = provider;
    _model = model;
    _apiBaseUrl = apiBaseUrl;
    sendMessagesInternal(messages, tools, allowGeminiNativeToolsPath);

    _provider = currentProvider;
    _model = currentModel;
    _apiBaseUrl = currentApiBaseUrl;
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

// Universal "thought / reasoning" extractor. Walks an arbitrary response
// object (full non-streaming response OR a single streaming chunk) and
// returns any human-readable thought text it can find, joined with blank
// lines. Recognises every shape we currently know about so we don't need
// per-provider emit code.
//
// Supported shapes:
//   * OpenAI Responses API (full):
//        output[].type == "reasoning" -> summary[].{type:"summary_text",text}
//        output[].type == "reasoning" -> content[].{type:"reasoning_text"|"text",text}
//   * OpenAI Chat Completions (full message OR streaming delta):
//        choices[0].message.reasoning_content   (string)
//        choices[0].message.reasoning           (string OR {content:string})
//        choices[0].delta.reasoning_content     (string)
//        choices[0].delta.reasoning             (string OR {content:string})
//   * Gemini (native streamGenerateContent OR full generateContent):
//        candidates[0].content.parts[].thought == true -> text
//   * Gemini OpenAI-compat: same as OpenAI Chat Completions (`reasoning` field)
//   * Anthropic Messages (full):
//        content[].type == "thinking" -> thinking
//   * Anthropic streaming events:
//        type == "content_block_delta" + delta.type == "thinking_delta" -> delta.thinking
//        type == "content_block_start" + content_block.type == "thinking" -> content_block.thinking
//   * Generic top-level fallbacks: "reasoning", "reasoning_content", "thought", "thinking"
static QString extractReasoningFromJson(const QJsonObject &obj)
{
    auto appendLine = [](QString &out, const QString &t) {
        if (t.isEmpty()) return;
        if (!out.isEmpty()) out += QStringLiteral("\n\n");
        out += t;
    };
    auto stringOrContent = [](const QJsonValue &v) -> QString {
        if (v.isString()) return v.toString();
        if (v.isObject()) {
            QJsonObject o = v.toObject();
            QString s = o.value(QStringLiteral("content")).toString();
            if (!s.isEmpty()) return s;
            s = o.value(QStringLiteral("text")).toString();
            if (!s.isEmpty()) return s;
            s = o.value(QStringLiteral("thinking")).toString();
            if (!s.isEmpty()) return s;
        }
        return QString();
    };

    QString out;

    // ---- OpenAI Responses API ---------------------------------------------
    if (obj.contains(QStringLiteral("output")) && obj.value(QStringLiteral("output")).isArray()) {
        for (const QJsonValue &item : obj.value(QStringLiteral("output")).toArray()) {
            QJsonObject itemObj = item.toObject();
            if (itemObj.value(QStringLiteral("type")).toString() != QStringLiteral("reasoning"))
                continue;
            for (const QJsonValue &s : itemObj.value(QStringLiteral("summary")).toArray()) {
                QJsonObject sObj = s.toObject();
                QString t = sObj.value(QStringLiteral("text")).toString();
                if (!t.isEmpty()) appendLine(out, t);
            }
            for (const QJsonValue &c : itemObj.value(QStringLiteral("content")).toArray()) {
                QJsonObject cObj = c.toObject();
                QString t = cObj.value(QStringLiteral("text")).toString();
                if (!t.isEmpty()) appendLine(out, t);
            }
        }
    }

    // ---- OpenAI Chat Completions (full + streaming) -----------------------
    if (obj.contains(QStringLiteral("choices")) && obj.value(QStringLiteral("choices")).isArray()) {
        for (const QJsonValue &cv : obj.value(QStringLiteral("choices")).toArray()) {
            QJsonObject choice = cv.toObject();
            for (const QString &key : { QStringLiteral("delta"), QStringLiteral("message") }) {
                QJsonObject m = choice.value(key).toObject();
                if (m.isEmpty()) continue;
                QString t = m.value(QStringLiteral("reasoning_content")).toString();
                if (!t.isEmpty()) appendLine(out, t);
                t = stringOrContent(m.value(QStringLiteral("reasoning")));
                if (!t.isEmpty()) appendLine(out, t);
                t = stringOrContent(m.value(QStringLiteral("thinking")));
                if (!t.isEmpty()) appendLine(out, t);
            }
        }
    }

    // ---- Gemini native (candidates[].content.parts[].thought) -------------
    if (obj.contains(QStringLiteral("candidates")) && obj.value(QStringLiteral("candidates")).isArray()) {
        for (const QJsonValue &cv : obj.value(QStringLiteral("candidates")).toArray()) {
            QJsonObject cand = cv.toObject();
            QJsonObject content = cand.value(QStringLiteral("content")).toObject();
            for (const QJsonValue &pv : content.value(QStringLiteral("parts")).toArray()) {
                QJsonObject part = pv.toObject();
                if (part.value(QStringLiteral("thought")).toBool()) {
                    QString t = part.value(QStringLiteral("text")).toString();
                    if (!t.isEmpty()) appendLine(out, t);
                }
            }
        }
    }

    // ---- Anthropic Messages (full content[]) ------------------------------
    if (obj.contains(QStringLiteral("content")) && obj.value(QStringLiteral("content")).isArray()) {
        for (const QJsonValue &cv : obj.value(QStringLiteral("content")).toArray()) {
            QJsonObject part = cv.toObject();
            if (part.value(QStringLiteral("type")).toString() == QStringLiteral("thinking")) {
                QString t = part.value(QStringLiteral("thinking")).toString();
                if (t.isEmpty()) t = part.value(QStringLiteral("text")).toString();
                if (!t.isEmpty()) appendLine(out, t);
            }
        }
    }

    // ---- Anthropic streaming events ---------------------------------------
    QString eventType = obj.value(QStringLiteral("type")).toString();
    if (eventType == QStringLiteral("content_block_delta")) {
        QJsonObject delta = obj.value(QStringLiteral("delta")).toObject();
        if (delta.value(QStringLiteral("type")).toString() == QStringLiteral("thinking_delta")) {
            QString t = delta.value(QStringLiteral("thinking")).toString();
            if (!t.isEmpty()) appendLine(out, t);
        }
    } else if (eventType == QStringLiteral("content_block_start")) {
        QJsonObject block = obj.value(QStringLiteral("content_block")).toObject();
        if (block.value(QStringLiteral("type")).toString() == QStringLiteral("thinking")) {
            QString t = block.value(QStringLiteral("thinking")).toString();
            if (!t.isEmpty()) appendLine(out, t);
        }
    }

    // ---- Generic top-level fallback ---------------------------------------
    if (out.isEmpty()) {
        for (const QString &k : { QStringLiteral("reasoning"),
                                  QStringLiteral("reasoning_content"),
                                  QStringLiteral("thought"),
                                  QStringLiteral("thinking") }) {
            QString t = stringOrContent(obj.value(k));
            if (!t.isEmpty()) { appendLine(out, t); break; }
        }
    }

    return out;
}

// Converts a /v1/responses response into the same shape as /v1/chat/completions
// so the rest of the code (AgentRunner, Simple mode) can stay unchanged.
// Also extracts any `reasoning.summary[]` text from `output[].type=="reasoning"`
// items into `outReasoningText` (joined with blank lines) so the caller can
// forward it to the UI via `streamReasoningDelta`.
static QJsonObject normalizeResponsesApiResponse(const QJsonObject &respObj,
                                                 QString *outReasoningText = nullptr)
{
    QJsonObject message;
    message[QStringLiteral("role")] = QStringLiteral("assistant");

    QString textContent;
    QJsonArray toolCalls;
    QString reasoningText;

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
        } else if (type == QStringLiteral("reasoning")) {
            // OpenAI thought summary: optional summary[] of {type:"summary_text", text:"..."}.
            for (const QJsonValue &s : itemObj[QStringLiteral("summary")].toArray()) {
                QJsonObject sObj = s.toObject();
                if (sObj[QStringLiteral("type")].toString() == QStringLiteral("summary_text")) {
                    QString t = sObj[QStringLiteral("text")].toString();
                    if (!t.isEmpty()) {
                        if (!reasoningText.isEmpty())
                            reasoningText += QStringLiteral("\n\n");
                        reasoningText += t;
                    }
                }
            }
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
        normalized[QStringLiteral("usage")] = normalizeResponsesUsage(
            respObj[QStringLiteral("usage")].toObject());
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

    // Universal "thought" extraction: works for OpenAI Responses, OpenAI
    // Chat Completions (with reasoning_content / reasoning), Gemini native
    // (parts[].thought), Anthropic (content[].type=="thinking"), and any
    // provider that exposes one of the common keys. Emits a single delta
    // with the full text — the UI streams it inline as gray italic.
    // Must run BEFORE normalize, because Responses API output[] is dropped
    // by the conversion to Chat Completions shape.
    {
        QString reasoningText = extractReasoningFromJson(responseObj);
        if (!reasoningText.isEmpty()) {
            logApi(QStringLiteral("[REASONING] %1 chars: %2")
                   .arg(reasoningText.size())
                   .arg(reasoningText.left(200)));
            emit streamReasoningDelta(reasoningText);
        }
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

// === Streaming (SSE) support — Simple mode + Agent loop ===

bool AiClient::agentStreamingEnabled() const
{
    return _settings.value(QStringLiteral("AI/streaming_mode"), QStringLiteral("on")).toString()
           != QStringLiteral("off");
}

// === Streaming-fallback helpers ===
//
// Some providers/models advertise SSE but mis-implement it: they answer
// `stream:true` requests with HTTP 4xx/5xx, or worse, return a clean HTTP 200
// with an empty body so we silently end up with no content and no tool calls.
// To stay usable across the long tail of OpenRouter/Anthropic/Gemini/Custom
// models we:
//   1. Save the original request payload at every streaming entry point.
//   2. After the stream finishes, decide whether the result looks broken.
//   3. If so, re-issue the request via the non-streaming code path AND
//      remember `<provider>:<model>` in QSettings so the next call skips
//      streaming entirely (no second user-visible delay).

QString AiClient::streamingBlocklistKey(const QString &provider,
                                         const QString &model) const
{
    QString p = provider.isEmpty() ? QStringLiteral("openai") : provider;
    return QStringLiteral("AI/streaming_blocklist_v2/") + p + QStringLiteral(":") + model;
}

bool AiClient::streamingDisabledForCurrentModel() const
{
    return streamingBlockedForSession(_provider, _model);
}

bool AiClient::streamingDisabledForCurrentModel(bool withTools) const
{
    return streamingBlockedForSession(_provider, _model, withTools);
}

bool AiClient::streamingBlockedForSession(const QString &provider, const QString &model)
{
    if (model.isEmpty()) return false;
    auto &bl = sessionStreamingBlocklist();
    return bl.contains(sessionStreamingBlockKey(provider, model, false))
        || bl.contains(sessionStreamingBlockKey(provider, model, true));
}

bool AiClient::streamingBlockedForSession(const QString &provider,
                                          const QString &model,
                                          bool withTools)
{
    if (model.isEmpty()) return false;
    return sessionStreamingBlocklist().contains(
        sessionStreamingBlockKey(provider, model, withTools));
}

void AiClient::clearStreamingBlockForSession(const QString &provider, const QString &model)
{
    if (provider.isEmpty() && model.isEmpty()) {
        sessionStreamingBlocklist().clear();
        return;
    }
    if (model.isEmpty()) return;
    auto &bl = sessionStreamingBlocklist();
    bl.remove(sessionStreamingBlockKey(provider, model, false));
    bl.remove(sessionStreamingBlockKey(provider, model, true));
}

void AiClient::markStreamingUnsupportedForCurrentModel(const QString &reason)
{
    // Infer the mode from the in-flight request. _hasToolsInRequest is set
    // by the agent-mode senders before issuing the request; the simple-mode
    // senders leave it false. _streamHasTools mirrors it for streaming.
    const bool withTools = _hasToolsInRequest || _streamHasTools;
    markStreamingUnsupported(_provider, _model, withTools, reason);
}

void AiClient::markStreamingUnsupported(const QString &provider,
                                        const QString &model,
                                        const QString &reason)
{
    // Legacy 3-arg overload: fall back to "any mode" by blocking both
    // entries so older callers continue to disable streaming entirely.
    markStreamingUnsupported(provider, model, false, reason);
    markStreamingUnsupported(provider, model, true, reason);
}

void AiClient::markStreamingUnsupported(const QString &provider,
                                        const QString &model,
                                        bool withTools,
                                        const QString &reason)
{
    if (model.isEmpty()) return;
    sessionStreamingBlocklist().insert(sessionStreamingBlockKey(provider, model, withTools));
    logApi(QStringLiteral("[STREAM-FALLBACK] disabling stream for this session for %1:%2 mode=%3 (%4)")
           .arg(provider, model,
                withTools ? QStringLiteral("agent") : QStringLiteral("simple"),
                reason));
}

void AiClient::clearStreamingBlocklist(const QString &provider,
                                        const QString &model)
{
    if (provider.isEmpty() && model.isEmpty()) {
        clearStreamingBlockForSession();

        // Remove persisted legacy entries from builds that stored the
        // streaming fallback blocklist in QSettings.
        _settings.beginGroup(QStringLiteral("AI/streaming_blocklist_v2"));
        const QStringList keys = _settings.childKeys();
        for (const QString &k : keys) _settings.remove(k);
        _settings.endGroup();

        // Also remove legacy v1 entries so users can fully reset the
        // fallback cache after streaming parser fixes or provider changes.
        _settings.beginGroup(QStringLiteral("AI/streaming_blocklist"));
        const QStringList legacyKeys = _settings.childKeys();
        for (const QString &k : legacyKeys) _settings.remove(k);
        _settings.endGroup();
        return;
    }
    clearStreamingBlockForSession(provider, model);

    // Remove persisted legacy entries from builds that stored the streaming
    // fallback blocklist in QSettings.
    _settings.remove(streamingBlocklistKey(provider, model));

    QString p = provider.isEmpty() ? QStringLiteral("openai") : provider;
    _settings.remove(QStringLiteral("AI/streaming_blocklist/") + p + QStringLiteral(":") + model);
}

// === Tool-incapable model flag (Phase 28.2) ===

static QString toolsIncapableKey(const QString &provider, const QString &model)
{
    QString p = provider.isEmpty() ? QStringLiteral("openai") : provider;
    return QStringLiteral("AI/incapable_tools/") + p + QStringLiteral(":") + model;
}

bool AiClient::toolsIncapableForCurrentModel() const
{
    if (_model.isEmpty()) return false;
    return _settings.value(toolsIncapableKey(_provider, _model), false).toBool();
}

void AiClient::markToolsIncapableForCurrentModel(const QString &reason)
{
    if (_model.isEmpty()) return;
    _settings.setValue(toolsIncapableKey(_provider, _model), true);
    logApi(QStringLiteral("[TOOLS-INCAPABLE] flagging %1:%2 (%3)")
           .arg(_provider, _model, reason));
}

void AiClient::clearToolsIncapableFlag(const QString &provider,
                                        const QString &model)
{
    if (provider.isEmpty() && model.isEmpty()) {
        _settings.beginGroup(QStringLiteral("AI/incapable_tools"));
        const QStringList keys = _settings.childKeys();
        for (const QString &k : keys) _settings.remove(k);
        _settings.endGroup();
        return;
    }
    _settings.remove(toolsIncapableKey(provider, model));
}

bool AiClient::errorIndicatesNoToolSupport(const QString &error)
{
    const QString l = error.toLower();
    // OpenRouter's typical message:
    //   "No endpoints found that support tool use. Try disabling
    //    \"get_editor_state\". To learn more about provider routing, ..."
    // plus generic variants we may see from other gateways.
    return l.contains(QStringLiteral("no endpoints found that support tool"))
        || l.contains(QStringLiteral("does not support tools"))
        || l.contains(QStringLiteral("does not support tool use"))
        || l.contains(QStringLiteral("does not support tool calling"))
        || l.contains(QStringLiteral("function calling is not supported"))
        || l.contains(QStringLiteral("tool use is not supported"))
        || l.contains(QStringLiteral("tools are not supported"));
}

void AiClient::armStreamingRetryAgent(const QJsonArray &messages,
                                       const QJsonArray &tools)
{
    _streamRetryArmed = true;
    _streamRetrySimpleMode = false;
    _streamRetryMessages = messages;
    _streamRetryTools = tools;
    _streamRetrySystemPrompt.clear();
    _streamRetryHistory = QJsonArray();
    _streamRetryUserMessage.clear();
    _streamRetryProvider = _provider;
    _streamRetryModel = _model;
    _streamRetryApiBaseUrl = _apiBaseUrl;
}

void AiClient::armStreamingRetrySimple(const QString &systemPrompt,
                                        const QJsonArray &history,
                                        const QString &userMessage)
{
    _streamRetryArmed = true;
    _streamRetrySimpleMode = true;
    _streamRetryMessages = QJsonArray();
    _streamRetryTools = QJsonArray();
    _streamRetrySystemPrompt = systemPrompt;
    _streamRetryHistory = history;
    _streamRetryUserMessage = userMessage;
    _streamRetryProvider = _provider;
    _streamRetryModel = _model;
    _streamRetryApiBaseUrl = _apiBaseUrl;
}

void AiClient::clearStreamingRetryContext()
{
    _streamRetryArmed = false;
    _streamRetrySimpleMode = false;
    _streamRetryMessages = QJsonArray();
    _streamRetryTools = QJsonArray();
    _streamRetrySystemPrompt.clear();
    _streamRetryHistory = QJsonArray();
    _streamRetryUserMessage.clear();
    _streamRetryProvider.clear();
    _streamRetryModel.clear();
    _streamRetryApiBaseUrl.clear();
}

bool AiClient::shouldFallbackToNonStreaming(int httpStatus,
                                             QNetworkReply::NetworkError netError,
                                             bool gotContent,
                                             bool gotToolCalls) const
{
    if (!_streamRetryArmed) return false;
    // User-cancelled → never retry.
    if (netError == QNetworkReply::OperationCanceledError) return false;
    // Hard network/HTTP failure on a streaming request → retry without stream.
    if (netError != QNetworkReply::NoError) return true;
    if (httpStatus >= 400) return true;
    // Silent failure: HTTP 200 OK but neither content nor tool calls were
    // emitted. That almost always means the SSE schema didn't match what we
    // parse (e.g. a Custom provider that uses a different event shape).
    if (!gotContent && !gotToolCalls) return true;
    return false;
}

bool AiClient::tryStreamingFallback(const QString &reason)
{
    if (!_streamRetryArmed) return false;
    // Snapshot + clear context BEFORE re-dispatching: the non-streaming send
    // path may itself fail and we don't want to bounce around forever.
    bool wasSimple = _streamRetrySimpleMode;
    QJsonArray messages = _streamRetryMessages;
    QJsonArray tools = _streamRetryTools;
    QString systemPrompt = _streamRetrySystemPrompt;
    QJsonArray history = _streamRetryHistory;
    QString userMessage = _streamRetryUserMessage;
    QString retryProvider = _streamRetryProvider;
    QString retryModel = _streamRetryModel;
    QString retryApiBaseUrl = _streamRetryApiBaseUrl;
    clearStreamingRetryContext();

    markStreamingUnsupported(retryProvider, retryModel, !wasSimple, reason);
    emit retrying(tr("Streaming failed (%1) — retrying without streaming…").arg(reason));

    if (wasSimple) {
        const QString currentProvider = _provider;
        const QString currentModel = _model;
        const QString currentApiBaseUrl = _apiBaseUrl;
        _provider = retryProvider;
        _model = retryModel;
        _apiBaseUrl = retryApiBaseUrl;
        sendRequest(systemPrompt, history, userMessage);
        _provider = currentProvider;
        _model = currentModel;
        _apiBaseUrl = currentApiBaseUrl;
    } else {
        sendMessagesWithRequestSnapshot(messages, tools,
                                        retryProvider, retryModel, retryApiBaseUrl,
                                        false);
    }
    return true;
}

void AiClient::sendStreamingMessages(const QJsonArray &messages, const QJsonArray &tools)
{
    // Auto-blocklist: a previous streaming attempt for this provider/model in
    // Agent Mode failed silently or with HTTP error. Skip the streaming code
    // path so the user doesn't have to wait through it twice. Note: scoped
    // to mode=agent so a Simple-Mode failure does not poison this path.
    const bool withTools = !tools.isEmpty();
    if (streamingDisabledForCurrentModel(withTools)) {
        logApi(QStringLiteral("[STREAM-AGENT-SKIP] blocklisted %1:%2 — using non-streaming send")
               .arg(_provider, _model));
        sendMessages(messages, tools);
        return;
    }

    // GPT-5 + tools requires the Responses API. We have a dedicated SSE
    // handler for it (sendStreamingMessagesResponses) so the user gets
    // live reasoning + text deltas just like Gemini. Keep this family-wide
    // so new versions such as gpt-5.5 do not fall back to Chat Completions.
    bool wouldUseResponses = !tools.isEmpty()
        && _model.toLower().startsWith(QStringLiteral("gpt-5"))
        && (_provider.isEmpty() || _provider == QStringLiteral("openai"));
    if (wouldUseResponses) {
        sendStreamingMessagesResponses(messages, tools);
        return;
    }

    // Google's Gemini OpenAI-compat endpoint
    // (generativelanguage.googleapis.com/v1beta/openai/chat/completions)
    // returns HTTP 400 for `stream: true` + `tools` — it streams text fine
    // and it accepts tools fine, but not both together. For Gemini we
    // therefore route the agent loop through Google's *native*
    // streamGenerateContent endpoint instead, which natively supports
    // streaming text + tool calls AND live thought summaries.
    if (!tools.isEmpty() && _provider == QStringLiteral("gemini")) {
        sendStreamingMessagesGemini(messages, tools);
        return;
    }

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
    _streamHasTools = !tools.isEmpty();
    _hasToolsInRequest = _streamHasTools;
    _useResponsesApi = false;
    _streamBuffer.clear();
    _streamContent.clear();
    _streamReasoning.clear();
    _streamToolCalls.clear();

    // Arm the streaming-fallback safety net BEFORE issuing the request so
    // that we can transparently re-issue it via sendMessages() if the SSE
    // stream errors out or returns an empty body.
    armStreamingRetryAgent(messages, tools);

    bool reasoning = isReasoningModel();
    bool geminiThinking = isGeminiThinkingModel();

    QJsonObject body;
    body[QStringLiteral("model")] = _model;
    body[QStringLiteral("messages")] = messages;
    body[QStringLiteral("stream")] = true;

    if (!tools.isEmpty())
        body[QStringLiteral("tools")] = tools;

    QJsonObject streamOpts;
    streamOpts[QStringLiteral("include_usage")] = true;
    body[QStringLiteral("stream_options")] = streamOpts;

    if (reasoning) {
        if (_thinkingEnabled)
            body[QStringLiteral("reasoning_effort")] = _reasoningEffort;
        else if (!tools.isEmpty())
            body[QStringLiteral("reasoning_effort")] = QStringLiteral("medium");
        else
            body[QStringLiteral("reasoning_effort")] = QStringLiteral("low");
    } else if (geminiThinking) {
        body[QStringLiteral("reasoning_effort")] = _thinkingEnabled
            ? _reasoningEffort : QStringLiteral("low");
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

    logInstructionProfileState(QStringLiteral("STREAM-AGENT"), _model, messages);
    logApi(QStringLiteral("[STREAM-AGENT-REQ] model=%1 tools=%2 body=%3")
        .arg(_model, QString::number(tools.size()), QString::fromUtf8(data.left(2000))));

    _currentReply = _manager->post(request, data);
    disconnect(_manager, &QNetworkAccessManager::finished, this, &AiClient::onReplyFinished);
    connect(_currentReply, &QNetworkReply::readyRead, this, &AiClient::onStreamDataAvailable);
    connect(_currentReply, &QNetworkReply::finished, this, [this]() {
        if (!_streamBuffer.isEmpty())
            onStreamDataAvailable();
        QNetworkReply *reply = _currentReply;
        _currentReply = nullptr;
        _isStreaming = false;
        connect(_manager, &QNetworkAccessManager::finished, this, &AiClient::onReplyFinished);

        if (reply->error() == QNetworkReply::OperationCanceledError) {
            logApi(QStringLiteral("[STREAM-AGENT-CANCEL]"));
            _streamHasTools = false;
            _streamToolCalls.clear();
            clearStreamingRetryContext();
            reply->deleteLater();
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            QString detail = QString::fromUtf8(reply->readAll().left(500));
            // Streaming-fallback safety net: try once more without `stream:true`.
            if (shouldFallbackToNonStreaming(statusCode, reply->error(), false, false)) {
                _streamHasTools = false;
                _streamToolCalls.clear();
                reply->deleteLater();
                tryStreamingFallback(QStringLiteral("HTTP %1").arg(statusCode));
                return;
            }
            emit errorOccurred(tr("Streaming error (HTTP %1): %2 %3")
                .arg(statusCode).arg(reply->errorString(), detail));
            _streamHasTools = false;
            _streamToolCalls.clear();
            clearStreamingRetryContext();
            reply->deleteLater();
            return;
        }

        // Build synthetic Chat-Completions response for AgentRunner
        QJsonObject responseObj;
        QJsonObject message;
        message[QStringLiteral("role")] = QStringLiteral("assistant");

        QString finishReason = QStringLiteral("stop");
        QJsonArray toolCalls;
        if (!_streamToolCalls.isEmpty()) {
            QList<int> indices = _streamToolCalls.keys();
            std::sort(indices.begin(), indices.end());
            for (int idx : indices) {
                const StreamToolCall &acc = _streamToolCalls[idx];
                if (acc.id.isEmpty() || acc.name.isEmpty()) continue;
                QJsonObject tc;
                tc[QStringLiteral("id")] = acc.id;
                tc[QStringLiteral("type")] = QStringLiteral("function");
                QJsonObject fn;
                fn[QStringLiteral("name")] = acc.name;
                fn[QStringLiteral("arguments")] = acc.arguments;
                tc[QStringLiteral("function")] = fn;
                toolCalls.append(tc);
            }
        }

        if (!toolCalls.isEmpty()) {
            message[QStringLiteral("tool_calls")] = toolCalls;
            finishReason = QStringLiteral("tool_calls");
            message[QStringLiteral("content")] = _streamContent.isEmpty()
                ? QJsonValue(QJsonValue::Null) : QJsonValue(_streamContent);
        } else {
            message[QStringLiteral("content")] = _streamContent;
        }

        if (shouldFallbackToNonStreaming(0, QNetworkReply::NoError,
                                         !_streamContent.isEmpty(),
                                         !toolCalls.isEmpty())) {
            logApi(QStringLiteral("[STREAM-AGENT-EMPTY] no content/tool calls — falling back to non-streaming"));
            _streamHasTools = false;
            _streamToolCalls.clear();
            reply->deleteLater();
            tryStreamingFallback(QStringLiteral("empty stream"));
            return;
        }

        QJsonObject choice;
        choice[QStringLiteral("message")] = message;
        choice[QStringLiteral("finish_reason")] = finishReason;
        QJsonArray choices;
        choices.append(choice);
        responseObj[QStringLiteral("choices")] = choices;

        if (!_streamReasoning.isEmpty()) {
            logApi(QStringLiteral("[STREAM-AGENT-REASONING] %1 chars: %2")
                .arg(_streamReasoning.size())
                .arg(_streamReasoning));
        }
        for (const QJsonValue &tcVal : toolCalls) {
            QJsonObject tc = tcVal.toObject();
            QJsonObject fn = tc.value(QStringLiteral("function")).toObject();
            logApi(QStringLiteral("[STREAM-AGENT-TOOLCALL] id=%1 name=%2 args=%3")
                .arg(tc.value(QStringLiteral("id")).toString(),
                     fn.value(QStringLiteral("name")).toString(),
                     fn.value(QStringLiteral("arguments")).toString()));
        }
        logApi(QStringLiteral("[STREAM-AGENT-DONE] chars=%1 toolCalls=%2")
            .arg(_streamContent.size()).arg(toolCalls.size()));

        clearStreamingRetryContext();
        emit responseReceived(_streamContent, responseObj);

        _streamHasTools = false;
        _streamToolCalls.clear();
        reply->deleteLater();
    });
}

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

    // Auto-blocklist: skip the streaming round trip if a previous attempt
    // for this provider/model in Simple Mode failed silently or with HTTP
    // error. Scoped to mode=simple so an Agent-Mode failure does not
    // poison this path.
    if (streamingDisabledForCurrentModel(/*withTools=*/false)) {
        logApi(QStringLiteral("[STREAM-SKIP] blocklisted %1:%2 — using non-streaming send")
               .arg(_provider, _model));
        sendRequest(systemPrompt, conversationHistory, userMessage);
        return;
    }

    _isTestRequest = false;
    _isStreaming = true;
    _streamHasTools = false;
    _streamBuffer.clear();
    _streamContent.clear();
    _streamReasoning.clear();
    _streamToolCalls.clear();
    _hasToolsInRequest = false;
    _useResponsesApi = false;

    // Arm the streaming-fallback safety net so a broken stream falls back to
    // the non-streaming sendRequest() path automatically.
    armStreamingRetrySimple(systemPrompt, conversationHistory, userMessage);

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
            clearStreamingRetryContext();
            reply->deleteLater();
            return;
        }

        if (reply->error() != QNetworkReply::NoError) {
            int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (shouldFallbackToNonStreaming(statusCode, reply->error(), false, false)) {
                reply->deleteLater();
                tryStreamingFallback(QStringLiteral("HTTP %1").arg(statusCode));
                return;
            }
            emit errorOccurred(tr("Streaming error (HTTP %1): %2").arg(statusCode).arg(reply->errorString()));
            clearStreamingRetryContext();
            reply->deleteLater();
            return;
        }

        // Build a synthetic response object for token tracking
        QJsonObject responseObj;
        QJsonObject choice;
        QJsonObject message;
        message[QStringLiteral("role")] = QStringLiteral("assistant");
        message[QStringLiteral("content")] = _streamContent;

        // Reassemble tool_calls from the per-index accumulators (agent path)
        QString finishReason = QStringLiteral("stop");
        if (_streamHasTools && !_streamToolCalls.isEmpty()) {
            // Stable order by index
            QList<int> indices = _streamToolCalls.keys();
            std::sort(indices.begin(), indices.end());
            QJsonArray toolCalls;
            for (int idx : indices) {
                const StreamToolCall &acc = _streamToolCalls[idx];
                if (acc.id.isEmpty() || acc.name.isEmpty()) continue;
                QJsonObject tc;
                tc[QStringLiteral("id")] = acc.id;
                tc[QStringLiteral("type")] = QStringLiteral("function");
                QJsonObject fn;
                fn[QStringLiteral("name")] = acc.name;
                fn[QStringLiteral("arguments")] = acc.arguments;
                tc[QStringLiteral("function")] = fn;
                toolCalls.append(tc);
            }
            if (!toolCalls.isEmpty()) {
                message[QStringLiteral("tool_calls")] = toolCalls;
                finishReason = QStringLiteral("tool_calls");
                if (_streamContent.isEmpty())
                    message[QStringLiteral("content")] = QJsonValue(QJsonValue::Null);
            }
        }

        choice[QStringLiteral("message")] = message;
        choice[QStringLiteral("finish_reason")] = finishReason;
        QJsonArray choices;
        choices.append(choice);
        responseObj[QStringLiteral("choices")] = choices;

        // Usage was captured from the final SSE chunk if available
        if (!_streamReasoning.isEmpty()) {
            logApi(QStringLiteral("[STREAM-REASONING] %1 chars: %2")
                .arg(_streamReasoning.size())
                .arg(_streamReasoning));
        }
        logApi(QStringLiteral("[STREAM-DONE] chars=%1").arg(_streamContent.size()));

        if (_streamHasTools) {
            // Agent-loop path — feed reconstructed message into the normal
            // responseReceived handler so AgentRunner stays unchanged.
            QString textOnly = _streamContent;
            clearStreamingRetryContext();
            emit responseReceived(textOnly, responseObj);
            // Reset for next request
            _streamHasTools = false;
            _streamToolCalls.clear();
        } else {
            // Silent-empty stream on the simple-mode path → fall back to
            // a non-streaming sendRequest so the user actually gets an answer.
            if (_streamContent.isEmpty()
                && shouldFallbackToNonStreaming(0, QNetworkReply::NoError, false, false)) {
                reply->deleteLater();
                tryStreamingFallback(QStringLiteral("empty SSE response"));
                return;
            }
            clearStreamingRetryContext();
            emit streamFinished(_streamContent, responseObj);
        }
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

            // Universal thought extraction — handles `delta.reasoning_content`
            // (DeepSeek / Qwen / OpenAI o-series via Chat Completions),
            // `delta.reasoning` (OpenRouter / Gemini OpenAI-compat), and any
            // other shape the helper recognises. Each chunk emits its own
            // delta so the UI can stream the thought live.
            {
                QString rdelta = extractReasoningFromJson(obj);
                if (!rdelta.isEmpty()) {
                    _streamReasoning += rdelta;
                    emit streamReasoningDelta(rdelta);
                }
            }

            // Extract content delta
            QJsonArray choices = obj[QStringLiteral("choices")].toArray();
            if (!choices.isEmpty()) {
                QJsonObject delta = choices[0].toObject()[QStringLiteral("delta")].toObject();
                QString text = delta[QStringLiteral("content")].toString();
                if (!text.isEmpty()) {
                    _streamContent += text;
                    emit streamDelta(text);
                    if (_streamHasTools)
                        emit streamAssistantTextDelta(text);
                }

                // Extract tool_calls deltas (agent-loop streaming path)
                if (_streamHasTools && delta.contains(QStringLiteral("tool_calls"))) {
                    for (const QJsonValue &tcVal : delta[QStringLiteral("tool_calls")].toArray()) {
                        QJsonObject tc = tcVal.toObject();
                        // index identifies the call across deltas; some providers
                        // (Ollama OpenAI-compat, see ollama#15457) always send 0
                        // — we still keyed by index because id-based correlation
                        // would require lookahead. The first chunk for an index
                        // carries id + function.name; subsequent chunks only carry
                        // function.arguments fragments.
                        int idx = tc[QStringLiteral("index")].toInt(0);
                        StreamToolCall &acc = _streamToolCalls[idx];
                        if (tc.contains(QStringLiteral("id"))) {
                            QString id = tc[QStringLiteral("id")].toString();
                            if (!id.isEmpty()) acc.id = id;
                        }
                        QJsonObject fn = tc[QStringLiteral("function")].toObject();
                        if (fn.contains(QStringLiteral("name"))) {
                            QString name = fn[QStringLiteral("name")].toString();
                            if (!name.isEmpty()) acc.name = name;
                        }
                        if (!acc.started && !acc.id.isEmpty() && !acc.name.isEmpty()) {
                            acc.started = true;
                            emit streamToolCallStarted(acc.id, acc.name);
                        }
                        if (fn.contains(QStringLiteral("arguments"))) {
                            QString frag = fn[QStringLiteral("arguments")].toString();
                            if (!frag.isEmpty()) {
                                acc.arguments += frag;
                                if (acc.started)
                                    emit streamToolCallArgsDelta(acc.id, frag);
                            }
                        }
                    }
                }

                // finish_reason on the choice signals end-of-call for tool_calls
                QString finish = choices[0].toObject()[QStringLiteral("finish_reason")].toString();
                if (_streamHasTools && finish == QStringLiteral("tool_calls")) {
                    for (auto it = _streamToolCalls.begin(); it != _streamToolCalls.end(); ++it) {
                        if (it.value().started)
                            emit streamToolCallArgsDone(it.value().id);
                    }
                }
            }

            // Capture usage from the final chunk (OpenAI sends it with stream_options)
            if (obj.contains(QStringLiteral("usage"))) {
                const QJsonObject usage = obj[QStringLiteral("usage")].toObject();
                if (!usage.isEmpty()) {
                    // Store usage in _streamBuffer-adjacent member isn't practical,
                    // so we re-parse in the finished handler. Just log it.
                    logApi(QStringLiteral("[STREAM-USAGE] %1")
                        .arg(QString::fromUtf8(QJsonDocument(usage).toJson(QJsonDocument::Compact))));
                }
            }
        }
    }
}

// === Native Gemini streaming (streamGenerateContent?alt=sse) ============
//
// Google's OpenAI-compat endpoint rejects `stream: true` + `tools` in the
// same request, so when the user is on the Gemini provider AND we're in
// the agent loop (= tools present), we POST to the native endpoint
// instead. We additionally enable `thinkingConfig.includeThoughts: true`
// so we can emit live reasoning deltas via `streamReasoningDelta`.
//
// Once the stream finishes we synthesize a Chat-Completions-shaped
// response object so AgentRunner::onApiResponse stays unchanged.

namespace {

// Convert OpenAI Chat-Completions message array → Gemini contents array
// + systemInstruction. Returns the assembled body fragment.
//
// Mappings:
//   role=system    → systemInstruction.parts[].text
//   role=user      → contents[{role:"user",  parts:[{text}]}]
//   role=assistant → contents[{role:"model", parts:[{text} or {functionCall}]}]
//   role=tool      → contents[{role:"user",  parts:[{functionResponse}]}]
//
// We need to map tool_call_id → name to fill `functionResponse.name`,
// since Gemini wants the function name there (OpenAI uses an opaque id).
void buildGeminiContents(const QJsonArray &messages,
                         QJsonArray &outContents,
                         QJsonObject &outSystemInstruction)
{
    QHash<QString, QString> callIdToName;
    bool hasSystem = false;
    QString systemText;

    for (const QJsonValue &mv : messages) {
        QJsonObject m = mv.toObject();
        QString role = m.value(QStringLiteral("role")).toString();

        if (role == QStringLiteral("system")) {
            if (hasSystem)
                systemText += QStringLiteral("\n\n");
            systemText += m.value(QStringLiteral("content")).toString();
            hasSystem = true;
            continue;
        }

        if (role == QStringLiteral("assistant")) {
            QJsonArray parts;
            QString content = m.value(QStringLiteral("content")).toString();
            if (!content.isEmpty()) {
                QJsonObject p;
                p[QStringLiteral("text")] = content;
                parts.append(p);
            }
            if (m.contains(QStringLiteral("tool_calls"))) {
                for (const QJsonValue &tcv : m.value(QStringLiteral("tool_calls")).toArray()) {
                    QJsonObject tc = tcv.toObject();
                    QJsonObject fn = tc.value(QStringLiteral("function")).toObject();
                    QString name = fn.value(QStringLiteral("name")).toString();
                    QString argsStr = fn.value(QStringLiteral("arguments")).toString();
                    QString id = tc.value(QStringLiteral("id")).toString();
                    if (!id.isEmpty() && !name.isEmpty())
                        callIdToName.insert(id, name);
                    QJsonObject argsObj;
                    if (!argsStr.isEmpty()) {
                        QJsonDocument d = QJsonDocument::fromJson(argsStr.toUtf8());
                        if (d.isObject())
                            argsObj = d.object();
                    }
                    QJsonObject fc;
                    fc[QStringLiteral("name")] = name;
                    fc[QStringLiteral("args")] = argsObj;
                    QJsonObject p;
                    p[QStringLiteral("functionCall")] = fc;
                    // Gemini 3.x requires the thoughtSignature from the
                    // original streaming response to be echoed back on the
                    // `functionCall` part of the follow-up turn. Stored by
                    // onGeminiStreamDataAvailable() on the synthetic
                    // tool_call under `_gemini_thought_signature`.
                    QString sig = tc.value(
                        QStringLiteral("_gemini_thought_signature")).toString();
                    if (!sig.isEmpty())
                        p[QStringLiteral("thoughtSignature")] = sig;
                    parts.append(p);
                }
            }
            if (parts.isEmpty())
                continue;
            QJsonObject c;
            c[QStringLiteral("role")] = QStringLiteral("model");
            c[QStringLiteral("parts")] = parts;
            outContents.append(c);
            continue;
        }

        if (role == QStringLiteral("tool")) {
            QString id = m.value(QStringLiteral("tool_call_id")).toString();
            QString name = callIdToName.value(id);
            QString resultStr = m.value(QStringLiteral("content")).toString();
            QJsonObject responseObj;
            // Gemini wants the response field to be an object. Tool results
            // in MidiEditor are JSON strings; if it parses as object/array
            // wrap accordingly, otherwise stash it under "result".
            QJsonDocument d = QJsonDocument::fromJson(resultStr.toUtf8());
            if (d.isObject()) {
                responseObj = d.object();
            } else if (d.isArray()) {
                responseObj[QStringLiteral("result")] = d.array();
            } else {
                responseObj[QStringLiteral("result")] = resultStr;
            }
            QJsonObject fr;
            fr[QStringLiteral("name")] = name.isEmpty() ? QStringLiteral("unknown") : name;
            fr[QStringLiteral("response")] = responseObj;
            QJsonObject p;
            p[QStringLiteral("functionResponse")] = fr;
            QJsonObject c;
            c[QStringLiteral("role")] = QStringLiteral("user");
            c[QStringLiteral("parts")] = QJsonArray{ p };
            outContents.append(c);
            continue;
        }

        // role == "user" (default)
        QJsonObject p;
        p[QStringLiteral("text")] = m.value(QStringLiteral("content")).toString();
        QJsonObject c;
        c[QStringLiteral("role")] = QStringLiteral("user");
        c[QStringLiteral("parts")] = QJsonArray{ p };
        outContents.append(c);
    }

    // Gemini requires strictly alternating user/model turns. If the upstream
    // conversation contains consecutive same-role entries (e.g. user retried
    // after an empty assistant response, leaving two user turns in a row),
    // merge their parts into a single turn so the request is valid.
    QJsonArray coalesced;
    for (const QJsonValue &cv : outContents) {
        QJsonObject c = cv.toObject();
        if (!coalesced.isEmpty()) {
            QJsonObject prev = coalesced.last().toObject();
            if (prev.value(QStringLiteral("role")).toString()
                == c.value(QStringLiteral("role")).toString()) {
                QJsonArray prevParts = prev.value(QStringLiteral("parts")).toArray();
                for (const QJsonValue &pv : c.value(QStringLiteral("parts")).toArray())
                    prevParts.append(pv);
                prev[QStringLiteral("parts")] = prevParts;
                coalesced.replace(coalesced.size() - 1, prev);
                continue;
            }
        }
        coalesced.append(c);
    }
    outContents = coalesced;

    if (hasSystem) {
        QJsonObject p;
        p[QStringLiteral("text")] = systemText;
        outSystemInstruction[QStringLiteral("parts")] = QJsonArray{ p };
    }
}

// Recursively strip JSON-Schema fields that Gemini's `functionDeclarations`
// validator rejects (chiefly `additionalProperties`, `$schema`, `$id`,
// `$ref`, `definitions`). MidiEditor's tool schemas use these for OpenAI's
// strict-mode — Gemini only accepts the OpenAPI 3.0 schema subset.
void sanitizeSchemaForGemini(QJsonObject &schema)
{
    static const QStringList unsupported = {
        QStringLiteral("additionalProperties"),
        QStringLiteral("$schema"),
        QStringLiteral("$id"),
        QStringLiteral("$ref"),
        QStringLiteral("definitions"),
        QStringLiteral("strict"),
    };
    for (const QString &k : unsupported)
        schema.remove(k);

    // Gemini accepts `enum` only on string-typed schemas. OpenAI tolerates
    // numeric enums; Gemini rejects them with HTTP 400 "TYPE_STRING".
    // For non-string fields with a numeric enum we drop the enum (the
    // description usually already lists the allowed values).
    if (schema.contains(QStringLiteral("enum"))) {
        const QString t = schema.value(QStringLiteral("type")).toString();
        if (!t.isEmpty() && t != QStringLiteral("string")) {
            schema.remove(QStringLiteral("enum"));
        }
    }

    if (schema.contains(QStringLiteral("properties"))) {
        QJsonObject props = schema.value(QStringLiteral("properties")).toObject();
        for (const QString &propKey : props.keys()) {
            QJsonObject prop = props.value(propKey).toObject();
            sanitizeSchemaForGemini(prop);
            props[propKey] = prop;
        }
        schema[QStringLiteral("properties")] = props;
    }
    if (schema.contains(QStringLiteral("items"))) {
        QJsonValue items = schema.value(QStringLiteral("items"));
        if (items.isObject()) {
            QJsonObject inner = items.toObject();
            sanitizeSchemaForGemini(inner);
            schema[QStringLiteral("items")] = inner;
        }
    }
    // anyOf / oneOf / allOf children
    for (const QString &combiner : {QStringLiteral("anyOf"),
                                     QStringLiteral("oneOf"),
                                     QStringLiteral("allOf")}) {
        if (schema.contains(combiner)) {
            QJsonArray arr = schema.value(combiner).toArray();
            for (int i = 0; i < arr.size(); ++i) {
                QJsonObject child = arr[i].toObject();
                sanitizeSchemaForGemini(child);
                arr[i] = child;
            }
            schema[combiner] = arr;
        }
    }
}

// Convert OpenAI Chat-Completions tools → Gemini tools[0].functionDeclarations.
QJsonArray buildGeminiTools(const QJsonArray &tools)
{
    QJsonArray decls;
    for (const QJsonValue &tv : tools) {
        QJsonObject fn = tv.toObject().value(QStringLiteral("function")).toObject();
        if (fn.isEmpty()) continue;
        QJsonObject decl;
        decl[QStringLiteral("name")] = fn.value(QStringLiteral("name"));
        decl[QStringLiteral("description")] = fn.value(QStringLiteral("description"));
        if (fn.contains(QStringLiteral("parameters"))) {
            QJsonObject params = fn.value(QStringLiteral("parameters")).toObject();
            sanitizeSchemaForGemini(params);
            decl[QStringLiteral("parameters")] = params;
        }
        decls.append(decl);
    }
    QJsonObject toolGroup;
    toolGroup[QStringLiteral("functionDeclarations")] = decls;
    return QJsonArray{ toolGroup };
}

// Map our reasoning_effort string → Gemini thinkingBudget.
// -1 = auto/dynamic (Gemini picks), 0 = off, otherwise a token budget.
int geminiThinkingBudget(const QString &effort, bool thinkingEnabled)
{
    if (!thinkingEnabled) return 0;
    if (effort == QStringLiteral("low"))    return 2048;
    if (effort == QStringLiteral("medium")) return 8192;
    if (effort == QStringLiteral("high"))   return 24576;
    return -1; // auto
}

} // namespace

// =============================================================================
// OpenAI /v1/responses streaming
// =============================================================================
//
// The Responses API uses typed SSE events instead of Chat Completions' single
// `data: {choices:[{delta:...}]}` shape. Each event has:
//   event: <type>
//   data:  <json>
//   <blank line>
//
// Event types we care about:
//   * response.output_text.delta              — assistant text chunk
//   * response.reasoning_summary_text.delta   — thought summary chunk (LIVE)
//   * response.function_call_arguments.delta  — tool args fragment
//   * response.output_item.added              — new output item (gives item id)
//   * response.output_item.done               — item finished (full call/text)
//   * response.completed                      — final event with usage + full
//                                               response object
//   * response.failed / error                 — error
//
// We forward thought/text deltas to the UI live and accumulate function calls
// in `_responsesStreamItems`, then build a synthetic Chat-Completions response
// at the end so AgentRunner is unchanged.
void AiClient::sendStreamingMessagesResponses(const QJsonArray &messages,
                                              const QJsonArray &tools)
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
    _useResponsesApi = true;
    _streamHasTools = !tools.isEmpty();
    _hasToolsInRequest = _streamHasTools;
    _streamBuffer.clear();
    _streamContent.clear();
    _streamReasoning.clear();
    _streamToolCalls.clear();
    _responsesStreamItems.clear();
    _responsesStreamUsage = QJsonObject();

    // Arm the streaming-fallback safety net for the Responses path too.
    armStreamingRetryAgent(messages, tools);

    // Build the Responses-API body (same as sendMessages, but stream:true).
    QJsonObject body;
    body[QStringLiteral("model")] = _model;
    body[QStringLiteral("stream")] = true;

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
            input.append(m);
        }
    }
    body[QStringLiteral("input")] = input;

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

    // Same cache routing hint as the non-streaming Responses path. The
    // dynamic editor snapshot is at the end of the input, while the stable
    // developer prompt and tool schemas stay at the front for prefix hits.
    body[QStringLiteral("prompt_cache_key")] = promptCacheKeyForRequest(_model, !tools.isEmpty());

    QJsonObject reasoningObj;
    reasoningObj[QStringLiteral("effort")] = _thinkingEnabled
        ? _reasoningEffort : QStringLiteral("medium");
    reasoningObj[QStringLiteral("summary")] = QStringLiteral("auto");
    body[QStringLiteral("reasoning")] = reasoningObj;

    QByteArray data = QJsonDocument(body).toJson(QJsonDocument::Compact);

    QString url = _apiBaseUrl + QStringLiteral("/responses");
    QNetworkRequest request{QUrl(url)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(apiKey()).toUtf8());
    request.setTransferTimeout(600000); // reasoning models can take minutes

    logInstructionProfileState(QStringLiteral("STREAM-RESPONSES"), _model, messages);
    logApi(QStringLiteral("[STREAM-RESPONSES-REQ] model=%1 tools=%2 body=%3")
        .arg(_model, QString::number(tools.size()), QString::fromUtf8(data.left(2000))));

    _currentReply = _manager->post(request, data);
    disconnect(_manager, &QNetworkAccessManager::finished, this, &AiClient::onReplyFinished);
    connect(_currentReply, &QNetworkReply::readyRead, this, &AiClient::onResponsesStreamDataAvailable);
    connect(_currentReply, &QNetworkReply::finished, this, [this]() {
        if (!_streamBuffer.isEmpty())
            onResponsesStreamDataAvailable();
        QNetworkReply *reply = _currentReply;
        _currentReply = nullptr;
        _isStreaming = false;
        _useResponsesApi = false;
        connect(_manager, &QNetworkAccessManager::finished, this, &AiClient::onReplyFinished);

        if (reply->error() == QNetworkReply::OperationCanceledError) {
            logApi(QStringLiteral("[STREAM-RESPONSES-CANCEL]"));
            _streamHasTools = false;
            _streamToolCalls.clear();
            _responsesStreamItems.clear();
            _responsesStreamUsage = QJsonObject();
            clearStreamingRetryContext();
            reply->deleteLater();
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            QString detail = QString::fromUtf8(reply->readAll().left(500));
            if (shouldFallbackToNonStreaming(statusCode, reply->error(), false, false)) {
                _streamHasTools = false;
                _streamToolCalls.clear();
                _responsesStreamItems.clear();
                _responsesStreamUsage = QJsonObject();
                reply->deleteLater();
                tryStreamingFallback(QStringLiteral("Responses HTTP %1").arg(statusCode));
                return;
            }
            emit errorOccurred(tr("Streaming error (HTTP %1): %2 %3")
                .arg(statusCode).arg(reply->errorString(), detail));
            _streamHasTools = false;
            _streamToolCalls.clear();
            _responsesStreamItems.clear();
            _responsesStreamUsage = QJsonObject();
            clearStreamingRetryContext();
            reply->deleteLater();
            return;
        }

        // Build synthetic Chat-Completions response from accumulated state.
        QJsonObject responseObj;
        QJsonObject message;
        message[QStringLiteral("role")] = QStringLiteral("assistant");

        QString finishReason = QStringLiteral("stop");
        QJsonArray toolCalls;
        QList<int> indices = _responsesStreamItems.keys();
        std::sort(indices.begin(), indices.end());
        for (int idx : indices) {
            const StreamToolCall &acc = _responsesStreamItems[idx];
            if (acc.id.isEmpty() || acc.name.isEmpty()) continue;
            QJsonObject tc;
            tc[QStringLiteral("id")] = acc.id;
            tc[QStringLiteral("type")] = QStringLiteral("function");
            QJsonObject fn;
            fn[QStringLiteral("name")] = acc.name;
            fn[QStringLiteral("arguments")] = acc.arguments;
            tc[QStringLiteral("function")] = fn;
            toolCalls.append(tc);
        }

        if (!toolCalls.isEmpty()) {
            message[QStringLiteral("tool_calls")] = toolCalls;
            finishReason = QStringLiteral("tool_calls");
            message[QStringLiteral("content")] = _streamContent.isEmpty()
                ? QJsonValue(QJsonValue::Null) : QJsonValue(_streamContent);
        } else {
            message[QStringLiteral("content")] = _streamContent;
        }

        QJsonObject choice;
        choice[QStringLiteral("message")] = message;
        choice[QStringLiteral("finish_reason")] = finishReason;
        QJsonArray choices;
        choices.append(choice);
        responseObj[QStringLiteral("choices")] = choices;
        if (!_responsesStreamUsage.isEmpty())
            responseObj[QStringLiteral("usage")] = normalizeResponsesUsage(_responsesStreamUsage);

        if (!_streamReasoning.isEmpty()) {
            logApi(QStringLiteral("[STREAM-RESPONSES-REASONING] %1 chars: %2")
                .arg(_streamReasoning.size())
                .arg(_streamReasoning));
        }
        for (const QJsonValue &tcVal : toolCalls) {
            QJsonObject tc = tcVal.toObject();
            QJsonObject fn = tc.value(QStringLiteral("function")).toObject();
            logApi(QStringLiteral("[STREAM-RESPONSES-TOOLCALL] id=%1 name=%2 args=%3")
                .arg(tc.value(QStringLiteral("id")).toString(),
                     fn.value(QStringLiteral("name")).toString(),
                     fn.value(QStringLiteral("arguments")).toString()));
        }
        logApi(QStringLiteral("[STREAM-RESPONSES-DONE] chars=%1 toolCalls=%2")
            .arg(_streamContent.size()).arg(toolCalls.size()));

        if (toolCalls.isEmpty() && _streamContent.isEmpty()
            && shouldFallbackToNonStreaming(0, QNetworkReply::NoError, false, false)) {
            _streamHasTools = false;
            _streamToolCalls.clear();
            _responsesStreamItems.clear();
            _responsesStreamUsage = QJsonObject();
            reply->deleteLater();
            tryStreamingFallback(QStringLiteral("empty Responses stream"));
            return;
        }

        clearStreamingRetryContext();
        emit responseReceived(_streamContent, responseObj);

        _streamHasTools = false;
        _streamToolCalls.clear();
        _responsesStreamItems.clear();
        _responsesStreamUsage = QJsonObject();
        reply->deleteLater();
    });
}

void AiClient::onResponsesStreamDataAvailable()
{
    if (!_currentReply) return;

    _streamBuffer += _currentReply->readAll();

    // SSE events are separated by a blank line. Be tolerant of \r\n vs \n.
    while (true) {
        int idx = _streamBuffer.indexOf("\n\n");
        int sep = 2;
        int crIdx = _streamBuffer.indexOf("\r\n\r\n");
        if (crIdx >= 0 && (idx < 0 || crIdx < idx)) {
            idx = crIdx;
            sep = 4;
        }
        if (idx < 0) break;

        QByteArray chunk = _streamBuffer.left(idx);
        _streamBuffer.remove(0, idx + sep);

        QString eventType;
        QByteArray dataLine;
        for (const QByteArray &line : chunk.split('\n')) {
            QByteArray t = line.trimmed();
            if (t.startsWith("event:")) {
                eventType = QString::fromUtf8(t.mid(6).trimmed());
            } else if (t.startsWith("data:")) {
                // Concatenate multi-line data (rare here; usually single line).
                if (!dataLine.isEmpty()) dataLine += '\n';
                dataLine += t.mid(5).trimmed();
            }
        }
        if (dataLine.isEmpty() || dataLine == "[DONE]") continue;

        QJsonDocument doc = QJsonDocument::fromJson(dataLine);
        if (!doc.isObject()) continue;
        QJsonObject obj = doc.object();

        // Fall back to the `type` field if no `event:` line was present
        // (some proxies strip event lines).
        if (eventType.isEmpty())
            eventType = obj.value(QStringLiteral("type")).toString();

        if (eventType == QStringLiteral("response.output_text.delta")) {
            QString delta = obj.value(QStringLiteral("delta")).toString();
            if (!delta.isEmpty()) {
                _streamContent += delta;
                emit streamDelta(delta);
                if (_streamHasTools)
                    emit streamAssistantTextDelta(delta);
            }
        }
        else if (eventType == QStringLiteral("response.reasoning_summary_text.delta")) {
            QString delta = obj.value(QStringLiteral("delta")).toString();
            if (!delta.isEmpty()) {
                _streamReasoning += delta;
                emit streamReasoningDelta(delta);
            }
        }
        else if (eventType == QStringLiteral("response.reasoning_summary_text.done")) {
            // Insert a paragraph break between successive thought summaries
            // so multi-summary responses aren't visually fused.
            _streamReasoning += QStringLiteral("\n\n");
            emit streamReasoningDelta(QStringLiteral("\n\n"));
        }
        else if (eventType == QStringLiteral("response.output_item.added")) {
            QJsonObject item = obj.value(QStringLiteral("item")).toObject();
            QString itemType = item.value(QStringLiteral("type")).toString();
            int outputIndex = obj.value(QStringLiteral("output_index")).toInt(-1);
            if (itemType == QStringLiteral("function_call") && outputIndex >= 0) {
                StreamToolCall &acc = _responsesStreamItems[outputIndex];
                acc.id = item.value(QStringLiteral("call_id")).toString();
                acc.name = item.value(QStringLiteral("name")).toString();
                acc.arguments.clear();
                acc.started = false;
            }
        }
        else if (eventType == QStringLiteral("response.function_call_arguments.delta")) {
            int outputIndex = obj.value(QStringLiteral("output_index")).toInt(-1);
            QString delta = obj.value(QStringLiteral("delta")).toString();
            if (outputIndex >= 0 && !delta.isEmpty()) {
                StreamToolCall &acc = _responsesStreamItems[outputIndex];
                acc.arguments += delta;
            }
        }
        else if (eventType == QStringLiteral("response.output_item.done")) {
            // Authoritative full item — overwrite our accumulator so we
            // don't ship a partial JSON to the tool router.
            QJsonObject item = obj.value(QStringLiteral("item")).toObject();
            QString itemType = item.value(QStringLiteral("type")).toString();
            int outputIndex = obj.value(QStringLiteral("output_index")).toInt(-1);
            if (itemType == QStringLiteral("function_call") && outputIndex >= 0) {
                StreamToolCall &acc = _responsesStreamItems[outputIndex];
                acc.id = item.value(QStringLiteral("call_id")).toString();
                acc.name = item.value(QStringLiteral("name")).toString();
                acc.arguments = item.value(QStringLiteral("arguments")).toString();
            }
        }
        else if (eventType == QStringLiteral("response.completed")) {
            QJsonObject response = obj.value(QStringLiteral("response")).toObject();
            QJsonObject usage = response.value(QStringLiteral("usage")).toObject();
            if (usage.isEmpty())
                usage = obj.value(QStringLiteral("usage")).toObject();
            if (!usage.isEmpty())
                _responsesStreamUsage = usage;
            logApi(QStringLiteral("[STREAM-RESPONSES-EVT] completed"));
        }
        else if (eventType == QStringLiteral("response.failed")
                 || eventType == QStringLiteral("error")) {
            QString errMsg = obj.value(QStringLiteral("message")).toString();
            if (errMsg.isEmpty())
                errMsg = obj.value(QStringLiteral("error")).toObject()
                            .value(QStringLiteral("message")).toString();
            logApi(QStringLiteral("[STREAM-RESPONSES-ERR] %1").arg(errMsg));
        }
    }
}

void AiClient::sendStreamingMessagesGemini(const QJsonArray &messages,
                                           const QJsonArray &tools)
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
    _streamHasTools = !tools.isEmpty();
    _hasToolsInRequest = _streamHasTools;
    _useResponsesApi = false;
    _streamBuffer.clear();
    _streamContent.clear();
    _streamReasoning.clear();
    _streamToolCalls.clear();
    _streamGeminiCallCounter = 0;
    _streamGeminiThoughtChars = 0;
    _streamGeminiFinishReason.clear();

    // Arm the streaming-fallback safety net for the native Gemini path too.
    armStreamingRetryAgent(messages, tools);

    // Build native body
    QJsonArray contents;
    QJsonObject systemInstruction;
    buildGeminiContents(messages, contents, systemInstruction);

    QJsonObject body;
    body[QStringLiteral("contents")] = contents;
    if (!systemInstruction.isEmpty())
        body[QStringLiteral("systemInstruction")] = systemInstruction;
    if (!tools.isEmpty()) {
        body[QStringLiteral("tools")] = buildGeminiTools(tools);
        // Without an explicit toolConfig, gemini-2.5-pro with thinking enabled
        // has been observed to return finishReason=STOP with empty parts[] and
        // zero thought tokens — i.e. it doesn't even start. Forcing AUTO mode
        // makes the model commit to either answer or call a tool.
        QJsonObject fnCallCfg;
        fnCallCfg[QStringLiteral("mode")] = QStringLiteral("AUTO");
        QJsonObject toolCfg;
        toolCfg[QStringLiteral("functionCallingConfig")] = fnCallCfg;
        body[QStringLiteral("toolConfig")] = toolCfg;
    }

    QJsonObject genCfg;
    genCfg[QStringLiteral("temperature")] = 0.3;

    bool isThinkingModel = isGeminiThinkingModel();
    int thinkingBudget = 0;
    if (isThinkingModel && _thinkingEnabled) {
        QJsonObject thinkCfg;
        thinkCfg[QStringLiteral("includeThoughts")] = true;
        // Map our reasoning_effort → an explicit thinkingBudget. With tools,
        // gemini-2.5-pro at "high" (24576) has been observed to burn its
        // entire output budget on thinking and emit nothing. Cap the budget
        // when tools are present so output room remains.
        int budget = geminiThinkingBudget(_reasoningEffort, true);
        if (budget < 0) budget = 4096;          // "auto" → moderate
        if (!tools.isEmpty() && budget > 8192)  // cap when tool-calling
            budget = 8192;
        thinkCfg[QStringLiteral("thinkingBudget")] = budget;
        genCfg[QStringLiteral("thinkingConfig")] = thinkCfg;
        thinkingBudget = budget;
    }
    // Always set a generous maxOutputTokens so the model has room to actually
    // emit text/tool calls AFTER thinking. Gemini counts thinking tokens
    // separately but in practice exhausts the response budget too.
    int maxOut = (_maxTokensEnabled && _maxTokensLimit > 0) ? _maxTokensLimit : 0;
    if (maxOut <= 0) maxOut = 16384;
    if (maxOut < thinkingBudget + 4096) maxOut = thinkingBudget + 4096;
    genCfg[QStringLiteral("maxOutputTokens")] = maxOut;
    body[QStringLiteral("generationConfig")] = genCfg;

    QByteArray data = QJsonDocument(body).toJson(QJsonDocument::Compact);

    // Derive native base URL by stripping the "/openai" suffix that the
    // user has configured for the OpenAI-compat path. Default base is
    // "https://generativelanguage.googleapis.com/v1beta/openai" — strip
    // → "https://generativelanguage.googleapis.com/v1beta".
    QString nativeBase = _apiBaseUrl;
    if (nativeBase.endsWith(QStringLiteral("/openai")))
        nativeBase.chop(7);
    else if (nativeBase.endsWith(QStringLiteral("/openai/")))
        nativeBase.chop(8);

    QString url = QStringLiteral("%1/models/%2:streamGenerateContent?alt=sse&key=%3")
        .arg(nativeBase, _model, QString::fromUtf8(QUrl::toPercentEncoding(apiKey())));

    QNetworkRequest request{QUrl(url)};
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    request.setTransferTimeout(600000);

    logApi(QStringLiteral("[STREAM-GEMINI-REQ] model=%1 contents=%2 tools=%3 thinking=%4 body=%5")
        .arg(_model)
        .arg(contents.size())
        .arg(tools.size())
        .arg(isThinkingModel && _thinkingEnabled ? QStringLiteral("on") : QStringLiteral("off"),
             QString::fromUtf8(data.left(4000))));

    _currentReply = _manager->post(request, data);
    disconnect(_manager, &QNetworkAccessManager::finished,
               this, &AiClient::onReplyFinished);
    connect(_currentReply, &QNetworkReply::readyRead,
            this, &AiClient::onGeminiStreamDataAvailable);
    connect(_currentReply, &QNetworkReply::finished, this, [this]() {
        // Drain anything still on the socket (some Qt versions don't fire
        // readyRead before finished for short responses).
        if (_currentReply)
            _streamBuffer += _currentReply->readAll();
        logApi(QStringLiteral("[STREAM-GEMINI-RAW] bufferBytes=%1 head=%2")
            .arg(_streamBuffer.size())
            .arg(QString::fromUtf8(_streamBuffer.left(2000))));
        if (!_streamBuffer.isEmpty())
            onGeminiStreamDataAvailable();
        QNetworkReply *reply = _currentReply;
        _currentReply = nullptr;
        _isStreaming = false;
        connect(_manager, &QNetworkAccessManager::finished,
                this, &AiClient::onReplyFinished);

        if (reply->error() == QNetworkReply::OperationCanceledError) {
            logApi(QStringLiteral("[STREAM-GEMINI-CANCEL]"));
            _streamHasTools = false;
            _streamToolCalls.clear();
            clearStreamingRetryContext();
            reply->deleteLater();
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            int statusCode = reply->attribute(
                QNetworkRequest::HttpStatusCodeAttribute).toInt();
            // For HTTP errors the body was eaten by readyRead and lives in
            // _streamBuffer (Gemini sends the error JSON the same way as a
            // single SSE event). Try _streamBuffer first, fall back to
            // anything still on the reply.
            QByteArray bodySnippet = _streamBuffer.left(800);
            if (bodySnippet.isEmpty())
                bodySnippet = reply->readAll().left(800);
            QString detail = QString::fromUtf8(bodySnippet).trimmed();
            // Strip leading "data: " if Gemini wrapped the error as SSE
            if (detail.startsWith(QStringLiteral("data: ")))
                detail = detail.mid(6).trimmed();
            logApi(QStringLiteral("[STREAM-GEMINI-ERR] http=%1 reply=%2 body=%3")
                .arg(statusCode).arg(reply->errorString(), detail));
            // Streaming-fallback safety net for misbehaving Gemini-compat
            // endpoints. Note: when Gemini returns a real semantic error (e.g.
            // SAFETY/MAX_TOKENS) it does so over HTTP 200, so HTTP 4xx/5xx
            // here genuinely means "the streaming round trip itself broke".
            if (shouldFallbackToNonStreaming(statusCode, reply->error(), false, false)) {
                _streamBuffer.clear();
                _streamHasTools = false;
                _streamToolCalls.clear();
                reply->deleteLater();
                tryStreamingFallback(QStringLiteral("Gemini HTTP %1").arg(statusCode));
                return;
            }
            emit errorOccurred(tr("Gemini streaming error (HTTP %1): %2\n%3")
                .arg(statusCode).arg(reply->errorString(), detail));
            _streamBuffer.clear();
            _streamHasTools = false;
            _streamToolCalls.clear();
            clearStreamingRetryContext();
            reply->deleteLater();
            return;
        }

        // Reassemble synthetic Chat-Completions response for AgentRunner
        QJsonObject responseObj;
        QJsonObject message;
        message[QStringLiteral("role")] = QStringLiteral("assistant");

        QString finishReason = QStringLiteral("stop");
        QJsonArray toolCalls;
        if (!_streamToolCalls.isEmpty()) {
            QList<int> indices = _streamToolCalls.keys();
            std::sort(indices.begin(), indices.end());
            for (int idx : indices) {
                const StreamToolCall &acc = _streamToolCalls[idx];
                if (acc.id.isEmpty() || acc.name.isEmpty()) continue;
                QJsonObject tc;
                tc[QStringLiteral("id")] = acc.id;
                tc[QStringLiteral("type")] = QStringLiteral("function");
                QJsonObject fn;
                fn[QStringLiteral("name")] = acc.name;
                fn[QStringLiteral("arguments")] = acc.arguments;
                tc[QStringLiteral("function")] = fn;
                // Non-standard field — Gemini 3.x requires the opaque
                // thought signature to be echoed back on follow-up requests.
                // buildGeminiContents() reads this back out when converting
                // the assistant turn to Gemini's contents[] shape.
                if (!acc.thoughtSignature.isEmpty())
                    tc[QStringLiteral("_gemini_thought_signature")] = acc.thoughtSignature;
                toolCalls.append(tc);
            }
        }

        if (!toolCalls.isEmpty()) {
            message[QStringLiteral("tool_calls")] = toolCalls;
            finishReason = QStringLiteral("tool_calls");
            message[QStringLiteral("content")] = _streamContent.isEmpty()
                ? QJsonValue(QJsonValue::Null) : QJsonValue(_streamContent);
        } else {
            message[QStringLiteral("content")] = _streamContent;
        }

        QJsonObject choice;
        choice[QStringLiteral("message")] = message;
        choice[QStringLiteral("finish_reason")] = finishReason;
        QJsonArray choices;
        choices.append(choice);
        responseObj[QStringLiteral("choices")] = choices;

        if (!_streamReasoning.isEmpty()) {
            logApi(QStringLiteral("[STREAM-GEMINI-REASONING] %1 chars: %2")
                .arg(_streamReasoning.size())
                .arg(_streamReasoning));
        }
        for (const QJsonValue &tcVal : toolCalls) {
            QJsonObject tc = tcVal.toObject();
            QJsonObject fn = tc.value(QStringLiteral("function")).toObject();
            logApi(QStringLiteral("[STREAM-GEMINI-TOOLCALL] id=%1 name=%2 args=%3")
                .arg(tc.value(QStringLiteral("id")).toString(),
                     fn.value(QStringLiteral("name")).toString(),
                     fn.value(QStringLiteral("arguments")).toString()));
        }
        logApi(QStringLiteral("[STREAM-GEMINI-DONE] chars=%1 toolCalls=%2 thoughtChars=%3 finish=%4")
            .arg(_streamContent.size()).arg(toolCalls.size())
            .arg(_streamGeminiThoughtChars)
            .arg(_streamGeminiFinishReason.isEmpty()
                 ? QStringLiteral("STOP") : _streamGeminiFinishReason));

        // If Gemini finished with an error reason and produced no usable
        // output, surface it as an error so AgentRunner doesn't fall through
        // to "Done." with an empty assistant message.
        if (toolCalls.isEmpty() && _streamContent.isEmpty()) {
            const QString reason = _streamGeminiFinishReason.isEmpty()
                ? QStringLiteral("STOP") : _streamGeminiFinishReason;
            // Try the streaming-fallback safety net first — maybe the
            // streaming endpoint is broken but the regular endpoint works.
            // Skip for known semantic finishes where retrying won't help.
            bool semanticFinish = reason.contains(QStringLiteral("MALFORMED"), Qt::CaseInsensitive)
                || reason == QStringLiteral("SAFETY")
                || reason == QStringLiteral("RECITATION")
                || reason == QStringLiteral("MAX_TOKENS");
            if (!semanticFinish
                && shouldFallbackToNonStreaming(0, QNetworkReply::NoError, false, false)) {
                _streamHasTools = false;
                _streamToolCalls.clear();
                reply->deleteLater();
                tryStreamingFallback(QStringLiteral("empty Gemini stream (%1)").arg(reason));
                return;
            }
            QString hint;
            if (reason.contains(QStringLiteral("MALFORMED"), Qt::CaseInsensitive))
                hint = tr(" (Gemini produced an invalid function call — try a different model or simpler prompt)");
            else if (reason == QStringLiteral("MAX_TOKENS"))
                hint = tr(" (response cut off — increase max tokens or reduce request size)");
            else if (reason == QStringLiteral("SAFETY") || reason == QStringLiteral("RECITATION"))
                hint = tr(" (blocked by Gemini safety filter)");
            else if (reason == QStringLiteral("STOP") && _streamGeminiThoughtChars == 0
                     && _thinkingEnabled && isGeminiThinkingModel())
                hint = tr(" (model spent its budget on hidden thinking — try disabling thinking or lowering reasoning effort)");
            else if (reason == QStringLiteral("STOP"))
                hint = tr(" (the model returned no text and no tool call — try rephrasing the prompt)");
            emit errorOccurred(tr("Gemini ended without output (finishReason=%1)%2")
                .arg(reason, hint));
            _streamHasTools = false;
            _streamToolCalls.clear();
            clearStreamingRetryContext();
            reply->deleteLater();
            return;
        }

        clearStreamingRetryContext();
        emit responseReceived(_streamContent, responseObj);

        _streamHasTools = false;
        _streamToolCalls.clear();
        reply->deleteLater();
    });
}

void AiClient::onGeminiStreamDataAvailable()
{
    if (!_currentReply) return;

    _streamBuffer += _currentReply->readAll();

    while (true) {
        int startIdx = _streamBuffer.indexOf("data: ");
        if (startIdx < 0) {
            if (_currentReply->isFinished())
                _streamBuffer.clear();
            break;
        }

        // Find the END of the current data block.
        int rnIdx = _streamBuffer.indexOf("\r\n\r\n", startIdx);
        int nnIdx = _streamBuffer.indexOf("\n\n", startIdx);
        
        int endIdx = -1;
        int sepLen = 0;
        if (nnIdx >= 0 && (rnIdx < 0 || nnIdx < rnIdx)) {
            endIdx = nnIdx;
            sepLen = 2;
        } else if (rnIdx >= 0) {
            endIdx = rnIdx;
            sepLen = 4;
        }

        QByteArray chunk;
        if (endIdx < 0) {
            if (_currentReply->isFinished()) {
                chunk = _streamBuffer.mid(startIdx);
                _streamBuffer.clear();
            } else {
                break;
            }
        } else {
            chunk = _streamBuffer.mid(startIdx, endIdx - startIdx);
            // Drop everything up to and including the separator
            _streamBuffer.remove(0, endIdx + sepLen);
        }

        QByteArray payload;
        for (const QByteArray &line : chunk.split('\n')) {
            QByteArray trimmed = line.trimmed();
            if (trimmed.startsWith("data: "))
                payload += trimmed.mid(6);
            else if (!trimmed.isEmpty())
                payload += trimmed;
        }
        if (payload.isEmpty() || payload == "[DONE]") continue;

        QJsonDocument doc = QJsonDocument::fromJson(payload);
        if (!doc.isObject()) continue;
        QJsonObject obj = doc.object();

        // Diagnostic: log raw chunk so we can see promptFeedback / blockReason
        // on otherwise-empty STOP responses.
        logApi(QStringLiteral("[STREAM-GEMINI-CHUNK] %1")
            .arg(QString::fromUtf8(payload.left(2000))));

        QJsonArray cands = obj.value(QStringLiteral("candidates")).toArray();
        if (cands.isEmpty()) continue;
        QJsonObject cand = cands[0].toObject();
        QJsonObject content = cand.value(QStringLiteral("content")).toObject();
        QJsonArray parts = content.value(QStringLiteral("parts")).toArray();

        // Surface non-stop finish reasons (MALFORMED_FUNCTION_CALL, MAX_TOKENS,
        // SAFETY, RECITATION, …). Gemini emits these on the final chunk along
        // with empty `parts`, which would otherwise silently produce an empty
        // assistant message that AgentRunner reports as "Done.".
        QString finish = cand.value(QStringLiteral("finishReason")).toString();
        if (!finish.isEmpty() && finish != QStringLiteral("STOP")
            && finish != QStringLiteral("FINISH_REASON_UNSPECIFIED")) {
            _streamGeminiFinishReason = finish;
            logApi(QStringLiteral("[STREAM-GEMINI-FINISH] %1").arg(finish));
        }

        for (const QJsonValue &pv : parts) {
            QJsonObject part = pv.toObject();

            // Function call — comes whole, not as deltas
            if (part.contains(QStringLiteral("functionCall"))) {
                QJsonObject fc = part.value(QStringLiteral("functionCall")).toObject();
                QString name = fc.value(QStringLiteral("name")).toString();
                if (name.isEmpty()) continue;
                QJsonObject args = fc.value(QStringLiteral("args")).toObject();
                QString argsJson = QString::fromUtf8(
                    QJsonDocument(args).toJson(QJsonDocument::Compact));

                int idx = _streamGeminiCallCounter++;
                StreamToolCall &acc = _streamToolCalls[idx];
                acc.id = QStringLiteral("gemini_%1").arg(idx);
                acc.name = name;
                acc.arguments = argsJson;
                acc.started = true;
                // Gemini 3.x: capture the signature so it can be echoed back
                // on the follow-up request (required by the API).
                acc.thoughtSignature = part.value(
                    QStringLiteral("thoughtSignature")).toString();

                emit streamToolCallStarted(acc.id, acc.name);
                emit streamToolCallArgsDelta(acc.id, argsJson);
                emit streamToolCallArgsDone(acc.id);
                continue;
            }

            // Text part — could be a thought or actual content
            if (part.contains(QStringLiteral("text"))) {
                QString text = part.value(QStringLiteral("text")).toString();
                if (text.isEmpty()) continue;
                bool isThought = part.value(QStringLiteral("thought")).toBool();
                if (isThought) {
                    _streamGeminiThoughtChars += text.size();
                    _streamReasoning += text;
                    emit streamReasoningDelta(text);
                } else {
                    _streamContent += text;
                    emit streamDelta(text);
                    if (_streamHasTools)
                        emit streamAssistantTextDelta(text);
                }
            }
        }

        // Usage metadata appears on the final chunk
        if (obj.contains(QStringLiteral("usageMetadata"))) {
            logApi(QStringLiteral("[STREAM-GEMINI-USAGE] %1")
                .arg(QString::fromUtf8(QJsonDocument(
                    obj.value(QStringLiteral("usageMetadata")).toObject()
                ).toJson(QJsonDocument::Compact))));
        }
    }
}
