/*
 * MidiEditor AI - WebhookClient implementation.
 */

#include "WebhookClient.h"

#include <QByteArray>
#include <QColor>
#include <QCoreApplication>
#include <QHash>
#include <QHttpMultiPart>
#include <QHttpPart>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QString>
#include <QUrl>

namespace {

WebhookClient *s_instance = nullptr;

constexpr int kDiscordDescriptionLimit = 4000;       // safe under the 4096 hard limit
constexpr int kInlineTokenSafeLimit = 3000;          // leaves room for the rest of the description

/**
 * Stable per-user color for Discord embeds. Hash the author display name
 * (and machineId, so two "Alice"s on different machines get different
 * colors) into a hue, then convert to RGB. Saturation + lightness are
 * fixed so every author's color is equally vivid and equally readable
 * against a dark Discord theme.
 */
int colorForAuthor(const QString &author, const QString &machineId) {
    QString combined = author + QStringLiteral(":") + machineId;
    uint hash = qHash(combined);
    int hue = static_cast<int>(hash % 360u);
    return QColor::fromHslF(hue / 360.0, 0.65, 0.55).rgb() & 0xFFFFFF;
}

QJsonObject buildEmbed(const PrBundle &bundle, const QString &fileLabel) {
    int totalAdded = 0, totalRemoved = 0, totalModified = 0;
    QStringList scopeLabels;
    for (const QJsonValue &v : bundle.hunks) {
        QJsonObject h = v.toObject();
        totalAdded += h.value(QStringLiteral("added")).toArray().size();
        totalRemoved += h.value(QStringLiteral("removed")).toArray().size();
        totalModified += h.value(QStringLiteral("modified")).toArray().size();
        QJsonObject scope = h.value(QStringLiteral("scope")).toObject();
        int ch = scope.value(QStringLiteral("channel")).toInt();
        int trk = scope.value(QStringLiteral("track")).toInt();
        int mStart = scope.value(QStringLiteral("measureStart")).toInt();
        int mEnd = scope.value(QStringLiteral("measureEnd")).toInt();
        QString lbl = (mStart == mEnd)
            ? QStringLiteral("ch%1·trk%2·m.%3").arg(ch).arg(trk).arg(mStart + 1)
            : QStringLiteral("ch%1·trk%2·m.%3–%4").arg(ch).arg(trk).arg(mStart + 1).arg(mEnd + 1);
        scopeLabels.append(lbl);
    }

    QString token = bundle.toInlineToken();
    QString tokenSection;
    if (token.size() <= kInlineTokenSafeLimit) {
        tokenSection = QStringLiteral("\n\n**Smart-paste token:**\n```\n%1\n```").arg(token);
    } else {
        tokenSection = QStringLiteral("\n\n*Token too large for chat — download the attachment and use File → Collaboration → Import PR…*");
    }

    QString sessionPrefix = bundle.sessionId.left(8);
    QString hashPrefix; // bundle has no own hash field; we use the parentHash for context
    if (!bundle.parentHash.isEmpty()) hashPrefix = bundle.parentHash.left(8);

    QString description = QStringLiteral("**%1 hunks:** +%2 ⋅%3 ~%4")
        .arg(bundle.hunks.size())
        .arg(totalAdded).arg(totalRemoved).arg(totalModified);
    if (!scopeLabels.isEmpty()) {
        QString scopeJoined = scopeLabels.mid(0, 6).join(QStringLiteral(", "));
        if (scopeLabels.size() > 6) scopeJoined += QStringLiteral(", …");
        description += QStringLiteral("  •  %1").arg(scopeJoined);
    }
    description += QStringLiteral("\n\n**Session:** `%1…`").arg(sessionPrefix);
    if (!hashPrefix.isEmpty()) {
        description += QStringLiteral("  •  **Parent:** `%1…`").arg(hashPrefix);
    }
    description += tokenSection;

    if (description.size() > kDiscordDescriptionLimit) {
        description.truncate(kDiscordDescriptionLimit - 4);
        description += QStringLiteral("\n…");
    }

    QString title = QStringLiteral("🎵 %1 saved '%2'")
        .arg(bundle.author.isEmpty() ? QStringLiteral("(unknown)") : bundle.author,
             fileLabel.isEmpty() ? QStringLiteral("(unsaved file)") : fileLabel);
    if (!bundle.message.isEmpty() && bundle.message != QStringLiteral("Save")) {
        title = QStringLiteral("🎵 %1 — %2").arg(bundle.author, bundle.message);
    }

    QJsonObject embed;
    embed.insert(QStringLiteral("title"), title);
    embed.insert(QStringLiteral("description"), description);
    embed.insert(QStringLiteral("color"), colorForAuthor(bundle.author, bundle.machineId));
    return embed;
}

}

WebhookClient *WebhookClient::instance() {
    if (!s_instance) {
        s_instance = new WebhookClient(QCoreApplication::instance());
    }
    return s_instance;
}

WebhookClient::WebhookClient(QObject *parent)
    : QObject(parent),
      _nam(new QNetworkAccessManager(this)) {
}

void WebhookClient::postPr(const QString &webhookUrl,
                           const PrBundle &bundle,
                           const QString &fileLabel) {
    if (webhookUrl.isEmpty()) return;
    QUrl url(webhookUrl);
    if (!url.isValid() || (url.scheme() != QStringLiteral("https") && url.scheme() != QStringLiteral("http"))) {
        emit postFinished(false, tr("Webhook URL is not a valid http(s) URL."));
        return;
    }

    // Build the Discord payload.
    QJsonObject payload;
    QJsonArray embeds;
    embeds.append(buildEmbed(bundle, fileLabel));
    payload.insert(QStringLiteral("embeds"), embeds);
    payload.insert(QStringLiteral("username"), QStringLiteral("MidiEditor PR"));

    QHttpMultiPart *multi = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    QHttpPart payloadPart;
    payloadPart.setHeader(QNetworkRequest::ContentTypeHeader,
                          QStringLiteral("application/json"));
    payloadPart.setHeader(QNetworkRequest::ContentDispositionHeader,
                          QStringLiteral("form-data; name=\"payload_json\""));
    payloadPart.setBody(QJsonDocument(payload).toJson(QJsonDocument::Compact));
    multi->append(payloadPart);

    // Attachment: full bundle JSON. Fallback for recipients who don't
    // copy the inline token (or for tokens too large for the embed).
    QString bundleFileName = QStringLiteral("%1-%2%3")
        .arg(bundle.author.isEmpty() ? QStringLiteral("anon") : bundle.author)
        .arg(bundle.timestamp)
        .arg(QString::fromLatin1(PrBundle::kBundleFileExtension));
    QHttpPart filePart;
    filePart.setHeader(QNetworkRequest::ContentTypeHeader,
                       QStringLiteral("application/json"));
    filePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QStringLiteral("form-data; name=\"files[0]\"; filename=\"%1\"").arg(bundleFileName));
    filePart.setBody(bundle.toBundleJson());
    multi->append(filePart);

    QNetworkRequest req(url);
    QNetworkReply *reply = _nam->post(req, multi);
    multi->setParent(reply);  // auto-delete with reply

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        bool ok = reply->error() == QNetworkReply::NoError;
        QString msg;
        if (ok) {
            int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            ok = (httpStatus >= 200 && httpStatus < 300);
            msg = ok
                ? tr("Posted to Discord webhook (HTTP %1).").arg(httpStatus)
                : tr("Discord webhook returned HTTP %1.").arg(httpStatus);
        } else {
            msg = tr("Webhook error: %1").arg(reply->errorString());
        }
        emit postFinished(ok, msg);
        reply->deleteLater();
    });
}
