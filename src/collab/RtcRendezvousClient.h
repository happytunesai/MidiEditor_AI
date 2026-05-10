/*
 * MidiEditor AI
 *
 * RtcRendezvousClient — HTTPS client for the Cloudflare Worker
 * rendezvous service (Plan §11.10, Phase 9.6 + 9.8).
 *
 * --------------------------------------------------------------
 *  Phase 9.8 multi-peer protocol (joiner-initiated)
 * --------------------------------------------------------------
 *
 * The earlier 1:1 protocol had the host as WebRTC initiator. For N:1
 * we flip: each joiner is initiator, the host generates an answer per
 * joiner. See cloudflare/rendezvous.js for the wire-protocol spec.
 *
 *   Host side:
 *     • postSession(sessionId, displayName) → returns 4-char `code`
 *       via \ref sessionPosted. Share OOB with joiners.
 *     • pollJoinerOffers(code)              → background polling;
 *       emits \ref joinerOfferReceived once per *new* joinerId. Caller
 *       creates a fresh WebRtcTransport (Responder role) per fired
 *       signal, generates an answer, then calls postHostAnswer.
 *     • postHostAnswer(code, joinerId, sdp) → publishes the answer
 *       for one specific joiner.
 *     • cancelPolling()                    → stop the offer-poll loop
 *       (e.g. on session leave).
 *
 *   Joiner side:
 *     • verifyCode(code)                  → GET /code/<code> to check
 *       the session exists (gives a clear "not found" UX before ICE
 *       gathering starts). Emits \ref codeVerified.
 *     • postJoinerOffer(code, joinerId, sdp)
 *     • pollHostAnswer(code, joinerId)    → polls until the host
 *       publishes our answer. Emits \ref hostAnswerReceived once.
 *
 * All async — results land via Qt signals.
 *
 * The rendezvous URL is read from QSettings("Collab/wan/rendezvousUrl"),
 * defaulting to the production worker.
 */

#ifndef RTCRENDEZVOUSCLIENT_H
#define RTCRENDEZVOUSCLIENT_H

#ifdef MIDIEDITOR_WEBRTC_ENABLED

#include <QObject>
#include <QSet>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

class RtcRendezvousClient : public QObject {
    Q_OBJECT
public:
    explicit RtcRendezvousClient(QObject *parent = nullptr);
    ~RtcRendezvousClient() override;

    /** \brief Read the rendezvous URL from QSettings; falls back to
     *  the built-in default. Returned without trailing slash. */
    static QString configuredUrl();

    /** \brief Persist a new rendezvous URL (e.g. self-hosted worker).
     *  Empty string reverts to the default on next read. */
    static void setConfiguredUrl(const QString &url);

    /** \brief Built-in default URL the build ships with. */
    static QString defaultUrl();

    /** \brief Generate a random joiner-id (URL-safe, 16 chars). Use
     *  this on the joiner side; pass the same id to postJoinerOffer
     *  and pollHostAnswer so the worker can route the answer back. */
    static QString newJoinerId();

    // ---- Host side -------------------------------------------------

    /** \brief POST /session — register a new session and receive a
     *  4-char pairing code via \ref sessionPosted. */
    void postSession(const QString &sessionId, const QString &displayName);

    /** \brief Begin polling GET /code/<code>/joiner-offers until
     *  cancelPolling() runs. Emits \ref joinerOfferReceived once per
     *  joinerId we haven't seen on this object yet (so a sustained
     *  poll loop produces one signal per joiner, regardless of how
     *  many list-responses include the same offer). */
    void pollJoinerOffers(const QString &code);

    /** \brief POST /code/<code>/host-answer — publish the answer SDP
     *  for one specific joiner. Emits \ref hostAnswerPosted on success. */
    void postHostAnswer(const QString &code,
                        const QString &joinerId,
                        const QString &sdp);

    /** \brief Stop the host-side joiner-offers polling loop. */
    void cancelPolling();

    // ---- Joiner side ----------------------------------------------

    /** \brief GET /code/<code> — verify the session exists and (if so)
     *  surface its sessionId / displayName. Emits \ref codeVerified
     *  on 200, \ref error on 404 / network failure. */
    void verifyCode(const QString &code);

    /** \brief POST /code/<code>/joiner-offer — publish our offer SDP
     *  with our generated joinerId. */
    void postJoinerOffer(const QString &code,
                         const QString &joinerId,
                         const QString &sdp);

    /** \brief Start polling GET /code/<code>/host-answer/<joinerId>
     *  until the host publishes our answer. Emits \ref hostAnswerReceived
     *  once on success and stops. */
    void pollHostAnswer(const QString &code, const QString &joinerId);

    // ---- Health probe ----------------------------------------------

    /** \brief Async health check against the configured rendezvous's
     *  `/health` endpoint. Used as a pre-flight gate. */
    void ping(int timeoutMs = 3000);

signals:
    /** \brief Host: rendezvous accepted our session announcement;
     *  \a code is the 4-char pairing code to share OOB. */
    void sessionPosted(const QString &code);

    /** \brief Host: a joiner has posted their offer. Fires once per
     *  unique joinerId observed on this client object. */
    void joinerOfferReceived(const QString &joinerId, const QString &sdp);

    /** \brief Host: our answer for the given joiner has been stored. */
    void hostAnswerPosted(const QString &joinerId);

    /** \brief Joiner: the code is valid, session exists. */
    void codeVerified(const QString &sessionId, const QString &displayName);

    /** \brief Joiner: our offer was accepted. */
    void joinerOfferPosted();

    /** \brief Joiner: the host's answer for our joinerId arrived. */
    void hostAnswerReceived(const QString &sdp);

    /** \brief Anything failed — network error, 4xx/5xx response, JSON
     *  parse error. \a stage describes which call failed. */
    void error(const QString &stage, const QString &reason);

    /** \brief Health-check result. \a latencyMs is the round-trip time
     *  on success and -1 on failure. */
    void pingResult(bool ok, qint64 latencyMs, const QString &reason);

private slots:
    void pollJoinerOffersOnce();
    void pollHostAnswerOnce();

private:
    void sendJson(const QString &method,
                  const QString &path,
                  const QByteArray &body,
                  std::function<void(QNetworkReply *)> onDone);

    QNetworkAccessManager *_net = nullptr;

    // Host-side polling state.
    QString _hostCode;
    QTimer *_offerPollTimer = nullptr;
    QSet<QString> _seenJoiners;  // joinerIds we've already emitted

    // Joiner-side polling state.
    QString _joinerCode;
    QString _joinerId;
    QTimer *_answerPollTimer = nullptr;
    int _answerPollAttempts = 0;
};

#endif // MIDIEDITOR_WEBRTC_ENABLED

#endif // RTCRENDEZVOUSCLIENT_H
