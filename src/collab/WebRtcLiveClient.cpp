/*
 * MidiEditor AI - WebRtcLiveClient implementation (Phase 9.8 multi-peer).
 *
 * Joiner-side: now the WebRTC initiator. We generate our offer first,
 * ship it via rendezvous, await the host's answer.
 */

#ifdef MIDIEDITOR_WEBRTC_ENABLED

#include "WebRtcLiveClient.h"

#include <QLoggingCategory>

#include "WebRtcTransport.h"

Q_DECLARE_LOGGING_CATEGORY(rtcLog)

WebRtcLiveClient::WebRtcLiveClient(QObject *parent) : ILiveClient(parent) {}

WebRtcLiveClient::~WebRtcLiveClient() { disconnectFromHost(); }

void WebRtcLiveClient::start(int gatheringTimeoutMs) {
    if (_transport) disconnectFromHost();
    _transport = new WebRtcTransport(WebRtcTransport::Role::Initiator, this);
    connect(_transport, &WebRtcTransport::offerReady,
            this, &WebRtcLiveClient::offerReady);
    connect(_transport, &WebRtcTransport::transportFailed,
            this, [this](const QString &reason) {
                emit transportFailed(reason);
                if (!_connected) emit connectionFailed(reason);
            });
    connect(_transport, &WebRtcTransport::messageReceived,
            this, &ILiveClient::messageReceived);
    connect(_transport, &IPeerLink::disconnected,
            this, &WebRtcLiveClient::onTransportDisconnected);
    connect(_transport, &WebRtcTransport::connected,
            this, &WebRtcLiveClient::onTransportConnected);
    _transport->start({}, gatheringTimeoutMs);
    // Initiator: setLocalDescription has already been called inside
    // WebRtcTransport::start; offerReady fires once ICE gathering
    // completes (or the timeout). Caller awaits offerReady and ships
    // the SDP to the rendezvous.
}

void WebRtcLiveClient::setRemoteAnswer(const QString &sdp) {
    if (!_transport) return;
    _transport->setRemoteAnswer(sdp);
}

void WebRtcLiveClient::onTransportConnected() {
    _connected = true;
    qCInfo(rtcLog) << "WebRtcLiveClient: connected (DTLS done)";
    emit connected();
}

void WebRtcLiveClient::onTransportDisconnected() {
    bool wasConnected = _connected;
    _connected = false;
    if (wasConnected) emit disconnected();
}

bool WebRtcLiveClient::sendMessage(const QByteArray &payload) {
    if (!_transport || !_connected) return false;
    return _transport->sendMessage(payload);
}

bool WebRtcLiveClient::isConnected() const { return _connected; }

void WebRtcLiveClient::disconnectFromHost() {
    if (_transport) {
        _transport->closeConnection();
        _transport->deleteLater();
        _transport = nullptr;
    }
    _connected = false;
}

#endif // MIDIEDITOR_WEBRTC_ENABLED
