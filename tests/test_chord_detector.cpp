// Unit tests for ChordDetector — pure pitch-class chord identification.
//
// ChordDetector has no dependencies on MidiFile, MidiChannel or any other
// project source, so the test executable links only the single .cpp.
//
// Run from a build configured with -DBUILD_TESTING=ON:
//     ctest --test-dir build -V
// or directly:
//     build/tests/test_chord_detector.exe

#include "ChordDetector.h"

#include <QtTest/QtTest>

class TestChordDetector : public QObject {
    Q_OBJECT

private slots:
    // getNoteName
    void getNoteName_pitchClassesWithoutOctave();
    void getNoteName_includesOctaveWhenRequested();
    void getNoteName_middleCIsC4();

    // detectChord — degenerate input
    void detectChord_emptyListReturnsEmpty();
    void detectChord_singleNoteReturnsNoteName();
    void detectChord_unisonOctaveCollapsesToNoteName();

    // Two-note intervals
    void detectChord_powerChordReturns5();

    // Three-note triads (root position)
    void detectChord_majorTriad();
    void detectChord_minorTriad();
    void detectChord_diminishedTriad();
    void detectChord_augmentedTriad();
    void detectChord_sus2AndSus4();

    // Four-note seventh chords
    void detectChord_majorSeventh();
    void detectChord_minorSeventh();
    void detectChord_dominantSeventh();

    // Inversions
    void detectChord_firstInversionRendersSlashChord();

    // Fallback path
    void detectChord_clusterFallsBackToNoteList();
};

// ---------------------------------------------------------------------------
// getNoteName
// ---------------------------------------------------------------------------

void TestChordDetector::getNoteName_pitchClassesWithoutOctave() {
    QCOMPARE(ChordDetector::getNoteName(0),  QStringLiteral("C"));
    QCOMPARE(ChordDetector::getNoteName(1),  QStringLiteral("C#"));
    QCOMPARE(ChordDetector::getNoteName(11), QStringLiteral("B"));
    // Wraps modulo 12.
    QCOMPARE(ChordDetector::getNoteName(60), QStringLiteral("C"));
    QCOMPARE(ChordDetector::getNoteName(69), QStringLiteral("A"));
}

void TestChordDetector::getNoteName_includesOctaveWhenRequested() {
    // MIDI 0 → C-1 by the GM convention used here ((note/12)-1).
    QCOMPARE(ChordDetector::getNoteName(0,  true), QStringLiteral("C-1"));
    QCOMPARE(ChordDetector::getNoteName(12, true), QStringLiteral("C0"));
    QCOMPARE(ChordDetector::getNoteName(69, true), QStringLiteral("A4"));
}

void TestChordDetector::getNoteName_middleCIsC4() {
    QCOMPARE(ChordDetector::getNoteName(60, true), QStringLiteral("C4"));
}

// ---------------------------------------------------------------------------
// Degenerate input
// ---------------------------------------------------------------------------

void TestChordDetector::detectChord_emptyListReturnsEmpty() {
    QVERIFY(ChordDetector::detectChord({}).isEmpty());
}

void TestChordDetector::detectChord_singleNoteReturnsNoteName() {
    // A single MIDI note collapses to a single pitch class → just the note name
    // (no octave, no chord quality).
    QCOMPARE(ChordDetector::detectChord({60}), QStringLiteral("C"));
    QCOMPARE(ChordDetector::detectChord({69}), QStringLiteral("A"));
}

void TestChordDetector::detectChord_unisonOctaveCollapsesToNoteName() {
    // Notes that share a pitch class form a single-PC set.
    QCOMPARE(ChordDetector::detectChord({60, 72, 84}), QStringLiteral("C"));
}

// ---------------------------------------------------------------------------
// Two-note intervals
// ---------------------------------------------------------------------------

void TestChordDetector::detectChord_powerChordReturns5() {
    // C + G a perfect fifth apart.
    QCOMPARE(ChordDetector::detectChord({60, 67}), QStringLiteral("C5"));
    // Order should not matter.
    QCOMPARE(ChordDetector::detectChord({67, 60}), QStringLiteral("C5"));
    // G + D — the lowest note is the root, so this is G5.
    QCOMPARE(ChordDetector::detectChord({55, 62}), QStringLiteral("G5"));
}

// ---------------------------------------------------------------------------
// Three-note triads (root position)
// ---------------------------------------------------------------------------

void TestChordDetector::detectChord_majorTriad() {
    // C E G
    QCOMPARE(ChordDetector::detectChord({60, 64, 67}), QStringLiteral("Cmaj"));
    // F# A# C#  — sharps round-trip through the noteNames table.
    QCOMPARE(ChordDetector::detectChord({66, 70, 73}), QStringLiteral("F#maj"));
}

void TestChordDetector::detectChord_minorTriad() {
    // A C E
    QCOMPARE(ChordDetector::detectChord({57, 60, 64}), QStringLiteral("Am"));
    // D F A
    QCOMPARE(ChordDetector::detectChord({62, 65, 69}), QStringLiteral("Dm"));
}

void TestChordDetector::detectChord_diminishedTriad() {
    // B D F → B dim
    QCOMPARE(ChordDetector::detectChord({59, 62, 65}), QStringLiteral("Bdim"));
}

void TestChordDetector::detectChord_augmentedTriad() {
    // C E G# → C aug
    QCOMPARE(ChordDetector::detectChord({60, 64, 68}), QStringLiteral("Caug"));
}

void TestChordDetector::detectChord_sus2AndSus4() {
    // C D G → Csus2
    QCOMPARE(ChordDetector::detectChord({60, 62, 67}), QStringLiteral("Csus2"));
    // C F G → Csus4
    QCOMPARE(ChordDetector::detectChord({60, 65, 67}), QStringLiteral("Csus4"));
}

// ---------------------------------------------------------------------------
// Seventh chords
// ---------------------------------------------------------------------------

void TestChordDetector::detectChord_majorSeventh() {
    // C E G B
    QCOMPARE(ChordDetector::detectChord({60, 64, 67, 71}), QStringLiteral("Cmaj7"));
}

void TestChordDetector::detectChord_minorSeventh() {
    // A C E G
    QCOMPARE(ChordDetector::detectChord({57, 60, 64, 67}), QStringLiteral("Am7"));
}

void TestChordDetector::detectChord_dominantSeventh() {
    // G B D F
    QCOMPARE(ChordDetector::detectChord({55, 59, 62, 65}), QStringLiteral("G7"));
}

// ---------------------------------------------------------------------------
// Inversions
// ---------------------------------------------------------------------------

void TestChordDetector::detectChord_firstInversionRendersSlashChord() {
    // E3 G3 C4 — first inversion of C major. Lowest note is E, root is C.
    // detectChord should return "Cmaj/E".
    QCOMPARE(ChordDetector::detectChord({52, 55, 60}), QStringLiteral("Cmaj/E"));
}

// ---------------------------------------------------------------------------
// Fallback (no recognised chord)
// ---------------------------------------------------------------------------

void TestChordDetector::detectChord_clusterFallsBackToNoteList() {
    // C C# D — no quality matches; falls back to note list with octaves.
    QString result = ChordDetector::detectChord({60, 61, 62});
    QCOMPARE(result, QStringLiteral("C4, C#4, D4"));
}

QTEST_APPLESS_MAIN(TestChordDetector)
#include "test_chord_detector.moc"
