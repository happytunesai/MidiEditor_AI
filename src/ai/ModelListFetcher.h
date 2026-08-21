#ifndef MODELLISTFETCHER_H
#define MODELLISTFETCHER_H

#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QString>

/**
 * \class ModelListFetcher
 *
 * \brief Fetches the model list for a given provider and normalises the
 *        response into the schema documented in ModelListCache.
 *
 * Per-provider endpoints:
 *  - openai     GET https://api.openai.com/v1/models
 *  - openrouter GET https://openrouter.ai/api/v1/models  (no auth needed)
 *  - gemini     GET https://generativelanguage.googleapis.com/v1beta/models?key=...
 *  - ollama     GET <host>/api/tags   (installed models + size/capabilities)
 *  - custom     GET <baseUrl>/models   (OpenAI-compatible)
 *
 * Emits \c finished on success (with the normalised array) or \c failed on
 * any kind of error (network, HTTP non-2xx, JSON parse). The fetcher is
 * single-shot: it deletes itself after emitting either signal.
 */
class ModelListFetcher : public QObject {
    Q_OBJECT
public:
    explicit ModelListFetcher(QObject *parent = nullptr);

    /**
     * \brief Starts the fetch.
     * \param provider one of "openai" / "openrouter" / "gemini" / "ollama" / "custom"
     * \param apiKey Bearer key (Gemini uses ?key=, others use Authorization header)
     * \param baseUrl Used for the "custom" provider; ignored otherwise.
     * \param scope Cache/favourites scope the result belongs to
     *        (ProviderProfileStore::modelScopeId). Empty means "the provider
     *        itself". Pass a profile scope together with that profile's base
     *        URL and key to refresh a stored endpoint without switching the
     *        application's active connection. The signals below report this
     *        scope, so the receiver can store the result straight away.
     */
    void fetch(const QString &provider,
               const QString &apiKey,
               const QString &baseUrl,
               const QString &scope = QString());

    /**
     * \brief Normalise Ollama's /api/tags `models` array into the cache schema
     *        (id, displayName with size badge, contextWindow, supportsTools,
     *        supportsReasoning). Static + public so it is unit-testable without
     *        a network round-trip. Uses no instance state.
     */
    static QJsonArray normaliseOllama(const QJsonArray &raw);

signals:
    /**
     * \brief Emitted on success.
     * \param scope The cache scope this fetch was for (the provider, unless
     *              the caller passed an explicit scope).
     * \param models Normalised array (id, displayName, contextWindow,
     *               supportsTools, supportsReasoning).
     */
    void finished(const QString &scope, const QJsonArray &models);

    /**
     * \brief Emitted on failure.
     * \param scope The cache scope this fetch was for.
     * \param error A short, user-presentable error message.
     */
    void failed(const QString &scope, const QString &error);

private slots:
    void onReplyFinished();

private:
    QJsonArray normaliseOpenAi(const QJsonArray &raw) const;
    QJsonArray normaliseOpenRouter(const QJsonArray &raw) const;
    QJsonArray normaliseGemini(const QJsonArray &raw) const;
    QJsonArray normaliseCustom(const QJsonArray &raw) const;

    /** Best-effort context-window guess from the model id (used as a fallback
     *  when the provider response does not declare one). */
    static int contextWindowFromId(const QString &id);

    QNetworkAccessManager *_manager;
    QNetworkReply *_reply;
    QString _provider;   ///< decides the endpoint + the normaliser
    QString _scope;      ///< decides where the caller files the result
};

#endif // MODELLISTFETCHER_H
