#ifndef MODELLISTCACHE_H
#define MODELLISTCACHE_H

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>

/**
 * \class ModelListCache
 *
 * \brief On-disk cache of provider model lists.
 *
 * Stores per-provider model lists (id, display name, context window, capability
 * flags) in a single JSON file under the application data directory
 * (\c "<userdata>/midipilot_models.json"). Used by AiSettingsWidget,
 * MidiPilotWidget and AiClient::contextWindowForModel to avoid hardcoded
 * model lists.
 *
 * Schema per entry (each object inside the \c "models" array):
 * \code
 * {
 *   "id": "gpt-5.4",
 *   "displayName": "GPT-5.4",
 *   "contextWindow": 1000000,
 *   "supportsTools": true,
 *   "supportsReasoning": true
 * }
 * \endcode
 *
 * TTL is 7 days. The cache is intentionally synchronous and tiny: the file
 * never exceeds a few KB because we keep at most a few hundred model entries
 * across all providers.
 */
class ModelListCache {
public:
    /** Cache version. Bump to invalidate older on-disk caches. */
    static constexpr int CACHE_VERSION = 1;

    /** TTL after which a provider entry is considered stale. */
    static constexpr int TTL_DAYS = 7;

    /**
     * \brief Returns the absolute path to the cache file.
     */
    static QString cacheFilePath();

    /**
     * \brief Returns the cached model list for the given provider.
     * \return JSON array of model objects (empty if no cache or wrong version)
     */
    static QJsonArray models(const QString &provider);

    /**
     * \brief Returns the timestamp of the last successful fetch for the provider.
     * \return Invalid QDateTime if never fetched.
     */
    static QDateTime lastFetched(const QString &provider);

    /**
     * \brief True if the cached entry for the provider is older than TTL_DAYS
     *        or has never been fetched.
     */
    static bool isStale(const QString &provider);

    /**
     * \brief Replaces the cached entry for the provider with the given list.
     *
     * Saves to disk synchronously. The caller is expected to have already
     * normalised the entries to the schema documented above.
     */
    static void store(const QString &provider, const QJsonArray &models);

    /**
     * \brief Returns the context window declared in the cache for the given
     *        model id, or 0 if unknown.
     */
    static int contextWindowFor(const QString &modelId);

private:
    static QJsonObject readFile();
    static void writeFile(const QJsonObject &obj);
};

#endif // MODELLISTCACHE_H
