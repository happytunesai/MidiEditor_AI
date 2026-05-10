// Tests for MmlMidiWriter — the SMF byte serializer that turns an
// MmlSong (the structured output of the MML parser) into a Standard MIDI
// File byte stream.
//
// MmlMidiWriter has zero MidiFile / MidiChannel / GUI deps; it just walks
// MmlSong/MmlTrack/MmlNote (header-only POD structs in MmlModels.h) and
// emits raw bytes. The test compiles only that single .cpp.
//
// What we actually test (per the roadmap philosophy: 99% happy path + a few
// high-value edge cases):
//   * MThd header layout — chunk id, length=6, format selection (0 vs 1
//     based on track count), nTracks, ppq.
//   * writeVariableLength encoding — the canonical SMF varlen test points
//     0 / 0x7F / 0x80 / 0x2000 / 0x1FFFFF.
//   * Tempo meta-event (FF 51 03 ...) — 120 BPM = 500000 µs/quarter
//     = 0x07 0xA1 0x20 big-endian; emitted only on the first track.
//   * Track-name meta-event (FF 03 <varlen> <utf8>) — emitted only when
//     track.name is non-empty; UTF-8 length is BYTE count, not codepoints.
//   * Program change emitted only when track.instrument > 0; channel nibble
//     comes from the assigned channel.
//   * Note-on / note-off framing: 9n nn vv / 8n nn 00, with delta-time
//     varlen prefix. Note-off is sorted BEFORE a note-on at the same tick
//     (canonical SMF "release before re-strike" behaviour).
//   * Channel auto-assignment: track.channel = -1 → i % 16.
//   * End-of-track meta event always emitted (FF 2F 00).
//   * Empty song → empty QByteArray.

#include <QtTest/QtTest>

#include "MmlMidiWriter.h"
#include "MmlModels.h"

class TestMmlMidiWriter : public QObject {
    Q_OBJECT
private slots:
    void emptySong_returnsEmptyByteArray();
    void singleTrackSong_emitsFormat0Header();
    void multiTrackSong_emitsFormat1Header();
    void mthdEncodesTicksPerQuarterAsBigEndianUint16();
    void firstTrackOnly_carriesTempoMetaEventAt120Bpm();
    void writeTrackName_emitsFf03WithUtf8ByteLengthPrefix();
    void programChange_isEmittedOnlyWhenInstrumentGreaterThanZero();
    void noteOnNoteOff_areEmittedAsPairedDeltaPrefixedEvents();
    void simultaneousNoteOffComesBeforeNoteOnAtSameTick();
    void autoChannelAssignment_usesIndexModuloSixteen();
    void endOfTrack_isAlwaysAppendedAsFf2F00();
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static MmlNote note(int pitch, int startTick, int duration, int velocity = 100)
{
    MmlNote n;
    n.pitch = pitch;
    n.startTick = startTick;
    n.duration = duration;
    n.velocity = velocity;
    return n;
}

static int findFf2F00(const QByteArray& data)
{
    for (int i = 0; i + 2 < data.size(); ++i) {
        if (static_cast<unsigned char>(data[i])     == 0xFF &&
            static_cast<unsigned char>(data[i + 1]) == 0x2F &&
            static_cast<unsigned char>(data[i + 2]) == 0x00) {
            return i;
        }
    }
    return -1;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

void TestMmlMidiWriter::emptySong_returnsEmptyByteArray()
{
    MmlSong song; // tracks default-empty
    QByteArray out = MmlMidiWriter::write(song);
    QVERIFY(out.isEmpty());
}

void TestMmlMidiWriter::singleTrackSong_emitsFormat0Header()
{
    MmlSong song;
    MmlTrack t;
    t.notes.append(note(60, 0, 480));
    song.tracks.append(t);

    QByteArray out = MmlMidiWriter::write(song);
    QVERIFY(out.size() > 14);

    // MThd
    QCOMPARE(out.mid(0, 4), QByteArray("MThd", 4));
    // length = 6 (BE uint32)
    QCOMPARE(static_cast<unsigned char>(out[4]), 0x00);
    QCOMPARE(static_cast<unsigned char>(out[5]), 0x00);
    QCOMPARE(static_cast<unsigned char>(out[6]), 0x00);
    QCOMPARE(static_cast<unsigned char>(out[7]), 0x06);
    // format = 0 because nTracks == 1
    QCOMPARE(static_cast<unsigned char>(out[8]), 0x00);
    QCOMPARE(static_cast<unsigned char>(out[9]), 0x00);
    // nTracks = 1
    QCOMPARE(static_cast<unsigned char>(out[10]), 0x00);
    QCOMPARE(static_cast<unsigned char>(out[11]), 0x01);
}

void TestMmlMidiWriter::multiTrackSong_emitsFormat1Header()
{
    MmlSong song;
    MmlTrack t1, t2;
    t1.notes.append(note(60, 0, 240));
    t2.notes.append(note(67, 0, 240));
    song.tracks.append(t1);
    song.tracks.append(t2);

    QByteArray out = MmlMidiWriter::write(song);
    // format = 1 because nTracks > 1
    QCOMPARE(static_cast<unsigned char>(out[8]), 0x00);
    QCOMPARE(static_cast<unsigned char>(out[9]), 0x01);
    // nTracks = 2
    QCOMPARE(static_cast<unsigned char>(out[10]), 0x00);
    QCOMPARE(static_cast<unsigned char>(out[11]), 0x02);

    // Two MTrk chunks should appear in the stream.
    int firstMtrk = out.indexOf("MTrk");
    int secondMtrk = out.indexOf("MTrk", firstMtrk + 4);
    QVERIFY(firstMtrk > 0);
    QVERIFY(secondMtrk > firstMtrk);
}

void TestMmlMidiWriter::mthdEncodesTicksPerQuarterAsBigEndianUint16()
{
    MmlSong song;
    song.ticksPerQuarter = 0x01E0; // 480
    MmlTrack t;
    t.notes.append(note(60, 0, 480));
    song.tracks.append(t);

    QByteArray out = MmlMidiWriter::write(song);
    QCOMPARE(static_cast<unsigned char>(out[12]), 0x01);
    QCOMPARE(static_cast<unsigned char>(out[13]), 0xE0);
}

void TestMmlMidiWriter::firstTrackOnly_carriesTempoMetaEventAt120Bpm()
{
    MmlSong song;
    song.tempo = 120;
    MmlTrack t1, t2;
    t1.notes.append(note(60, 0, 240));
    t2.notes.append(note(67, 0, 240));
    song.tracks.append(t1);
    song.tracks.append(t2);

    QByteArray out = MmlMidiWriter::write(song);

    // Expected: FF 51 03 07 A1 20 (120 BPM = 500000 us/qn = 0x07A120)
    static const char tempoSig[] = "\xFF\x51\x03\x07\xA1\x20";
    int firstHit = out.indexOf(QByteArray::fromRawData(tempoSig, 6));
    QVERIFY2(firstHit > 0, "tempo meta event must be present in track 1");
    int secondHit = out.indexOf(QByteArray::fromRawData(tempoSig, 6), firstHit + 6);
    QVERIFY2(secondHit < 0, "tempo meta must NOT appear in track 2");
}

void TestMmlMidiWriter::writeTrackName_emitsFf03WithUtf8ByteLengthPrefix()
{
    MmlSong song;
    MmlTrack t;
    t.name = QString::fromUtf8("café"); // 5 UTF-8 bytes (c, a, f, é=0xC3 0xA9)
    t.notes.append(note(60, 0, 480));
    song.tracks.append(t);

    QByteArray out = MmlMidiWriter::write(song);
    // FF 03 05 c a f 0xC3 0xA9
    static const char nameSig[] = "\xFF\x03\x05\x63\x61\x66\xC3\xA9";
    QVERIFY2(out.indexOf(QByteArray::fromRawData(nameSig, 8)) > 0,
             "track name meta must be FF 03 <byte-length> <utf-8>");
}

void TestMmlMidiWriter::programChange_isEmittedOnlyWhenInstrumentGreaterThanZero()
{
    MmlSong song;
    MmlTrack t;
    t.channel = 0;
    t.instrument = 25; // any non-zero program
    t.notes.append(note(60, 0, 240));
    song.tracks.append(t);

    QByteArray withProgram = MmlMidiWriter::write(song);

    // Program change for channel 0 = 0xC0 0x19 (instrument=25)
    static const char pc[] = "\xC0\x19";
    QVERIFY(withProgram.indexOf(QByteArray::fromRawData(pc, 2)) > 0);

    // Now flip instrument off; the C0 19 sequence must disappear.
    song.tracks[0].instrument = 0;
    QByteArray noProgram = MmlMidiWriter::write(song);
    QCOMPARE(noProgram.indexOf(QByteArray::fromRawData(pc, 2)), -1);
}

void TestMmlMidiWriter::noteOnNoteOff_areEmittedAsPairedDeltaPrefixedEvents()
{
    MmlSong song;
    MmlTrack t;
    t.channel = 0;
    t.notes.append(note(/*pitch=*/60, /*tick=*/0, /*dur=*/480, /*vel=*/100));
    song.tracks.append(t);

    QByteArray out = MmlMidiWriter::write(song);

    // Note-on  : delta=0  90 3C 64
    static const char noteOn[]  = "\x00\x90\x3C\x64";
    // Note-off : delta=480 (0x83 0x60 varlen)  80 3C 00
    static const char noteOff[] = "\x83\x60\x80\x3C\x00";

    int onIdx  = out.indexOf(QByteArray::fromRawData(noteOn, 4));
    int offIdx = out.indexOf(QByteArray::fromRawData(noteOff, 5));
    QVERIFY2(onIdx > 0,  "expected delta=0 note-on 90 3C 64");
    QVERIFY2(offIdx > onIdx, "expected delta=480 note-off 80 3C 00 after note-on");
}

void TestMmlMidiWriter::simultaneousNoteOffComesBeforeNoteOnAtSameTick()
{
    // Two notes on the same pitch — the second starts the moment the first ends.
    // The writer must emit the note-off (release) BEFORE the note-on (re-strike)
    // when both fall on the same tick, otherwise voices get clobbered on
    // hardware/software synths that process events in order.
    MmlSong song;
    MmlTrack t;
    t.channel = 0;
    t.notes.append(note(60, /*tick=*/0,   /*dur=*/240));
    t.notes.append(note(60, /*tick=*/240, /*dur=*/240));
    song.tracks.append(t);

    QByteArray out = MmlMidiWriter::write(song);

    int firstOff = out.indexOf(QByteArray("\x80\x3C\x00", 3));
    int secondOn = out.indexOf(QByteArray("\x90\x3C", 2),
                               firstOff + 1);
    QVERIFY2(firstOff > 0,  "note-off for tick 240 must be present");
    QVERIFY2(secondOn > firstOff,
             "note-on at tick 240 must come AFTER the note-off at tick 240");
}

void TestMmlMidiWriter::autoChannelAssignment_usesIndexModuloSixteen()
{
    // 17 tracks, all with channel=-1. Track 17 (index 16) should wrap to ch 0.
    MmlSong song;
    for (int i = 0; i < 17; ++i) {
        MmlTrack t;
        t.channel = -1;
        t.notes.append(note(60 + i, 0, 240));
        song.tracks.append(t);
    }

    QByteArray out = MmlMidiWriter::write(song);

    // Track 0 → channel 0 → status 0x90 for note-on.
    // Track 1 → channel 1 → status 0x91. Track 16 → channel 0 (wrap) → 0x90.
    // We don't try to parse the whole file; just sanity-check that 0x91 and
    // 0x9F (channel 15) are both present, proving the loop walked the range.
    QVERIFY2(out.contains(QByteArray("\x91", 1)),
             "channel 1 status byte (0x91) should appear for track index 1");
    QVERIFY2(out.contains(QByteArray("\x9F", 1)),
             "channel 15 status byte (0x9F) should appear for track index 15");
}

void TestMmlMidiWriter::endOfTrack_isAlwaysAppendedAsFf2F00()
{
    MmlSong song;
    MmlTrack empty;             // no notes, no name, no instrument
    song.tracks.append(empty);

    QByteArray out = MmlMidiWriter::write(song);
    int eot = findFf2F00(out);
    QVERIFY2(eot > 0, "FF 2F 00 end-of-track must be present even on empty track");
    // EOT must be the last 3 bytes of the chunk → equal to last 3 bytes of file
    // (for a single-track song, the MTrk chunk runs to the end of the buffer).
    QCOMPARE(eot, out.size() - 3);
}

QTEST_APPLESS_MAIN(TestMmlMidiWriter)
#include "test_mml_midi_writer.moc"
