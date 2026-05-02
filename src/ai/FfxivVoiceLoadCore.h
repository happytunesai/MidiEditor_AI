/*
 * FfxivVoiceLoadCore — pure-data voice-load + rate analysis for FFXIV.
 *
 * Lives separately from FfxivVoiceAnalyzer so unit tests can link this
 * single TU without dragging in MidiFile / MidiChannel / Qt GUI deps.
 *
 * Phase 32.4 — see Planning/02_ROADMAP.md.
 */

#pragma once

#include <QVector>
#include <functional>

namespace FfxivVoiceLoad {

constexpr int kVoiceCeiling = 16;
constexpr int kNoteRateCeilingPerChannel = 14;
constexpr int kNoteRateWindowMs = 250;

struct VoiceSample {
    int tick;
    int voiceCount;
};

struct RateHotspot {
    int channel;
    int startTick;
    int endTick;
    int notesInWindow;
    double notesPerSecond;
};

struct Result {
    QVector<VoiceSample> voiceSamples;
    QVector<RateHotspot> rateHotspots;
    int globalPeak = 0;
    int globalPeakTick = 0;
    int overflowEvents = 0;
    bool valid = false;
};

struct NoteSpan {
    int channel;
    int startTick;
    int endTick;
    int program = 0;       ///< GM program 0..127 active at NoteOn time
    int pitch = 60;        ///< MIDI pitch 0..127
    bool isDrumChannel = false; ///< true for channel 9 (GM drum kit)
};

/// Returns the simulated voice lifetime in ms for a single note, modeled
/// after BMP / MogNotate's MinimumLength table. This is the *total* time
/// from NoteOn that the voice is still counted as active in the FFXIV
/// game-client mixer.  Pluck/piano/drum samples ring out for a fixed
/// per-pitch duration regardless of how long the key is held.  Sustained
/// instruments (winds, brass, bowed strings) are floored to a minimum
/// release tail and capped at the in-game 4500 ms ceiling.
int sampleTailMs(int program, int pitch, bool isDrumChannel,
                 int noteDurationMs);

/// Optional analysis tweaks. When `simulateSampleTail` is true, each
/// note's effective end is extended by `sampleTailMs(...)` — this matches
/// MogNotate's "active voices" display, which counts samples that are
/// still ringing after NoteOff.  Requires `tickAtMs` to convert the
/// extended end-ms back to a tick.
struct AnalyzeOptions {
    bool simulateSampleTail = false;
    std::function<int(int)> tickAtMs; ///< inverse of msAtTick; required when simulateSampleTail
};

/// Pure compute: notes are already extracted; `msAtTick` converts ticks to
/// milliseconds (used for the 250 ms note-rate window).
Result computeFromNotes(const QVector<NoteSpan> &notes,
                        const std::function<int(int)> &msAtTick,
                        const AnalyzeOptions &opts = {});

/// LightAmp-style edge stream: one entry per NoteOn (delta=+1) and one per
/// NoteOff (delta=-1). Channel is used only for the per-channel rate
/// detection. Use this when NoteOn/Off pairing is not reliable (loaded
/// MIDI files where pairs may be missing or split across tracks).
struct NoteEdge {
    int tick;
    int delta;       ///< +1 for NoteOn, -1 for NoteOff
    int channel;     ///< 0..15
};

Result computeFromEdges(const QVector<NoteEdge> &edges,
                        const std::function<int(int)> &msAtTick);

} // namespace FfxivVoiceLoad
