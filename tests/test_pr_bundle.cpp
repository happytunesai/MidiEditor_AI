// Unit tests for PrBundle — codec for the smart-paste token (§10.1) and
// the on-disk bundle JSON (§6.3).
//
// Pure JSON / QByteArray / QString — no MidiFile dep. Testable in isolation.
//
// Run from a build configured with -DBUILD_TESTING=ON:
//     ctest --test-dir build -V -R PrBundle

#include "PrBundle.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QTemporaryDir>
#include <QtTest/QtTest>

class TestPrBundle : public QObject {
    Q_OBJECT

private slots:
    // isValid()
    void isValid_emptyBundleIsInvalid();
    void isValid_minimalRequiredFieldsMakeValid();

    // Bundle JSON round-trip
    void bundleJson_roundTripPreservesAllFields();
    void bundleJson_emptyHunksRoundTrip();
    void bundleJson_richHunksRoundTrip();
    void bundleJson_parseErrorReportsHumanReadableMessage();
    void bundleJson_futureSchemaVersionIsRejected();

    // Bundle file round-trip
    void bundleFile_writeReadRoundTrip();
    void bundleFile_missingFileReportsError();

    // Token: classification
    void classify_pureTextIsInvalid();
    void classify_inlineTokenRecognized();
    void classify_linkTokenRecognized();
    void classify_unknownKindIsInvalid();
    void looksLikeToken_doesNotMatchOtherStrings();

    // Token: extractors
    void extractSessionId_fromInlineToken();
    void extractSessionId_fromLinkToken();
    void extractSessionId_fromGarbageReturnsEmpty();
    void extractLinkUrl_returnsEmbeddedUrl();
    void extractLinkUrl_notALinkReturnsEmpty();

    // Inline token round-trip
    void inlineToken_roundTripSmallBundle();
    void inlineToken_roundTripWithLargeHunks();
    void inlineToken_corruptedPayloadFailsGracefully();
    void inlineToken_compressionShrinksLargeJson();

    // Cross-format consistency
    void crossFormat_bundleJsonAndInlineTokenProduceSameBundle();

private:
    static PrBundle makeSampleBundle() {
        PrBundle b;
        b.sessionId = QStringLiteral("f47ac10b-5832-4abc-a8e9-1f2c3d4e5f60");
        b.author = QStringLiteral("Alice");
        b.machineId = QStringLiteral("uuid-of-alice-machine");
        b.parentHash = QStringLiteral("abc123def456");
        b.timestamp = 1714730400;
        b.message = QStringLiteral("added bridge harmony");

        QJsonObject scope;
        scope.insert(QStringLiteral("channel"), 1);
        scope.insert(QStringLiteral("track"), 2);
        scope.insert(QStringLiteral("tickStart"), 1920);
        scope.insert(QStringLiteral("tickEnd"), 3840);
        QJsonObject hunk;
        hunk.insert(QStringLiteral("scope"), scope);
        hunk.insert(QStringLiteral("removed"), QJsonArray());
        QJsonArray added;
        QJsonObject n;
        n.insert(QStringLiteral("type"), QStringLiteral("note"));
        n.insert(QStringLiteral("channel"), 1);
        n.insert(QStringLiteral("track"), 2);
        n.insert(QStringLiteral("tick"), 1920);
        n.insert(QStringLiteral("note"), 64);
        n.insert(QStringLiteral("velocity"), 80);
        added.append(n);
        hunk.insert(QStringLiteral("added"), added);
        hunk.insert(QStringLiteral("modified"), QJsonArray());
        b.hunks.append(hunk);
        return b;
    }
};

// ---------------------------------------------------------------------
// isValid()
// ---------------------------------------------------------------------
void TestPrBundle::isValid_emptyBundleIsInvalid() {
    PrBundle b;
    QVERIFY(!b.isValid());
}

void TestPrBundle::isValid_minimalRequiredFieldsMakeValid() {
    PrBundle b;
    b.sessionId = QStringLiteral("sid");
    b.author = QStringLiteral("Alice");
    b.timestamp = 1;
    QVERIFY(b.isValid());
}

// ---------------------------------------------------------------------
// Bundle JSON round-trip
// ---------------------------------------------------------------------
void TestPrBundle::bundleJson_roundTripPreservesAllFields() {
    PrBundle in = makeSampleBundle();
    QByteArray bytes = in.toBundleJson();
    QVERIFY(!bytes.isEmpty());
    PrBundle out = PrBundle::fromBundleJson(bytes);
    QCOMPARE(out.sessionId, in.sessionId);
    QCOMPARE(out.author, in.author);
    QCOMPARE(out.machineId, in.machineId);
    QCOMPARE(out.parentHash, in.parentHash);
    QCOMPARE(out.timestamp, in.timestamp);
    QCOMPARE(out.message, in.message);
    QCOMPARE(out.hunks.size(), in.hunks.size());
}

void TestPrBundle::bundleJson_emptyHunksRoundTrip() {
    PrBundle in;
    in.sessionId = QStringLiteral("s");
    in.author = QStringLiteral("a");
    in.timestamp = 1;
    QByteArray bytes = in.toBundleJson();
    PrBundle out = PrBundle::fromBundleJson(bytes);
    QCOMPARE(out.hunks.size(), 0);
}

void TestPrBundle::bundleJson_richHunksRoundTrip() {
    PrBundle in = makeSampleBundle();
    // Add a second hunk with a modified entry.
    QJsonObject hunk2;
    QJsonObject scope2;
    scope2.insert(QStringLiteral("channel"), 9);
    scope2.insert(QStringLiteral("track"), 5);
    scope2.insert(QStringLiteral("tickStart"), 0);
    scope2.insert(QStringLiteral("tickEnd"), 480);
    hunk2.insert(QStringLiteral("scope"), scope2);
    QJsonObject before, after;
    before.insert(QStringLiteral("type"), QStringLiteral("note"));
    before.insert(QStringLiteral("velocity"), 60);
    after.insert(QStringLiteral("type"), QStringLiteral("note"));
    after.insert(QStringLiteral("velocity"), 100);
    QJsonObject pair;
    pair.insert(QStringLiteral("before"), before);
    pair.insert(QStringLiteral("after"), after);
    QJsonArray modified;
    modified.append(pair);
    hunk2.insert(QStringLiteral("modified"), modified);
    hunk2.insert(QStringLiteral("removed"), QJsonArray());
    hunk2.insert(QStringLiteral("added"), QJsonArray());
    in.hunks.append(hunk2);

    PrBundle out = PrBundle::fromBundleJson(in.toBundleJson());
    QCOMPARE(out.hunks.size(), 2);
    QJsonObject h2 = out.hunks.at(1).toObject();
    QCOMPARE(h2.value(QStringLiteral("modified")).toArray().size(), 1);
}

void TestPrBundle::bundleJson_parseErrorReportsHumanReadableMessage() {
    QString error;
    PrBundle b = PrBundle::fromBundleJson(QByteArrayLiteral("not json {{{ "), &error);
    QVERIFY(!b.isValid());
    QVERIFY(!error.isEmpty());
    QVERIFY(error.contains(QStringLiteral("parse"), Qt::CaseInsensitive));
}

void TestPrBundle::bundleJson_futureSchemaVersionIsRejected() {
    QJsonObject obj;
    obj.insert(QStringLiteral("schemaVersion"), 99);
    obj.insert(QStringLiteral("sessionId"), QStringLiteral("x"));
    obj.insert(QStringLiteral("author"), QStringLiteral("a"));
    obj.insert(QStringLiteral("ts"), 1);
    QJsonDocument d(obj);
    QString error;
    PrBundle b = PrBundle::fromBundleJson(d.toJson(QJsonDocument::Compact), &error);
    QVERIFY(!b.isValid());
    QVERIFY(error.contains(QStringLiteral("schema version"), Qt::CaseInsensitive));
}

// ---------------------------------------------------------------------
// Bundle file round-trip
// ---------------------------------------------------------------------
void TestPrBundle::bundleFile_writeReadRoundTrip() {
    PrBundle in = makeSampleBundle();
    // Use QTemporaryDir + a fresh filename inside it so QSaveFile's atomic
    // rename has no pre-existing target with a held handle.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString path = dir.path() + QStringLiteral("/bundle.midiedit-pr.json");
    QVERIFY(in.saveBundleToFile(path));
    QVERIFY(QFile::exists(path));

    QString error;
    PrBundle out = PrBundle::fromBundleFile(path, &error);
    QVERIFY2(out.isValid(), qPrintable(error));
    QCOMPARE(out.sessionId, in.sessionId);
    QCOMPARE(out.message, in.message);
}

void TestPrBundle::bundleFile_missingFileReportsError() {
    QString error;
    PrBundle b = PrBundle::fromBundleFile(QStringLiteral("Z:/this/path/does/not/exist.json"), &error);
    QVERIFY(!b.isValid());
    QVERIFY(!error.isEmpty());
}

// ---------------------------------------------------------------------
// Token classification
// ---------------------------------------------------------------------
void TestPrBundle::classify_pureTextIsInvalid() {
    QCOMPARE(PrBundle::classifyToken(QStringLiteral("hello world")), PrBundle::TokenKind::Invalid);
}

void TestPrBundle::classify_inlineTokenRecognized() {
    QString tok = QStringLiteral("midiedit-pr://v1:abc-sess:inline:eJx_nonsense_payload");
    QCOMPARE(PrBundle::classifyToken(tok), PrBundle::TokenKind::Inline);
}

void TestPrBundle::classify_linkTokenRecognized() {
    QString tok = QStringLiteral("midiedit-pr://v1:abc-sess:link:https://cdn.discord/abc.json");
    QCOMPARE(PrBundle::classifyToken(tok), PrBundle::TokenKind::Link);
}

void TestPrBundle::classify_unknownKindIsInvalid() {
    QString tok = QStringLiteral("midiedit-pr://v1:abc-sess:carrierpigeon:foo");
    QCOMPARE(PrBundle::classifyToken(tok), PrBundle::TokenKind::Invalid);
}

void TestPrBundle::looksLikeToken_doesNotMatchOtherStrings() {
    QVERIFY(!PrBundle::looksLikeToken(QStringLiteral("https://foo.bar/baz")));
    QVERIFY(!PrBundle::looksLikeToken(QStringLiteral("midi-pr-too-short")));
    QVERIFY(!PrBundle::looksLikeToken(QStringLiteral("")));
    QVERIFY(PrBundle::looksLikeToken(QStringLiteral("midiedit-pr://v1:s:inline:p")));
}

// ---------------------------------------------------------------------
// Extractors
// ---------------------------------------------------------------------
void TestPrBundle::extractSessionId_fromInlineToken() {
    QString tok = QStringLiteral("midiedit-pr://v1:abc-sess:inline:foo");
    QCOMPARE(PrBundle::extractSessionId(tok), QStringLiteral("abc-sess"));
}

void TestPrBundle::extractSessionId_fromLinkToken() {
    QString tok = QStringLiteral("midiedit-pr://v1:def-sess:link:https://x.y/z");
    QCOMPARE(PrBundle::extractSessionId(tok), QStringLiteral("def-sess"));
}

void TestPrBundle::extractSessionId_fromGarbageReturnsEmpty() {
    QCOMPARE(PrBundle::extractSessionId(QStringLiteral("not-a-token")), QString());
}

void TestPrBundle::extractLinkUrl_returnsEmbeddedUrl() {
    QString tok = QStringLiteral("midiedit-pr://v1:s:link:https://cdn.discord/path/to/bundle.json");
    QCOMPARE(PrBundle::extractLinkUrl(tok), QStringLiteral("https://cdn.discord/path/to/bundle.json"));
}

void TestPrBundle::extractLinkUrl_notALinkReturnsEmpty() {
    QString tok = QStringLiteral("midiedit-pr://v1:s:inline:abc");
    QCOMPARE(PrBundle::extractLinkUrl(tok), QString());
}

// ---------------------------------------------------------------------
// Inline token round-trip
// ---------------------------------------------------------------------
void TestPrBundle::inlineToken_roundTripSmallBundle() {
    PrBundle in = makeSampleBundle();
    QString token = in.toInlineToken();
    QVERIFY(PrBundle::looksLikeToken(token));
    QCOMPARE(PrBundle::classifyToken(token), PrBundle::TokenKind::Inline);

    QString error;
    PrBundle out = PrBundle::fromInlineToken(token, &error);
    QVERIFY2(out.isValid(), qPrintable(error));
    QCOMPARE(out.sessionId, in.sessionId);
    QCOMPARE(out.author, in.author);
    QCOMPARE(out.timestamp, in.timestamp);
    QCOMPARE(out.message, in.message);
    QCOMPARE(out.hunks.size(), in.hunks.size());
}

void TestPrBundle::inlineToken_roundTripWithLargeHunks() {
    PrBundle in = makeSampleBundle();
    // Pad with 100 synthetic note events to exercise compression.
    QJsonObject hunk = in.hunks.at(0).toObject();
    QJsonArray added = hunk.value(QStringLiteral("added")).toArray();
    for (int i = 0; i < 100; ++i) {
        QJsonObject n;
        n.insert(QStringLiteral("type"), QStringLiteral("note"));
        n.insert(QStringLiteral("channel"), 1);
        n.insert(QStringLiteral("track"), 2);
        n.insert(QStringLiteral("tick"), 1920 + i * 60);
        n.insert(QStringLiteral("note"), 60 + (i % 12));
        n.insert(QStringLiteral("velocity"), 80);
        added.append(n);
    }
    hunk.insert(QStringLiteral("added"), added);
    in.hunks = QJsonArray{hunk};

    QString token = in.toInlineToken();
    PrBundle out = PrBundle::fromInlineToken(token);
    QVERIFY(out.isValid());
    QCOMPARE(out.hunks.at(0).toObject().value(QStringLiteral("added")).toArray().size(), 101);
}

void TestPrBundle::inlineToken_corruptedPayloadFailsGracefully() {
    QString token = QStringLiteral("midiedit-pr://v1:s:inline:NOT_VALID_BASE64_$$");
    QString error;
    PrBundle out = PrBundle::fromInlineToken(token, &error);
    QVERIFY(!out.isValid());
    QVERIFY(!error.isEmpty());
}

void TestPrBundle::inlineToken_compressionShrinksLargeJson() {
    PrBundle in = makeSampleBundle();
    // Repetitive payload compresses well.
    QJsonObject hunk = in.hunks.at(0).toObject();
    QJsonArray added;
    QJsonObject sample = hunk.value(QStringLiteral("added")).toArray().at(0).toObject();
    for (int i = 0; i < 200; ++i) added.append(sample);
    hunk.insert(QStringLiteral("added"), added);
    in.hunks = QJsonArray{hunk};

    QByteArray rawJson = in.toBundleJson();
    QString token = in.toInlineToken();
    // Compressed token (incl. scheme prefix) should be much smaller than raw
    // JSON for this very repetitive payload.
    QVERIFY2(token.size() * 2 < rawJson.size(),
             qPrintable(QString("token size %1, raw json %2").arg(token.size()).arg(rawJson.size())));
}

// ---------------------------------------------------------------------
// Cross-format consistency
// ---------------------------------------------------------------------
void TestPrBundle::crossFormat_bundleJsonAndInlineTokenProduceSameBundle() {
    PrBundle in = makeSampleBundle();
    PrBundle viaJson = PrBundle::fromBundleJson(in.toBundleJson());
    PrBundle viaToken = PrBundle::fromInlineToken(in.toInlineToken());
    QVERIFY(viaJson.isValid());
    QVERIFY(viaToken.isValid());
    QCOMPARE(viaJson.sessionId, viaToken.sessionId);
    QCOMPARE(viaJson.message, viaToken.message);
    QCOMPARE(viaJson.hunks.size(), viaToken.hunks.size());
}

QTEST_APPLESS_MAIN(TestPrBundle)
#include "test_pr_bundle.moc"
