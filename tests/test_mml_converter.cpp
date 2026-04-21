// Tests for the MML (Music Macro Language) converter pipeline.
//
// MmlConverter::convert() is an end-to-end pure transform:
//     QString MML text  ->  MmlSong { tempo, ticksPerQuarter, tracks[notes] }
//
// It chains:
//   1. semicolon split into per-track texts
//   2. MmlLexer::tokenize  -> QList<MmlToken>
//   3. MmlParser::parse    -> MmlTrack with absolute-tick MmlNotes
//
// All three .cpp files are pure value transforms with no MidiFile / Qt GUI
// deps, so this test compiles them directly and links nothing else from the
// project.
//
// The MML grammar is forgiving by design — unknown characters are silently
// dropped in the lexer, unknown tokens are silently skipped in the parser.
// There is no "syntax error" channel; the closest analogue is verifying
// that malformed input does not crash and produces the documented graceful
// fallback (empty track / no notes / song with default tempo).

#include <QtTest/QtTest>

#include "MmlConverter.h"
#include "MmlLexer.h"
#include "MmlModels.h"
#include "MmlParser.h"

class TestMmlConverter : public QObject {
    Q_OBJECT
private slots:
    void scaleAtDefaultLength_producesSevenAscendingQuarterNotes();
    void noTempoCommand_returnsDefault120Bpm();
    void tempoCommand_isExtractedFromAnywhereInText();
    void lengthCommand_changesDefaultDurationOfFollowingNotes();
    void dottedNote_addsHalfDuration();
    void octaveCommands_shiftPitchBetweenC4AndC5();
    void semicolonSeparatedTracks_produceMultipleTracksOnSeparateChannels();
    void tieBetweenSamePitch_extendsExistingNote();
    void sharpAndFlat_adjustPitchByOneSemitone();
    void garbageInput_doesNotCrashAndReturnsEmptyTracks();
    void instrumentCommand_setsTrackProgramNumber();
    void emptyInput_returnsSongWithNoTracksAndDefaultTempo();
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static MmlTrack onlyTrack(const MmlSong& song)
{
    Q_ASSERT(song.tracks.size() == 1);
    return song.tracks.first();
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

void TestMmlConverter::scaleAtDefaultLength_producesSevenAscendingQuarterNotes()
{
    // Default length is l4 (quarter), default octave is 4.
    // ticksPerQuarter = 480 -> each note duration = 480 ticks.
    MmlSong song = MmlConverter::convert("cdefgab", 480);

    QCOMPARE(song.tempo, 120);
    QCOMPARE(song.ticksPerQuarter, 480);
    QCOMPARE(song.tracks.size(), 1);

    MmlTrack t = onlyTrack(song);
    QCOMPARE(t.notes.size(), 7);

    // octave 4 in MML conventions used here: pitch = (octave+1)*12 + offset
    // -> C4 = 60. We assert the natural C major scale.
    const int expected[7] = { 60, 62, 64, 65, 67, 69, 71 };
    int currentTick = 0;
    for (int i = 0; i < 7; ++i) {
        QCOMPARE(t.notes[i].pitch, expected[i]);
        QCOMPARE(t.notes[i].duration, 480);
        QCOMPARE(t.notes[i].startTick, currentTick);
        currentTick += 480;
    }
}

void TestMmlConverter::noTempoCommand_returnsDefault120Bpm()
{
    QCOMPARE(MmlConverter::extractTempo("cdefg"), 120);
    QCOMPARE(MmlConverter::convert("cdefg", 480).tempo, 120);
}

void TestMmlConverter::tempoCommand_isExtractedFromAnywhereInText()
{
    QCOMPARE(MmlConverter::extractTempo("t140 cde"), 140);
    QCOMPARE(MmlConverter::extractTempo("cde t96 fg"), 96);
    // Bogus tempo (out of 1..999 range) is rejected -> default 120 returned.
    QCOMPARE(MmlConverter::extractTempo("t0 cde"), 120);
}

void TestMmlConverter::lengthCommand_changesDefaultDurationOfFollowingNotes()
{
    // l8 -> default = eighth = 240 ticks @ 480 tpq
    MmlSong song = MmlConverter::convert("l8 cd", 480);
    MmlTrack t = onlyTrack(song);
    QCOMPARE(t.notes.size(), 2);
    QCOMPARE(t.notes[0].duration, 240);
    QCOMPARE(t.notes[1].duration, 240);
    QCOMPARE(t.notes[1].startTick, 240);
}

void TestMmlConverter::dottedNote_addsHalfDuration()
{
    // c4. -> 480 + 240 = 720 ticks
    MmlSong song = MmlConverter::convert("c4.", 480);
    MmlTrack t = onlyTrack(song);
    QCOMPARE(t.notes.size(), 1);
    QCOMPARE(t.notes[0].duration, 720);

    // Double-dotted: c4.. -> 480 + 240 + 120 = 840
    MmlSong song2 = MmlConverter::convert("c4..", 480);
    QCOMPARE(onlyTrack(song2).notes[0].duration, 840);
}

void TestMmlConverter::octaveCommands_shiftPitchBetweenC4AndC5()
{
    // o4 c (=60), > c (octave 5 -> 72), < c (back to octave 4 -> 60),
    // o3 c (-> 48)
    MmlSong song = MmlConverter::convert("o4 c > c < c o3 c", 480);
    MmlTrack t = onlyTrack(song);
    QCOMPARE(t.notes.size(), 4);
    QCOMPARE(t.notes[0].pitch, 60);
    QCOMPARE(t.notes[1].pitch, 72);
    QCOMPARE(t.notes[2].pitch, 60);
    QCOMPARE(t.notes[3].pitch, 48);
}

void TestMmlConverter::semicolonSeparatedTracks_produceMultipleTracksOnSeparateChannels()
{
    MmlSong song = MmlConverter::convert("cde; efg; gab", 480);
    QCOMPARE(song.tracks.size(), 3);

    QCOMPARE(song.tracks[0].channel, 0);
    QCOMPARE(song.tracks[1].channel, 1);
    QCOMPARE(song.tracks[2].channel, 2);

    QCOMPARE(song.tracks[0].name, QStringLiteral("Track 1"));
    QCOMPARE(song.tracks[1].name, QStringLiteral("Track 2"));
    QCOMPARE(song.tracks[2].name, QStringLiteral("Track 3"));

    for (const MmlTrack& t : song.tracks) {
        QCOMPARE(t.notes.size(), 3);
    }
}

void TestMmlConverter::tieBetweenSamePitch_extendsExistingNote()
{
    // c4&c4 -> single note, duration = 480 + 480 = 960
    MmlSong song = MmlConverter::convert("c4&c4", 480);
    MmlTrack t = onlyTrack(song);
    QCOMPARE(t.notes.size(), 1);
    QCOMPARE(t.notes[0].duration, 960);

    // c4&d4 -> two separate notes (different pitch breaks the tie)
    MmlSong song2 = MmlConverter::convert("c4&d4", 480);
    QCOMPARE(onlyTrack(song2).notes.size(), 2);
    QCOMPARE(onlyTrack(song2).notes[0].duration, 480);
    QCOMPARE(onlyTrack(song2).notes[1].duration, 480);
}

void TestMmlConverter::sharpAndFlat_adjustPitchByOneSemitone()
{
    // o4 c c+ c-  -> 60, 61, 59
    MmlSong song = MmlConverter::convert("o4 c c+ c-", 480);
    MmlTrack t = onlyTrack(song);
    QCOMPARE(t.notes.size(), 3);
    QCOMPARE(t.notes[0].pitch, 60);
    QCOMPARE(t.notes[1].pitch, 61);
    QCOMPARE(t.notes[2].pitch, 59);

    // # is an alias for + (Sharp token). Verify same semantics.
    MmlSong songHash = MmlConverter::convert("o4 c#", 480);
    QCOMPARE(onlyTrack(songHash).notes[0].pitch, 61);
}

void TestMmlConverter::garbageInput_doesNotCrashAndReturnsEmptyTracks()
{
    // Pure punctuation / unknown characters -> lexer drops them all,
    // parser sees only EndOfTrack -> no notes -> track filtered out.
    // (The convert loop only appends tracks whose notes list is non-empty.)
    MmlSong song = MmlConverter::convert("!@#$%^&*()_=?|\\", 480);
    QCOMPARE(song.tracks.size(), 0);
    QCOMPARE(song.tempo, 120);

    // A stray sharp before any note has nothing to attach to but must
    // not crash. The trailing 'c' is then a plain default note.
    MmlSong song2 = MmlConverter::convert("+++c", 480);
    QCOMPARE(song2.tracks.size(), 1);
    QCOMPARE(onlyTrack(song2).notes.size(), 1);
    // Documented behaviour: leading sharps with no preceding note letter
    // are ignored by the parser. The single 'c' lands at pitch 60.
    QCOMPARE(onlyTrack(song2).notes[0].pitch, 60);
}

void TestMmlConverter::instrumentCommand_setsTrackProgramNumber()
{
    // @25 = Acoustic Steel Guitar in GM. The parser stores it on track.instrument.
    MmlSong song = MmlConverter::convert("@25 cde", 480);
    MmlTrack t = onlyTrack(song);
    QCOMPARE(t.instrument, 25);

    // Out-of-range program is clamped to 0..127.
    MmlSong songHigh = MmlConverter::convert("@9999 c", 480);
    QCOMPARE(onlyTrack(songHigh).instrument, 127);
}

void TestMmlConverter::emptyInput_returnsSongWithNoTracksAndDefaultTempo()
{
    MmlSong song = MmlConverter::convert(QString(), 480);
    QCOMPARE(song.tracks.size(), 0);
    QCOMPARE(song.tempo, 120);
    QCOMPARE(song.ticksPerQuarter, 480);

    MmlSong songWs = MmlConverter::convert("   \t\n  ", 480);
    QCOMPARE(songWs.tracks.size(), 0);
}

QTEST_APPLESS_MAIN(TestMmlConverter)
#include "test_mml_converter.moc"
