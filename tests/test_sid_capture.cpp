/*
 * MidiEditor AI - End-to-end test for SID register capture (Phase 42).
 *
 * Builds a synthetic PSID whose embedded 6502 "player" increments voice-1's
 * frequency-low register and holds the gate on each frame, then checks that
 * captureSid() reproduces the expected per-frame register progression.
 * Exercises parser -> 6502 emulator -> capture together, deterministically.
 */

#include <QtTest/QtTest>

#include <cstdint>
#include <string>
#include <vector>

#include "../src/converter/Sid/SidCapture.h"
#include "../src/converter/Sid/SidFile.h"

using namespace sid;

namespace {

void putBE16(std::vector<uint8_t> &b, std::size_t off, uint16_t v) {
    b[off] = uint8_t(v >> 8); b[off + 1] = uint8_t(v & 0xFF);
}

// v2 PSID with load address in the header + a tiny embedded player.
//   init ($1000): gate voice1 on (triangle), zero its freq-low.
//   play ($100B): INC $D400 (freq-low) once per frame.
std::vector<uint8_t> buildPlayerPsid(uint16_t clockBits) {
    std::vector<uint8_t> prog = {
        0xA9, 0x11,        // $1000 LDA #$11
        0x8D, 0x04, 0xD4,  // $1002 STA $D404 (gate on + triangle)
        0xA9, 0x00,        // $1005 LDA #$00
        0x8D, 0x00, 0xD4,  // $1007 STA $D400 (freq-low = 0)
        0x60,              // $100A RTS
        0xEE, 0x00, 0xD4,  // $100B INC $D400
        0x60               // $100E RTS
    };
    std::vector<uint8_t> b(0x7C, 0);
    b[0] = 'P'; b[1] = 'S'; b[2] = 'I'; b[3] = 'D';
    putBE16(b, 0x04, 2);          // version
    putBE16(b, 0x06, 0x7C);       // data offset
    putBE16(b, 0x08, 0x1000);     // load address (in header)
    putBE16(b, 0x0A, 0x1000);     // init
    putBE16(b, 0x0C, 0x100B);     // play
    putBE16(b, 0x0E, 1);          // songs
    putBE16(b, 0x10, 1);          // start song
    putBE16(b, 0x76, clockBits);  // flags (clock bits 2-3)
    b.insert(b.end(), prog.begin(), prog.end());
    return b;
}

} // namespace

class SidCaptureTest : public QObject {
    Q_OBJECT
private slots:

    void capturesFrameProgression() {
        SidFile f = parseSid(buildPlayerPsid(0x0004)); // PAL
        QVERIFY(f.valid);

        CaptureResult r = captureSid(f, 1, 5);
        QVERIFY(r.ok);
        QCOMPARE(r.song, 1);
        QCOMPARE(int(r.frames.size()), 5);

        // freq-low ($D400 = reg 0) increments each frame: 1,2,3,4,5.
        QCOMPARE(int(r.frames[0].regs[0]), 1);
        QCOMPARE(int(r.frames[1].regs[0]), 2);
        QCOMPARE(int(r.frames[4].regs[0]), 5);

        // control register ($D404 = reg 4) stays gate-on + triangle (0x11).
        for (const SidFrame &fr : r.frames)
            QCOMPARE(int(fr.regs[4]), 0x11);
    }

    void palTimingDefaults() {
        SidFile f = parseSid(buildPlayerPsid(0x0004)); // PAL
        CaptureResult r = captureSid(f, 1, 2);
        QVERIFY(r.ok);
        QCOMPARE(r.framesPerSecond, kPalFramesPerSecond);
        QCOMPARE(r.clockHz, kPalClockHz);
    }

    void ntscTimingFromFlags() {
        SidFile f = parseSid(buildPlayerPsid(0x0008)); // NTSC (clock bits = 10)
        QVERIFY(f.clock == Clock::NTSC);
        CaptureResult r = captureSid(f, 1, 2);
        QVERIFY(r.ok);
        QCOMPARE(r.framesPerSecond, kNtscFramesPerSecond);
        QCOMPARE(r.clockHz, kNtscClockHz);
    }

    void rejectsInvalidFile() {
        SidFile bad; // valid == false
        CaptureResult r = captureSid(bad, 1, 5);
        QVERIFY(!r.ok);
        QVERIFY(!r.error.empty());
    }

    // Build a gated voice-1 frame whose coarse pitch (freq>>6) encodes `step`.
    static SidFrame mkFrame(int step) {
        int freq = (step + 1) * 64; // distinct freq>>6 per step
        SidFrame fr;
        fr.regs[0] = uint8_t(freq & 0xFF);
        fr.regs[1] = uint8_t((freq >> 8) & 0xFF);
        fr.regs[4] = 0x41; // gate + pulse
        return fr;
    }

    void detectLoopTrimsToOnePass() {
        // 50-frame intro + three iterations of a 100-frame loop pattern.
        // Natural length = intro (50) + one loop (100) = 150.
        std::vector<SidFrame> frames;
        for (int f = 0; f < 50; ++f) frames.push_back(mkFrame(200 + f)); // intro
        for (int rep = 0; rep < 3; ++rep)
            for (int k = 0; k < 100; ++k) frames.push_back(mkFrame(k));
        QCOMPARE(detectLoopEnd(frames), 150);
    }

    void detectLoopKeepsAllWhenNotPeriodic() {
        std::vector<SidFrame> frames;
        for (int f = 0; f < 300; ++f) frames.push_back(mkFrame(f)); // monotonic
        QCOMPARE(detectLoopEnd(frames), 300);
    }
};

QTEST_MAIN(SidCaptureTest)
#include "test_sid_capture.moc"
