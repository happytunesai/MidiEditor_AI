/*
 * MidiEditor AI
 *
 * RtcSignalingToken — codec for the WebRTC offer/answer signaling
 * tokens (Plan §11.10, Phase 9.6).
 *
 * Token form:
 *
 *   midiedit-rtc://v1:<sessionId>:offer:<base64-zlib-sdp>
 *   midiedit-rtc://v1:<sessionId>:answer:<base64-zlib-sdp>
 *
 * Mirrors the smart-paste-token format used for async PRs (§10.1).
 * The SDP is zlib-compressed (typical 60-70% reduction) and base64-
 * encoded so the whole token is a single chat-friendly string —
 * 800-byte SDPs typically end up ~600-byte tokens, which fits a
 * Discord message easily.
 *
 * Out-of-band sharing: the user copies the token, sends via Discord/
 * email/SMS, and the recipient pastes it into the matching dialog
 * (Start = paste answer, Join = paste offer). The Ctrl+V dispatcher
 * in MainWindow recognizes both `midiedit-pr://` (PR bundles) and
 * `midiedit-rtc://` (signaling) prefixes.
 *
 * Design rationale: keeping signaling out of band (no rendezvous
 * server) means zero infrastructure to host. Trade-off: the user
 * has to copy/paste twice (offer one way, answer back). For a
 * better UX, a future phase could add an optional rendezvous
 * service — but the current token format is forward-compatible
 * with that.
 */

#ifndef RTCSIGNALINGTOKEN_H
#define RTCSIGNALINGTOKEN_H

#ifdef MIDIEDITOR_WEBRTC_ENABLED

#include <QByteArray>
#include <QString>

class RtcSignalingToken {
public:
    static constexpr const char *kSchemePrefix = "midiedit-rtc://v1:";

    enum class Kind { Invalid, Offer, Answer };

    /** \brief Build the offer-form token. */
    static QString encodeOffer(const QString &sessionId, const QString &sdp);
    /** \brief Build the answer-form token. */
    static QString encodeAnswer(const QString &sessionId, const QString &sdp);

    /** \brief Parse a token. On success returns Offer or Answer and
     *  fills \a sessionId / \a sdp; on failure returns Invalid and
     *  optionally writes a reason to \a errorOut. */
    static Kind decode(const QString &token,
                       QString *sessionId,
                       QString *sdp,
                       QString *errorOut = nullptr);

    /** \brief Cheap prefix check used by the Ctrl+V dispatcher to
     *  decide whether the clipboard text is one of our tokens. */
    static bool looksLikeToken(const QString &text);
};

#endif // MIDIEDITOR_WEBRTC_ENABLED

#endif // RTCSIGNALINGTOKEN_H
