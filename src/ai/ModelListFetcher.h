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
     * \param provider one of "openai" / "openrouter" / "gemini" / "custom"
     * \param apiKey Bearer key (Gemini uses ?key=, others use Authorization header)
     * \param baseUrl Used for the "custom" provider; ignored otherwise.
     */
    void fetch(const QString &provider,
               const QString &apiKey,
               const QString &baseUrl);

signals:
    /**
     * \brief Emitted on success.
     * \param provider The provider this fetch was for.
     * \param models Normalised array (id, displayName, contextWindow,
     *               supportsTools, supportsReasoning).
     */
    void finished(const QString &provider, const QJsonArray &models);

    /**
     * \brief Emitted on failure.
     * \param provider The provider this fetch was for.
     * \param error A short, user-presentable error message.
     */
    void failed(const QString &provider, const QString &error);

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
    QString _provider;
};

#endif // MODELLISTFETCHER_H
