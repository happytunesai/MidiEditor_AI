/*
 * MidiEditor AI
 *
 * WebRtcLiveServer — host-side ILiveServer over N parallel WebRTC
 * connections, one per joiner (Plan §11.10, Phase 9.6 + 9.8).
 *
 * --------------------------------------------------------------
 *  Phase 9.8 multi-peer protocol
 * --------------------------------------------------------------
 *
 * The Phase 9.6 implementation supported a single peer per host
 * (1:1 WAN session) by holding one `WebRtcTransport` and being the
 * WebRTC initiator. Phase 9.8 extends this to N joiners using
 * host-star topology — host accepts N parallel WebRTC connections,
 * one per joiner. Each WebRTC PeerConnection is 1:1 by spec
 * (point-to-point DTLS / SCTP), so multi-peer requires N transports.
 *
 * The role flip (host now responder, joiner initiator) is needed so
 * each joiner can drive their own offer/answer negotiation independently
 * — the host can't pre-generate one offer that's shared across N peers
 * because each PeerConnection has its own DTLS credentials and ICE
 * candidates.
 *
 * Lifecycle:
 *   1. Caller calls \ref start() — no offer, no ICE yet. The server
 *      just announces it's ready to accept joiner offers.
 *   2. Caller (LanLiveSession) polls the rendezvous for joiner offers.
 *      For each new joiner offer arriving, caller invokes
 *      \ref acceptJoinerOffer(joinerId, offerSdp).
 *   3. The server creates a new WebRtcTransport (Responder role) for
 *      that joiner, sets the remote offer, gathers ICE candidates,
 *      and emits \ref answerReady(joinerId, sdp) once the answer is
 *      finalized. Caller posts that answer to the rendezvous.
 *   4. DTLS handshake completes; \ref peerConnected fires with the
 *      newly-attached IPeerLink. From this point forward the joiner
 *      is part of broadcast() / broadcastExcept() iteration.
 *
 * The transport map is keyed by joinerId so a peer disconnect can be
 * cleanly attributed back to a specific joiner without a linear scan.
 */

#ifndef WEBRTCLIVESERVER_H
#define WEBRTCLIVESERVER_H

#ifdef MIDIEDITOR_WEBRTC_ENABLED

#include <QHash>
#include <QString>

#include "ILiveServer.h"

class WebRtcTransport;

class WebRtcLiveServer : public ILiveServer {
    Q_OBJECT
public:
    explicit WebRtcLiveServer(QObject *parent = nullptr);
    ~WebRtcLiveServer() override;

    /** \brief Mark the server as ready to accept joiners. Doesn't
     *  spin up any WebRTC machinery — that happens lazily per joiner.
     *  \a gatheringTimeoutMs is remembered and applied to each
     *  per-joiner transport's ICE gathering. */
    void start(int gatheringTimeoutMs = 8000);

    /** \brief Accept an incoming joiner offer. Spins up a new
     *  WebRtcTransport (Responder role) for this joiner, sets the
     *  remote offer, and begins ICE gathering. Emits
     *  \ref answerReady(joinerId, sdp) when the answer is finalised
     *  so the caller can post it to the rendezvous. */
    void acceptJoinerOffer(const QString &joinerId, const QString &offerSdp);

    // ---- ILiveServer overrides --------------------------------------
    int peerCount() const override;
    QList<IPeerLink *> peers() const override;
    bool broadcast(const QByteArray &payload) override;
    bool broadcastExcept(const QByteArray &payload, IPeerLink *exclude) override;
    void stop() override;

signals:
    /** \brief Answer SDP for a specific joiner is finalised. Caller
     *  publishes this via the rendezvous so the joiner can complete
     *  the WebRTC handshake. */
    void answerReady(const QString &joinerId, const QString &sdp);

    /** \brief Diagnostic detail when a per-joiner WebRTC transport
     *  fails. \a joinerId identifies which joiner is affected; an
     *  empty string is used only for catastrophic failures that
     *  affect every transport. */
    void transportFailed(const QString &joinerId, const QString &reason);

private slots:
    void onTransportConnected(const QString &joinerId);
    void onTransportDisconnected(const QString &joinerId);

private:
    int _gatheringTimeoutMs = 8000;
    // joinerId → WebRtcTransport*. Pointers are owned by `this` (the
    // QObject parent chain); deleteLater fires on stop() / disconnect.
    QHash<QString, WebRtcTransport *> _transports;
    // Subset of _transports whose data channel has fully opened. peers()
    // and broadcast() use this set so half-handshaked joiners aren't
    // attempted as broadcast targets (they'd return false on send).
    QHash<QString, WebRtcTransport *> _connected;
};

#endif // MIDIEDITOR_WEBRTC_ENABLED

#endif // WEBRTCLIVESERVER_H
