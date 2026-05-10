/*
 * MidiEditor AI - LanTransport implementation.
 */

#include "LanTransport.h"

#include <QHostAddress>
#include <QLoggingCategory>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtEndian>

Q_DECLARE_LOGGING_CATEGORY(lanLog)

// =============================================================================
// LanPeerSocket
// =============================================================================

LanPeerSocket::LanPeerSocket(QTcpSocket *socket, QObject *parent)
    : IPeerLink(parent), _socket(socket) {
    if (_socket) {
        _socket->setParent(this);
        connect(_socket, &QTcpSocket::readyRead, this, &LanPeerSocket::onReadyRead);
        connect(_socket, &QTcpSocket::disconnected, this, &IPeerLink::disconnected);
    }
}

LanPeerSocket::~LanPeerSocket() = default;

bool LanPeerSocket::sendMessage(const QByteArray &payload) {
    if (!_socket || _socket->state() != QAbstractSocket::ConnectedState) return false;
    if (payload.size() > std::numeric_limits<qint32>::max()) return false;
    quint32 len = static_cast<quint32>(payload.size());
    QByteArray header(4, 0);
    qToBigEndian(len, reinterpret_cast<uchar *>(header.data()));
    _socket->write(header);
    _socket->write(payload);
    return true;
}

bool LanPeerSocket::isConnected() const {
    return _socket && _socket->state() == QAbstractSocket::ConnectedState;
}

void LanPeerSocket::closeConnection() {
    if (_socket) _socket->disconnectFromHost();
}

void LanPeerSocket::onReadyRead() {
    if (!_socket) return;
    _readBuffer.append(_socket->readAll());
    while (true) {
        if (_expectedSize == 0) {
            // We're between messages; read the 4-byte size prefix.
            if (_readBuffer.size() < 4) return;
            _expectedSize = qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(_readBuffer.constData()));
            _readBuffer.remove(0, 4);
            // Sanity cap: 32 MB per frame. Anything bigger is almost
            // certainly garbage / hostile; close the connection rather
            // than try to allocate.
            if (_expectedSize > 32u * 1024u * 1024u) {
                if (_socket) _socket->disconnectFromHost();
                _expectedSize = 0;
                _readBuffer.clear();
                return;
            }
        }
        if (static_cast<quint32>(_readBuffer.size()) < _expectedSize) return;
        QByteArray payload = _readBuffer.left(static_cast<int>(_expectedSize));
        _readBuffer.remove(0, static_cast<int>(_expectedSize));
        _expectedSize = 0;
        emit messageReceived(payload);
        // The receiver may have torn down the session (leaveSession etc.)
        // synchronously, which destroys this object. Bail out before
        // touching any member again.
        if (!_socket) return;
    }
}

// =============================================================================
// LanServer
// =============================================================================

LanServer::LanServer(QObject *parent) : ILiveServer(parent) {}

LanServer::~LanServer() { stop(); }

quint16 LanServer::startListening() {
    stop();
    _server = new QTcpServer(this);
    if (!_server->listen(QHostAddress::AnyIPv4, 0)) {
        qCWarning(lanLog) << "server: TCP listen failed:" << _server->errorString();
        delete _server;
        _server = nullptr;
        return 0;
    }
    qCInfo(lanLog) << "server: TCP listening on port" << _server->serverPort();
    connect(_server, &QTcpServer::newConnection,
            this, &LanServer::onNewConnection);
    return _server->serverPort();
}

void LanServer::stop() {
    for (LanPeerSocket *p : _peers) {
        if (p->socket()) p->socket()->disconnectFromHost();
        p->deleteLater();
    }
    _peers.clear();
    if (_server) {
        _server->close();
        _server->deleteLater();
        _server = nullptr;
    }
}

void LanServer::onNewConnection() {
    // _server can be torn down during emit peerConnected() if the receiver
    // calls leaveSession() (e.g. snapshot encode failed). Re-check on
    // every iteration to avoid dereferencing a stopped server.
    while (_server && _server->hasPendingConnections()) {
        QTcpSocket *raw = _server->nextPendingConnection();
        if (!raw) continue;
        qCInfo(lanLog) << "server: new peer accepted from"
                       << raw->peerAddress() << ":" << raw->peerPort();
        LanPeerSocket *p = new LanPeerSocket(raw, this);
        _peers.append(p);
        connect(p, &IPeerLink::messageReceived,
                this, [this, p](const QByteArray &payload) {
                    emit messageReceived(p, payload);
                });
        connect(p, &IPeerLink::disconnected,
                this, &LanServer::onPeerDisconnected);
        emit peerConnected(p);
    }
}

void LanServer::onPeerDisconnected() {
    LanPeerSocket *p = qobject_cast<LanPeerSocket *>(sender());
    if (!p) return;
    _peers.removeAll(p);
    emit peerDisconnected(p);
    p->deleteLater();
}

bool LanServer::broadcast(const QByteArray &payload) {
    bool allOk = true;
    for (LanPeerSocket *p : _peers) {
        if (!p->sendMessage(payload)) allOk = false;
    }
    return allOk;
}

bool LanServer::broadcastExcept(const QByteArray &payload, IPeerLink *exclude) {
    bool allOk = true;
    for (LanPeerSocket *p : _peers) {
        if (static_cast<IPeerLink *>(p) == exclude) continue;
        if (!p->sendMessage(payload)) allOk = false;
    }
    return allOk;
}

int LanServer::peerCount() const { return _peers.size(); }

QList<IPeerLink *> LanServer::peers() const {
    QList<IPeerLink *> out;
    out.reserve(_peers.size());
    for (LanPeerSocket *p : _peers) out.append(p);
    return out;
}

// =============================================================================
// LanClient
// =============================================================================

LanClient::LanClient(QObject *parent) : ILiveClient(parent) {}

LanClient::~LanClient() { disconnectFromHost(); }

void LanClient::connectToHost(const QHostAddress &host, quint16 port) {
    disconnectFromHost();
    qCInfo(lanLog) << "client: connectToHost" << host.toString() << ":" << port;
    _socket = new QTcpSocket(this);
    connect(_socket, &QTcpSocket::connected,
            this, &LanClient::onSocketConnected);
    connect(_socket, &QTcpSocket::disconnected,
            this, &LanClient::onSocketDisconnected);
    connect(_socket, &QAbstractSocket::errorOccurred,
            this, &LanClient::onSocketError);
    _socket->connectToHost(host, port);
}

void LanClient::disconnectFromHost() {
    if (_wrapper) {
        _wrapper->deleteLater();  // takes ownership of the socket via setParent in ctor
        _wrapper = nullptr;
        _socket = nullptr;
        return;
    }
    if (_socket) {
        _socket->disconnectFromHost();
        _socket->deleteLater();
        _socket = nullptr;
    }
}

void LanClient::onSocketConnected() {
    qCInfo(lanLog) << "client: TCP connected to"
                   << _socket->peerAddress() << ":" << _socket->peerPort();
    // Wrap the socket only AFTER connect succeeds — LanPeerSocket's
    // readyRead handler is only meaningful on a connected stream.
    _wrapper = new LanPeerSocket(_socket, this);
    connect(_wrapper, &IPeerLink::messageReceived,
            this, &LanClient::messageReceived);
    connect(_wrapper, &IPeerLink::disconnected,
            this, &LanClient::disconnected);
    emit connected();
}

void LanClient::onSocketError() {
    if (!_socket) return;
    QString reason = _socket->errorString();
    qCWarning(lanLog) << "client: socket error:" << reason
                      << "state=" << _socket->state();
    emit connectionFailed(reason);
}

void LanClient::onSocketDisconnected() {
    // We always emit disconnected via the wrapper if we have one, since
    // it sees the same signal. Avoid duplicate emission by no-op'ing here
    // when wrapped. If the disconnect happens BEFORE we wrap (refused
    // connection), surface it once via connectionFailed instead.
    if (!_wrapper) emit disconnected();
}

bool LanClient::sendMessage(const QByteArray &payload) {
    if (!_wrapper) return false;
    return _wrapper->sendMessage(payload);
}

bool LanClient::isConnected() const {
    return _socket && _socket->state() == QAbstractSocket::ConnectedState;
}
