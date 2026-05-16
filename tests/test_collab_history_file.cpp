// Unit tests for CollabHistoryFile — sidecar JSON file that tracks the
// collaboration history of one .mid file. Lives at
// "<song>.midiedit-collab.json" next to the .mid (Phase 9.1b).
//
// CollabHistoryFile is pure Qt (QJson + QSaveFile + QUuid) with no MidiFile,
// MidiEvent, or other project deps — the test compiles only the single .cpp
// and uses QTemporaryDir to keep all I/O scoped to a per-run sandbox.
//
// Run from a build configured with -DBUILD_TESTING=ON:
//     ctest --test-dir build -V -R CollabHistoryFile

#include "CollabHistoryFile.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest/QtTest>

class TestCollabHistoryFile : public QObject {
    Q_OBJECT

private slots:
    // sidecarPathFor — pure path translation
    void sidecarPathFor_emptyInputReturnsEmpty();
    void sidecarPathFor_stripsMidExtensionCaseInsensitive();
    void sidecarPathFor_stripsMidiAndKarExtensions();
    void sidecarPathFor_unknownExtensionStripsLastDot();
    void sidecarPathFor_noExtensionAppendsSuffix();

    // exists
    void exists_falseForEmptyPath();
    void exists_falseWhenSidecarMissing();
    void exists_trueAfterSave();

    // load / loadFromJson — tolerant field parsing
    void load_missingFileReturnsFalseAndLeavesStateEmpty();
    void load_malformedJsonReturnsFalseAndLeavesStateEmpty();
    void loadFromJson_missingFieldsFallBackToDefaults();
    void loadFromJson_legacySidecarWithoutSessionIdGetsOneAutomatically();
    void loadFromJson_preservesExistingSessionId();

    // save round-trip
    void save_thenLoad_roundTripsAllFields();
    void save_atomicWriteOverwritesPriorContents();

    // appendCommit
    void appendCommit_updatesCurrentHeadAndAppendsEntry();
    void appendCommit_preservesInsertionOrder();
    void appendCommit_markedAsLoaded();

    // ensureSessionId
    void ensureSessionId_generatesOnEmpty();
    void ensureSessionId_idempotent();

    // compactHistory
    void compactHistory_keepsLastNHunks();
    void compactHistory_negativeKeepNTreatedAsZero();
    void compactHistory_preservesMetadataOfStrippedEntries();
};

// ---------------------------------------------------------------------
// sidecarPathFor
// ---------------------------------------------------------------------

void TestCollabHistoryFile::sidecarPathFor_emptyInputReturnsEmpty() {
    QCOMPARE(CollabHistoryFile::sidecarPathFor(QString()), QString());
}

// "foo.mid" -> "foo.midiedit-collab.json"; case-insensitive match on suffix
// so "FOO.MID" / "FOO.Mid" behave the same as "foo.mid".
void TestCollabHistoryFile::sidecarPathFor_stripsMidExtensionCaseInsensitive() {
    QString p1 = CollabHistoryFile::sidecarPathFor(QStringLiteral("/tmp/foo.mid"));
    QVERIFY(p1.endsWith(QStringLiteral("/foo.midiedit-collab.json")));

    QString p2 = CollabHistoryFile::sidecarPathFor(QStringLiteral("/tmp/BAR.MID"));
    QVERIFY(p2.endsWith(QStringLiteral("/BAR.midiedit-collab.json")));

    QString p3 = CollabHistoryFile::sidecarPathFor(QStringLiteral("/tmp/Mixed.MiD"));
    QVERIFY(p3.endsWith(QStringLiteral("/Mixed.midiedit-collab.json")));
}

void TestCollabHistoryFile::sidecarPathFor_stripsMidiAndKarExtensions() {
    QString midi = CollabHistoryFile::sidecarPathFor(QStringLiteral("/tmp/song.midi"));
    QVERIFY(midi.endsWith(QStringLiteral("/song.midiedit-collab.json")));

    QString kar = CollabHistoryFile::sidecarPathFor(QStringLiteral("/tmp/song.kar"));
    QVERIFY(kar.endsWith(QStringLiteral("/song.midiedit-collab.json")));
}

// Unknown extensions strip everything after the last dot (no special-case
// handling). Reasonable behavior for ".foo.bar" / ".something.weird" inputs.
void TestCollabHistoryFile::sidecarPathFor_unknownExtensionStripsLastDot() {
    QString p = CollabHistoryFile::sidecarPathFor(QStringLiteral("/tmp/song.weird"));
    QVERIFY(p.endsWith(QStringLiteral("/song.midiedit-collab.json")));
}

// A path with no extension at all just appends the suffix; the basename
// is preserved verbatim.
void TestCollabHistoryFile::sidecarPathFor_noExtensionAppendsSuffix() {
    QString p = CollabHistoryFile::sidecarPathFor(QStringLiteral("/tmp/songfile"));
    QVERIFY(p.endsWith(QStringLiteral("/songfile.midiedit-collab.json")));
}

// ---------------------------------------------------------------------
// exists
// ---------------------------------------------------------------------

void TestCollabHistoryFile::exists_falseForEmptyPath() {
    QVERIFY(!CollabHistoryFile::exists(QString()));
}

void TestCollabHistoryFile::exists_falseWhenSidecarMissing() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString midi = dir.filePath(QStringLiteral("nothing.mid"));
    QVERIFY(!CollabHistoryFile::exists(midi));
}

void TestCollabHistoryFile::exists_trueAfterSave() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString midi = dir.filePath(QStringLiteral("present.mid"));

    CollabHistoryFile f;
    f.appendCommit(QStringLiteral("hash1"), QString(),
                   QStringLiteral("Alice"), QStringLiteral("mid1"),
                   1714730000, QStringLiteral("init"));
    QVERIFY(f.save(midi));
    QVERIFY(CollabHistoryFile::exists(midi));
}

// ---------------------------------------------------------------------
// load — failure modes
// ---------------------------------------------------------------------

// Missing file: load returns false; the instance state is the same as
// default-constructed (empty currentHead / empty history / branch=="main").
void TestCollabHistoryFile::load_missingFileReturnsFalseAndLeavesStateEmpty() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString midi = dir.filePath(QStringLiteral("absent.mid"));

    CollabHistoryFile f;
    QVERIFY(!f.load(midi));
    QVERIFY(!f.isLoaded());
    QCOMPARE(f.currentHead(), QString());
    QCOMPARE(f.branch(), QStringLiteral("main"));
    QCOMPARE(f.history().size(), 0);
}

// Malformed JSON (not even valid JSON) is rejected; state is left empty.
void TestCollabHistoryFile::load_malformedJsonReturnsFalseAndLeavesStateEmpty() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString midi = dir.filePath(QStringLiteral("broken.mid"));
    QString sidecar = CollabHistoryFile::sidecarPathFor(midi);

    QFile out(sidecar);
    QVERIFY(out.open(QIODevice::WriteOnly));
    out.write("not-json-at-all{{{");
    out.close();

    CollabHistoryFile f;
    QVERIFY(!f.load(midi));
    QVERIFY(!f.isLoaded());
    QCOMPARE(f.currentHead(), QString());
    QCOMPARE(f.history().size(), 0);
}

// ---------------------------------------------------------------------
// loadFromJson — tolerant field parsing
// ---------------------------------------------------------------------

// Missing fields fall back to documented defaults (branch="main",
// currentHead="", lastSharedHead="", history=[]). loadFromJson always
// returns true for a valid JSON object — schema lenience by design.
void TestCollabHistoryFile::loadFromJson_missingFieldsFallBackToDefaults() {
    QJsonObject obj;  // completely empty object
    CollabHistoryFile f;
    QVERIFY(f.loadFromJson(obj));
    QVERIFY(f.isLoaded());
    QCOMPARE(f.currentHead(), QString());
    QCOMPARE(f.lastSharedHead(), QString());
    QCOMPARE(f.branch(), QStringLiteral("main"));
    QCOMPARE(f.history().size(), 0);
    // ensureSessionId() runs at the end of loadFromJson so a fresh UUID
    // is auto-assigned even though the source object had none.
    QVERIFY(!f.sessionId().isEmpty());
}

// Legacy sidecar without a sessionId — the field is auto-generated on
// load so subsequent saves can carry it. Important for the upgrade path
// from v1.7.0-pre schema to v1.7.0+.
void TestCollabHistoryFile::loadFromJson_legacySidecarWithoutSessionIdGetsOneAutomatically() {
    QJsonObject obj;
    obj.insert(QStringLiteral("currentHead"), QStringLiteral("abc"));
    obj.insert(QStringLiteral("branch"), QStringLiteral("main"));
    obj.insert(QStringLiteral("history"), QJsonArray());

    CollabHistoryFile f;
    QVERIFY(f.loadFromJson(obj));
    QVERIFY(!f.sessionId().isEmpty());
    // Looks like a UUID-without-braces
    QUuid u = QUuid::fromString(f.sessionId());
    QVERIFY2(!u.isNull(), qPrintable(f.sessionId()));
}

// An existing sessionId is preserved verbatim (no regeneration).
void TestCollabHistoryFile::loadFromJson_preservesExistingSessionId() {
    QJsonObject obj;
    obj.insert(QStringLiteral("sessionId"), QStringLiteral("fixed-id-abc"));

    CollabHistoryFile f;
    QVERIFY(f.loadFromJson(obj));
    QCOMPARE(f.sessionId(), QStringLiteral("fixed-id-abc"));
}

// ---------------------------------------------------------------------
// save / load round-trip
// ---------------------------------------------------------------------

void TestCollabHistoryFile::save_thenLoad_roundTripsAllFields() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString midi = dir.filePath(QStringLiteral("song.mid"));

    CollabHistoryFile a;
    a.setSessionId(QStringLiteral("sess-xyz"));
    a.setBranch(QStringLiteral("feature-bridge"));
    a.setLastSharedHead(QStringLiteral("shared-deadbeef"));
    a.appendCommit(QStringLiteral("hashA"), QString(),
                   QStringLiteral("Alice"), QStringLiteral("mid-a"),
                   1714730000, QStringLiteral("init"));
    a.appendCommit(QStringLiteral("hashB"), QStringLiteral("hashA"),
                   QStringLiteral("Bob"), QStringLiteral("mid-b"),
                   1714730100, QStringLiteral("bridge"));
    QVERIFY(a.save(midi));

    CollabHistoryFile b;
    QVERIFY(b.load(midi));
    QCOMPARE(b.sessionId(), QStringLiteral("sess-xyz"));
    QCOMPARE(b.branch(), QStringLiteral("feature-bridge"));
    QCOMPARE(b.lastSharedHead(), QStringLiteral("shared-deadbeef"));
    QCOMPARE(b.currentHead(), QStringLiteral("hashB"));
    QCOMPARE(b.history().size(), 2);

    // Spot-check the chained commit metadata.
    QJsonObject e0 = b.history().at(0).toObject();
    QJsonObject e1 = b.history().at(1).toObject();
    QCOMPARE(e0.value("hash").toString(), QStringLiteral("hashA"));
    QCOMPARE(e0.value("parentHash").toString(), QString());
    QCOMPARE(e0.value("author").toString(), QStringLiteral("Alice"));
    QCOMPARE(e1.value("hash").toString(), QStringLiteral("hashB"));
    QCOMPARE(e1.value("parentHash").toString(), QStringLiteral("hashA"));
    QCOMPARE(e1.value("author").toString(), QStringLiteral("Bob"));
}

// QSaveFile-based atomic replace: saving over an existing sidecar
// fully overwrites it (no leftover bytes from the previous version).
void TestCollabHistoryFile::save_atomicWriteOverwritesPriorContents() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString midi = dir.filePath(QStringLiteral("song.mid"));

    CollabHistoryFile big;
    for (int i = 0; i < 20; ++i) {
        big.appendCommit(QStringLiteral("hash%1").arg(i), QString(),
                         QStringLiteral("Alice"), QStringLiteral("mid"),
                         1700000000 + i, QStringLiteral("commit %1").arg(i));
    }
    QVERIFY(big.save(midi));
    qint64 bigSize = QFile(CollabHistoryFile::sidecarPathFor(midi)).size();
    QVERIFY(bigSize > 0);

    CollabHistoryFile small;
    small.setSessionId(QStringLiteral("post-replace"));
    QVERIFY(small.save(midi));
    qint64 smallSize = QFile(CollabHistoryFile::sidecarPathFor(midi)).size();
    QVERIFY2(smallSize < bigSize,
             qPrintable(QStringLiteral("expected truncation; before=%1 after=%2")
                            .arg(bigSize).arg(smallSize)));

    CollabHistoryFile loaded;
    QVERIFY(loaded.load(midi));
    QCOMPARE(loaded.sessionId(), QStringLiteral("post-replace"));
    QCOMPARE(loaded.history().size(), 0);
}

// ---------------------------------------------------------------------
// appendCommit
// ---------------------------------------------------------------------

void TestCollabHistoryFile::appendCommit_updatesCurrentHeadAndAppendsEntry() {
    CollabHistoryFile f;
    QCOMPARE(f.currentHead(), QString());
    QCOMPARE(f.history().size(), 0);

    f.appendCommit(QStringLiteral("hash1"), QString(),
                   QStringLiteral("Alice"), QStringLiteral("mid1"),
                   1714730000, QStringLiteral("init"));
    QCOMPARE(f.currentHead(), QStringLiteral("hash1"));
    QCOMPARE(f.history().size(), 1);

    QJsonArray hunks;
    QJsonObject hunk;
    hunk.insert(QStringLiteral("scope"), QJsonObject{{QStringLiteral("channel"), 1}});
    hunks.append(hunk);
    f.appendCommit(QStringLiteral("hash2"), QStringLiteral("hash1"),
                   QStringLiteral("Bob"), QStringLiteral("mid2"),
                   1714730100, QStringLiteral("note tweak"), hunks);
    QCOMPARE(f.currentHead(), QStringLiteral("hash2"));
    QCOMPARE(f.history().size(), 2);
    // Hunks survive the round-trip into the JSON array
    QJsonObject e = f.history().at(1).toObject();
    QCOMPARE(e.value("hunks").toArray().size(), 1);
}

void TestCollabHistoryFile::appendCommit_preservesInsertionOrder() {
    CollabHistoryFile f;
    f.appendCommit(QStringLiteral("first"),  QString(),
                   QStringLiteral("A"), QStringLiteral("ma"), 1, QStringLiteral("a"));
    f.appendCommit(QStringLiteral("second"), QStringLiteral("first"),
                   QStringLiteral("B"), QStringLiteral("mb"), 2, QStringLiteral("b"));
    f.appendCommit(QStringLiteral("third"),  QStringLiteral("second"),
                   QStringLiteral("C"), QStringLiteral("mc"), 3, QStringLiteral("c"));

    QCOMPARE(f.history().at(0).toObject().value("hash").toString(),
             QStringLiteral("first"));
    QCOMPARE(f.history().at(1).toObject().value("hash").toString(),
             QStringLiteral("second"));
    QCOMPARE(f.history().at(2).toObject().value("hash").toString(),
             QStringLiteral("third"));
}

// appendCommit must transition isLoaded() to true so subsequent save()
// callers can tell that the in-memory state diverges from disk.
void TestCollabHistoryFile::appendCommit_markedAsLoaded() {
    CollabHistoryFile f;
    QVERIFY(!f.isLoaded());
    f.appendCommit(QStringLiteral("h"), QString(),
                   QStringLiteral("A"), QStringLiteral("m"), 1, QStringLiteral("x"));
    QVERIFY(f.isLoaded());
}

// ---------------------------------------------------------------------
// ensureSessionId
// ---------------------------------------------------------------------

void TestCollabHistoryFile::ensureSessionId_generatesOnEmpty() {
    CollabHistoryFile f;
    QVERIFY(f.sessionId().isEmpty());
    f.ensureSessionId();
    QVERIFY(!f.sessionId().isEmpty());
    QUuid u = QUuid::fromString(f.sessionId());
    QVERIFY2(!u.isNull(), qPrintable(f.sessionId()));
    QVERIFY(!f.sessionId().startsWith(QChar('{')));
    QVERIFY(!f.sessionId().endsWith(QChar('}')));
}

void TestCollabHistoryFile::ensureSessionId_idempotent() {
    CollabHistoryFile f;
    f.ensureSessionId();
    QString first = f.sessionId();
    f.ensureSessionId();
    f.ensureSessionId();
    QCOMPARE(f.sessionId(), first);
}

// ---------------------------------------------------------------------
// compactHistory
// ---------------------------------------------------------------------

namespace {

QJsonArray makeHunk() {
    QJsonObject h;
    h.insert(QStringLiteral("scope"), QJsonObject{{QStringLiteral("channel"), 1}});
    h.insert(QStringLiteral("added"), QJsonArray{1, 2, 3});
    QJsonArray arr;
    arr.append(h);
    return arr;
}

CollabHistoryFile buildFiveCommitHistory() {
    CollabHistoryFile f;
    for (int i = 0; i < 5; ++i) {
        f.appendCommit(
            QStringLiteral("hash%1").arg(i),
            i == 0 ? QString() : QStringLiteral("hash%1").arg(i - 1),
            QStringLiteral("Author"),
            QStringLiteral("machine"),
            1700000000 + i,
            QStringLiteral("commit %1").arg(i),
            makeHunk()
        );
    }
    return f;
}

}  // namespace

// Keep the last 2 — entries 0/1/2 lose their hunks, 3/4 keep them.
void TestCollabHistoryFile::compactHistory_keepsLastNHunks() {
    CollabHistoryFile f = buildFiveCommitHistory();
    int stripped = f.compactHistory(2);
    QCOMPARE(stripped, 3);

    auto hunksAt = [&](int i) {
        return f.history().at(i).toObject().value("hunks").toArray().size();
    };
    QCOMPARE(hunksAt(0), 0);
    QCOMPARE(hunksAt(1), 0);
    QCOMPARE(hunksAt(2), 0);
    QCOMPARE(hunksAt(3), 1);
    QCOMPARE(hunksAt(4), 1);
}

// Negative keepN is documented as "clamped to 0" — strips ALL hunks.
void TestCollabHistoryFile::compactHistory_negativeKeepNTreatedAsZero() {
    CollabHistoryFile f = buildFiveCommitHistory();
    int stripped = f.compactHistory(-3);
    QCOMPARE(stripped, 5);
    for (int i = 0; i < 5; ++i) {
        QCOMPARE(f.history().at(i).toObject().value("hunks").toArray().size(), 0);
    }
}

// Stripping hunks must preserve every other audit field (hash, parentHash,
// author, machineId, ts, message). Important: the chain stays auditable
// even after compaction.
void TestCollabHistoryFile::compactHistory_preservesMetadataOfStrippedEntries() {
    CollabHistoryFile f = buildFiveCommitHistory();
    f.compactHistory(1);
    QJsonObject e0 = f.history().at(0).toObject();
    QCOMPARE(e0.value("hash").toString(), QStringLiteral("hash0"));
    QCOMPARE(e0.value("parentHash").toString(), QString());
    QCOMPARE(e0.value("author").toString(), QStringLiteral("Author"));
    QCOMPARE(e0.value("machineId").toString(), QStringLiteral("machine"));
    QCOMPARE(static_cast<qint64>(e0.value("ts").toDouble()),
             static_cast<qint64>(1700000000));
    QCOMPARE(e0.value("message").toString(), QStringLiteral("commit 0"));
    // ...but hunks are gone
    QCOMPARE(e0.value("hunks").toArray().size(), 0);
}

QTEST_APPLESS_MAIN(TestCollabHistoryFile)
#include "test_collab_history_file.moc"
