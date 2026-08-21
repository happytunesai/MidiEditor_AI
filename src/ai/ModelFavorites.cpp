#include "ModelFavorites.h"

#include "../AppPaths.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QSettings>

namespace {
// Phase 45 / PORTABLE-SPLIT-001: through AppPaths (the old function-static
// hard-wired the registry - portable installs forked favourites per machine).
std::unique_ptr<QSettings> settings()
{
    return AppPaths::settings();
}

// The scope is an opaque string here (see the class comment): a provider id,
// or "custom:profile:<id>" for a stored custom endpoint.
QString settingsKey(const QString &scope)
{
    return QStringLiteral("AI/favorites/") + scope;
}
} // namespace

QSet<QString> ModelFavorites::favorites(const QString &scope)
{
    const QStringList list =
        settings()->value(settingsKey(scope)).toStringList();
    return QSet<QString>(list.cbegin(), list.cend());
}

void ModelFavorites::setFavorites(const QString &scope, const QStringList &ids)
{
    if (ids.isEmpty())
        settings()->remove(settingsKey(scope));
    else
        settings()->setValue(settingsKey(scope), ids);
}

bool ModelFavorites::hasFavorites(const QString &scope)
{
    return !favorites(scope).isEmpty();
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

QJsonArray ModelFavorites::visibleModels(const QString &scope,
                                         const QJsonArray &cached)
{
    const QSet<QString> favs = favorites(scope);
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
