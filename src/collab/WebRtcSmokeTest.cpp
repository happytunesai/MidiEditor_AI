/*
 * MidiEditor AI - WebRtcSmokeTest implementation.
 */

#ifdef MIDIEDITOR_WEBRTC_ENABLED

#include "WebRtcSmokeTest.h"

#include <QLoggingCategory>
#include <QPointer>
#include <memory>

// libdatachannel — keep-static
#include <rtc/rtc.hpp>

#include "WebRtcTransport.h"

Q_DECLARE_LOGGING_CATEGORY(lanLog)
Q_LOGGING_CATEGORY(rtcSmokeLog, "midieditor.collab.rtc.smoke")

void WebRtcSmokeTest::runStunPing() {
    // Google's public STUN pool (per the user-supplied list, Phase
    // 9.6 §11.10). All ten endpoints are configured for redundancy.
    rtc::Configuration cfg;
    cfg.iceServers.emplace_back("stun:stun.l.google.com:19302");
    cfg.iceServers.emplace_back("stun:stun.l.google.com:5349");
    cfg.iceServers.emplace_back("stun:stun1.l.google.com:3478");
    cfg.iceServers.emplace_back("stun:stun1.l.google.com:5349");
    cfg.iceServers.emplace_back("stun:stun2.l.google.com:19302");
    cfg.iceServers.emplace_back("stun:stun2.l.google.com:5349");
    cfg.iceServers.emplace_back("stun:stun3.l.google.com:3478");
    cfg.iceServers.emplace_back("stun:stun3.l.google.com:5349");
    cfg.iceServers.emplace_back("stun:stun4.l.google.com:19302");
    cfg.iceServers.emplace_back("stun:stun4.l.google.com:5349");

    // Static so the connection lives long enough to gather candidates
    // (~2-5 seconds typically). The smoke test is one-shot per process
    // run; subsequent calls just replace the previous instance.
    static std::shared_ptr<rtc::PeerConnection> pc;
    pc = std::make_shared<rtc::PeerConnection>(cfg);

    pc->onLocalCandidate([](rtc::Candidate cand) {
        // The candidate's string form looks like:
        //   "candidate:842163049 1 udp 1677729535 1.2.3.4 51924 typ srflx ..."
        // Where typ=srflx is the STUN-discovered public reflexive
        // address — that's the proof STUN works.
        qCInfo(rtcSmokeLog) << "candidate:" << QString::fromStdString(std::string(cand));
    });
    pc->onGatheringStateChange([](rtc::PeerConnection::GatheringState state) {
        QString name;
        switch (state) {
            case rtc::PeerConnection::GatheringState::New:        name = "New"; break;
            case rtc::PeerConnection::GatheringState::InProgress: name = "InProgress"; break;
            case rtc::PeerConnection::GatheringState::Complete:   name = "Complete"; break;
        }
        qCInfo(rtcSmokeLog) << "gathering state →" << name;
    });
    pc->onStateChange([](rtc::PeerConnection::State state) {
        QString name;
        switch (state) {
            case rtc::PeerConnection::State::New:          name = "New"; break;
            case rtc::PeerConnection::State::Connecting:   name = "Connecting"; break;
            case rtc::PeerConnection::State::Connected:    name = "Connected"; break;
            case rtc::PeerConnection::State::Disconnected: name = "Disconnected"; break;
            case rtc::PeerConnection::State::Failed:       name = "Failed"; break;
            case rtc::PeerConnection::State::Closed:       name = "Closed"; break;
        }
        qCInfo(rtcSmokeLog) << "state →" << name;
    });

    // A data channel must exist for ICE gathering to start (per
    // libdatachannel API). The label is arbitrary.
    auto dc = pc->createDataChannel("smoke");
    Q_UNUSED(dc);

    // Generate the local SDP offer. This kicks off ICE gathering
    // automatically; candidates flow into the onLocalCandidate
    // callback above as STUN responses arrive.
    pc->setLocalDescription();
    qCInfo(rtcSmokeLog) << "local description created — gathering candidates";
}

void WebRtcSmokeTest::runLoopback() {
    qCInfo(rtcSmokeLog) << "=== loopback test: initiator + responder in-process ===";

    // Hold both transports static so they outlive the function call;
    // a re-trigger replaces them, which is desirable for repeat tests.
    static WebRtcTransport *initiator = nullptr;
    static WebRtcTransport *responder = nullptr;
    if (initiator) initiator->deleteLater();
    if (responder) responder->deleteLater();
    initiator = new WebRtcTransport(WebRtcTransport::Role::Initiator);
    responder = new WebRtcTransport(WebRtcTransport::Role::Responder);

    QPointer<WebRtcTransport> initGuard(initiator);
    QPointer<WebRtcTransport> respGuard(responder);

    // Initiator's offer → responder.
    QObject::connect(initiator, &WebRtcTransport::offerReady,
                     [respGuard](const QString &sdp) {
        qCInfo(rtcSmokeLog) << "initiator offer ready (" << sdp.size() << "bytes)"
                            << "→ feeding to responder";
        if (respGuard) respGuard->setRemoteOffer(sdp);
    });

    // Responder's answer → initiator.
    QObject::connect(responder, &WebRtcTransport::answerReady,
                     [initGuard](const QString &sdp) {
        qCInfo(rtcSmokeLog) << "responder answer ready (" << sdp.size() << "bytes)"
                            << "→ feeding to initiator";
        if (initGuard) initGuard->setRemoteAnswer(sdp);
    });

    // Connection events.
    QObject::connect(initiator, &WebRtcTransport::connected, []() {
        qCInfo(rtcSmokeLog) << "✓ initiator connected — sending ping";
    });
    QObject::connect(responder, &WebRtcTransport::connected, []() {
        qCInfo(rtcSmokeLog) << "✓ responder connected";
    });

    // Send/receive: once initiator's data channel opens, ping. The
    // responder echoes back a pong. Confirms bidirectional flow +
    // framing.
    QObject::connect(initiator, &WebRtcTransport::connected,
                     [initGuard]() {
        if (initGuard) {
            QByteArray ping = QByteArrayLiteral("ping");
            bool ok = initGuard->sendMessage(ping);
            qCInfo(rtcSmokeLog) << "initiator → ping sent? " << ok;
        }
    });
    QObject::connect(responder, &WebRtcTransport::messageReceived,
                     [respGuard](const QByteArray &payload) {
        qCInfo(rtcSmokeLog) << "← responder received:" << payload;
        if (respGuard) {
            QByteArray pong = QByteArrayLiteral("pong");
            respGuard->sendMessage(pong);
        }
    });
    QObject::connect(initiator, &WebRtcTransport::messageReceived,
                     [](const QByteArray &payload) {
        qCInfo(rtcSmokeLog) << "← initiator received:" << payload
                            << "  ✓✓✓ ROUND-TRIP COMPLETE";
    });

    // Disconnect logs for diagnostics.
    QObject::connect(initiator, &WebRtcTransport::transportFailed,
                     [](const QString &reason) {
        qCInfo(rtcSmokeLog) << "initiator disconnected:" << reason;
    });
    QObject::connect(responder, &WebRtcTransport::transportFailed,
                     [](const QString &reason) {
        qCInfo(rtcSmokeLog) << "responder disconnected:" << reason;
    });

    // Kick off both — order matters: responder must be ready to
    // accept the offer before we generate one. Because both are
    // local in-process, this is fine to do back-to-back.
    //
    // Empty ICE servers list: loopback peers reach each other via
    // host candidates (loopback / local LAN), so there's nothing to
    // ask STUN about. Skipping STUN makes gathering complete in
    // milliseconds instead of waiting for network round-trips.
    QStringList noStun;
    responder->start(noStun, /*gatheringTimeoutMs=*/2000);
    initiator->start(noStun, /*gatheringTimeoutMs=*/2000);
}

#endif // MIDIEDITOR_WEBRTC_ENABLED
