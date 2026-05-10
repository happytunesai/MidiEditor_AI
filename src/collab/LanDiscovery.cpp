/*
 * MidiEditor AI - LanDiscovery implementation.
 */

#include "LanDiscovery.h"

#include <QByteArray>
#include <QLoggingCategory>
#include <QNetworkDatagram>
#include <QNetworkInterface>
#include <QRandomGenerator>
#include <QStringList>
#include <QTimer>
#include <QUdpSocket>

Q_LOGGING_CATEGORY(lanLog, "midieditor.collab.lan")

LanDiscovery::LanDiscovery(QObject *parent)
    : QObject(parent) {}

LanDiscovery::~LanDiscovery() {
    stopAnnouncing();
    stopListening();
}

bool LanDiscovery::startAnnouncing(const QString &sessionId,
                                    const QString &displayName,
                                    quint16 tcpPort,
                                    const QString &pairingCode) {
    stopAnnouncing();

    _sessionId = sessionId;
    _displayName = displayName;
    _tcpPort = tcpPort;
    _pairingCode = pairingCode;

    _announceSocket = new QUdpSocket(this);
    // Bind to ephemeral local port so the OS picks one. We're a sender,
    // not a receiver, on this socket.
    if (!_announceSocket->bind(QHostAddress::AnyIPv4, 0, QUdpSocket::ShareAddress)) {
        qCWarning(lanLog) << "announce: bind failed:" << _announceSocket->errorString();
        delete _announceSocket;
        _announceSocket = nullptr;
        return false;
    }
    qCInfo(lanLog) << "announce: bound to ephemeral port" << _announceSocket->localPort()
                   << "code=" << pairingCode << "tcpPort=" << tcpPort;

    _announceTimer = new QTimer(this);
    _announceTimer->setInterval(kAnnounceIntervalMs);
    connect(_announceTimer, &QTimer::timeout, this, &LanDiscovery::emitAnnouncement);
    _announceTimer->start();

    // Emit one immediately so a client doesn't have to wait up to a full
    // tick for the first packet.
    emitAnnouncement();
    return true;
}

void LanDiscovery::stopAnnouncing() {
    if (_announceTimer) {
        _announceTimer->stop();
        _announceTimer->deleteLater();
        _announceTimer = nullptr;
    }
    if (_announceSocket) {
        _announceSocket->deleteLater();
        _announceSocket = nullptr;
    }
}

void LanDiscovery::emitAnnouncement() {
    if (!_announceSocket) return;
    QByteArray pkt = QStringLiteral("%1|%2|%3|%4|%5|%6")
                         .arg(QString::fromLatin1(kProtocolMagic),
                              QString::fromLatin1(kProtocolVersion),
                              _sessionId,
                              _displayName,
                              QString::number(_tcpPort),
                              _pairingCode)
                         .toUtf8();
    QHostAddress group(QString::fromLatin1(kMulticastAddress));

    // Windows often has multiple active IPv4 interfaces (Wi-Fi + Ethernet +
    // Hyper-V switch + VMware bridge + VPN tap + …). The default outgoing
    // multicast interface is whatever the routing table picks first, which
    // is frequently NOT the real LAN. Send the announcement out every
    // viable IPv4 interface so the client-side listener — which already
    // joins the group on every interface — sees it on at least one path.
    int sent = 0;
    int interfacesSeen = 0;
    for (const QNetworkInterface &iface : QNetworkInterface::allInterfaces()) {
        if (!(iface.flags() & QNetworkInterface::IsUp)) continue;
        if (!(iface.flags() & QNetworkInterface::IsRunning)) continue;
        if (iface.flags() & QNetworkInterface::IsLoopBack) continue;
        if (!(iface.flags() & QNetworkInterface::CanMulticast)) continue;
        bool hasIPv4 = false;
        for (const QNetworkAddressEntry &ae : iface.addressEntries()) {
            if (ae.ip().protocol() == QAbstractSocket::IPv4Protocol) { hasIPv4 = true; break; }
        }
        if (!hasIPv4) continue;
        ++interfacesSeen;
        _announceSocket->setMulticastInterface(iface);
        qint64 wrote = _announceSocket->writeDatagram(pkt, group, kMulticastPort);
        if (wrote > 0) ++sent;
        else qCWarning(lanLog) << "announce: writeDatagram via" << iface.humanReadableName()
                                << "failed:" << _announceSocket->errorString();
    }
    if (interfacesSeen == 0) {
        // Fallback for hosts with no qualifying interface (e.g. some VPN-only
        // configs): let the OS pick whatever it considers the default.
        qint64 wrote = _announceSocket->writeDatagram(pkt, group, kMulticastPort);
        if (wrote > 0) ++sent;
    }
    static int s_announceCounter = 0;
    if ((s_announceCounter++ % 5) == 0) {
        // Every 5s, emit one log line so the file isn't drowned but we can
        // still verify the announce loop is alive.
        qCInfo(lanLog) << "announce: tick — code=" << _pairingCode
                       << "interfaces=" << interfacesSeen << "sent=" << sent;
    }
}

bool LanDiscovery::startListening(const QString &pairingCode) {
    stopListening();
    _listenCode = pairingCode;

    _listenSocket = new QUdpSocket(this);
    // Bind to multicast port with ShareAddress so multiple peers on the
    // same machine could in theory listen — uncommon, but safe.
    if (!_listenSocket->bind(QHostAddress::AnyIPv4, kMulticastPort,
                              QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        qCWarning(lanLog) << "listen: bind to multicast port failed:"
                          << _listenSocket->errorString();
        delete _listenSocket;
        _listenSocket = nullptr;
        return false;
    }
    qCInfo(lanLog) << "listen: bound to UDP" << kMulticastPort << "for code" << pairingCode;
    // Join multicast group on every active IPv4 interface — needed on
    // Windows where the default interface alone is not always the right
    // one (e.g. when both Wi-Fi and Ethernet are up).
    bool joined = false;
    int joinedCount = 0;
    QHostAddress group(QString::fromLatin1(kMulticastAddress));
    for (const QNetworkInterface &iface : QNetworkInterface::allInterfaces()) {
        if (!(iface.flags() & QNetworkInterface::IsUp)) continue;
        if (!(iface.flags() & QNetworkInterface::IsRunning)) continue;
        if (iface.flags() & QNetworkInterface::IsLoopBack) continue;
        if (_listenSocket->joinMulticastGroup(group, iface)) {
            joined = true;
            ++joinedCount;
            qCInfo(lanLog) << "listen: joined multicast on" << iface.humanReadableName();
        } else {
            qCDebug(lanLog) << "listen: joinMulticastGroup failed on"
                            << iface.humanReadableName() << ":"
                            << _listenSocket->errorString();
        }
    }
    if (!joined) {
        // Last-ditch fallback: try without an explicit interface.
        joined = _listenSocket->joinMulticastGroup(group);
        if (joined) qCInfo(lanLog) << "listen: joined multicast via OS-default interface";
    }
    if (!joined) {
        qCWarning(lanLog) << "listen: could not join multicast on any interface — abort";
        _listenSocket->deleteLater();
        _listenSocket = nullptr;
        return false;
    }
    qCInfo(lanLog) << "listen: ready, joined on" << joinedCount << "interface(s)";

    connect(_listenSocket, &QUdpSocket::readyRead,
            this, &LanDiscovery::readPendingDatagrams);
    return true;
}

void LanDiscovery::stopListening() {
    if (_listenSocket) {
        _listenSocket->deleteLater();
        _listenSocket = nullptr;
    }
    _listenCode.clear();
}

void LanDiscovery::readPendingDatagrams() {
    // peerFound() listeners commonly call stopListening() (we found our
    // host, stop scanning) which sets _listenSocket to nullptr via
    // deleteLater. We must re-check the member on every iteration —
    // otherwise the next loop dereferences null. This bug crashed
    // every LAN join attempt with EXCEPTION_ACCESS_VIOLATION before.
    while (_listenSocket && _listenSocket->hasPendingDatagrams()) {
        QNetworkDatagram dg = _listenSocket->receiveDatagram();
        QByteArray data = dg.data();
        // Quick magic prefix filter; cheaper than parsing a foreign packet.
        QByteArray expected = QByteArray(kProtocolMagic) + '|';
        if (!data.startsWith(expected)) continue;

        QString text = QString::fromUtf8(data);
        QStringList parts = text.split(QLatin1Char('|'));
        // MAGIC | v1 | sessionId | displayName | tcpPort | code  → 6 parts
        if (parts.size() < 6) {
            qCDebug(lanLog) << "listen: malformed datagram from" << dg.senderAddress();
            continue;
        }
        if (parts[1] != QLatin1String(kProtocolVersion)) {
            qCDebug(lanLog) << "listen: version mismatch" << parts[1];
            continue;
        }
        if (parts[5] != _listenCode) {
            // Different session sharing the same multicast group — not
            // necessarily a problem, just not for us.
            qCDebug(lanLog) << "listen: code mismatch — got" << parts[5]
                            << "want" << _listenCode << "from" << dg.senderAddress();
            continue;
        }

        bool ok = false;
        quint16 port = parts[4].toUShort(&ok);
        if (!ok) {
            qCWarning(lanLog) << "listen: bad port in datagram:" << parts[4];
            continue;
        }
        qCInfo(lanLog) << "listen: matched code" << _listenCode
                       << "from" << parts[3] << "@" << dg.senderAddress() << ":" << port;
        emit peerFound(parts[2], parts[3], dg.senderAddress(), port);
    }
}

QString LanDiscovery::generatePairingCode() {
    // Drop visually ambiguous characters (0/O, 1/I) so users can read codes
    // out loud without errors.
    static const char *kAlphabet = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    const int alphabetLen = 31;  // length of kAlphabet
    QString code;
    code.reserve(4);
    for (int i = 0; i < 4; ++i) {
        code.append(QLatin1Char(kAlphabet[QRandomGenerator::global()->bounded(alphabetLen)]));
    }
    return code;
}
