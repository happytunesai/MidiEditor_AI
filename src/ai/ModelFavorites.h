#ifndef MODELFAVORITES_H
#define MODELFAVORITES_H

#include <QJsonArray>
#include <QSet>
#include <QString>
#include <QStringList>

/**
 * \class ModelFavorites
 *
 * \brief Per-scope favourite-model selection + non-LLM model filter.
 *
 * Favourites are stored in QSettings under \c "AI/favorites/<scope>" as a
 * QStringList of model ids. If a scope has at least one favourite, only
 * those are surfaced in the model dropdowns. If none are set, all models from
 * the cache that survive \ref isLikelyChatModel() are shown.
 *
 * A \e scope is a provider id ("openai", "gemini", ...) for the built-in
 * providers and for ad-hoc Custom settings, or \c "custom:profile:<id>" while
 * a stored Custom provider profile describes the endpoint. Callers resolve it
 * with ProviderProfileStore::modelScopeId(); this class only ever sees the
 * resulting string, which is why it carries no dependency on the profile
 * store. Two Custom endpoints therefore never share favourites, and
 * favourites written before profiles existed stay under the plain "custom"
 * scope.
 *
 * \ref isLikelyChatModel() is a defensive heuristic that drops obvious
 * non-text-generation models (image, video, audio, embedding, moderation,
 * tts, transcription) regardless of provider. Existing per-provider filters
 * in ModelListFetcher remain — this is a second-line filter that also
 * applies to entries already cached on disk before this version.
 */
class ModelFavorites {
public:
    /** Returns the set of favourite model ids for the given scope. */
    static QSet<QString> favorites(const QString &scope);

    /** Replaces the favourite set for the given scope. */
    static void setFavorites(const QString &scope, const QStringList &ids);

    /** True if the scope has any favourites set. */
    static bool hasFavorites(const QString &scope);

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
    static QJsonArray visibleModels(const QString &scope,
                                    const QJsonArray &cached);
};

#endif // MODELFAVORITES_H
