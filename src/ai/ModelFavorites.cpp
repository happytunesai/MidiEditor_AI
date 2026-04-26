#include "ModelFavorites.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QSettings>

namespace {
QSettings *settings()
{
    static QSettings s(QStringLiteral("MidiEditor"), QStringLiteral("NONE"));
    return &s;
}

QString settingsKey(const QString &provider)
{
    return QStringLiteral("AI/favorites/") + provider;
}
} // namespace

QSet<QString> ModelFavorites::favorites(const QString &provider)
{
    const QStringList list =
        settings()->value(settingsKey(provider)).toStringList();
    return QSet<QString>(list.cbegin(), list.cend());
}

void ModelFavorites::setFavorites(const QString &provider, const QStringList &ids)
{
    if (ids.isEmpty())
        settings()->remove(settingsKey(provider));
    else
        settings()->setValue(settingsKey(provider), ids);
}

bool ModelFavorites::hasFavorites(const QString &provider)
{
    return !favorites(provider).isEmpty();
}

bool ModelFavorites::isLikelyChatModel(const QString &modelId)
{
    if (modelId.isEmpty())
        return false;

    // Defensive deny-list. Anything matching is treated as a non-LLM
    // (image/audio/video/embedding) model regardless of provider.
    static const QRegularExpression deny(
        QStringLiteral(
            "(?:^|[-/_])("
            "embedding|embed|"
            "tts|speech|whisper|transcribe|transcription|audio|voice|"
            "vision|image|images|img|imagen|dall-?e|stable-?diffusion|sd[0-9]?|"
            "video|veo|sora|"
            "clip|"
            "moderation|safety|guard|"
            "bge|cohere-embed|text-embedding"
            ")(?:[-/_]|$)"),
        QRegularExpression::CaseInsensitiveOption);
    if (deny.match(modelId).hasMatch())
        return false;
    return true;
}

QJsonArray ModelFavorites::visibleModels(const QString &provider,
                                         const QJsonArray &cached)
{
    const QSet<QString> favs = favorites(provider);
    const bool useFavs = !favs.isEmpty();

    QJsonArray out;
    for (const QJsonValue &v : cached) {
        const QJsonObject m = v.toObject();
        const QString id = m.value(QStringLiteral("id")).toString();
        if (id.isEmpty())
            continue;
        if (!isLikelyChatModel(id))
            continue;
        if (useFavs && !favs.contains(id))
            continue;
        out.append(m);
    }
    return out;
}
