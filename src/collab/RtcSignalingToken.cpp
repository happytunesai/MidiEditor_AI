/*
 * MidiEditor AI - RtcSignalingToken implementation.
 */

#ifdef MIDIEDITOR_WEBRTC_ENABLED

#include "RtcSignalingToken.h"

#include <QByteArray>

namespace {

QString buildToken(const QString &sessionId, const QString &kind, const QString &sdp) {
    // zlib-compress the SDP, then base64. Same encoding pattern as
    // PrBundle's inline token (§10.1). qCompress writes a 4-byte
    // big-endian uncompressed-size header; qUncompress on the other
    // side reads it. We don't strip it because it's part of the
    // recognized inline format.
    QByteArray utf8 = sdp.toUtf8();
    QByteArray comp = qCompress(utf8, /*level=*/9);
    QByteArray b64  = comp.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    return QString::fromLatin1(RtcSignalingToken::kSchemePrefix)
            + sessionId + QStringLiteral(":") + kind + QStringLiteral(":")
            + QString::fromLatin1(b64);
}

}  // namespace

QString RtcSignalingToken::encodeOffer(const QString &sessionId, const QString &sdp) {
    return buildToken(sessionId, QStringLiteral("offer"), sdp);
}

QString RtcSignalingToken::encodeAnswer(const QString &sessionId, const QString &sdp) {
    return buildToken(sessionId, QStringLiteral("answer"), sdp);
}

bool RtcSignalingToken::looksLikeToken(const QString &text) {
    return text.startsWith(QString::fromLatin1(kSchemePrefix));
}

RtcSignalingToken::Kind RtcSignalingToken::decode(const QString &token,
                                                   QString *sessionId,
                                                   QString *sdp,
                                                   QString *errorOut) {
    auto setError = [errorOut](const QString &msg) {
        if (errorOut) *errorOut = msg;
    };

    if (!looksLikeToken(token)) {
        setError(QStringLiteral("Token doesn't start with %1").arg(kSchemePrefix));
        return Kind::Invalid;
    }
    QString remainder = token.mid(QString::fromLatin1(kSchemePrefix).length());

    // Expect three colon-separated parts: <sessionId>:<kind>:<base64>
    int firstColon  = remainder.indexOf(QChar(':'));
    if (firstColon < 0) { setError(QStringLiteral("missing sessionId")); return Kind::Invalid; }
    int secondColon = remainder.indexOf(QChar(':'), firstColon + 1);
    if (secondColon < 0) { setError(QStringLiteral("missing kind")); return Kind::Invalid; }

    QString sessionPart = remainder.left(firstColon);
    QString kindPart    = remainder.mid(firstColon + 1, secondColon - firstColon - 1);
    QString b64Part     = remainder.mid(secondColon + 1);

    Kind kind = Kind::Invalid;
    if (kindPart == QStringLiteral("offer"))  kind = Kind::Offer;
    if (kindPart == QStringLiteral("answer")) kind = Kind::Answer;
    if (kind == Kind::Invalid) {
        setError(QStringLiteral("unknown kind '%1' (expected offer/answer)").arg(kindPart));
        return Kind::Invalid;
    }

    QByteArray comp = QByteArray::fromBase64(b64Part.toLatin1(),
                                             QByteArray::Base64UrlEncoding);
    if (comp.isEmpty()) {
        setError(QStringLiteral("base64 payload empty or malformed"));
        return Kind::Invalid;
    }
    QByteArray utf8 = qUncompress(comp);
    if (utf8.isEmpty()) {
        setError(QStringLiteral("zlib decompress failed"));
        return Kind::Invalid;
    }

    if (sessionId) *sessionId = sessionPart;
    if (sdp)       *sdp       = QString::fromUtf8(utf8);
    return kind;
}

#endif // MIDIEDITOR_WEBRTC_ENABLED
