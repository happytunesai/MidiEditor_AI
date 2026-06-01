/*
 * MidiEditor AI - Unit test for the SID -> SMF writer (Phase 42.1).
 *
 * Checks that writeSidNotesToSmf() emits a well-formed format-1 Standard
 * MIDI File: correct MThd header, 4 tracks, and MTrk chunks whose lengths
 * walk the buffer exactly to its end (so MidiFile's loader can parse it).
 */

#include <QtTest/QtTest>

#include <cstdint>
#include <vector>

#include "../src/converter/Sid/SidCapture.h"
#include "../src/converter/Sid/SidMidiWriter.h"
#include "../src/converter/Sid/SidReconstruct.h"

using namespace sid;

namespace {
quint32 be32(const QByteArray &b, int o) {
    return (quint32(quint8(b[o])) << 24) | (quint32(quint8(b[o+1])) << 16)
         | (quint32(quint8(b[o+2])) << 8) | quint32(quint8(b[o+3]));
}
quint16 be16(const QByteArray &b, int o) {
    return quint16((quint8(b[o]) << 8) | quint8(b[o+1]));
}
}

class SidMidiWriterTest : public QObject {
    Q_OBJECT
private slots:

    void wellFormedSmf() {
        CaptureResult cap;
        cap.framesPerSecond = kPalFramesPerSecond;
        cap.clockHz = kPalClockHz;

        std::vector<SidNote> notes;
        // {voice, startFrame, endFrame, midiNote, sidFreq, waveform, velocity}
        notes.push_back({0, 0, 4, 69, 7492, 0x10, 100}); // V1 A4 triangle
        notes.push_back({0, 4, 8, 72, 8911, 0x10, 100}); // V1 C5 triangle
        notes.push_back({1, 0, 8, 57, 3746, 0x40, 90});  // V2 pulse

        QByteArray smf = writeSidNotesToSmf(notes, cap, "Test Tune");
        QVERIFY(smf.size() > 22);

        // MThd
        QCOMPARE(QString::fromLatin1(smf.left(4)), QStringLiteral("MThd"));
        QCOMPARE(int(be32(smf, 4)), 6);    // header length
        QCOMPARE(int(be16(smf, 8)), 1);    // format 1
        QCOMPARE(int(be16(smf, 10)), 5);   // conductor + 3 voices + percussion
        QCOMPARE(int(be16(smf, 12)), 600); // ppq

        // Walk the MTrk chunks; their declared lengths must consume the
        // buffer exactly - the strongest "MidiFile can parse this" invariant.
        int pos = 14, tracks = 0;
        while (pos + 8 <= smf.size()) {
            QCOMPARE(QString::fromLatin1(smf.mid(pos, 4)), QStringLiteral("MTrk"));
            int len = int(be32(smf, pos + 4));
            pos += 8 + len;
            tracks++;
        }
        QCOMPARE(tracks, 5);
        QCOMPARE(pos, int(smf.size()));
    }

    void emptyNotesStillValidHeader() {
        CaptureResult cap;
        cap.framesPerSecond = kPalFramesPerSecond;
        QByteArray smf = writeSidNotesToSmf({}, cap, "Empty");
        QCOMPARE(QString::fromLatin1(smf.left(4)), QStringLiteral("MThd"));
        QCOMPARE(int(be16(smf, 10)), 5); // still 5 (empty) tracks, parseable
    }
};

QTEST_MAIN(SidMidiWriterTest)
#include "test_sid_midiwriter.moc"
