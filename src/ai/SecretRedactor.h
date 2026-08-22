#ifndef SECRETREDACTOR_H
#define SECRETREDACTOR_H

#include <QRegularExpression>
#include <QString>

/**
 * \file SecretRedactor.h
 *
 * \brief One shared redactor for every string that leaves the AI layer
 *        towards a human: the plaintext API log, a status label, a chat
 *        error bubble.
 *
 * Two providers hand us the credential back in plain sight:
 *  - Gemini carries the API key as a URL query item, so Qt's
 *    QNetworkReply::errorString() (which quotes the full URL) and some HTTP
 *    error bodies contain the key verbatim;
 *  - a custom endpoint may be configured with a key in the query as well.
 *
 * Header-only on purpose: AiClient.cpp and ModelListFetcher.cpp both need it
 * and they are compiled into different unit-test targets, so an inline
 * function keeps the one implementation reachable from both without a new
 * translation unit in any target's source list.
 *
 * NEVER run this over data that is about to be sent to the API - it is an
 * output filter for human-visible text only.
 */
namespace AiSecrets {

/**
 * \brief Returns \a text with \a apiKey and any key=/token= query value
 *        replaced by "***".
 * \param text Text about to be logged or shown to the user.
 * \param apiKey The credential in use, if known. Only keys of 8 characters
 *        or more are replaced literally: a 1-2 character value (a placeholder
 *        or a half-typed field) would otherwise turn ordinary words into
 *        "***" and make the log unreadable. Real provider keys are far
 *        longer, so nothing that matters is missed.
 */
inline QString redactSecrets(const QString &text, const QString &apiKey = QString())
{
    if (text.isEmpty())
        return text;

    QString out = text;
    if (apiKey.size() >= 8)
        out.replace(apiKey, QStringLiteral("***"));

    // Catches the key even when it is not the configured one (a profile key,
    // a stale request still in flight) and covers the common spellings.
    static const QRegularExpression secretParam(
        QStringLiteral("\\b(key|api_key|apikey|access_token|token)=[^&\\s\"']+"),
        QRegularExpression::CaseInsensitiveOption);
    out.replace(secretParam, QStringLiteral("\\1=***"));
    return out;
}

} // namespace AiSecrets

#endif // SECRETREDACTOR_H
