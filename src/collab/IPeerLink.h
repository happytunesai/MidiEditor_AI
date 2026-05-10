/*
 * MidiEditor AI
 *
 * IPeerLink — transport-agnostic interface for one half of a live-
 * session connection (Plan §11.10 architectural cleanup, Phase 9.6.b).
 *
 * Two implementations as of this writing:
 *
 *   - `LanPeerSocket` (TCP + 4-byte length-prefix framing, §11)
 *   - `WebRtcTransport` (DTLS-secured RTCDataChannel + same length
 *     prefix for handler parity, §11.10)
 *
 * The single signal contract `messageReceived` / `disconnected` lets
 * `LanLiveSession`'s wire-protocol handlers consume both transports
 * without branching. Concrete subclasses override the pure-virtual
 * methods; the signals live here so they're declared once.
 *
 * Design notes:
 *   - `closeConnection` rather than `disconnect` to dodge the clash
 *     with QObject's static `disconnect` overloads.
 *   - `peerLabel` is mutable (via setter) because the host learns the
 *     peer's display name from the `hello` frame after the connection
 *     is already up.
 */

#ifndef IPEERLINK_H
#define IPEERLINK_H

#include <QByteArray>
#include <QObject>
#include <QString>

class IPeerLink : public QObject {
    Q_OBJECT
public:
    explicit IPeerLink(QObject *parent = nullptr) : QObject(parent) {}
    ~IPeerLink() override = default;

    /** \brief Send one length-prefixed framed payload. Returns false
     *  when the underlying transport is not yet open / has closed. */
    virtual bool sendMessage(const QByteArray &payload) = 0;

    /** \brief True once the transport's wire is ready (TCP connected
     *  for LAN, DTLS-handshake done for WebRTC). */
    virtual bool isConnected() const = 0;

    /** \brief Human-readable peer label, set by higher-level code
     *  after a `hello` frame arrives. Empty until then. */
    virtual QString peerLabel() const = 0;
    virtual void setPeerLabel(const QString &label) = 0;

    /** \brief Stable per-PC identity from the peer's `hello` frame.
     *  Used by the host's ghost-peer dedup (BUG-COLLAB-030): when a
     *  peer reconnects after a network drop the OS may not have
     *  detected the old TCP socket dying yet, so the new connection
     *  arrives while the old one is still in `_peers`. We use the
     *  machineId carried in `hello` to spot the duplicate and kick
     *  the old slot. Default impl in this base class; overrides only
     *  needed if a transport wants to derive it from elsewhere. */
    virtual QString peerMachineId() const { return _peerMachineId; }
    virtual void setPeerMachineId(const QString &id) { _peerMachineId = id; }

    /** \brief Wall-clock ms of the last frame this peer was seen
     *  sending. Updated by the wire-protocol layer. Used together
     *  with the periodic heartbeat to drop silent zombies. */
    qint64 lastSeenMs() const { return _lastSeenMs; }
    void touchLastSeen(qint64 nowMs) { _lastSeenMs = nowMs; }

    /** \brief Close the transport. Idempotent. The corresponding
     *  \ref disconnected signal fires once the close completes. */
    virtual void closeConnection() = 0;

protected:
    QString _peerMachineId;
    qint64 _lastSeenMs = 0;

signals:
    /** \brief One reassembled framed payload arrived. The bytes are
     *  exactly what the sender passed to \ref sendMessage. */
    void messageReceived(const QByteArray &payload);

    /** \brief Transport went down (peer hung up, network drop, DTLS
     *  failure, explicit close). Receiver should treat this peer as
     *  gone and prune any state tied to it. */
    void disconnected();
};

#endif // IPEERLINK_H
