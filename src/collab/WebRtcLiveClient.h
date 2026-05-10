/*
 * MidiEditor AI
 *
 * WebRtcLiveClient — joiner-side ILiveClient over a WebRTC connection
 * (Plan §11.10, Phase 9.6 + 9.8).
 *
 * Phase 9.8 protocol flip: the joiner is now the WebRTC INITIATOR
 * (was Responder in Phase 9.6). The joiner generates its own offer,
 * publishes it to the rendezvous, and waits for the host's answer.
 * This is required so each joiner has its own offer/answer pair —
 * the host can't share a single offer across N joiners because each
 * RTCPeerConnection has its own DTLS credentials and ICE candidates.
 *
 * Lifecycle:
 *   1. Caller calls \ref start() — opens the PeerConnection and
 *      begins ICE gathering. Caller does NOT yet know the host's SDP.
 *   2. Once the offer is ready, \ref offerReady fires with our local
 *      SDP. Caller ships it to the rendezvous (POST joiner-offer).
 *   3. Caller polls the rendezvous for the host's answer; once it
 *      arrives, calls \ref setRemoteAnswer().
 *   4. DTLS handshake completes; \ref connected() fires.
 */

#ifndef WEBRTCLIVECLIENT_H
#define WEBRTCLIVECLIENT_H

#ifdef MIDIEDITOR_WEBRTC_ENABLED

#include <QString>

#include "ILiveClient.h"

class WebRtcTransport;

class WebRtcLiveClient : public ILiveClient {
    Q_OBJECT
public:
    explicit WebRtcLiveClient(QObject *parent = nullptr);
    ~WebRtcLiveClient() override;

    /** \brief Begin signaling: spin up the underlying transport in
     *  Initiator role and start ICE gathering. Emits \ref offerReady
     *  with our local SDP once gathering completes (or the timeout
     *  fires). \a gatheringTimeoutMs caps gathering. */
    void start(int gatheringTimeoutMs = 8000);

    /** \brief Feed the host's answer SDP (received via rendezvous). */
    void setRemoteAnswer(const QString &sdp);

    // ---- ILiveClient overrides --------------------------------------
    bool sendMessage(const QByteArray &payload) override;
    bool isConnected() const override;
    void disconnectFromHost() override;

signals:
    /** \brief Local offer SDP is ready to be shipped to the rendezvous. */
    void offerReady(const QString &sdp);

    /** \brief Diagnostic detail when the WebRTC layer fails. */
    void transportFailed(const QString &reason);

private slots:
    void onTransportConnected();
    void onTransportDisconnected();

private:
    WebRtcTransport *_transport = nullptr;
    bool _connected = false;
};

#endif // MIDIEDITOR_WEBRTC_ENABLED

#endif // WEBRTCLIVECLIENT_H
