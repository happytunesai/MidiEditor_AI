#include "ModelListFetcher.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLocale>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QStringList>
#include <QUrl>
#include <QUrlQuery>

ModelListFetcher::ModelListFetcher(QObject *parent)
    : QObject(parent), _manager(new QNetworkAccessManager(this)), _reply(nullptr)
{
}

void ModelListFetcher::fetch(const QString &provider,
                             const QString &apiKey,
                             const QString &baseUrl)
{
    _provider = provider;

    QUrl url;
    QNetworkRequest req;

    if (provider == QStringLiteral("openai")) {
        url = QUrl(QStringLiteral("https://api.openai.com/v1/models"));
        req.setRawHeader("Authorization", QByteArray("Bearer ") + apiKey.toUtf8());
    } else if (provider == QStringLiteral("openrouter")) {
        url = QUrl(QStringLiteral("https://openrouter.ai/api/v1/models"));
        if (!apiKey.isEmpty())
            req.setRawHeader("Authorization", QByteArray("Bearer ") + apiKey.toUtf8());
    } else if (provider == QStringLiteral("gemini")) {
        url = QUrl(QStringLiteral("https://generativelanguage.googleapis.com/v1beta/models"));
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("key"), apiKey);
        url.setQuery(q);
    } else if (provider == QStringLiteral("ollama")) {
        // Phase 26.2c: use Ollama's native /api/tags (NOT the OpenAI-compat
        // /v1/models, which returns only {id}). /api/tags lists the user's
        // installed models with size + details, so the picker can show real
        // download sizes and flag tool-capable models. The configured base URL
        // is the /v1 compat endpoint (e.g. http://localhost:11434/v1); strip the
        // /v1 suffix to reach the native API root.
        QString host = baseUrl.trimmed();
        if (host.isEmpty())
            host = QStringLiteral("http://localhost:11434");
        if (host.endsWith('/'))
            host.chop(1);
        if (host.endsWith(QStringLiteral("/v1")))
            host.chop(3);
        url = QUrl(host + QStringLiteral("/api/tags"));
        // Local server, normally keyless; forward a key only if the user set one.
        if (!apiKey.isEmpty())
            req.setRawHeader("Authorization", QByteArray("Bearer ") + apiKey.toUtf8());
    } else { // custom
        QString b = baseUrl.trimmed();
        if (b.isEmpty()) {
            emit failed(provider, tr("No base URL configured for Custom provider"));
            deleteLater();
            return;
        }
        if (b.endsWith('/'))
            b.chop(1);
        url = QUrl(b + QStringLiteral("/models"));
        if (!apiKey.isEmpty())
            req.setRawHeader("Authorization", QByteArray("Bearer ") + apiKey.toUtf8());
    }

    req.setUrl(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    _reply = _manager->get(req);
    connect(_reply, &QNetworkReply::finished, this, &ModelListFetcher::onReplyFinished);
}

void ModelListFetcher::onReplyFinished()
{
    if (!_reply) {
        deleteLater();
        return;
    }

    QByteArray body = _reply->readAll();
    QNetworkReply::NetworkError netErr = _reply->error();
    int httpStatus = _reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QString netErrStr = _reply->errorString();
    _reply->deleteLater();
    _reply = nullptr;

    if (netErr != QNetworkReply::NoError) {
        emit failed(_provider, tr("Network error: %1").arg(netErrStr));
        deleteLater();
        return;
    }
    if (httpStatus >= 400) {
        emit failed(_provider, tr("HTTP %1: %2").arg(httpStatus).arg(QString::fromUtf8(body.left(200))));
        deleteLater();
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(body);
    if (!doc.isObject()) {
        emit failed(_provider, tr("Invalid JSON response"));
        deleteLater();
        return;
    }

    QJsonObject root = doc.object();
    QJsonArray rawArr;
    // OpenAI / OpenRouter / Custom shape: { "data": [...] }
    if (root.contains(QStringLiteral("data")))
        rawArr = root.value(QStringLiteral("data")).toArray();
    // Gemini shape: { "models": [...] }
    else if (root.contains(QStringLiteral("models")))
        rawArr = root.value(QStringLiteral("models")).toArray();

    QJsonArray normalised;
    if (_provider == QStringLiteral("openai"))
        normalised = normaliseOpenAi(rawArr);
    else if (_provider == QStringLiteral("openrouter"))
        normalised = normaliseOpenRouter(rawArr);
    else if (_provider == QStringLiteral("gemini"))
        normalised = normaliseGemini(rawArr);
    else if (_provider == QStringLiteral("ollama"))
        normalised = normaliseOllama(rawArr);
    else
        normalised = normaliseCustom(rawArr);

    if (normalised.isEmpty()) {
        emit failed(_provider, tr("No usable chat models in response"));
        deleteLater();
        return;
    }

    emit finished(_provider, normalised);
    deleteLater();
}

QJsonArray ModelListFetcher::normaliseOpenAi(const QJsonArray &raw) const
{
    static const QRegularExpression keep(
        QStringLiteral("^(gpt-|chatgpt-|o[1-9])"));
    static const QRegularExpression drop(
        QStringLiteral("(embedding|whisper|tts|audio|image|moderation|davinci|babbage|"
                       "dall-e|search|realtime|transcribe|preview-\\d{4}-\\d{2}-\\d{2})"));

    QJsonArray out;
    for (const QJsonValue &v : raw) {
        QJsonObject m = v.toObject();
        QString id = m.value(QStringLiteral("id")).toString();
        if (id.isEmpty())
            continue;
        if (!keep.match(id).hasMatch())
            continue;
        if (drop.match(id).hasMatch())
            continue;

        QJsonObject n;
        n.insert(QStringLiteral("id"), id);
        n.insert(QStringLiteral("displayName"), id);
        n.insert(QStringLiteral("contextWindow"), contextWindowFromId(id));
        n.insert(QStringLiteral("supportsTools"), true);
        n.insert(QStringLiteral("supportsReasoning"),
                 id.startsWith(QStringLiteral("o")) || id.startsWith(QStringLiteral("gpt-5")));
        out.append(n);
    }
    return out;
}

QJsonArray ModelListFetcher::normaliseOpenRouter(const QJsonArray &raw) const
{
    QJsonArray out;
    for (const QJsonValue &v : raw) {
        QJsonObject m = v.toObject();
        QString id = m.value(QStringLiteral("id")).toString();
        if (id.isEmpty())
            continue;

        // Filter to text->text architectures only
        QJsonObject arch = m.value(QStringLiteral("architecture")).toObject();
        QJsonArray inMods = arch.value(QStringLiteral("input_modalities")).toArray();
        QJsonArray outMods = arch.value(QStringLiteral("output_modalities")).toArray();
        bool hasTextIn = inMods.isEmpty() || inMods.contains(QJsonValue(QStringLiteral("text")));
        bool hasTextOut = outMods.isEmpty() || outMods.contains(QJsonValue(QStringLiteral("text")));
        if (!hasTextIn || !hasTextOut)
            continue;

        QString name = m.value(QStringLiteral("name")).toString();
        int cw = m.value(QStringLiteral("context_length")).toInt(0);
        if (cw <= 0)
            cw = contextWindowFromId(id);

        QJsonObject n;
        n.insert(QStringLiteral("id"), id);
        n.insert(QStringLiteral("displayName"), name.isEmpty() ? id : name);
        n.insert(QStringLiteral("contextWindow"), cw);
        n.insert(QStringLiteral("supportsTools"),
                 m.value(QStringLiteral("supported_parameters"))
                     .toArray().contains(QJsonValue(QStringLiteral("tools"))));
        n.insert(QStringLiteral("supportsReasoning"),
                 m.value(QStringLiteral("supported_parameters"))
                     .toArray().contains(QJsonValue(QStringLiteral("reasoning"))));
        out.append(n);
    }
    return out;
}

QJsonArray ModelListFetcher::normaliseGemini(const QJsonArray &raw) const
{
    QJsonArray out;
    for (const QJsonValue &v : raw) {
        QJsonObject m = v.toObject();
        QString fullName = m.value(QStringLiteral("name")).toString(); // e.g. "models/gemini-2.5-pro"
        if (fullName.isEmpty())
            continue;
        QString id = fullName.startsWith(QStringLiteral("models/"))
                         ? fullName.mid(7)
                         : fullName;

        QJsonArray methods = m.value(QStringLiteral("supportedGenerationMethods")).toArray();
        if (!methods.contains(QJsonValue(QStringLiteral("generateContent"))))
            continue;
        if (id.contains(QStringLiteral("embedding"), Qt::CaseInsensitive))
            continue;
        if (id.contains(QStringLiteral("aqa"), Qt::CaseInsensitive))
            continue;

        QString display = m.value(QStringLiteral("displayName")).toString();
        int cw = m.value(QStringLiteral("inputTokenLimit")).toInt(0);
        if (cw <= 0)
            cw = contextWindowFromId(id);

        QJsonObject n;
        n.insert(QStringLiteral("id"), id);
        n.insert(QStringLiteral("displayName"), display.isEmpty() ? id : display);
        n.insert(QStringLiteral("contextWindow"), cw);
        n.insert(QStringLiteral("supportsTools"), true);
        n.insert(QStringLiteral("supportsReasoning"),
                 id.contains(QStringLiteral("thinking"), Qt::CaseInsensitive)
                     || id.startsWith(QStringLiteral("gemini-2.5"))
                     || id.startsWith(QStringLiteral("gemini-3")));
        out.append(n);
    }
    return out;
}

QJsonArray ModelListFetcher::normaliseOllama(const QJsonArray &raw)
{
    QJsonArray out;
    for (const QJsonValue &v : raw) {
        QJsonObject m = v.toObject();
        // /api/tags uses "name" (e.g. "qwen3.6:latest"); fall back to "model".
        QString id = m.value(QStringLiteral("name")).toString();
        if (id.isEmpty())
            id = m.value(QStringLiteral("model")).toString();
        if (id.isEmpty())
            continue;

        QJsonObject details = m.value(QStringLiteral("details")).toObject();

        // Capabilities may live at the top level or under details depending on
        // the Ollama version; older /api/tags omits them entirely. When absent,
        // default supportsTools=true so Agent Mode isn't falsely blocked (the
        // user can still pick the model; a non-tool model just won't tool-call).
        QJsonArray caps = m.value(QStringLiteral("capabilities")).toArray();
        if (caps.isEmpty())
            caps = details.value(QStringLiteral("capabilities")).toArray();
        bool hasCaps = !caps.isEmpty();

        // When capabilities are known, drop models that can't do chat completion
        // (e.g. embedding models like nomic-embed-text) so they don't clutter the
        // chat-model picker. Without capabilities we can't tell, so keep them.
        if (hasCaps && !caps.contains(QJsonValue(QStringLiteral("completion"))))
            continue;

        bool supportsTools = hasCaps
            ? caps.contains(QJsonValue(QStringLiteral("tools")))
            : true;
        bool supportsReasoning = hasCaps
            && caps.contains(QJsonValue(QStringLiteral("thinking")));

        // Build a readable label: "qwen3.6:latest (36.0B, 22.0 GB)". Use Qt's
        // data-size formatter (SI units, matching how Ollama itself reports size).
        QString paramSize = details.value(QStringLiteral("parameter_size")).toString();
        qint64 bytes = static_cast<qint64>(m.value(QStringLiteral("size")).toDouble());
        QStringList badges;
        if (!paramSize.isEmpty())
            badges << paramSize;
        if (bytes > 0)
            badges << QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeSIFormat);
        QString display = badges.isEmpty()
            ? id
            : QStringLiteral("%1 (%2)").arg(id, badges.join(QStringLiteral(", ")));

        // context_length is only present on some versions; 0 -> fall back to default.
        int cw = details.value(QStringLiteral("context_length")).toInt(0);

        QJsonObject n;
        n.insert(QStringLiteral("id"), id);
        n.insert(QStringLiteral("displayName"), display);
        n.insert(QStringLiteral("contextWindow"), cw);
        n.insert(QStringLiteral("supportsTools"), supportsTools);
        n.insert(QStringLiteral("supportsReasoning"), supportsReasoning);
        out.append(n);
    }
    return out;
}

QJsonArray ModelListFetcher::normaliseCustom(const QJsonArray &raw) const
{
    QJsonArray out;
    for (const QJsonValue &v : raw) {
        QJsonObject m = v.toObject();
        QString id = m.value(QStringLiteral("id")).toString();
        if (id.isEmpty())
            continue;
        QJsonObject n;
        n.insert(QStringLiteral("id"), id);
        n.insert(QStringLiteral("displayName"), id);
        // Local servers rarely declare context length; leave 0 → fall back to default
        n.insert(QStringLiteral("contextWindow"), 0);
        n.insert(QStringLiteral("supportsTools"), true);
        n.insert(QStringLiteral("supportsReasoning"), false);
        out.append(n);
    }
    return out;
}

int ModelListFetcher::contextWindowFromId(const QString &id)
{
    QString m = id.toLower();
    if (m.contains(QStringLiteral("gpt-5")))     return 1000000;
    if (m.contains(QStringLiteral("gpt-4o")))    return 128000;
    if (m.contains(QStringLiteral("gpt-4.1")))   return 1000000;
    if (m.contains(QStringLiteral("o4-mini")))   return 200000;
    if (m.contains(QStringLiteral("o3")))        return 200000;
    if (m.contains(QStringLiteral("o1")))        return 200000;
    if (m.contains(QStringLiteral("claude-4")))  return 200000;
    if (m.contains(QStringLiteral("claude-3")))  return 200000;
    if (m.contains(QStringLiteral("gemini-3")))  return 1000000;
    if (m.contains(QStringLiteral("gemini-2")))  return 1000000;
    if (m.contains(QStringLiteral("gemini-1.5"))) return 1000000;
    return 0;
}
