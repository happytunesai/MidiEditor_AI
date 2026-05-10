/*
 * MidiEditor AI
 *
 * ILiveClient — transport-agnostic interface for the client side
 * (one outbound connection to a host). Implemented by LanClient
 * (one TCP socket) and WebRtcLiveClient (one WebRTC data channel
 * after manual offer/answer signaling).
 */

#ifndef ILIVECLIENT_H
#define ILIVECLIENT_H

#include <QByteArray>
#include <QObject>
#include <QString>

class ILiveClient : public QObject {
    Q_OBJECT
public:
    explicit ILiveClient(QObject *parent = nullptr) : QObject(parent) {}
    ~ILiveClient() override = default;

    virtual bool sendMessage(const QByteArray &payload) = 0;
    virtual bool isConnected() const = 0;
    virtual void disconnectFromHost() = 0;

signals:
    void connected();
    void connectionFailed(const QString &reason);
    void disconnected();
    void messageReceived(const QByteArray &payload);
};

#endif // ILIVECLIENT_H
