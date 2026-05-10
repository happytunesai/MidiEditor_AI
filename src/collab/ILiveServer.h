/*
 * MidiEditor AI
 *
 * ILiveServer — transport-agnostic interface for the host-side
 * connection manager that holds N peer links and exposes a
 * broadcast / per-peer routing API. Implemented by LanServer
 * (TCP/multicast LAN) and WebRtcLiveServer (one peer for v1, multi-
 * peer planned later).
 *
 * Signals carry IPeerLink* so callers (LanLiveSession's wire-protocol
 * handlers) don't have to know whether the peer arrived over LAN
 * or over a WebRTC data channel.
 */

#ifndef ILIVESERVER_H
#define ILIVESERVER_H

#include <QByteArray>
#include <QList>
#include <QObject>

#include "IPeerLink.h"

class ILiveServer : public QObject {
    Q_OBJECT
public:
    explicit ILiveServer(QObject *parent = nullptr) : QObject(parent) {}
    ~ILiveServer() override = default;

    /** \brief Number of currently-connected peers. */
    virtual int peerCount() const = 0;

    /** \brief Read-only view of connected peers. */
    virtual QList<IPeerLink *> peers() const = 0;

    /** \brief Broadcast a framed payload to every peer.
     *  \return true iff every connected peer accepted the send (zero peers
     *          counts as success — there's nothing to lose).  A `false`
     *          here means the caller must NOT advance any sync baseline,
     *          otherwise the unsent edits are lost silently (BUG-COLLAB-021). */
    virtual bool broadcast(const QByteArray &payload) = 0;

    /** \brief Broadcast to every peer except \a exclude. Used to avoid
     *  echoing a peer's own forwarded hunks back to them.
     *  \return same semantics as broadcast(). */
    virtual bool broadcastExcept(const QByteArray &payload,
                                 IPeerLink *exclude) = 0;

    /** \brief Tear down: disconnect peers, stop listening. */
    virtual void stop() = 0;

signals:
    void peerConnected(IPeerLink *peer);
    void peerDisconnected(IPeerLink *peer);
    void messageReceived(IPeerLink *peer, const QByteArray &payload);
};

#endif // ILIVESERVER_H
