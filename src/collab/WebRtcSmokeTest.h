/*
 * MidiEditor AI
 *
 * WebRtcSmokeTest — feasibility-gate harness for Phase 9.6
 * (libdatachannel + OpenSSL + Google STUN).
 *
 * Builds a single \c rtc::PeerConnection with the Google STUN pool,
 * triggers ICE gathering, and logs every candidate via Qt's logging
 * categories. Used to confirm that:
 *
 *   1. libdatachannel + OpenSSL link cleanly into the binary
 *      (the binary size grows visibly once this TU is compiled);
 *   2. NAT traversal succeeds — peer's public address shows up as a
 *      `srflx` candidate in the log.
 *
 * Wired to a dev-only menu action; not exposed to end users.
 */

#ifndef WEBRTCSMOKETEST_H
#define WEBRTCSMOKETEST_H

#include <QObject>
#include <QString>

#ifdef MIDIEDITOR_WEBRTC_ENABLED

class WebRtcSmokeTest : public QObject {
    Q_OBJECT
public:
    /** \brief Open one PeerConnection, trigger ICE gathering against
     *  the Google STUN pool, and log candidates as they arrive.
     *  Returns immediately; results stream into the qCInfo logs. */
    static void runStunPing();

    /** \brief Phase 9.6.b end-to-end smoke: in-process loopback
     *  test. Spins up two WebRtcTransport instances (Initiator +
     *  Responder), shuttles their offer/answer between them, waits
     *  for both data channels to open, sends a "ping" frame and
     *  expects a "pong" reply. Logs every step. Confirms framing,
     *  DTLS, and bidirectional message delivery work end-to-end
     *  without a remote peer. */
    static void runLoopback();
};

#endif // MIDIEDITOR_WEBRTC_ENABLED

#endif // WEBRTCSMOKETEST_H
