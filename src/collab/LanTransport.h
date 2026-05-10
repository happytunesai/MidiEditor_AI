/*
 * MidiEditor AI
 *
 * TCP transport for LAN Live Mode (Plan §11.1, §11.3).
 *
 * Two roles:
 *   - LanServer (host): QTcpServer that accepts incoming clients,
 *     keeps a list of LanPeerSocket per accepted connection.
 *   - LanClient: single QTcpSocket connecting to a host's TCP port.
 *
 * Both roles share the same wire framing: a 4-byte big-endian length
 * prefix followed by the payload bytes. Payloads are JSON UTF-8 by
 * convention (PrBundle JSON, hello/heartbeat frames). Anything that
 * reduces to a QByteArray works.
 *
 * No encryption — LAN-only mode is implicitly trusted (Plan §11.9).
 */

#ifndef LANTRANSPORT_H
#define LANTRANSPORT_H

#include <QByteArray>
#include <QHostAddress>
#include <QObject>
#include <QString>

#include "IPeerLink.h"
#include "ILiveServer.h"
#include "ILiveClient.h"

class QTcpServer;
class QTcpSocket;

/**
 * \class LanPeerSocket
 *
 * \brief Wraps one QTcpSocket with length-prefix framing.
 *
 * Used by both LanServer (one per accepted client) and LanClient (the
 * single socket to the host). Exposes a write-message API and emits
 * messageReceived() once a complete framed payload arrives.
 */
class LanPeerSocket : public IPeerLink {
    Q_OBJECT

public:
    explicit LanPeerSocket(QTcpSocket *socket, QObject *parent = nullptr);
    ~LanPeerSocket() override;

    /** \brief Send a framed message. Adds the 4-byte length prefix. */
    bool sendMessage(const QByteArray &payload) override;

    /** \brief True while the underlying TCP socket is in ConnectedState. */
    bool isConnected() const override;

    /** \brief Underlying socket for state queries (state, peerAddress). */
    QTcpSocket *socket() const { return _socket; }

    /** \brief Human-readable peer label, set by higher-level code. */
    QString peerLabel() const override { return _peerLabel; }
    void setPeerLabel(const QString &label) override { _peerLabel = label; }

    /** \brief IPeerLink override that disconnects the underlying socket. */
    void closeConnection() override;

private slots:
    void onReadyRead();

private:
    QTcpSocket *_socket;
    QByteArray _readBuffer;
    QString _peerLabel;
    quint32 _expectedSize = 0;       // 0 = next bytes are the 4-byte size prefix
};

/**
 * \class LanServer
 *
 * \brief Host side of the LAN transport. Listens on a dynamically
 *        chosen TCP port, accepts incoming connections, owns a
 *        LanPeerSocket per accepted client.
 */
class LanServer : public ILiveServer {
    Q_OBJECT

public:
    explicit LanServer(QObject *parent = nullptr);
    ~LanServer() override;

    /**
     * \brief Start listening on an OS-assigned port.
     * \return The listening port, or 0 on failure.
     */
    quint16 startListening();

    // ILiveServer overrides — same behavior, transport-agnostic types.
    void stop() override;
    bool broadcast(const QByteArray &payload) override;
    bool broadcastExcept(const QByteArray &payload, IPeerLink *exclude) override;
    int peerCount() const override;
    QList<IPeerLink *> peers() const override;

private slots:
    void onNewConnection();
    void onPeerDisconnected();

private:
    QTcpServer *_server = nullptr;
    QList<LanPeerSocket *> _peers;
};

/**
 * \class LanClient
 *
 * \brief Client side of the LAN transport. Connects to a host and
 *        wraps the socket in a LanPeerSocket for framing.
 */
class LanClient : public ILiveClient {
    Q_OBJECT

public:
    explicit LanClient(QObject *parent = nullptr);
    ~LanClient() override;

    /**
     * \brief Initiate a connection. Async — emits connected() on success,
     *        connectionFailed() on error.
     */
    void connectToHost(const QHostAddress &host, quint16 port);

    void disconnectFromHost() override;

    /** \brief Send one message to the host. False if not connected. */
    bool sendMessage(const QByteArray &payload) override;

    /** \brief True when the underlying socket is in ConnectedState. */
    bool isConnected() const override;

    // connected/connectionFailed/disconnected/messageReceived signals
    // are inherited from ILiveClient.

private slots:
    void onSocketConnected();
    void onSocketError();
    void onSocketDisconnected();

private:
    QTcpSocket *_socket = nullptr;
    LanPeerSocket *_wrapper = nullptr;
};

#endif // LANTRANSPORT_H
