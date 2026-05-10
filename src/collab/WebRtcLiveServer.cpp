/*
 * MidiEditor AI - WebRtcLiveServer implementation (Phase 9.8 multi-peer).
 */

#ifdef MIDIEDITOR_WEBRTC_ENABLED

#include "WebRtcLiveServer.h"

#include <QLoggingCategory>

#include "WebRtcTransport.h"

Q_DECLARE_LOGGING_CATEGORY(rtcLog)

WebRtcLiveServer::WebRtcLiveServer(QObject *parent) : ILiveServer(parent) {}

WebRtcLiveServer::~WebRtcLiveServer() { stop(); }

void WebRtcLiveServer::start(int gatheringTimeoutMs) {
    stop();
    _gatheringTimeoutMs = gatheringTimeoutMs;
    qCInfo(rtcLog) << "WebRtcLiveServer: ready (gatheringTimeoutMs="
                   << gatheringTimeoutMs << "). Awaiting joiner offers.";
}

void WebRtcLiveServer::acceptJoinerOffer(const QString &joinerId,
                                          const QString &offerSdp) {
    if (_transports.contains(joinerId)) {
        qCWarning(rtcLog) << "WebRtcLiveServer: duplicate joiner-offer for"
                          << joinerId.left(8) << "— ignoring";
        return;
    }
    qCInfo(rtcLog) << "WebRtcLiveServer: accepting joiner" << joinerId.left(8);

    auto *t = new WebRtcTransport(WebRtcTransport::Role::Responder, this);
    t->setPeerLabel(QStringLiteral("joiner:") + joinerId.left(8));
    _transports.insert(joinerId, t);

    // Wire signals BEFORE start(), in particular answerReady — the
    // gathering-timeout fires this synchronously from inside start()
    // when the underlying PC has its candidates ready. We forward
    // it with the joinerId so the caller can route it to rendezvous.
    connect(t, &WebRtcTransport::answerReady, this,
            [this, joinerId](const QString &sdp) {
                emit answerReady(joinerId, sdp);
            });
    connect(t, &WebRtcTransport::transportFailed, this,
            [this, joinerId](const QString &reason) {
                emit transportFailed(joinerId, reason);
            });
    connect(t, &WebRtcTransport::messageReceived, this,
            [this, t](const QByteArray &payload) {
                emit messageReceived(t, payload);
            });
    connect(t, &WebRtcTransport::connected, this,
            [this, joinerId]() { onTransportConnected(joinerId); });
    connect(t, &IPeerLink::disconnected, this,
            [this, joinerId]() { onTransportDisconnected(joinerId); });

    // Responder role: start() opens the PeerConnection; the offer is
    // fed in via setRemoteOffer below. ICE gathering happens during
    // setRemoteOffer in libdatachannel.
    t->start({}, _gatheringTimeoutMs);
    t->setRemoteOffer(offerSdp);
}

void WebRtcLiveServer::onTransportConnected(const QString &joinerId) {
    auto it = _transports.find(joinerId);
    if (it == _transports.end()) return;
    _connected.insert(joinerId, it.value());
    qCInfo(rtcLog) << "WebRtcLiveServer: joiner" << joinerId.left(8)
                   << "fully connected (DTLS done) —"
                   << _connected.size() << "active peer(s)";
    emit peerConnected(it.value());
}

void WebRtcLiveServer::onTransportDisconnected(const QString &joinerId) {
    auto trIt = _transports.find(joinerId);
    if (trIt == _transports.end()) return;
    WebRtcTransport *t = trIt.value();
    bool wasConnected = _connected.remove(joinerId) > 0;
    _transports.erase(trIt);
    qCInfo(rtcLog) << "WebRtcLiveServer: joiner" << joinerId.left(8)
                   << "disconnected (was-connected=" << wasConnected
                   << ") —" << _connected.size() << "active peer(s) remaining";
    if (wasConnected) emit peerDisconnected(t);
    t->deleteLater();
}

int WebRtcLiveServer::peerCount() const {
    return _connected.size();
}

QList<IPeerLink *> WebRtcLiveServer::peers() const {
    QList<IPeerLink *> out;
    out.reserve(_connected.size());
    for (auto *t : _connected) out.append(t);
    return out;
}

bool WebRtcLiveServer::broadcast(const QByteArray &payload) {
    if (_connected.isEmpty()) return true;  // no peers — nothing to fail on
    bool allOk = true;
    for (auto *t : _connected) {
        if (!t->sendMessage(payload)) allOk = false;
    }
    return allOk;
}

bool WebRtcLiveServer::broadcastExcept(const QByteArray &payload, IPeerLink *exclude) {
    if (_connected.isEmpty()) return true;
    bool allOk = true;
    for (auto *t : _connected) {
        if (static_cast<IPeerLink *>(t) == exclude) continue;
        if (!t->sendMessage(payload)) allOk = false;
    }
    return allOk;
}

void WebRtcLiveServer::stop() {
    for (auto *t : _transports) {
        if (t) {
            t->closeConnection();
            t->deleteLater();
        }
    }
    _transports.clear();
    _connected.clear();
}

#endif // MIDIEDITOR_WEBRTC_ENABLED
