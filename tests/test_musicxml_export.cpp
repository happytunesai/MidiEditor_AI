/*
 * MidiEditor AI - Unit test for the MusicXML export pipeline (Phase 43, 1.8.0).
 *
 * Tests the two pure layers:
 *   - score::buildScore  (MidiToScore.cpp)  - the MIDI->notation engraver:
 *       note-value mapping, rest filling, ties across barlines, chords,
 *       enharmonic spelling from the key signature.
 *   - MusicXmlWriter::write - Score IR -> MusicXML 4.0 partwise text.
 *
 * buildScore takes a plain ScoreInput, so this links only the two pure .cpp
 * files (no MidiFile / event / GUI tree) + Qt6::Core/Test.
 */
#include <QtTest/QtTest>

#include "../src/converter/Score/ScoreModel.h"
#include "../src/converter/Score/MidiToScore.h"
#include "../src/converter/MusicXml/MusicXmlWriter.h"

using namespace score;

namespace {

// A one-part input in 4/4 (divisions = 480) with the given notes.
ScoreInput input44(const QList<RawNote> &notes, int endTick, int fifths = 0) {
    ScoreInput in;
    in.divisions = 480;
    in.endTick = endTick;
    if (fifths != 0) in.keySigs.append({ 0, fifths, false });
    RawPart p;
    p.name = "Lead";
    p.channel = 0;
    p.program = 0;
    p.notes = notes;
    in.parts.append(p);
    return in;
}

int sumDur(const QList<ScoreEvent> &voice) {
    int s = 0;
    for (const ScoreEvent &e : voice) if (!e.isChord) s += e.durDivs;
    return s;
}

} // namespace

class MusicXmlExportTest : public QObject {
    Q_OBJECT
private slots:

    // ---- engraver: basic measure + rest fill ------------------------------

    void singleQuarter_fillsMeasureWithRests() {
        Score s = buildScore(input44({ { 0, 480, 60, 80 } }, 1920)); // C4 quarter
        QCOMPARE(s.parts.size(), 1);
        const QList<ScoreMeasure> &m = s.parts[0].measures;
        QCOMPARE(m.size(), 1);
        const QList<ScoreEvent> &v = m[0].voice;
        QVERIFY(v.size() >= 2);
        QCOMPARE(v[0].isRest, false);
        QVERIFY(v[0].type == NoteType::Quarter);
        QCOMPARE(v[0].dots, 0);
        QCOMPARE(v[0].step, 'C');
        QCOMPARE(v[0].alter, 0);
        QCOMPARE(v[0].octave, 4);
        QVERIFY(v[1].isRest);                 // rest fills the rest of the bar
        QCOMPARE(sumDur(v), 1920);            // bar fully accounted for
    }

    void measureCount_fromEndTick() {
        Score s = buildScore(input44({ { 0, 480, 60, 80 } }, 1920 * 3));
        QCOMPARE(s.parts[0].measures.size(), 3);
    }

    // ---- engraver: dotted value -------------------------------------------

    void dottedQuarter_isDetected() {
        Score s = buildScore(input44({ { 0, 720, 67, 80 } }, 1920)); // dotted quarter G4
        const ScoreEvent &e = s.parts[0].measures[0].voice[0];
        QVERIFY(e.type == NoteType::Quarter);
        QCOMPARE(e.dots, 1);
        QCOMPARE(e.durDivs, 720);
    }

    // ---- engraver: tie across a barline -----------------------------------

    void noteAcrossBarline_isTied() {
        // 5-quarter note in 4/4 => whole (bar 1, tie start) + quarter (bar 2, tie stop).
        Score s = buildScore(input44({ { 0, 480 * 5, 60, 80 } }, 1920 * 2));
        QCOMPARE(s.parts[0].measures.size(), 2);
        const ScoreEvent &a = s.parts[0].measures[0].voice[0];
        const ScoreEvent &b = s.parts[0].measures[1].voice[0];
        QVERIFY(a.type == NoteType::Whole);
        QCOMPARE(a.tieStart, true);
        QCOMPARE(a.tieStop, false);
        QVERIFY(b.type == NoteType::Quarter);
        QCOMPARE(b.tieStart, false);
        QCOMPARE(b.tieStop, true);
    }

    // ---- engraver: enharmonic spelling from the key -----------------------

    void spelling_sharpKeyUsesSharps() {
        Score s = buildScore(input44({ { 0, 480, 61, 80 } }, 1920, 0)); // C#/Db, C major
        const ScoreEvent &e = s.parts[0].measures[0].voice[0];
        QCOMPARE(e.step, 'C');
        QCOMPARE(e.alter, 1);
    }

    void spelling_flatKeyUsesFlats() {
        Score s = buildScore(input44({ { 0, 480, 61, 80 } }, 1920, -1)); // Db, F major
        const ScoreEvent &e = s.parts[0].measures[0].voice[0];
        QCOMPARE(e.step, 'D');
        QCOMPARE(e.alter, -1);
    }

    // ---- engraver: chords --------------------------------------------------

    void sameOnset_becomesChord() {
        Score s = buildScore(input44({ { 0, 480, 60, 80 }, { 0, 480, 64, 80 } }, 1920));
        const QList<ScoreEvent> &v = s.parts[0].measures[0].voice;
        QCOMPARE(v[0].isChord, false);
        QCOMPARE(v[0].step, 'C');
        QCOMPARE(v[1].isChord, true);   // E4 stacks on C4
        QCOMPARE(v[1].step, 'E');
        QCOMPARE(v[1].durDivs, v[0].durDivs);
    }

    // ---- writer: MusicXML fragments ---------------------------------------

    void writer_emitsWellFormedFragments() {
        Score s = buildScore(input44({ { 0, 480, 60, 80 } }, 1920));
        s.title = "Test";
        const QByteArray xml = MusicXmlWriter::write(s);
        QVERIFY(xml.contains("<score-partwise"));
        QVERIFY(xml.contains("version=\"4.0\""));
        QVERIFY(xml.contains("<work-title>Test</work-title>"));
        QVERIFY(xml.contains("<divisions>480</divisions>"));
        QVERIFY(xml.contains("<beats>4</beats>"));
        QVERIFY(xml.contains("<beat-type>4</beat-type>"));
        QVERIFY(xml.contains("<clef>"));
        QVERIFY(xml.contains("<step>C</step>"));
        QVERIFY(xml.contains("<octave>4</octave>"));
        QVERIFY(xml.contains("<type>quarter</type>"));
        QVERIFY(xml.contains("<rest/>"));
    }

    void writer_emitsTieAndChord() {
        Score tied = buildScore(input44({ { 0, 480 * 5, 60, 80 } }, 1920 * 2));
        const QByteArray xmlT = MusicXmlWriter::write(tied);
        QVERIFY(xmlT.contains("<tie type=\"start\"/>"));
        QVERIFY(xmlT.contains("<tied type=\"start\"/>"));

        Score chord = buildScore(input44({ { 0, 480, 60, 80 }, { 0, 480, 64, 80 } }, 1920));
        const QByteArray xmlC = MusicXmlWriter::write(chord);
        QVERIFY(xmlC.contains("<chord/>"));
    }

    void writer_emitsTempoDirection() {
        ScoreInput in = input44({ { 0, 480, 60, 80 } }, 1920);
        in.tempos.append({ 0, 144.0 });
        const QByteArray xml = MusicXmlWriter::write(buildScore(in));
        QVERIFY(xml.contains("<per-minute>144</per-minute>"));
    }
};

QTEST_MAIN(MusicXmlExportTest)
#include "test_musicxml_export.moc"
