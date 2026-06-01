/*
 * MidiEditor AI - SID note reconstruction (Phase 42.1b, C64 mode).
 *
 * Turns the deterministic per-frame SID register stream (from SidCapture)
 * into discrete notes. This is the heuristic, inherently-lossy half of the
 * SID->MIDI pipeline - modelled on sidtool's voice.rb:
 *   - a voice's gate bit (control & 1) marks note on/off,
 *   - the 16-bit frequency maps to the nearest MIDI pitch for the clock,
 *   - a pitch change while the gate stays on starts a new note (so fast
 *     register-level arpeggios become separate notes),
 *   - the waveform bits + sustain feed instrument / velocity hints.
 *
 * Plain C++ so it stays unit-testable; the MidiFile/SMF emission lives in
 * the importer layer (42.1).
 */

#ifndef SID_SIDRECONSTRUCT_H
#define SID_SIDRECONSTRUCT_H

#include <cstdint>
#include <vector>

#include "SidCapture.h"

namespace sid {

struct SidNote {
    int     voice = 0;       ///< 0..2 (SID voice)
    int     startFrame = 0;  ///< first frame the note sounds
    int     endFrame = 0;    ///< one past the last frame (exclusive)
    int     midiNote = 0;    ///< 0..127
    int     sidFreq = 0;     ///< raw 16-bit SID frequency at note start
    uint8_t waveform = 0;    ///< control bits 0x10..0x80 (tri/saw/pulse/noise)
    int     velocity = 100;  ///< 1..127, derived from the sustain level
};

/// Nearest MIDI note for a raw 16-bit SID frequency at the given clock.
/// Returns -1 for silence / out-of-range (freq 0 or sub-audible).
int sidFreqToMidiNote(int sidFreq, double clockHz);

/// Reconstruct notes across all three voices from the captured frames.
std::vector<SidNote> reconstructNotes(const CaptureResult &cap);

} // namespace sid

#endif // SID_SIDRECONSTRUCT_H
