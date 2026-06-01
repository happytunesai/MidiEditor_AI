/*
 * MidiEditor AI - SID register capture (Phase 42, C64 mode).
 *
 * Drives the 6502 emulator the way a C64 does for a PSID tune: place the
 * program in RAM, call the init routine once (accumulator = song number),
 * then call the play routine once per video frame (~50 Hz PAL), snapshotting
 * the 25 writable SID registers ($D400-$D418) after each frame. The SID
 * registers are just RAM here - the classic "SIDDump" capture trick.
 *
 * The resulting per-frame register table is the deterministic input to the
 * note-reconstruction heuristics (Phase 42.1b, modelled on sidtool's
 * voice.rb). Plain C++ so it stays unit-testable.
 */

#ifndef SID_SIDCAPTURE_H
#define SID_SIDCAPTURE_H

#include <cstdint>
#include <string>
#include <vector>

#include "SidFile.h"

namespace sid {

/// PAL timing - the rate the play routine is called and the SID clock.
constexpr double kPalFramesPerSecond = 50.0;
constexpr double kPalClockHz = 985248.0;
constexpr double kNtscFramesPerSecond = 60.0;
constexpr double kNtscClockHz = 1022730.0;

/// 25 writable SID registers, $D400..$D418, snapshotted at end of frame.
constexpr int kSidRegisterCount = 25;

struct SidFrame {
    uint8_t regs[kSidRegisterCount] = {0};
};

struct CaptureResult {
    bool                  ok = false;
    std::string           error;
    int                   song = 0;       ///< 1-based song actually played
    double                framesPerSecond = kPalFramesPerSecond;
    double                clockHz = kPalClockHz;
    bool                  loopDetected = false; ///< true if frames were trimmed to one loop
    std::vector<SidFrame> frames;
};

/// Find the natural length of a captured register stream: the number of
/// leading frames to keep so the result is "intro + exactly one loop"
/// (no repeated iterations). Returns frames.size() when no loop is found.
/// Exposed for unit testing.
int detectLoopEnd(const std::vector<SidFrame> &frames);

/// Run \a song (1-based) of \a sf for \a frameCount frames and capture the
/// SID register state after each frame. \a song <= 0 uses the file's start
/// song. On a runaway init/play routine the capture stops early but returns
/// whatever frames it gathered (ok == !frames.empty()).
CaptureResult captureSid(const SidFile &sf, int song, int frameCount);

} // namespace sid

#endif // SID_SIDCAPTURE_H
