/*
 * MidiEditor AI - WebRtcTransport implementation.
 */

#ifdef MIDIEDITOR_WEBRTC_ENABLED

#include "WebRtcTransport.h"

#include <QLoggingCategory>
#include <QTimer>
#include <QtEndian>

#include <rtc/rtc.hpp>

#include "IceConfig.h"

Q_LOGGING_CATEGORY(rtcLog, "midieditor.collab.rtc")

WebRtcTransport::WebRtcTransport(Role role, QObject *parent)
    : IPeerLink(parent), _role(role) {}

WebRtcTransport::~WebRtcTransport() {
    // BUG-COLLAB-023: tear down in a fixed order so libdatachannel
    // worker-thread callbacks can't race us.
    //   1. _alive = false  — capturing lambdas bail early.
    //   2. Reset every callback to a no-op so libdatachannel stops
    //      dispatching new ones to our (about-to-be-destroyed)
    //      member functions.  This is required even with the _alive
    //      check because the lambda body itself may run while the
    //      QObject vtable is being torn down — letting the lambda
    //      reach `emit transportFailed(...)` would dereference a
    //      half-destroyed object.
    //   3. close()         — synchronous tear down of the SCTP/DTLS.
    _alive = false;
    if (_dc) {
        _dc->onMessage(nullptr);
        _dc->onOpen(nullptr);
        _dc->onClosed(nullptr);
    }
    if (_pc) {
        _pc->onStateChange(nullptr);
        _pc->onGatheringStateChange(nullptr);
        _pc->onLocalCandidate(nullptr);
        _pc->onDataChannel(nullptr);
    }
    if (_dc) _dc->close();
    if (_pc) _pc->close();
}

void WebRtcTransport::closeConnection() {
    // closeConnection is callable from anywhere — typically the
    // session manager wants the channel down but the WebRtcTransport
    // object still alive (e.g. ahead of deleteLater).  We don't flip
    // _alive here because more callbacks (Closed → emit disconnected)
    // are still expected.
    if (_dc) _dc->close();
    if (_pc) _pc->close();
}

void WebRtcTransport::start(const QStringList &iceServersOverride,
                             int gatheringTimeoutMs,
                             bool useDefaultIceServersIfEmpty) {
    rtc::Configuration cfg;
    QStringList ice;
    if (!iceServersOverride.isEmpty()) {
        ice = iceServersOverride;
    } else if (useDefaultIceServersIfEmpty) {
        ice = IceConfig::load();
    }
    // else: explicit "no servers" — host candidates only.
    for (const QString &uri : ice) {
        try {
            cfg.iceServers.emplace_back(uri.toStdString());
        } catch (const std::exception &e) {
            qCWarning(rtcLog) << "ignoring malformed ICE URI" << uri << e.what();
        }
    }
    // libdatachannel defaults to a 64 KB max-message-size per the
    // SCTP/WebRTC standard. A single Insert / Delete-measures action on
    // a multi-thousand-event MIDI file shifts every event past the
    // insertion point, producing a hunks-frame that easily exceeds
    // 64 KB — `_dc->send` then returns "Message size exceeds limit"
    // and the change is silently lost on the wire. We bump our local
    // max-message-size to 16 MiB; both peers run MidiEditor so the
    // SDP negotiation aligns, and the existing 24 MiB filetransfer
    // ceiling remains the actual upper bound (LanLiveSession enforces
    // it before the bytes ever hit the transport).
    cfg.maxMessageSize = static_cast<size_t>(16) * 1024 * 1024;
    qCInfo(rtcLog) << "starting" << (_role == Role::Initiator ? "initiator" : "responder")
                   << "with" << cfg.iceServers.size() << "ICE server(s)"
                   << "(gathering timeout" << gatheringTimeoutMs << "ms)";

    _pc = std::make_shared<rtc::PeerConnection>(cfg);

    // Lifecycle hooks — log + emit Qt signals. libdatachannel's
    // callbacks fire on its internal worker thread; emitting Qt
    // signals across threads is safe because all our slot connections
    // use Qt::AutoConnection (= QueuedConnection across threads).
    _pc->onStateChange([this](rtc::PeerConnection::State s) {
        if (!_alive.load()) return;  // BUG-COLLAB-023
        QString name;
        switch (s) {
            case rtc::PeerConnection::State::New:          name = "New"; break;
            case rtc::PeerConnection::State::Connecting:   name = "Connecting"; break;
            case rtc::PeerConnection::State::Connected:    name = "Connected"; break;
            case rtc::PeerConnection::State::Disconnected: name = "Disconnected"; break;
            case rtc::PeerConnection::State::Failed:       name = "Failed"; break;
            case rtc::PeerConnection::State::Closed:       name = "Closed"; break;
        }
        qCInfo(rtcLog) << "pc state →" << name;
        if (s == rtc::PeerConnection::State::Failed
         || s == rtc::PeerConnection::State::Disconnected
         || s == rtc::PeerConnection::State::Closed) {
            emit transportFailed(name);
            emit IPeerLink::disconnected();
        }
    });

    _pc->onGatheringStateChange([this](rtc::PeerConnection::GatheringState s) {
        if (!_alive.load()) return;  // BUG-COLLAB-023
        if (s == rtc::PeerConnection::GatheringState::Complete) {
            qCInfo(rtcLog) << "gathering complete — local SDP is finalized";
            emit gatheringComplete();
            // Once gathering is complete, the local description has
            // every candidate inlined and is suitable for shipping
            // via signaling token. Publish it now (idempotent).
            publishLocalDescription();
        }
    });

    // Gathering safety timer: a slow or blocked STUN endpoint must
    // not deadlock the offer/answer publish. After the timeout, we
    // publish whatever the local description contains (host candidates
    // are usually enough for LAN-adjacent peers; over WAN, missing
    // srflx means connection will likely fail and the user gets the
    // "couldn't establish a direct connection" message via the
    // PeerConnection state-change to Failed).
    if (gatheringTimeoutMs > 0) {
        QTimer::singleShot(gatheringTimeoutMs, this, [this, gatheringTimeoutMs]() {
            if (_localDescriptionPublished) return;
            qCWarning(rtcLog) << "gathering timeout (" << gatheringTimeoutMs
                              << "ms) — publishing local description with current "
                              << "candidates";
            publishLocalDescription();
        });
    }

    _pc->onLocalCandidate([](rtc::Candidate cand) {
        qCDebug(rtcLog) << "local candidate" << QString::fromStdString(std::string(cand));
    });

    if (_role == Role::Initiator) {
        // Initiator owns the data channel creation. Responder receives
        // it via onDataChannel below.
        _dc = _pc->createDataChannel("midieditor");
        wireDataChannel(_dc);
        _pc->setLocalDescription();  // creates offer
        qCInfo(rtcLog) << "initiator: offer creation kicked off";
    } else {
        _pc->onDataChannel([this](std::shared_ptr<rtc::DataChannel> dc) {
            if (!_alive.load()) return;  // BUG-COLLAB-023
            qCInfo(rtcLog) << "responder: peer data channel arrived";
            _dc = dc;
            wireDataChannel(_dc);
        });
        qCInfo(rtcLog) << "responder: awaiting remote offer via setRemoteOffer";
    }
}

void WebRtcTransport::publishLocalDescription() {
    if (!_pc) return;
    if (_localDescriptionPublished) return;  // emit-once guard
    auto desc = _pc->localDescription();
    if (!desc) return;
    QString sdp = QString::fromStdString(std::string(*desc));
    _localDescriptionPublished = true;
    qCInfo(rtcLog) << "publishing" << (_role == Role::Initiator ? "offer" : "answer")
                   << "(" << sdp.size() << "bytes)";
    if (_role == Role::Initiator) emit offerReady(sdp);
    else                          emit answerReady(sdp);
}

void WebRtcTransport::setRemoteOffer(const QString &sdp) {
    if (!_pc) {
        qCWarning(rtcLog) << "setRemoteOffer called before start()";
        return;
    }
    if (_role != Role::Responder) {
        qCWarning(rtcLog) << "setRemoteOffer called on Initiator — ignoring";
        return;
    }
    try {
        rtc::Description offer(sdp.toStdString(), rtc::Description::Type::Offer);
        _pc->setRemoteDescription(offer);
        // setRemoteDescription auto-creates the answer for the
        // responder; our gatheringComplete handler publishes it.
        qCInfo(rtcLog) << "responder: remote offer set, generating answer";
    } catch (const std::exception &e) {
        qCWarning(rtcLog) << "setRemoteOffer failed:" << e.what();
        QString reason = QString::fromUtf8(e.what());
        emit transportFailed(reason);
        emit IPeerLink::disconnected();
    }
}

void WebRtcTransport::setRemoteAnswer(const QString &sdp) {
    if (!_pc) {
        qCWarning(rtcLog) << "setRemoteAnswer called before start()";
        return;
    }
    if (_role != Role::Initiator) {
        qCWarning(rtcLog) << "setRemoteAnswer called on Responder — ignoring";
        return;
    }
    try {
        rtc::Description answer(sdp.toStdString(), rtc::Description::Type::Answer);
        _pc->setRemoteDescription(answer);
        qCInfo(rtcLog) << "initiator: remote answer set; DTLS handshake in progress";
    } catch (const std::exception &e) {
        qCWarning(rtcLog) << "setRemoteAnswer failed:" << e.what();
        QString reason = QString::fromUtf8(e.what());
        emit transportFailed(reason);
        emit IPeerLink::disconnected();
    }
}

void WebRtcTransport::wireDataChannel(std::shared_ptr<rtc::DataChannel> dc) {
    if (!dc) return;
    dc->onOpen([this]() {
        if (!_alive.load()) return;  // BUG-COLLAB-023
        onDataChannelOpen();
    });
    dc->onClosed([this]() {
        if (!_alive.load()) return;  // BUG-COLLAB-023
        onDataChannelClosed();
    });
    dc->onMessage([this](rtc::message_variant data) {
        if (!_alive.load()) return;  // BUG-COLLAB-023
        // Binary path only — JSON frames travel as bytes already.
        if (std::holds_alternative<rtc::binary>(data)) {
            const auto &bin = std::get<rtc::binary>(data);
            QByteArray bytes(reinterpret_cast<const char *>(bin.data()),
                             static_cast<int>(bin.size()));
            onDataChannelMessage(bytes);
        } else if (std::holds_alternative<std::string>(data)) {
            // Some clients ship strings; treat as UTF-8 bytes for
            // wire-protocol parsing.
            const auto &s = std::get<std::string>(data);
            onDataChannelMessage(QByteArray(s.data(), static_cast<int>(s.size())));
        }
    });
}

void WebRtcTransport::onDataChannelOpen() {
    qCInfo(rtcLog) << "data channel OPEN — DTLS established, ready for messages";
    emit connected();
}

void WebRtcTransport::onDataChannelClosed() {
    qCInfo(rtcLog) << "data channel CLOSED";
    emit transportFailed(QStringLiteral("data channel closed"));
    emit IPeerLink::disconnected();
}

void WebRtcTransport::onDataChannelMessage(const QByteArray &bytes) {
    // Each datachannel message is one SCTP-framed unit, which contains
    // exactly one of our length-prefixed payloads. We still parse the
    // 4-byte size prefix so the receiver code path is identical to
    // LanPeerSocket — letting LanLiveSession's handlers consume
    // either transport without branches.
    _readBuffer.append(bytes);
    while (true) {
        if (_expectedSize == 0) {
            if (_readBuffer.size() < 4) return;
            _expectedSize = qFromBigEndian<quint32>(
                reinterpret_cast<const uchar *>(_readBuffer.constData()));
            _readBuffer.remove(0, 4);
            // Same 32 MB cap as LanPeerSocket — see LanTransport.cpp:53.
            if (_expectedSize > 32u * 1024u * 1024u) {
                qCWarning(rtcLog) << "oversized frame" << _expectedSize
                                  << "— closing channel";
                if (_dc) _dc->close();
                _readBuffer.clear();
                _expectedSize = 0;
                return;
            }
        }
        if (static_cast<quint32>(_readBuffer.size()) < _expectedSize) return;
        QByteArray payload = _readBuffer.left(static_cast<int>(_expectedSize));
        _readBuffer.remove(0, static_cast<int>(_expectedSize));
        _expectedSize = 0;
        emit messageReceived(payload);
        if (!_dc) return;  // receiver may have torn us down
    }
}

bool WebRtcTransport::sendMessage(const QByteArray &payload) {
    if (!_dc || !_dc->isOpen()) return false;
    if (payload.size() > std::numeric_limits<qint32>::max()) return false;

    // 4-byte big-endian length prefix matches LanPeerSocket exactly.
    QByteArray frame;
    frame.reserve(4 + payload.size());
    QByteArray header(4, 0);
    qToBigEndian(static_cast<quint32>(payload.size()),
                 reinterpret_cast<uchar *>(header.data()));
    frame.append(header);
    frame.append(payload);
    try {
        rtc::binary bin(reinterpret_cast<const std::byte *>(frame.constData()),
                        reinterpret_cast<const std::byte *>(frame.constData() + frame.size()));
        return _dc->send(bin);
    } catch (const std::exception &e) {
        qCWarning(rtcLog) << "send failed:" << e.what()
                          << "(payload" << payload.size() << "bytes,"
                          << "framed" << frame.size() << "bytes,"
                          << "negotiated max" << _dc->maxMessageSize() << "bytes)";
        return false;
    }
}

bool WebRtcTransport::isConnected() const {
    return _dc && _dc->isOpen();
}

QString WebRtcTransport::localFingerprint() const {
    if (!_pc) return QString();
    auto desc = _pc->localDescription();
    if (!desc) return QString();
    auto fp = desc->fingerprint();
    return fp.has_value() ? QString::fromStdString(fp->value) : QString();
}

#endif // MIDIEDITOR_WEBRTC_ENABLED
