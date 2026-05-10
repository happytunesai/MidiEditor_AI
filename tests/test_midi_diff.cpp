// Unit tests for MidiDiff — pure JSON-in/JSON-out diff algorithm.
//
// MidiDiff has no dependencies on MidiFile, MidiChannel or any other
// project source beyond Qt's JSON / container types, so the test
// executable links only the single .cpp.
//
// Run from a build configured with -DBUILD_TESTING=ON:
//     ctest --test-dir build -V -R MidiDiff
// or directly:
//     build/tests/test_midi_diff.exe

#include "MidiDiff.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QtTest/QtTest>

class TestMidiDiff : public QObject {
    Q_OBJECT

private slots:
    // Identity tuples
    void identity_noteIncludesPitch();
    void identity_ccIncludesController();
    void identity_pitchBendIsTickAndChannel();
    void identity_unknownTypeReturnsEmpty();

    // Equality (id field ignored)
    void equality_idFieldIsIgnored();
    void equality_velocityChangeMakesUnequal();

    // Diff: empty / no changes
    void diff_identicalSnapshotsProduceNoHunks();
    void diff_emptyParentMakesEverythingAdded();
    void diff_emptyCurrentMakesEverythingRemoved();

    // Diff: single change types
    void diff_oneNoteAdded();
    void diff_oneNoteRemoved();
    void diff_oneNoteModifiedByVelocity();

    // Hunk grouping
    void grouping_eventsOnDifferentChannelsProduceSeparateHunks();
    void grouping_eventsOnDifferentTracksProduceSeparateHunks();
    void grouping_eventsCloseInTimeStayInOneHunk();
    void grouping_eventsAcrossLargeGapSplitIntoSeparateHunks();

    // Scope content
    void scope_includesMeasureNumbersFromTicksPerQuarter();
    void scope_tickRangeMatchesEarliestAndLatestChange();

private:
    static QJsonObject makeNote(int channel, int track, int tick, int note,
                                int velocity = 80, int duration = 240) {
        QJsonObject o;
        o.insert(QStringLiteral("type"), QStringLiteral("note"));
        o.insert(QStringLiteral("channel"), channel);
        o.insert(QStringLiteral("track"), track);
        o.insert(QStringLiteral("tick"), tick);
        o.insert(QStringLiteral("note"), note);
        o.insert(QStringLiteral("velocity"), velocity);
        o.insert(QStringLiteral("duration"), duration);
        return o;
    }

    static QJsonObject makeCc(int channel, int track, int tick, int control, int value) {
        QJsonObject o;
        o.insert(QStringLiteral("type"), QStringLiteral("cc"));
        o.insert(QStringLiteral("channel"), channel);
        o.insert(QStringLiteral("track"), track);
        o.insert(QStringLiteral("tick"), tick);
        o.insert(QStringLiteral("control"), control);
        o.insert(QStringLiteral("value"), value);
        return o;
    }
};

// ---------------------------------------------------------------------
// Identity tuples
// ---------------------------------------------------------------------
void TestMidiDiff::identity_noteIncludesPitch() {
    QJsonObject n1 = makeNote(0, 1, 100, 60);
    QJsonObject n2 = makeNote(0, 1, 100, 61);
    QVERIFY(MidiDiff::identityKey(n1) != MidiDiff::identityKey(n2));
}

void TestMidiDiff::identity_ccIncludesController() {
    QJsonObject c1 = makeCc(0, 1, 100, 7, 80);
    QJsonObject c2 = makeCc(0, 1, 100, 11, 80);
    QVERIFY(MidiDiff::identityKey(c1) != MidiDiff::identityKey(c2));
}

void TestMidiDiff::identity_pitchBendIsTickAndChannel() {
    QJsonObject pb;
    pb.insert(QStringLiteral("type"), QStringLiteral("pitch_bend"));
    pb.insert(QStringLiteral("channel"), 2);
    pb.insert(QStringLiteral("track"), 3);
    pb.insert(QStringLiteral("tick"), 480);
    pb.insert(QStringLiteral("value"), 8000);
    QString key = MidiDiff::identityKey(pb);
    QVERIFY(!key.isEmpty());
    QVERIFY(key.contains(QStringLiteral("|2|3|480")));
}

void TestMidiDiff::identity_unknownTypeReturnsEmpty() {
    QJsonObject o;
    o.insert(QStringLiteral("type"), QStringLiteral("sysex"));
    QVERIFY(MidiDiff::identityKey(o).isEmpty());
}

// ---------------------------------------------------------------------
// Equality
// ---------------------------------------------------------------------
void TestMidiDiff::equality_idFieldIsIgnored() {
    QJsonObject a = makeNote(0, 1, 100, 60);
    QJsonObject b = makeNote(0, 1, 100, 60);
    a.insert(QStringLiteral("id"), 0);
    b.insert(QStringLiteral("id"), 999);
    QVERIFY(MidiDiff::eventsEqual(a, b));
}

void TestMidiDiff::equality_velocityChangeMakesUnequal() {
    QJsonObject a = makeNote(0, 1, 100, 60, 60);
    QJsonObject b = makeNote(0, 1, 100, 60, 100);
    QVERIFY(!MidiDiff::eventsEqual(a, b));
}

// ---------------------------------------------------------------------
// Diff: empty / no changes
// ---------------------------------------------------------------------
void TestMidiDiff::diff_identicalSnapshotsProduceNoHunks() {
    QJsonArray snap;
    snap.append(makeNote(0, 1, 0, 60));
    snap.append(makeNote(0, 1, 240, 64));
    QCOMPARE(MidiDiff::compute(snap, snap, 480).size(), 0);
}

void TestMidiDiff::diff_emptyParentMakesEverythingAdded() {
    QJsonArray current;
    current.append(makeNote(0, 1, 0, 60));
    current.append(makeNote(0, 1, 240, 64));
    QJsonArray hunks = MidiDiff::compute(QJsonArray(), current, 480);
    QCOMPARE(hunks.size(), 1);
    QJsonObject h = hunks.at(0).toObject();
    QCOMPARE(h.value(QStringLiteral("added")).toArray().size(), 2);
    QCOMPARE(h.value(QStringLiteral("removed")).toArray().size(), 0);
    QCOMPARE(h.value(QStringLiteral("modified")).toArray().size(), 0);
}

void TestMidiDiff::diff_emptyCurrentMakesEverythingRemoved() {
    QJsonArray parent;
    parent.append(makeNote(0, 1, 0, 60));
    QJsonArray hunks = MidiDiff::compute(parent, QJsonArray(), 480);
    QCOMPARE(hunks.size(), 1);
    QJsonObject h = hunks.at(0).toObject();
    QCOMPARE(h.value(QStringLiteral("removed")).toArray().size(), 1);
    QCOMPARE(h.value(QStringLiteral("added")).toArray().size(), 0);
}

// ---------------------------------------------------------------------
// Diff: single change types
// ---------------------------------------------------------------------
void TestMidiDiff::diff_oneNoteAdded() {
    QJsonArray parent;
    parent.append(makeNote(0, 1, 0, 60));
    QJsonArray current = parent;
    current.append(makeNote(0, 1, 240, 64));
    QJsonArray hunks = MidiDiff::compute(parent, current, 480);
    QCOMPARE(hunks.size(), 1);
    QJsonObject h = hunks.at(0).toObject();
    QCOMPARE(h.value(QStringLiteral("added")).toArray().size(), 1);
    QCOMPARE(h.value(QStringLiteral("removed")).toArray().size(), 0);
}

void TestMidiDiff::diff_oneNoteRemoved() {
    QJsonArray parent;
    parent.append(makeNote(0, 1, 0, 60));
    parent.append(makeNote(0, 1, 240, 64));
    QJsonArray current;
    current.append(makeNote(0, 1, 0, 60));
    QJsonArray hunks = MidiDiff::compute(parent, current, 480);
    QCOMPARE(hunks.size(), 1);
    QJsonObject h = hunks.at(0).toObject();
    QCOMPARE(h.value(QStringLiteral("removed")).toArray().size(), 1);
    QCOMPARE(h.value(QStringLiteral("added")).toArray().size(), 0);
}

void TestMidiDiff::diff_oneNoteModifiedByVelocity() {
    QJsonArray parent;
    parent.append(makeNote(0, 1, 0, 60, 60));
    QJsonArray current;
    current.append(makeNote(0, 1, 0, 60, 100));
    QJsonArray hunks = MidiDiff::compute(parent, current, 480);
    QCOMPARE(hunks.size(), 1);
    QJsonObject h = hunks.at(0).toObject();
    QCOMPARE(h.value(QStringLiteral("modified")).toArray().size(), 1);
    QCOMPARE(h.value(QStringLiteral("added")).toArray().size(), 0);
    QCOMPARE(h.value(QStringLiteral("removed")).toArray().size(), 0);
    // The modified entry carries before+after:
    QJsonObject m = h.value(QStringLiteral("modified")).toArray().at(0).toObject();
    QCOMPARE(m.value(QStringLiteral("before")).toObject().value(QStringLiteral("velocity")).toInt(), 60);
    QCOMPARE(m.value(QStringLiteral("after")).toObject().value(QStringLiteral("velocity")).toInt(), 100);
}

// ---------------------------------------------------------------------
// Hunk grouping
// ---------------------------------------------------------------------
void TestMidiDiff::grouping_eventsOnDifferentChannelsProduceSeparateHunks() {
    QJsonArray current;
    current.append(makeNote(0, 1, 0, 60));
    current.append(makeNote(1, 1, 0, 60));
    QJsonArray hunks = MidiDiff::compute(QJsonArray(), current, 480);
    QCOMPARE(hunks.size(), 2);
}

void TestMidiDiff::grouping_eventsOnDifferentTracksProduceSeparateHunks() {
    QJsonArray current;
    current.append(makeNote(0, 1, 0, 60));
    current.append(makeNote(0, 2, 0, 60));
    QJsonArray hunks = MidiDiff::compute(QJsonArray(), current, 480);
    QCOMPARE(hunks.size(), 2);
}

void TestMidiDiff::grouping_eventsCloseInTimeStayInOneHunk() {
    // Two notes on (ch 0, trk 1) within ~1 measure of 4/4 at PPQ=480 → one hunk.
    QJsonArray current;
    current.append(makeNote(0, 1, 0, 60));
    current.append(makeNote(0, 1, 480, 64));   // 1 quarter later
    current.append(makeNote(0, 1, 1920, 67));  // 4 quarters later (one full measure)
    QJsonArray hunks = MidiDiff::compute(QJsonArray(), current, 480);
    QCOMPARE(hunks.size(), 1);
}

void TestMidiDiff::grouping_eventsAcrossLargeGapSplitIntoSeparateHunks() {
    // gap of > 4 measures (= 16 quarters at PPQ 480 = 7680 ticks) → split.
    QJsonArray current;
    current.append(makeNote(0, 1, 0, 60));
    current.append(makeNote(0, 1, 8000, 60));  // 16+ quarters away
    QJsonArray hunks = MidiDiff::compute(QJsonArray(), current, 480);
    QCOMPARE(hunks.size(), 2);
}

// ---------------------------------------------------------------------
// Scope content
// ---------------------------------------------------------------------
void TestMidiDiff::scope_includesMeasureNumbersFromTicksPerQuarter() {
    // PPQ=480, 4/4 → 1920 ticks per measure.
    // Tick 1920 = start of measure 1 (zero-indexed); tick 0 = measure 0.
    QJsonArray current;
    current.append(makeNote(0, 1, 0, 60));
    QJsonArray hunks = MidiDiff::compute(QJsonArray(), current, 480);
    QJsonObject scope = hunks.at(0).toObject().value(QStringLiteral("scope")).toObject();
    QCOMPARE(scope.value(QStringLiteral("measureStart")).toInt(), 0);
    QCOMPARE(scope.value(QStringLiteral("measureEnd")).toInt(), 0);

    QJsonArray laterCurrent;
    laterCurrent.append(makeNote(0, 1, 1920, 60));   // measure 1
    laterCurrent.append(makeNote(0, 1, 1920 * 2, 60));  // measure 2
    QJsonArray laterHunks = MidiDiff::compute(QJsonArray(), laterCurrent, 480);
    QJsonObject laterScope = laterHunks.at(0).toObject().value(QStringLiteral("scope")).toObject();
    QCOMPARE(laterScope.value(QStringLiteral("measureStart")).toInt(), 1);
    QCOMPARE(laterScope.value(QStringLiteral("measureEnd")).toInt(), 2);
}

void TestMidiDiff::scope_tickRangeMatchesEarliestAndLatestChange() {
    QJsonArray current;
    current.append(makeNote(0, 1, 200, 60));
    current.append(makeNote(0, 1, 800, 64));
    current.append(makeNote(0, 1, 500, 62));
    QJsonArray hunks = MidiDiff::compute(QJsonArray(), current, 480);
    QCOMPARE(hunks.size(), 1);
    QJsonObject scope = hunks.at(0).toObject().value(QStringLiteral("scope")).toObject();
    QCOMPARE(scope.value(QStringLiteral("tickStart")).toInt(), 200);
    QCOMPARE(scope.value(QStringLiteral("tickEnd")).toInt(), 800);
}

QTEST_APPLESS_MAIN(TestMidiDiff)
#include "test_midi_diff.moc"
