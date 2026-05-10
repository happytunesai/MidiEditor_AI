/*
 * MidiEditor AI
 *
 * WebRtcTransport — single-class wrapper around an `rtc::PeerConnection`
 * + `rtc::DataChannel` that exposes the same framed-message API
 * surface as `LanPeerSocket` (Plan §11.10, Phase 9.6.b).
 *
 * Two roles, picked at construction:
 *
 *   • Role::Initiator  — creates the offer SDP. Caller takes the
 *                         offer string (after \ref offerReady fires),
 *                         ships it to the remote peer out-of-band
 *                         (smart-paste token §11.10), and feeds the
 *                         remote's answer back via \ref setRemoteAnswer.
 *
 *   • Role::Responder  — accepts a remote offer via \ref setRemoteOffer,
 *                         then produces an answer via \ref answerReady.
 *                         Caller ships the answer back to the initiator.
 *
 * Once ICE completes and DTLS handshakes, \ref connected() fires;
 * \ref sendMessage / \ref messageReceived match `LanPeerSocket` 1:1.
 *
 * The wire framing is the same length-prefix protocol as LAN: a
 * 4-byte big-endian size header followed by the payload bytes. WebRTC
 * data channels are message-oriented (not stream), so each
 * \c sendMessage call maps to a single framed packet with the same
 * encoding. Receivers reassemble exactly as in LanPeerSocket.
 */

#ifndef WEBRTCTRANSPORT_H
#define WEBRTCTRANSPORT_H

#ifdef MIDIEDITOR_WEBRTC_ENABLED

#include <QByteArray>
#include <QObject>
#include <QString>

#include <atomic>
#include <memory>

#include "IPeerLink.h"

namespace rtc {
class PeerConnection;
class DataChannel;
}

class WebRtcTransport : public IPeerLink {
    Q_OBJECT
public:
    enum class Role { Initiator, Responder };

    explicit WebRtcTransport(Role role, QObject *parent = nullptr);
    ~WebRtcTransport() override;

    /** \brief Begin the connection. For Initiator: creates the offer.
     *  For Responder: opens the peer connection waiting for
     *  \ref setRemoteOffer.
     *
     *  \param iceServersOverride  When non-empty, replaces the
     *      default \c IceConfig::load() list. By default (empty +
     *      \a useDefaultIceServersIfEmpty=true), the configured Google
     *      STUN pool is loaded.
     *  \param gatheringTimeoutMs  How long to wait for ICE gathering
     *      to complete before publishing whatever candidates we
     *      have. Prevents a slow/blocked STUN endpoint from
     *      deadlocking the offer/answer publish. Default 8 s.
     *  \param useDefaultIceServersIfEmpty  When false, an empty
     *      \a iceServersOverride means literally "no STUN servers"
     *      rather than "fall back to the default pool". Used by
     *      the in-process Connection Test so it relies solely on
     *      host candidates (192.168.x.y) — both transports run on
     *      the same machine, host candidates connect instantly via
     *      the local network, and we sidestep NAT hairpinning that
     *      otherwise breaks srflx-srflx pairs on home routers. */
    void start(const QStringList &iceServersOverride = {},
               int gatheringTimeoutMs = 8000,
               bool useDefaultIceServersIfEmpty = true);

    /** \brief Responder side: feed the remote's offer SDP. Triggers
     *  answer generation; \ref answerReady fires when the answer is
     *  ready to ship. No-op on the initiator side. */
    void setRemoteOffer(const QString &sdp);

    /** \brief Initiator side: feed the remote's answer SDP after the
     *  responder has produced one. Triggers DTLS handshake; \ref
     *  connected fires when the data channel is open. */
    void setRemoteAnswer(const QString &sdp);

    /** \brief Send one framed payload. Same 4-byte big-endian length
     *  prefix as LanPeerSocket. False if the data channel isn't open. */
    bool sendMessage(const QByteArray &payload) override;

    /** \brief True once the data channel is open and DTLS is done. */
    bool isConnected() const override;

    /** \brief Peer label — only meaningful after the application
     *  layer has set it (typically after a `hello` frame). */
    QString peerLabel() const override { return _peerLabel; }
    void setPeerLabel(const QString &label) override { _peerLabel = label; }

    /** \brief IPeerLink override. Closes the data channel and the
     *  underlying RTCPeerConnection. */
    void closeConnection() override;

    /** \brief Underlying SDP fingerprint of our local cert — useful
     *  for debug logging. Empty until \c start() runs. */
    QString localFingerprint() const;

signals:
    /** \brief Initiator: the offer SDP is ready to be shipped to the
     *  remote peer. */
    void offerReady(const QString &sdp);

    /** \brief Responder: the answer SDP is ready to be shipped back. */
    void answerReady(const QString &sdp);

    /** \brief ICE gathering finished — every candidate we have is in
     *  the local SDP at this point. The signaling token format
     *  embeds candidates inline so this also marks "offer/answer is
     *  fully usable for the remote peer". */
    void gatheringComplete();

    /** \brief Data channel open and DTLS-secured. Both sides see
     *  this fire; from this point forward, framed messages flow. */
    void connected();

    /** \brief Diagnostic-detail counterpart to IPeerLink::disconnected.
     *  Carries a human-readable reason for the close (ICE failure,
     *  DTLS failure, peer hung up, etc.). The bare disconnected()
     *  from the IPeerLink interface still fires alongside this one
     *  so transport-agnostic consumers don't need to know about it. */
    void transportFailed(const QString &reason);

    // messageReceived / disconnected are inherited from IPeerLink.

private:
    Role _role;
    std::shared_ptr<rtc::PeerConnection> _pc;
    std::shared_ptr<rtc::DataChannel> _dc;
    QString _peerLabel;

    bool _localDescriptionPublished = false;

    // BUG-COLLAB-023: libdatachannel callbacks fire on its internal
    // worker thread.  When this QObject is destroyed (e.g. during
    // session leave), an in-flight callback can race the destructor
    // and dereference a half-torn-down `this`.  We flip _alive to
    // false BEFORE close() in the dtor so any callback that's about
    // to enter sees it and bails before touching member state.
    std::atomic<bool> _alive { true };

    // Inbound reassembly: WebRTC datachannel messages are already
    // length-delimited at the SCTP layer, so each binary message
    // we receive IS one framed payload. We still strip our 4-byte
    // length prefix (kept for parity with LanPeerSocket so the same
    // wire-protocol parsers run unchanged).
    QByteArray _readBuffer;
    quint32 _expectedSize = 0;

    void onDataChannelOpen();
    void onDataChannelClosed();
    void onDataChannelMessage(const QByteArray &bytes);
    void wireDataChannel(std::shared_ptr<rtc::DataChannel> dc);
    void publishLocalDescription();
};

#endif // MIDIEDITOR_WEBRTC_ENABLED

#endif // WEBRTCTRANSPORT_H
