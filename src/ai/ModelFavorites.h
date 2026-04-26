#ifndef MODELFAVORITES_H
#define MODELFAVORITES_H

#include <QJsonArray>
#include <QSet>
#include <QString>
#include <QStringList>

/**
 * \class ModelFavorites
 *
 * \brief Per-provider favourite-model selection + non-LLM model filter.
 *
 * Favourites are stored in QSettings under \c "AI/favorites/<provider>" as a
 * QStringList of model ids. If a provider has at least one favourite, only
 * those are surfaced in the model dropdowns. If none are set, all models from
 * the cache that survive \ref isLikelyChatModel() are shown.
 *
 * \ref isLikelyChatModel() is a defensive heuristic that drops obvious
 * non-text-generation models (image, video, audio, embedding, moderation,
 * tts, transcription) regardless of provider. Existing per-provider filters
 * in ModelListFetcher remain — this is a second-line filter that also
 * applies to entries already cached on disk before this version.
 */
class ModelFavorites {
public:
    /** Returns the set of favourite model ids for the given provider. */
    static QSet<QString> favorites(const QString &provider);

    /** Replaces the favourite set for the given provider. */
    static void setFavorites(const QString &provider, const QStringList &ids);

    /** True if the provider has any favourites set. */
    static bool hasFavorites(const QString &provider);

    /**
     * \brief Heuristic: returns true if the entry looks like a text/chat LLM.
     *
     * Rejects ids containing image / video / audio / tts / whisper / embed /
     * moderation / dall-e / vision-only / clip / speech / transcribe markers.
     * Errs on the side of *keeping* — returns true for unknown ids so a brand
     * new model isn't silently hidden.
     */
    static bool isLikelyChatModel(const QString &modelId);

    /**
     * \brief Filters the cache array down to chat models, optionally further
     *        restricted to favourites if any are set.
     */
    static QJsonArray visibleModels(const QString &provider,
                                    const QJsonArray &cached);
};

#endif // MODELFAVORITES_H
