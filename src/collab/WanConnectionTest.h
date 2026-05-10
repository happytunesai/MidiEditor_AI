/*
 * MidiEditor AI
 *
 * WanConnectionTest — production-handshake self-test that exercises the
 * exact WAN code path the user gets when they Start/Join a WAN Live
 * Session, end-to-end, in one process (Plan §11.10g, Phase 9.6d).
 *
 * Two `WebRtcTransport` instances are paired via the configured
 * Cloudflare rendezvous (`RtcRendezvousClient::configuredUrl()`), the
 * same ICE-server pool, and DTLS handshake. Once both data channels
 * open, a single ping/pong round-trip measures end-to-end latency.
 *
 * Unlike `WebRtcSmokeTest::runLoopback()` — which short-circuits the
 * rendezvous by exchanging SDPs in-process — this test hits the real
 * rendezvous URL configured in QSettings. That makes it the right tool
 * for "is my WAN setup actually working?" — covering rendezvous
 * reachability, ICE-server pool health, NAT permeability, and DTLS
 * handshake all in one click.
 *
 * Lifecycle: one-shot. Construct, connect to signals, call run(), wait
 * for `finished`. Calling `run()` again on the same instance is
 * undefined; create a fresh one for each test.
 */

#ifndef WANCONNECTIONTEST_H
#define WANCONNECTIONTEST_H

#ifdef MIDIEDITOR_WEBRTC_ENABLED

#include <QElapsedTimer>
#include <QObject>
#include <QString>

class QTimer;
class WebRtcTransport;
class RtcRendezvousClient;

class WanConnectionTest : public QObject {
    Q_OBJECT
public:
    enum class Phase {
        Idle,
        PostingOffer,    // host: POST /offer to rendezvous
        FetchingOffer,   // peer: GET /code/<code>
        AnsweringOffer,  // peer: WebRTC answer generation + POST /answer
        AwaitingAnswer,  // host: poll GET /code/<code>/answer
        Handshaking,     // both sides: ICE + DTLS in flight
        Pinging,         // data channel open both sides; ping outbound
        DonePass,
        DoneFail,
    };
    Q_ENUM(Phase)

    /** \brief Quality bucket for the traffic-light UI.
     *
     *  Test design (Plan §11.10g, revised 2026-05-07): both transports
     *  run on the same machine and exchange ONLY host candidates
     *  (no STUN servers). They connect via the local network, no NAT
     *  traversal involved. This makes the test fast and reliable, but
     *  also means it cannot tell us whether the user's STUN pool or
     *  public-IP traversal works — that's only knowable by an actual
     *  two-machine session.
     *
     *  Inconclusive is reserved for transport failures AFTER the
     *  rendezvous round-trip succeeded — typically Windows-firewall /
     *  antivirus interference with the app's local UDP traffic, or a
     *  libdatachannel quirk. We surface as 🟡 (not 🔴) because real
     *  WAN sessions might still work even when the in-process loopback
     *  doesn't. */
    enum class Quality {
        Good,         // 🟢 — full handshake completed in time
        Acceptable,   // 🟡 — completed but slower than the Good budget
        Inconclusive, // 🟡 — rendezvous OK, in-process loopback broke (firewall/AV?)
        Failed,       // 🔴 — actual breakage: rendezvous unreachable
    };
    Q_ENUM(Quality)

    struct Result {
        bool ok = false;            ///< true if ping/pong completed
        Quality quality = Quality::Failed;
        QString failureStage;       ///< "rendezvous", "ice", "dtls", "ping", "" if ok
        QString failureReason;      ///< human-readable, suitable for tooltip
        QString rendezvousCode;     ///< 4-char code we got from postOffer
        qint64 rendezvousMs = -1;   ///< postOffer → answerReceived
        qint64 handshakeMs = -1;    ///< answerReceived → connected (ICE+DTLS)
        qint64 pingRttMs = -1;      ///< ping outbound → pong inbound
        qint64 totalMs = -1;        ///< run() start → finished
    };

    explicit WanConnectionTest(QObject *parent = nullptr);
    ~WanConnectionTest() override;

    /** \brief Begin the test. Result lands via \ref finished. The
     *  overall safety timeout (default 30 s) bounds how long this
     *  instance waits before declaring failure. */
    void run(int gatheringTimeoutMs = 8000, int overallTimeoutMs = 30000);

    /** \brief Best-effort abort; tears down both transports and the
     *  rendezvous clients, emits `finished` with quality=Failed if the
     *  test was still running. */
    void abort(const QString &reason = QString());

signals:
    void phaseChanged(Phase phase, const QString &humanText);
    void finished(const Result &result);

private:
    void setPhase(Phase phase, const QString &humanText);
    void emitFailure(const QString &stage, const QString &reason);
    void cleanup();

    Phase _phase = Phase::Idle;
    QElapsedTimer _totalTimer;
    qint64 _rdvStartMs = -1;
    qint64 _handshakeStartMs = -1;
    qint64 _pingSentMs = -1;

    QString _code;
    bool _hostConnected = false;
    bool _peerConnected = false;

    WebRtcTransport *_hostTransport = nullptr;
    WebRtcTransport *_peerTransport = nullptr;
    RtcRendezvousClient *_hostRdv = nullptr;
    RtcRendezvousClient *_peerRdv = nullptr;
    QTimer *_safety = nullptr;

    bool _finished = false;
};

#endif // MIDIEDITOR_WEBRTC_ENABLED

#endif // WANCONNECTIONTEST_H
