/*
 * MidiEditor AI — RtcRendezvousClient implementation.
 *
 * Phase 9.8 multi-peer protocol: the host polls a list of joiner offers
 * (one entry per joiner that's tried to connect), creates a Responder
 * WebRtcTransport per offer, and posts an answer SDP back keyed by
 * joinerId. Each joiner generates its own offer, polls for its answer.
 * See cloudflare/rendezvous.js for the wire protocol.
 */

#include "RtcRendezvousClient.h"

#ifdef MIDIEDITOR_WEBRTC_ENABLED

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QSettings>
#include <QTimer>
#include <QUrl>

#include <utility>

namespace {
Q_LOGGING_CATEGORY(rdvLog, "midieditor.collab.rdv")
}

namespace {

// Production rendezvous shipped with the build. Override per-user via
// Settings → Collaboration → WebRTC rendezvous URL.
constexpr const char *kBuiltInDefaultUrl =
    "https://midi-rdv.happytunesai.workers.dev";

constexpr const char *kSettingsKey = "Collab/wan/rendezvousUrl";

// Polling cadence. Phase 9.8 measured baseline: host poll dominates
// rendezvous time (each new joiner waits up to one full poll-interval
// before the host sees their offer). 2 s was too laggy — Connection
// Test showed 2.3 s rendezvous + 3 s handshake, both above the 2 s
// "Good" threshold. Drop host poll to 1 s for snappier startup; the
// extra KV reads stay under free-tier budget (~22k/h at 5 peers, vs
// 100k/day cap). Joiner answer-poll drops to 750 ms — it's
// short-lived (only until the host responds) so the cost is trivial.
constexpr int kHostPollIntervalMs   = 1000;
constexpr int kJoinerPollIntervalMs = 750;
constexpr int kMaxJoinerPollAttempts = 360;  // ~270 s @ 750 ms, under worker TTL

QString trimTrailingSlashes(QString s) {
    while (s.endsWith(QLatin1Char('/'))) s.chop(1);
    return s;
}

} // namespace

QString RtcRendezvousClient::defaultUrl() {
    return QString::fromLatin1(kBuiltInDefaultUrl);
}

QString RtcRendezvousClient::configuredUrl() {
    QSettings settings(QStringLiteral("MidiEditor"), QStringLiteral("NONE"));
    QString raw = settings.value(QString::fromLatin1(kSettingsKey)).toString().trimmed();
    if (raw.isEmpty()) raw = defaultUrl();
    return trimTrailingSlashes(raw);
}

void RtcRendezvousClient::setConfiguredUrl(const QString &url) {
    QSettings settings(QStringLiteral("MidiEditor"), QStringLiteral("NONE"));
    settings.setValue(QString::fromLatin1(kSettingsKey), url.trimmed());
}

QString RtcRendezvousClient::newJoinerId() {
    // 16 url-safe chars from the alphabet [A-Za-z0-9-]. The worker's
    // regex demands at least 8, max 64; 16 keeps us comfortably away
    // from the upper bound while remaining short enough to log.
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789-";
    QString s;
    s.reserve(16);
    auto *rng = QRandomGenerator::system();
    for (int i = 0; i < 16; ++i) {
        s.append(QLatin1Char(alphabet[rng->bounded(int(sizeof(alphabet) - 1))]));
    }
    return s;
}

RtcRendezvousClient::RtcRendezvousClient(QObject *parent)
    : QObject(parent),
      _net(new QNetworkAccessManager(this)),
      _offerPollTimer(new QTimer(this)),
      _answerPollTimer(new QTimer(this)) {
    _offerPollTimer->setInterval(kHostPollIntervalMs);
    _answerPollTimer->setInterval(kJoinerPollIntervalMs);
    connect(_offerPollTimer,  &QTimer::timeout, this,
            &RtcRendezvousClient::pollJoinerOffersOnce);
    connect(_answerPollTimer, &QTimer::timeout, this,
            &RtcRendezvousClient::pollHostAnswerOnce);
}

RtcRendezvousClient::~RtcRendezvousClient() = default;

void RtcRendezvousClient::sendJson(const QString &method,
                                   const QString &path,
                                   const QByteArray &body,
                                   std::function<void(QNetworkReply *)> onDone) {
    QUrl url(configuredUrl() + path);
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QStringLiteral("application/json"));
    // Force HTTP/1.1. Qt 6's HTTP/2 stack breaks against some
    // firewalls / proxies that inspect HTTP/2 frames — observed on
    // 2026-05-08 with PC3 (server, restrictive firewall) + PC4
    // (laptop on WLAN) where postSession hung for 30 s with
    // "qt.network.http2 stream 1 finished with error: Connection
    // closed" while the same URL worked fine in a browser.
    // Browsers have more battle-tested HTTP/2 stacks; for our small
    // JSON request/response we don't lose anything by going HTTP/1.1.
    req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);

    QNetworkReply *reply = nullptr;
    if (method == QLatin1String("POST")) {
        reply = _net->post(req, body);
    } else if (method == QLatin1String("GET")) {
        reply = _net->get(req);
    } else {
        emit error(QStringLiteral("config"),
                   tr("Unsupported HTTP method: %1").arg(method));
        return;
    }

    connect(reply, &QNetworkReply::finished, this,
            [reply, cb = std::move(onDone)]() {
                cb(reply);
                reply->deleteLater();
            });
}

// =====================================================================
//  Host side
// =====================================================================

void RtcRendezvousClient::postSession(const QString &sessionId,
                                       const QString &displayName) {
    QJsonObject body;
    body.insert(QStringLiteral("sessionId"), sessionId);
    body.insert(QStringLiteral("displayName"), displayName);
    QByteArray raw = QJsonDocument(body).toJson(QJsonDocument::Compact);

    sendJson(QStringLiteral("POST"), QStringLiteral("/session"), raw,
             [this](QNetworkReply *reply) {
        if (reply->error() != QNetworkReply::NoError) {
            emit error(QStringLiteral("postSession"), reply->errorString());
            return;
        }
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (!doc.isObject()) {
            emit error(QStringLiteral("postSession"),
                       tr("invalid JSON response from rendezvous"));
            return;
        }
        QString code = doc.object().value(QStringLiteral("code")).toString();
        if (code.isEmpty()) {
            QString srv = doc.object().value(QStringLiteral("error")).toString();
            emit error(QStringLiteral("postSession"),
                       srv.isEmpty() ? tr("rendezvous returned no code") : srv);
            return;
        }
        emit sessionPosted(code);
    });
}

void RtcRendezvousClient::pollJoinerOffers(const QString &code) {
    _hostCode = code;
    _seenJoiners.clear();
    // Fire one immediately so a fast joiner doesn't wait the full interval.
    QTimer::singleShot(0, this, &RtcRendezvousClient::pollJoinerOffersOnce);
    _offerPollTimer->start();
}

void RtcRendezvousClient::cancelPolling() {
    _offerPollTimer->stop();
    _hostCode.clear();
    _seenJoiners.clear();
}

void RtcRendezvousClient::pollJoinerOffersOnce() {
    if (_hostCode.isEmpty()) {
        _offerPollTimer->stop();
        return;
    }
    const QString thisCode = _hostCode;
    qCDebug(rdvLog) << "pollJoinerOffers GET /code/" << thisCode << "/joiner-offers";
    sendJson(QStringLiteral("GET"),
             QStringLiteral("/code/%1/joiner-offers").arg(thisCode),
             QByteArray(),
             [this, thisCode](QNetworkReply *reply) {
        if (_hostCode != thisCode) return;  // session left mid-flight
        const int status = reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError) {
            qCDebug(rdvLog) << "pollJoinerOffers reply error status=" << status
                            << "msg=" << reply->errorString();
            if (status == 404) {
                _offerPollTimer->stop();
                _hostCode.clear();
                emit error(QStringLiteral("pollJoinerOffers"),
                           tr("session record expired on rendezvous"));
                return;
            }
            // Transient network error — keep polling, don't tear down.
            return;
        }
        QByteArray body = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(body);
        if (!doc.isObject()) {
            qCWarning(rdvLog) << "pollJoinerOffers non-JSON body:" << body.left(200);
            return;
        }
        QJsonArray arr = doc.object().value(QStringLiteral("offers")).toArray();
        qCDebug(rdvLog) << "pollJoinerOffers got" << arr.size() << "offer(s)"
                        << "(seen so far:" << _seenJoiners.size() << ")";
        for (const QJsonValue &v : arr) {
            QJsonObject o = v.toObject();
            QString joinerId = o.value(QStringLiteral("joinerId")).toString();
            QString sdp      = o.value(QStringLiteral("sdp")).toString();
            if (joinerId.isEmpty() || sdp.isEmpty()) continue;
            if (_seenJoiners.contains(joinerId)) continue;  // already paired
            _seenJoiners.insert(joinerId);
            qCInfo(rdvLog) << "pollJoinerOffers: new joiner" << joinerId.left(8);
            emit joinerOfferReceived(joinerId, sdp);
        }
    });
}

void RtcRendezvousClient::postHostAnswer(const QString &code,
                                          const QString &joinerId,
                                          const QString &sdp) {
    QJsonObject body;
    body.insert(QStringLiteral("joinerId"), joinerId);
    body.insert(QStringLiteral("sdp"), sdp);
    QByteArray raw = QJsonDocument(body).toJson(QJsonDocument::Compact);

    sendJson(QStringLiteral("POST"),
             QStringLiteral("/code/%1/host-answer").arg(code), raw,
             [this, joinerId](QNetworkReply *reply) {
        if (reply->error() != QNetworkReply::NoError) {
            int status = reply->attribute(
                QNetworkRequest::HttpStatusCodeAttribute).toInt();
            QString why = (status == 404)
                ? tr("session expired on rendezvous")
                : reply->errorString();
            emit error(QStringLiteral("postHostAnswer"), why);
            return;
        }
        emit hostAnswerPosted(joinerId);
    });
}

// =====================================================================
//  Joiner side
// =====================================================================

void RtcRendezvousClient::verifyCode(const QString &code) {
    sendJson(QStringLiteral("GET"),
             QStringLiteral("/code/%1").arg(code), QByteArray(),
             [this](QNetworkReply *reply) {
        if (reply->error() != QNetworkReply::NoError) {
            int status = reply->attribute(
                QNetworkRequest::HttpStatusCodeAttribute).toInt();
            QString why = (status == 404)
                ? tr("code expired or not found")
                : reply->errorString();
            emit error(QStringLiteral("verifyCode"), why);
            return;
        }
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (!doc.isObject()) {
            emit error(QStringLiteral("verifyCode"),
                       tr("invalid JSON response from rendezvous"));
            return;
        }
        QString sid = doc.object().value(QStringLiteral("sessionId")).toString();
        QString dn  = doc.object().value(QStringLiteral("displayName")).toString();
        emit codeVerified(sid, dn);
    });
}

void RtcRendezvousClient::postJoinerOffer(const QString &code,
                                           const QString &joinerId,
                                           const QString &sdp) {
    qCDebug(rdvLog) << "postJoinerOffer code=" << code
                    << "joinerId=" << joinerId.left(8)
                    << "sdpBytes=" << sdp.size();
    QJsonObject body;
    body.insert(QStringLiteral("joinerId"), joinerId);
    body.insert(QStringLiteral("sdp"), sdp);
    QByteArray raw = QJsonDocument(body).toJson(QJsonDocument::Compact);

    sendJson(QStringLiteral("POST"),
             QStringLiteral("/code/%1/joiner-offer").arg(code), raw,
             [this, code, joinerId](QNetworkReply *reply) {
        const int status = reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError) {
            QString why;
            if (status == 404) {
                why = tr("code not found / session expired");
            } else if (status == 409) {
                why = tr("joinerId collision (retry with a fresh ID)");
            } else if (status == 503) {
                why = tr("session is full — host has reached the peer cap");
            } else {
                why = reply->errorString();
            }
            qCWarning(rdvLog) << "postJoinerOffer FAILED code=" << code
                              << "joinerId=" << joinerId.left(8)
                              << "status=" << status << "why=" << why;
            emit error(QStringLiteral("postJoinerOffer"), why);
            return;
        }
        qCInfo(rdvLog) << "postJoinerOffer OK code=" << code
                       << "joinerId=" << joinerId.left(8);
        emit joinerOfferPosted();
    });
}

void RtcRendezvousClient::pollHostAnswer(const QString &code,
                                          const QString &joinerId) {
    _joinerCode = code;
    _joinerId   = joinerId;
    _answerPollAttempts = 0;
    QTimer::singleShot(0, this, &RtcRendezvousClient::pollHostAnswerOnce);
    _answerPollTimer->start();
}

void RtcRendezvousClient::pollHostAnswerOnce() {
    if (_joinerCode.isEmpty() || _joinerId.isEmpty()) {
        _answerPollTimer->stop();
        return;
    }
    if (_answerPollAttempts >= kMaxJoinerPollAttempts) {
        _answerPollTimer->stop();
        QString c = _joinerCode;
        _joinerCode.clear();
        _joinerId.clear();
        emit error(QStringLiteral("pollHostAnswer"),
                   tr("timeout waiting for host answer (code %1)").arg(c));
        return;
    }
    _answerPollAttempts++;

    const QString thisCode = _joinerCode;
    const QString thisId   = _joinerId;
    sendJson(QStringLiteral("GET"),
             QStringLiteral("/code/%1/host-answer/%2").arg(thisCode, thisId),
             QByteArray(),
             [this, thisCode, thisId](QNetworkReply *reply) {
        if (_joinerCode != thisCode || _joinerId != thisId) return;
        const int status = reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() == QNetworkReply::NoError) {
            QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            QString sdp = doc.object().value(QStringLiteral("sdp")).toString();
            if (!sdp.isEmpty()) {
                _answerPollTimer->stop();
                _joinerCode.clear();
                _joinerId.clear();
                emit hostAnswerReceived(sdp);
            }
            return;
        }
        if (status == 404) {
            // Host hasn't answered yet — keep polling silently.
            return;
        }
        _answerPollTimer->stop();
        QString reason = reply->errorString();
        _joinerCode.clear();
        _joinerId.clear();
        emit error(QStringLiteral("pollHostAnswer"), reason);
    });
}

// =====================================================================
//  Health probe (Plan §11.10l)
// =====================================================================

void RtcRendezvousClient::ping(int timeoutMs) {
    QUrl url(configuredUrl() + QStringLiteral("/health"));
    qCInfo(rdvLog) << "ping starting GET" << url.toString()
                   << "timeoutMs=" << timeoutMs;
    QNetworkRequest req(url);
    // Same HTTP/1.1 forcing as sendJson — see comment there.
    req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    QNetworkReply *reply = _net->get(req);

    auto *clock = new QElapsedTimer;
    clock->start();

    auto *timeout = new QTimer(this);
    timeout->setSingleShot(true);
    timeout->setInterval(qMax(500, timeoutMs));

    auto *fired = new bool(false);

    auto cleanup = [reply, clock, timeout, fired]() {
        delete clock;
        timeout->deleteLater();
        if (reply) reply->deleteLater();
        delete fired;
    };

    // Surface TLS / cert errors immediately as they happen, even if the
    // overall timer fires later — otherwise users see only "Timed out"
    // and can't tell whether it was DNS, TLS handshake, or genuine
    // server-side hang. Common cases this catches:
    //   • Antivirus SSL-MITM with an untrusted custom CA → "self-signed
    //     certificate in certificate chain"
    //   • Captive portal returning a hijacked page → "host name not
    //     matched"
    //   • Stale system clock → "certificate not yet valid / expired"
    connect(reply, &QNetworkReply::sslErrors, this,
            [reply, clock](const QList<QSslError> &errs) {
                for (const QSslError &e : errs) {
                    qCWarning(rdvLog) << "ping ssl-error after"
                                      << clock->elapsed() << "ms:"
                                      << e.errorString();
                }
                Q_UNUSED(reply);
            });
    connect(reply, &QNetworkReply::errorOccurred, this,
            [reply, clock](QNetworkReply::NetworkError code) {
                qCWarning(rdvLog) << "ping network-error after"
                                  << clock->elapsed() << "ms code=" << code
                                  << "msg=" << reply->errorString();
            });

    connect(timeout, &QTimer::timeout, this, [this, reply, fired, clock, cleanup]() {
        if (*fired) return;
        *fired = true;
        if (reply && reply->isRunning()) reply->abort();
        qint64 elapsed = clock->elapsed();
        // If we have an actual error string from the reply (e.g. SSL
        // failure that took 3+ seconds and is still propagating),
        // include it in the user-visible message so the failure isn't
        // a generic "timeout" when we know more.
        QString detail = reply ? reply->errorString() : QString();
        QString msg = detail.isEmpty()
            ? tr("Timed out after %1 ms (no response)").arg(elapsed)
            : tr("Timed out after %1 ms — %2").arg(elapsed).arg(detail);
        emit pingResult(false, -1, msg);
        cleanup();
    });

    connect(reply, &QNetworkReply::finished, this,
            [this, reply, fired, clock, timeout, cleanup]() {
        if (*fired) return;
        *fired = true;
        timeout->stop();
        qint64 elapsed = clock->elapsed();
        if (reply->error() != QNetworkReply::NoError) {
            if (reply->error() == QNetworkReply::OperationCanceledError) {
                emit pingResult(false, -1, tr("Aborted"));
            } else {
                emit pingResult(false, -1, reply->errorString());
            }
            cleanup();
            return;
        }
        int status = reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status != 200) {
            emit pingResult(false, elapsed,
                tr("Rendezvous returned HTTP %1").arg(status));
            cleanup();
            return;
        }
        emit pingResult(true, elapsed, QString());
        cleanup();
    });

    timeout->start();
}

#endif // MIDIEDITOR_WEBRTC_ENABLED
