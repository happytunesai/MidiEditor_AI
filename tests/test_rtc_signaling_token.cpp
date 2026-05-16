// Unit tests for RtcSignalingToken — base64 + zlib codec for WebRTC
// offer/answer tokens (Plan §11.10, Phase 9.6). Symmetric to PrBundle's
// smart-paste token (which is already covered by test_pr_bundle).
//
// Pure Qt - qCompress/qUncompress + QByteArray::toBase64; no libdatachannel
// dependency. The header gates the declaration behind
// MIDIEDITOR_WEBRTC_ENABLED, so the test target must define it.
//
// Run from a build configured with -DBUILD_TESTING=ON:
//     ctest --test-dir build -V -R RtcSignalingToken

#include "RtcSignalingToken.h"

#include <QByteArray>
#include <QString>
#include <QtTest/QtTest>

class TestRtcSignalingToken : public QObject {
    Q_OBJECT

private slots:
    // looksLikeToken
    void looksLikeToken_recognizesValidPrefix();
    void looksLikeToken_rejectsOtherText();
    void looksLikeToken_rejectsEmpty();

    // encodeOffer / encodeAnswer
    void encodeOffer_startsWithSchemePrefix();
    void encodeAnswer_startsWithSchemePrefix();
    void encode_differentKindsProduceDifferentTokens();

    // Round-trip
    void roundTrip_offerPreservesSdpAndSession();
    void roundTrip_answerPreservesSdpAndSession();
    void roundTrip_realisticSdpPayload();
    void roundTrip_compressionShrinksRepetitiveSdp();

    // decode() — error paths
    void decode_emptyStringReturnsInvalid();
    void decode_missingPrefixReturnsInvalid();
    void decode_missingSessionIdReturnsInvalid();
    void decode_missingKindReturnsInvalid();
    void decode_unknownKindReturnsInvalid();
    void decode_emptyBase64ReturnsInvalid();
    void decode_corruptedBase64ReturnsInvalid();
    void decode_invalidErrorOutPopulated();
};

// ---------------------------------------------------------------------
// looksLikeToken
// ---------------------------------------------------------------------

void TestRtcSignalingToken::looksLikeToken_recognizesValidPrefix() {
    QVERIFY(RtcSignalingToken::looksLikeToken(
            QStringLiteral("midiedit-rtc://v1:abc:offer:anything")));
    QVERIFY(RtcSignalingToken::looksLikeToken(
            QStringLiteral("midiedit-rtc://v1:")));
}

void TestRtcSignalingToken::looksLikeToken_rejectsOtherText() {
    QVERIFY(!RtcSignalingToken::looksLikeToken(QStringLiteral("hello")));
    QVERIFY(!RtcSignalingToken::looksLikeToken(
            QStringLiteral("midiedit-pr://v1:abc:inline:foo")));
    QVERIFY(!RtcSignalingToken::looksLikeToken(
            QStringLiteral("https://example.com/midiedit-rtc")));
}

void TestRtcSignalingToken::looksLikeToken_rejectsEmpty() {
    QVERIFY(!RtcSignalingToken::looksLikeToken(QString()));
}

// ---------------------------------------------------------------------
// encode prefix
// ---------------------------------------------------------------------

void TestRtcSignalingToken::encodeOffer_startsWithSchemePrefix() {
    QString tok = RtcSignalingToken::encodeOffer(
            QStringLiteral("sess-1"),
            QStringLiteral("v=0\r\no=- 1 1 IN IP4 1.2.3.4\r\n"));
    QVERIFY(tok.startsWith(RtcSignalingToken::kSchemePrefix));
    QVERIFY(tok.contains(QStringLiteral(":offer:")));
    QVERIFY(tok.contains(QStringLiteral("sess-1")));
}

void TestRtcSignalingToken::encodeAnswer_startsWithSchemePrefix() {
    QString tok = RtcSignalingToken::encodeAnswer(
            QStringLiteral("sess-2"),
            QStringLiteral("v=0\r\no=- 2 2 IN IP4 5.6.7.8\r\n"));
    QVERIFY(tok.startsWith(RtcSignalingToken::kSchemePrefix));
    QVERIFY(tok.contains(QStringLiteral(":answer:")));
    QVERIFY(tok.contains(QStringLiteral("sess-2")));
}

void TestRtcSignalingToken::encode_differentKindsProduceDifferentTokens() {
    QString sdp = QStringLiteral("v=0\r\n");
    QString offer  = RtcSignalingToken::encodeOffer(QStringLiteral("s"), sdp);
    QString answer = RtcSignalingToken::encodeAnswer(QStringLiteral("s"), sdp);
    QVERIFY(offer != answer);
    QVERIFY(offer.contains(QStringLiteral(":offer:")));
    QVERIFY(answer.contains(QStringLiteral(":answer:")));
}

// ---------------------------------------------------------------------
// Round-trip
// ---------------------------------------------------------------------

void TestRtcSignalingToken::roundTrip_offerPreservesSdpAndSession() {
    QString sdp = QStringLiteral(
            "v=0\r\no=- 1 1 IN IP4 1.2.3.4\r\ns=-\r\nt=0 0\r\n"
            "m=application 9 UDP/DTLS/SCTP webrtc-datachannel\r\n");
    QString tok = RtcSignalingToken::encodeOffer(
            QStringLiteral("session-abc"), sdp);

    QString sessOut, sdpOut, err;
    auto kind = RtcSignalingToken::decode(tok, &sessOut, &sdpOut, &err);
    QCOMPARE(kind, RtcSignalingToken::Kind::Offer);
    QCOMPARE(sessOut, QStringLiteral("session-abc"));
    QCOMPARE(sdpOut, sdp);
    QVERIFY(err.isEmpty());
}

void TestRtcSignalingToken::roundTrip_answerPreservesSdpAndSession() {
    QString sdp = QStringLiteral("v=0\r\no=- 9 9 IN IP4 9.9.9.9\r\n");
    QString tok = RtcSignalingToken::encodeAnswer(
            QStringLiteral("answer-session"), sdp);

    QString sessOut, sdpOut;
    auto kind = RtcSignalingToken::decode(tok, &sessOut, &sdpOut, nullptr);
    QCOMPARE(kind, RtcSignalingToken::Kind::Answer);
    QCOMPARE(sessOut, QStringLiteral("answer-session"));
    QCOMPARE(sdpOut, sdp);
}

// A realistic SDP with ICE candidates, DTLS fingerprint, the works.
void TestRtcSignalingToken::roundTrip_realisticSdpPayload() {
    QString sdp = QStringLiteral(
            "v=0\r\n"
            "o=- 8000000000000000 1 IN IP4 127.0.0.1\r\n"
            "s=-\r\n"
            "t=0 0\r\n"
            "a=group:BUNDLE 0\r\n"
            "a=msid-semantic: WMS\r\n"
            "m=application 9 UDP/DTLS/SCTP webrtc-datachannel\r\n"
            "c=IN IP4 0.0.0.0\r\n"
            "a=ice-ufrag:abcd\r\n"
            "a=ice-pwd:abcdefghijklmnopqrstuv\r\n"
            "a=fingerprint:sha-256 "
            "AB:CD:EF:01:23:45:67:89:AB:CD:EF:01:23:45:67:89:"
            "AB:CD:EF:01:23:45:67:89:AB:CD:EF:01:23:45:67:89\r\n"
            "a=setup:actpass\r\n"
            "a=mid:0\r\n"
            "a=sctp-port:5000\r\n"
            "a=max-message-size:262144\r\n"
            "a=candidate:1 1 udp 2113937151 192.168.1.10 49152 typ host\r\n"
            "a=candidate:2 1 udp 1677729535 203.0.113.1  49153 typ srflx\r\n"
            "a=end-of-candidates\r\n");

    QString tok = RtcSignalingToken::encodeOffer(
            QStringLiteral("realistic-session-id-with-dashes"), sdp);

    QString sessOut, sdpOut;
    auto kind = RtcSignalingToken::decode(tok, &sessOut, &sdpOut);
    QCOMPARE(kind, RtcSignalingToken::Kind::Offer);
    QCOMPARE(sessOut, QStringLiteral("realistic-session-id-with-dashes"));
    QCOMPARE(sdpOut, sdp);
}

// Repetitive SDP payload compresses well; the token should be smaller
// than the raw SDP UTF-8 size.
void TestRtcSignalingToken::roundTrip_compressionShrinksRepetitiveSdp() {
    QString sdp;
    for (int i = 0; i < 100; ++i) {
        sdp.append(QStringLiteral(
                "a=candidate:%1 1 udp 2113937151 192.168.1.10 49152 typ host\r\n").arg(i));
    }
    QString tok = RtcSignalingToken::encodeOffer(QStringLiteral("s"), sdp);

    QString sessOut, sdpOut;
    auto kind = RtcSignalingToken::decode(tok, &sessOut, &sdpOut);
    QCOMPARE(kind, RtcSignalingToken::Kind::Offer);
    QCOMPARE(sdpOut, sdp);

    // Token size (incl. scheme prefix) should be a fraction of the raw
    // SDP byte size for repetitive content.
    QVERIFY2(tok.size() < sdp.toUtf8().size() / 2,
             qPrintable(QStringLiteral("token %1, raw sdp %2")
                                .arg(tok.size())
                                .arg(sdp.toUtf8().size())));
}

// ---------------------------------------------------------------------
// decode error paths
// ---------------------------------------------------------------------

void TestRtcSignalingToken::decode_emptyStringReturnsInvalid() {
    QString s, p, e;
    auto kind = RtcSignalingToken::decode(QString(), &s, &p, &e);
    QCOMPARE(kind, RtcSignalingToken::Kind::Invalid);
    QVERIFY(!e.isEmpty());
}

void TestRtcSignalingToken::decode_missingPrefixReturnsInvalid() {
    QString s, p;
    auto kind = RtcSignalingToken::decode(
            QStringLiteral("not-a-token-at-all"), &s, &p);
    QCOMPARE(kind, RtcSignalingToken::Kind::Invalid);
}

// "midiedit-rtc://v1:" without anything after - decode should fail
// because there's no sessionId/colon to split on.
void TestRtcSignalingToken::decode_missingSessionIdReturnsInvalid() {
    QString s, p, e;
    auto kind = RtcSignalingToken::decode(
            QStringLiteral("midiedit-rtc://v1:"), &s, &p, &e);
    QCOMPARE(kind, RtcSignalingToken::Kind::Invalid);
    QVERIFY(!e.isEmpty());
}

// "midiedit-rtc://v1:sess" - has sessionId but no second colon, so the
// kind is missing.
void TestRtcSignalingToken::decode_missingKindReturnsInvalid() {
    QString s, p, e;
    auto kind = RtcSignalingToken::decode(
            QStringLiteral("midiedit-rtc://v1:sess"), &s, &p, &e);
    QCOMPARE(kind, RtcSignalingToken::Kind::Invalid);
    QVERIFY(!e.isEmpty());
}

// Kind segment present but spelled wrong.
void TestRtcSignalingToken::decode_unknownKindReturnsInvalid() {
    QString s, p, e;
    auto kind = RtcSignalingToken::decode(
            QStringLiteral("midiedit-rtc://v1:s:carrierpigeon:abc"),
            &s, &p, &e);
    QCOMPARE(kind, RtcSignalingToken::Kind::Invalid);
    QVERIFY(e.contains(QStringLiteral("carrierpigeon")));
}

// Kind = offer but the base64 payload is empty.
void TestRtcSignalingToken::decode_emptyBase64ReturnsInvalid() {
    QString s, p, e;
    auto kind = RtcSignalingToken::decode(
            QStringLiteral("midiedit-rtc://v1:s:offer:"),
            &s, &p, &e);
    QCOMPARE(kind, RtcSignalingToken::Kind::Invalid);
    QVERIFY(!e.isEmpty());
}

void TestRtcSignalingToken::decode_corruptedBase64ReturnsInvalid() {
    QString s, p, e;
    // ASCII-looking but not valid base64 - qUncompress should fail on
    // the decompression step, returning empty.
    auto kind = RtcSignalingToken::decode(
            QStringLiteral("midiedit-rtc://v1:s:offer:CORRUPTED___$$$"),
            &s, &p, &e);
    QCOMPARE(kind, RtcSignalingToken::Kind::Invalid);
    QVERIFY(!e.isEmpty());
}

// errorOut is optional but must populate when supplied.
void TestRtcSignalingToken::decode_invalidErrorOutPopulated() {
    QString e;
    QString s, p;
    RtcSignalingToken::decode(
            QStringLiteral("garbage"), &s, &p, &e);
    QVERIFY(!e.isEmpty());
}

QTEST_APPLESS_MAIN(TestRtcSignalingToken)
#include "test_rtc_signaling_token.moc"
