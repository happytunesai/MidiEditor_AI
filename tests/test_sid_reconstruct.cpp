/*
 * MidiEditor AI - Unit test for SID note reconstruction (Phase 42.1b).
 *
 * Feeds hand-built per-frame register streams to reconstructNotes() and
 * checks the detected notes: pitch from SID frequency, note duration from
 * the gate run, and a mid-gate pitch change splitting into two notes
 * (arpeggio detection).
 */

#include <QtTest/QtTest>

#include <cstdint>

#include "../src/converter/Sid/SidCapture.h"
#include "../src/converter/Sid/SidReconstruct.h"

using namespace sid;

namespace {

// Set voice v's frequency, control and sustain/release registers in a frame.
void setVoice(SidFrame &fr, int v, int freq, uint8_t ctrl, uint8_t sr) {
    const int base[3] = {0, 7, 14};
    int b = base[v];
    fr.regs[b]     = uint8_t(freq & 0xFF);
    fr.regs[b + 1] = uint8_t((freq >> 8) & 0xFF);
    fr.regs[b + 4] = ctrl;
    fr.regs[b + 6] = sr;
}

} // namespace

class SidReconstructTest : public QObject {
    Q_OBJECT
private slots:

    void freqToMidi() {
        // PAL clock: SID freg 7492 ~ A4 (440 Hz, note 69); 8911 ~ C5 (72).
        QCOMPARE(sidFreqToMidiNote(7492, kPalClockHz), 69);
        QCOMPARE(sidFreqToMidiNote(8911, kPalClockHz), 72);
        QCOMPARE(sidFreqToMidiNote(0, kPalClockHz), -1);    // silence
    }

    void singleNoteAndArpeggioSplit() {
        CaptureResult cap;
        cap.ok = true;
        cap.clockHz = kPalClockHz;
        cap.framesPerSecond = kPalFramesPerSecond;
        cap.frames.resize(9);
        // frames 0-3: A4 gated (triangle); 4-7: C5 gated; 8: gate off.
        for (int f = 0; f <= 3; ++f) setVoice(cap.frames[f], 0, 7492, 0x11, 0xA0);
        for (int f = 4; f <= 7; ++f) setVoice(cap.frames[f], 0, 8911, 0x11, 0xA0);
        setVoice(cap.frames[8], 0, 8911, 0x10, 0xA0); // gate off

        std::vector<SidNote> notes = reconstructNotes(cap);
        QCOMPARE(int(notes.size()), 2);

        QCOMPARE(notes[0].voice, 0);
        QCOMPARE(notes[0].startFrame, 0);
        QCOMPARE(notes[0].endFrame, 4);
        QCOMPARE(notes[0].midiNote, 69);
        QCOMPARE(notes[0].velocity, 111); // sustain 0xA -> 80 + 10*47/15

        QCOMPARE(notes[1].startFrame, 4);
        QCOMPARE(notes[1].endFrame, 8);
        QCOMPARE(notes[1].midiNote, 72);
    }

    void gateOffEndsNote() {
        CaptureResult cap;
        cap.clockHz = kPalClockHz;
        cap.frames.resize(4);
        setVoice(cap.frames[0], 1, 7492, 0x11, 0x00); // voice 2, gate on
        setVoice(cap.frames[1], 1, 7492, 0x11, 0x00);
        setVoice(cap.frames[2], 1, 7492, 0x10, 0x00); // gate off at frame 2
        setVoice(cap.frames[3], 1, 7492, 0x10, 0x00);
        std::vector<SidNote> notes = reconstructNotes(cap);
        QCOMPARE(int(notes.size()), 1);
        QCOMPARE(notes[0].voice, 1);
        QCOMPARE(notes[0].startFrame, 0);
        QCOMPARE(notes[0].endFrame, 2);
    }

    void gateOffExtendsByRelease() {
        // Gate on f0-1, gate off f2 with release nibble 9 (750 ms -> 38 frames
        // @50fps). The note should ring out to frame 2 + 38 = 40, not stop at 2.
        CaptureResult cap;
        cap.clockHz = kPalClockHz;
        cap.framesPerSecond = kPalFramesPerSecond;
        cap.frames.resize(60);
        setVoice(cap.frames[0], 0, 7492, 0x11, 0x09);
        setVoice(cap.frames[1], 0, 7492, 0x11, 0x09);
        for (int f = 2; f < 60; ++f) setVoice(cap.frames[f], 0, 7492, 0x10, 0x09); // gate off
        std::vector<SidNote> notes = reconstructNotes(cap);
        QCOMPARE(int(notes.size()), 1);
        QCOMPARE(notes[0].midiNote, 69);
        QCOMPARE(notes[0].startFrame, 0);
        QCOMPARE(notes[0].endFrame, 40); // 2 + release(38)
    }

    void noiseBlipDoesNotChopMelody() {
        // A noise frame mid-note (hard restart, or a drum overlay) must not
        // chop the held melodic note. The noise itself goes to the percussion
        // stream (voice 3), but the melodic voice stays one continuous note.
        CaptureResult cap;
        cap.clockHz = kPalClockHz;
        cap.frames.resize(6);
        setVoice(cap.frames[0], 0, 7492, 0x41, 0x00);  // pulse A4, gate on
        setVoice(cap.frames[1], 0, 18000, 0x81, 0x00); // noise frame, gate on
        setVoice(cap.frames[2], 0, 7492, 0x41, 0x00);  // pulse A4 again
        setVoice(cap.frames[3], 0, 7492, 0x40, 0x00);  // gate off (release 0)
        std::vector<SidNote> notes = reconstructNotes(cap);
        int mel = 0;
        for (const SidNote &n : notes) {
            if (n.voice == 0) {
                ++mel;
                QCOMPARE(n.startFrame, 0);
                QCOMPARE(n.endFrame, 3);       // one continuous note, not chopped
                QCOMPARE(n.midiNote, 69);
                QVERIFY((n.waveform & 0x80) == 0);
            }
        }
        QCOMPARE(mel, 1);
    }

    void noiseHitBecomesPercussionWithoutChoppingMelody() {
        // A gate-on noise frame during a held melodic note -> a percussion
        // note (voice 3); the melodic note keeps playing uninterrupted.
        CaptureResult cap;
        cap.clockHz = kPalClockHz;
        cap.framesPerSecond = kPalFramesPerSecond;
        cap.frames.resize(10);
        for (int f = 0; f <= 8; ++f) setVoice(cap.frames[f], 0, 7492, 0x41, 0x00); // pulse A4
        setVoice(cap.frames[4], 0, 3970, 0x81, 0x00); // noise hit overlaid at frame 4
        setVoice(cap.frames[9], 0, 7492, 0x40, 0x00); // gate off (release 0)
        std::vector<SidNote> notes = reconstructNotes(cap);
        int mel = 0, perc = 0;
        for (const SidNote &n : notes) {
            if (n.voice == 0) mel++;
            else if (n.voice == 3) perc++;
        }
        QCOMPARE(mel, 1);
        QCOMPARE(perc, 1);
        for (const SidNote &n : notes) {
            if (n.voice == 0) {
                QCOMPARE(n.startFrame, 0);
                QCOMPARE(n.endFrame, 9); // not chopped at the noise frame
                QCOMPARE(n.midiNote, 69);
            }
        }
    }

    void ringmodOnsetSkipped() {
        // Ring modulation (ctrl bit 0x04) makes the frequency register's pitch
        // meaningless (triangle x the neighbour oscillator = inharmonic), so a
        // ringmod gate-on must NOT spawn a phantom pitched note (regression:
        // Commando V1's ringmod intro drone read as a lone E7 at frame 1).
        CaptureResult cap;
        cap.clockHz = kPalClockHz;
        cap.framesPerSecond = kPalFramesPerSecond;
        cap.frames.resize(6);
        setVoice(cap.frames[0], 0, 44888, 0x15, 0xFB); // triangle+ringmod+gate
        setVoice(cap.frames[1], 0, 44888, 0x14, 0xFB); // ringmod, gate off
        // frames 2-5 stay silent (all-zero registers)
        std::vector<SidNote> notes = reconstructNotes(cap);
        QCOMPARE(int(notes.size()), 0);
    }

    void ringmodMidNoteDoesNotSplit() {
        // A clean (non-ringmod) note with ringmod toggled on mid-sustain keeps
        // playing as ONE note - we just don't re-pitch it from the ringmod
        // frequency while the modulation is active.
        CaptureResult cap;
        cap.clockHz = kPalClockHz;
        cap.framesPerSecond = kPalFramesPerSecond;
        cap.frames.resize(6);
        setVoice(cap.frames[0], 0, 7492, 0x11, 0x00); // triangle A4, gate on (clean)
        setVoice(cap.frames[1], 0, 7492, 0x11, 0x00);
        setVoice(cap.frames[2], 0, 30000, 0x15, 0x00); // ringmod on, wild freq
        setVoice(cap.frames[3], 0, 30000, 0x15, 0x00);
        setVoice(cap.frames[4], 0, 7492, 0x11, 0x00);  // ringmod off again
        setVoice(cap.frames[5], 0, 7492, 0x10, 0x00);  // gate off
        std::vector<SidNote> notes = reconstructNotes(cap);
        QCOMPARE(int(notes.size()), 1);   // one continuous A4, not split
        QCOMPARE(notes[0].midiNote, 69);
        QCOMPARE(notes[0].startFrame, 0);
        QCOMPARE(notes[0].endFrame, 5);
    }

    void fastArpeggioStepsSurvive() {
        // A 1-frame-per-step arpeggio (gate held, pitch changes each frame)
        // must NOT be culled by the hard-restart filter: each step is within
        // an octave of its predecessor, so every step is kept - including the
        // final step that gates off after a single frame.
        CaptureResult cap;
        cap.clockHz = kPalClockHz;
        cap.framesPerSecond = kPalFramesPerSecond;
        cap.frames.resize(7);
        const int arp[6] = {7492, 8911, 11226, 7492, 8911, 11226}; // A4,C5,E5,...
        for (int f = 0; f < 6; ++f) setVoice(cap.frames[f], 0, arp[f], 0x41, 0x00);
        setVoice(cap.frames[6], 0, 11226, 0x40, 0x00); // gate off after last step
        std::vector<SidNote> notes = reconstructNotes(cap);
        QCOMPARE(int(notes.size()), 6); // every arpeggio step survives
    }

    void silenceProducesNoNotes() {
        CaptureResult cap;
        cap.clockHz = kPalClockHz;
        cap.frames.resize(10); // all-zero registers
        QCOMPARE(int(reconstructNotes(cap).size()), 0);
    }

    void vibratoStaysOneNote() {
        // Small frequency wobble around A4 must not split into many notes.
        CaptureResult cap;
        cap.clockHz = kPalClockHz;
        cap.frames.resize(6);
        const int wob[6] = {7492, 7510, 7475, 7500, 7485, 7492};
        for (int f = 0; f < 6; ++f) setVoice(cap.frames[f], 0, wob[f], 0x11, 0x00);
        std::vector<SidNote> notes = reconstructNotes(cap);
        QCOMPARE(int(notes.size()), 1);  // all quantise to note 69
        QCOMPARE(notes[0].midiNote, 69);
        QCOMPARE(notes[0].endFrame, 6);
    }
};

QTEST_MAIN(SidReconstructTest)
#include "test_sid_reconstruct.moc"
