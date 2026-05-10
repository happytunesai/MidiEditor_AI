/*
 * MidiEditor AI
 *
 * UDP multicast discovery for the LAN Live Mode (Plan §11.4).
 *
 * Host side: announce a 4-character pairing code on the multicast
 * group every second. Each announcement carries the host's TCP port,
 * sessionId, and displayName.
 *
 * Client side: listen on the same multicast group, filter incoming
 * announcements by user-entered pairing code, emit peerFound() when
 * a matching announcement arrives.
 *
 * Wire format (ASCII, '|'-separated, single UDP datagram):
 *
 *   MIDIEDIT-PR-LAN|v1|<sessionId>|<displayName>|<tcpPort>|<code>
 *
 * - The leading literal "MIDIEDIT-PR-LAN" is a quick filter for stray
 *   datagrams from unrelated apps on the same multicast group.
 * - Version "v1" lets us evolve the format later.
 * - The pairing code is 4 alphanumeric chars (~1.7M possibilities).
 *
 * No encryption: LAN is implicitly trusted in v1, and the pairing code
 * is the auth boundary. WAN/encrypted variants live in Phase 9.6+.
 */

#ifndef LANDISCOVERY_H
#define LANDISCOVERY_H

#include <QHostAddress>
#include <QObject>
#include <QString>

class QTimer;
class QUdpSocket;

class LanDiscovery : public QObject {
    Q_OBJECT

public:
    static constexpr const char *kMulticastAddress = "239.42.42.42";
    static constexpr quint16 kMulticastPort = 42424;
    static constexpr int kAnnounceIntervalMs = 1000;
    static constexpr const char *kProtocolMagic = "MIDIEDIT-PR-LAN";
    static constexpr const char *kProtocolVersion = "v1";

    explicit LanDiscovery(QObject *parent = nullptr);
    ~LanDiscovery() override;

    /**
     * \brief Start announcing this host on the multicast group.
     *
     * Sends one announcement immediately, then every kAnnounceIntervalMs
     * until stopAnnouncing() is called or the object is destroyed.
     */
    bool startAnnouncing(const QString &sessionId,
                         const QString &displayName,
                         quint16 tcpPort,
                         const QString &pairingCode);

    void stopAnnouncing();

    /**
     * \brief Start listening for announcements, filtering by pairing code.
     *
     * When an announcement matching \a pairingCode arrives, peerFound()
     * is emitted with the resolved host IP and TCP port. Listening
     * continues until stopListening() is called or the object is
     * destroyed (you typically stop listening as soon as a match is
     * found and the TCP connection is established).
     */
    bool startListening(const QString &pairingCode);

    void stopListening();

    /**
     * \brief Generate a fresh 4-character pairing code.
     *
     * Uses upper-case A-Z and 2-9 (skipping 0/1/I/O for readability).
     * ~30^4 ≈ 800K possibilities — sufficient for short-lived pairing.
     */
    static QString generatePairingCode();

signals:
    /**
     * \brief A peer announcement matching the listen filter was received.
     *
     * \param sessionId   the host's file sessionId (for cross-session check)
     * \param displayName human-readable host name from CollabIdentity
     * \param hostAddress source IP of the announcement (where to TCP-connect)
     * \param tcpPort     port the host's TCP server is listening on
     */
    void peerFound(const QString &sessionId,
                   const QString &displayName,
                   const QHostAddress &hostAddress,
                   quint16 tcpPort);

private slots:
    void emitAnnouncement();
    void readPendingDatagrams();

private:
    QUdpSocket *_announceSocket = nullptr;
    QUdpSocket *_listenSocket = nullptr;
    QTimer *_announceTimer = nullptr;

    // Announce-side state
    QString _sessionId;
    QString _displayName;
    quint16 _tcpPort = 0;
    QString _pairingCode;

    // Listen-side state
    QString _listenCode;
};

#endif // LANDISCOVERY_H
