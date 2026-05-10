/*
 * MidiEditor AI
 *
 * WebhookClient — HTTP POST a PR bundle to a Discord webhook URL.
 *
 * Implements the optional convenience-layer from Plan §10.4:
 *   - On every save where the file is collab-initialized AND the user
 *     has configured a webhook URL, the bundle for the latest commit
 *     is POSTed as a Discord embed (with the smart-paste token in a
 *     code block) plus the bundle JSON as an attachment.
 *   - The receive side is unchanged — peers still copy the token from
 *     the Discord message and Ctrl+V it into MidiEditor (or download
 *     the attachment and use File → Collaboration → Import PR…).
 *
 * Async by design: the post fires-and-forgets via QNetworkAccessManager.
 * Save flow is never blocked by network latency. Failures are logged via
 * the postFinished() signal so the UI can surface a status hint without
 * blocking the user.
 */

#ifndef WEBHOOKCLIENT_H
#define WEBHOOKCLIENT_H

#include <QObject>
#include <QString>

#include "PrBundle.h"

class QNetworkAccessManager;

class WebhookClient : public QObject {
    Q_OBJECT

public:
    /**
     * \brief Process-wide singleton accessor.
     */
    static WebhookClient *instance();

    /**
     * \brief Post a PR bundle to the given webhook URL.
     *
     * Async — returns immediately. The HTTP POST is dispatched on the
     * shared QNetworkAccessManager. On completion (success or error),
     * \ref postFinished is emitted.
     *
     * \param webhookUrl  The full Discord webhook URL (https://discord.com/api/webhooks/…).
     *                    No-op when empty.
     * \param bundle      The PR bundle to embed + attach.
     * \param fileLabel   Short identifier shown in the embed title (e.g. the .mid filename).
     */
    void postPr(const QString &webhookUrl,
                const PrBundle &bundle,
                const QString &fileLabel);

signals:
    /**
     * \brief Emitted when an async post finishes.
     *
     * \a success is true on HTTP 2xx response, false otherwise.
     * \a message is a short human-readable status string suitable for
     * the status bar (e.g. "Posted to Discord" or "Webhook error: 401").
     */
    void postFinished(bool success, const QString &message);

private:
    explicit WebhookClient(QObject *parent = nullptr);

    QNetworkAccessManager *_nam;
};

#endif // WEBHOOKCLIENT_H
