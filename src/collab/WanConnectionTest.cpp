/*
 * MidiEditor AI - WanConnectionTest implementation (Phase 9.8 multi-peer).
 *
 * In-process smoke test for the multi-peer rendezvous + WebRTC stack.
 * Spins up a "host" (Responder) and a "peer" (Initiator) on the same
 * machine, drives them through the new joiner-initiated handshake, and
 * measures the round-trip latency for a ping/pong.
 */

#ifdef MIDIEDITOR_WEBRTC_ENABLED

#include "WanConnectionTest.h"

#include <QLoggingCategory>
#include <QTimer>
#include <QUuid>

#include "RtcRendezvousClient.h"
#include "WebRtcTransport.h"

namespace {
Q_LOGGING_CATEGORY(wanTestLog, "midieditor.collab.rtc.contest")

constexpr const char *kPingPayload = "midieditor-ping";
constexpr const char *kPongPayload = "midieditor-pong";
}

WanConnectionTest::WanConnectionTest(QObject *parent) : QObject(parent) {}

WanConnectionTest::~WanConnectionTest() {
    cleanup();
}

void WanConnectionTest::run(int gatheringTimeoutMs, int overallTimeoutMs) {
    if (_phase != Phase::Idle) {
        qCWarning(wanTestLog) << "run() called twice on the same instance — ignoring";
        return;
    }
    _totalTimer.start();

    // Phase 9.8 role flip: host is responder, peer is initiator.
    _hostTransport = new WebRtcTransport(WebRtcTransport::Role::Responder, this);
    _peerTransport = new WebRtcTransport(WebRtcTransport::Role::Initiator, this);
    _hostRdv = new RtcRendezvousClient(this);
    _peerRdv = new RtcRendezvousClient(this);

    const QString joinerId = RtcRendezvousClient::newJoinerId();

    // Overall safety net — never let the test hang the UI thread forever.
    _safety = new QTimer(this);
    _safety->setSingleShot(true);
    connect(_safety, &QTimer::timeout, this, [this]() {
        emitFailure(QStringLiteral("timeout"),
                    tr("Test exceeded the overall %1 s budget.")
                        .arg(_safety->interval() / 1000));
    });
    _safety->start(overallTimeoutMs);

    // ---- Host side wiring -----------------------------------------------

    // Phase 9.8: pre-flight `/health` probe before any session POST.
    // Surfaces "rendezvous unreachable" in 3 seconds with a clear
    // human-readable failure stage, instead of letting the test sit
    // for 30 s on a stuck POST. The probe uses the same HTTP/1.1-
    // forced QNetworkAccessManager as the rest of the rendezvous
    // calls, so a positive ping is a reliable predictor that the
    // session POST will also work.
    _rdvStartMs = _totalTimer.elapsed();
    setPhase(Phase::PostingOffer, tr("Probing rendezvous /health…"));
    connect(_hostRdv, &RtcRendezvousClient::pingResult,
            this, [this](bool ok, qint64 latencyMs, const QString &reason) {
                if (_finished) return;
                if (!ok) {
                    emitFailure(QStringLiteral("rendezvous (health)"),
                                tr("Rendezvous /health failed: %1").arg(reason));
                    return;
                }
                qCInfo(wanTestLog) << "rendezvous /health OK in" << latencyMs << "ms";
                setPhase(Phase::PostingOffer, tr("Posting session to rendezvous…"));
                _hostRdv->postSession(
                    QUuid::createUuid().toString(QUuid::WithoutBraces),
                    QStringLiteral("ConnectionTest"));
            });
    // 5 s ping timeout — gives slow proxies / SSL-MITM-AV products time
    // to complete their cert chain validation. If even that times out,
    // the SSL-error and network-error handlers in RtcRendezvousClient
    // will have already logged the actual cause.
    _hostRdv->ping(5000);

    connect(_hostRdv, &RtcRendezvousClient::sessionPosted,
            this, [this](const QString &code) {
                if (_finished) return;
                _code = code;
                qCInfo(wanTestLog) << "rendezvous assigned code" << code;
                setPhase(Phase::FetchingOffer,
                         tr("Code %1 received — peer verifying…").arg(code));
                // Kick the peer side: verify the code, then offer.
                _hostRdv->pollJoinerOffers(code);
                _peerRdv->verifyCode(code);
            });

    // 4. A joiner-offer landed at the rendezvous → feed to host transport.
    connect(_hostRdv, &RtcRendezvousClient::joinerOfferReceived,
            this, [this](const QString &jid, const QString &sdp) {
                Q_UNUSED(jid);
                if (_finished) return;
                setPhase(Phase::AnsweringOffer,
                         tr("Generating answer for joiner…"));
                _hostTransport->setRemoteOffer(sdp);
            });

    // 5. Host's answer is ready → ship via rendezvous.
    connect(_hostTransport, &WebRtcTransport::answerReady,
            this, [this, joinerId](const QString &sdp) {
                if (_finished) return;
                _hostRdv->postHostAnswer(_code, joinerId, sdp);
                _handshakeStartMs = _totalTimer.elapsed();
                setPhase(Phase::Handshaking, tr("ICE + DTLS handshake in progress…"));
            });

    // ---- Peer side wiring -----------------------------------------------

    // 2. Code verified → NOW start peer transport (initiator).
    //    Important: must NOT start peer transport before we have the
    //    code, otherwise the peer's offerReady can fire before the
    //    host's /session POST has returned and we'd POST joiner-offer
    //    with an empty code (404 "session expired" — observed in the
    //    first 9.8 in-process test run, 2026-05-08).
    connect(_peerRdv, &RtcRendezvousClient::codeVerified,
            this, [this](const QString &sid, const QString &dn) {
                Q_UNUSED(sid); Q_UNUSED(dn);
                if (_finished) return;
                setPhase(Phase::PostingOffer, tr("Peer generating offer…"));
                _peerTransport->start(QStringList(), 2000,
                                      /*useDefaultIceServersIfEmpty=*/false);
            });

    // 3. Peer's offer is ready → POST to rendezvous with joinerId.
    connect(_peerTransport, &WebRtcTransport::offerReady,
            this, [this, joinerId](const QString &sdp) {
                if (_finished) return;
                _peerRdv->postJoinerOffer(_code, joinerId, sdp);
                _peerRdv->pollHostAnswer(_code, joinerId);
            });

    // 6. Host's answer arrives at peer → DTLS handshake.
    connect(_peerRdv, &RtcRendezvousClient::hostAnswerReceived,
            this, [this](const QString &sdp) {
                if (_finished) return;
                _peerTransport->setRemoteAnswer(sdp);
            });

    // ---- Connection & ping/pong -----------------------------------------

    auto checkBothConnected = [this]() {
        if (_finished || !_hostConnected || !_peerConnected) return;
        setPhase(Phase::Pinging, tr("Connection up — measuring latency…"));
        _pingSentMs = _totalTimer.elapsed();
        _hostTransport->sendMessage(QByteArray(kPingPayload));
    };

    connect(_hostTransport, &WebRtcTransport::connected,
            this, [this, checkBothConnected]() {
                _hostConnected = true;
                checkBothConnected();
            });
    connect(_peerTransport, &WebRtcTransport::connected,
            this, [this, checkBothConnected]() {
                _peerConnected = true;
                checkBothConnected();
            });

    connect(_peerTransport, &IPeerLink::messageReceived,
            this, [this](const QByteArray &payload) {
                if (_finished) return;
                if (payload == QByteArray(kPingPayload)) {
                    _peerTransport->sendMessage(QByteArray(kPongPayload));
                }
            });

    connect(_hostTransport, &IPeerLink::messageReceived,
            this, [this](const QByteArray &payload) {
                if (_finished) return;
                if (payload != QByteArray(kPongPayload)) return;

                Result r;
                r.ok = true;
                r.rendezvousCode = _code;
                r.rendezvousMs = (_rdvStartMs >= 0)
                    ? _handshakeStartMs - _rdvStartMs : -1;
                r.handshakeMs = _pingSentMs - _handshakeStartMs;
                r.pingRttMs = _totalTimer.elapsed() - _pingSentMs;
                r.totalMs = _totalTimer.elapsed();

                // Phase 9.8 calibration: the joiner-initiated multi-peer
                // protocol introduces one host-poll-interval of latency
                // on the way in (host sees the joiner offer on its next
                // poll) and another on the way out (joiner picks up
                // the host answer on its next poll). With kHostPoll=1s
                // and kJoinerPoll=0.75s, the floor is roughly:
                //   rendezvous ≈ 1.5–2.5 s   (postSession + verifyCode +
                //                              one host-poll cycle)
                //   handshake  ≈ 1.0–2.5 s   (ICE + DTLS + one joiner-
                //                              poll cycle)
                // So the "Good" threshold has to sit ABOVE the floor —
                // 3 s rdv / 4 s handshake covers the typical case while
                // still flagging genuinely slow connections.
                const bool fast = (r.handshakeMs <= 4000)
                               && (r.rendezvousMs >= 0 && r.rendezvousMs <= 3000);
                r.quality = fast ? Quality::Good : Quality::Acceptable;

                _finished = true;
                _phase = Phase::DonePass;
                qCInfo(wanTestLog) << "test PASS — rdv=" << r.rendezvousMs
                                    << "ms handshake=" << r.handshakeMs
                                    << "ms rtt=" << r.pingRttMs << "ms";
                emit finished(r);
                cleanup();
            });

    // ---- Failure paths --------------------------------------------------

    connect(_hostRdv, &RtcRendezvousClient::error,
            this, [this](const QString &stage, const QString &reason) {
                emitFailure(QStringLiteral("rendezvous (host %1)").arg(stage), reason);
            });
    connect(_peerRdv, &RtcRendezvousClient::error,
            this, [this](const QString &stage, const QString &reason) {
                emitFailure(QStringLiteral("rendezvous (peer %1)").arg(stage), reason);
            });
    connect(_hostTransport, &WebRtcTransport::transportFailed,
            this, [this](const QString &reason) {
                emitFailure(QStringLiteral("transport (host)"), reason);
            });
    connect(_peerTransport, &WebRtcTransport::transportFailed,
            this, [this](const QString &reason) {
                emitFailure(QStringLiteral("transport (peer)"), reason);
            });

    // ---- Kick everything off --------------------------------------------
    //
    // Same in-process rationale as Phase 9.6 — host candidates only
    // (no STUN), so srflx-srflx pairs that fail under NAT hairpinning
    // can't influence the test result.
    constexpr int kInProcessGatherMs = 2000;
    Q_UNUSED(gatheringTimeoutMs);

    setPhase(Phase::PostingOffer, tr("Starting host transport…"));
    // Phase 9.8 ordering: host transport starts now (it's a Responder
    // and won't generate any SDP until setRemoteOffer is called later).
    // Peer transport starts later — only after codeVerified fires —
    // so the offerReady signal can't race the rendezvous handshake.
    _hostTransport->start(QStringList(), kInProcessGatherMs,
                           /*useDefaultIceServersIfEmpty=*/false);
}

void WanConnectionTest::abort(const QString &reason) {
    if (_finished) return;
    emitFailure(QStringLiteral("aborted"),
                reason.isEmpty() ? tr("Test cancelled.") : reason);
}

void WanConnectionTest::setPhase(Phase phase, const QString &humanText) {
    _phase = phase;
    qCInfo(wanTestLog).noquote() << "phase →" << humanText;
    emit phaseChanged(phase, humanText);
}

void WanConnectionTest::emitFailure(const QString &stage, const QString &reason) {
    if (_finished) return;
    _finished = true;
    _phase = Phase::DoneFail;

    Result r;
    r.ok = false;
    r.failureStage = stage;
    r.failureReason = reason;
    r.rendezvousCode = _code;
    r.totalMs = _totalTimer.isValid() ? _totalTimer.elapsed() : -1;

    bool rendezvousReached = !_code.isEmpty();
    bool transportStage = stage.startsWith(QStringLiteral("transport"))
                       || stage == QStringLiteral("timeout");
    if (rendezvousReached && transportStage) {
        r.quality = Quality::Inconclusive;
    } else {
        r.quality = Quality::Failed;
    }

    qCWarning(wanTestLog) << "test FAIL —" << stage << ":" << reason
                          << "(quality=" << (r.quality == Quality::Inconclusive
                                              ? "Inconclusive" : "Failed") << ")";
    emit finished(r);
    cleanup();
}

void WanConnectionTest::cleanup() {
    if (_safety) {
        _safety->stop();
        _safety->deleteLater();
        _safety = nullptr;
    }
    if (_hostRdv) {
        _hostRdv->cancelPolling();
        _hostRdv->deleteLater();
        _hostRdv = nullptr;
    }
    if (_peerRdv) {
        _peerRdv->cancelPolling();
        _peerRdv->deleteLater();
        _peerRdv = nullptr;
    }
    if (_hostTransport) {
        _hostTransport->closeConnection();
        _hostTransport->deleteLater();
        _hostTransport = nullptr;
    }
    if (_peerTransport) {
        _peerTransport->closeConnection();
        _peerTransport->deleteLater();
        _peerTransport = nullptr;
    }
}

#endif // MIDIEDITOR_WEBRTC_ENABLED
