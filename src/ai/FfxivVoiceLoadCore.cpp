/*
 * FfxivVoiceLoadCore.cpp — Phase 32.4
 *
 * No MidiFile / MidiChannel includes; safe to link from unit tests.
 */

#include "FfxivVoiceLoadCore.h"

#include <algorithm>
#include <deque>

namespace FfxivVoiceLoad {

// ---------------------------------------------------------------------------
// sampleTailMs — per-instrument voice-lifetime model
// ---------------------------------------------------------------------------
// Returns the simulated *audible* release tail for one note, in ms.
//
// History:
//   v1.5.4 first cut imported BMP's `MinimumLength` table verbatim
//   (LightAmp/BardMusicPlayer.Ui/Windows/VoiceMap.xaml.cs).  Those values
//   are wrong for voice-counting because BMP uses MinimumLength to
//   *extend* short MIDI notes so the full sample plays out — they are
//   the sample's total length (1.1 – 1.7 s), not the perceptually
//   audible tail.
//
// v1.5.4-relaxed (this version) follows GPT-2026-04 deep-dive of
// community sources:
//   * Square-Enix forum bugreport: 16 simultaneous voices is the
//     observed eviction trigger
//   * MidiBard2: excess notes are queued + delayed, not silently
//     dropped
//   * AllaganHarp: 35 ms aggressive / 50 ms safe arpeggio spacing
//     (independent of release-tail!)
//   * No published Square-Enix specification for per-instrument
//     audible-release durations exists.
//
// We therefore use *audible* tails (estimated time until sample drops
// below ~-40 dB), which are roughly half of BMP's MinimumLength values:
//
//   Plucked (Harp/Lute/Guitar):  500 –  900 ms
//   Piano:                       400 –  800 ms
//   Drums (per pitch):           300 – 1500 ms
//   Sustained (winds/brass/bow): max(noteLen, 300 ms)
//   Reed/Pipe:                   max(noteLen, 500 ms)
//
// These are heuristics; users can recalibrate via the visual thresholds
// in `FfxivVoiceGaugeWidget` / `FfxivVoiceLaneWidget`.
int sampleTailMs(int program, int pitch, bool isDrumChannel,
                 int noteDurationMs)
{
    int p = pitch - 48; // pitch offset above C3, used by some buckets

    if (isDrumChannel) {
        // GM drum kit on channel 9: pitch *is* the instrument.
        // Audible tails (rough estimates, not measured):
        if (pitch == 35 || pitch == 36) return 300; // BassDrum (short thump)
        if (pitch == 38 || pitch == 40) return 200; // SnareDrum
        if (pitch >= 41 && pitch <= 50) return 280; // Toms
        if (pitch == 42 || pitch == 44) return 250; // ClosedHat / PedalHat
        if (pitch == 46)                return 600; // OpenHat
        if (pitch == 49 || pitch == 51 || pitch == 52
            || pitch == 55 || pitch == 57) return 1200; // Cymbals (long)
        if (pitch >= 60 && pitch <= 63) return 350; // Bongo / Conga
        return 300;
    }

    // Piano family (GM 0..7) — moderate sustain
    if (program >= 0 && program <= 7) {
        if (p <= 11) return 700;
        if (p <= 25) return 600;
        return 500;
    }
    // Chromatic percussion (8..15) — short pluck-like decay
    if (program >= 8 && program <= 15) return 500;
    // Organ (16..23) — sustained
    if (program >= 16 && program <= 23) {
        return std::max(noteDurationMs, 400);
    }
    // Guitar acoustic / Lute family (24..31)
    if (program >= 24 && program <= 31) {
        if (p <= 14) return 900;
        if (p <= 28) return 800;
        return 700;
    }
    // Bass (32..39) — pluck-like, slightly shorter than guitar
    if (program >= 32 && program <= 39) return 700;
    // Bowed strings (40..45) — sustained
    if (program >= 40 && program <= 45) {
        int d = noteDurationMs;
        if (d > 4500) d = 4500;
        if (d < 300)  d = 300;
        return d;
    }
    // Orchestral Harp (46) — flagship plucked instrument
    if (program == 46) {
        if (p <= 19) return 800;
        if (p <= 28) return 700;
        return 600;
    }
    // Timpani (47)
    if (program == 47) {
        if (p <= 15) return 700;
        return 800;
    }
    // Ensemble strings (48..55) — sustained
    if (program >= 48 && program <= 55) {
        int d = noteDurationMs;
        if (d > 4500) d = 4500;
        if (d < 300)  d = 300;
        return d;
    }
    // Brass (56..63) — sustained
    if (program >= 56 && program <= 63) {
        int d = noteDurationMs;
        if (d > 4500) d = 4500;
        if (d < 300)  d = 300;
        return d;
    }
    // Reed (64..71) — Sax/Oboe/Clarinet
    if (program >= 64 && program <= 71) {
        int d = noteDurationMs;
        if (d > 4500) d = 4500;
        if (d < 500)  d = 500;
        return d;
    }
    // Pipe (72..79) — Flute/Fife/Panpipes
    if (program >= 72 && program <= 79) {
        int d = noteDurationMs;
        if (d > 4500) d = 4500;
        if (d < 500)  d = 500;
        return d;
    }
    // Synth lead/pad (80..95) — treat as sustained
    if (program >= 80 && program <= 95) {
        return std::max(noteDurationMs, 400);
    }
    // FX / ethnic / percussive (96..127) — moderate decay
    if (program >= 112 && program <= 119) return 500; // percussive
    if (program >= 120 && program <= 127) return 350; // SFX
    return std::max(noteDurationMs, 300);
}

Result computeFromNotes(const QVector<NoteSpan> &notes,
                        const std::function<int(int)> &msAtTick,
                        const AnalyzeOptions &opts)
{
    QVector<NoteEdge> edges;
    edges.reserve(notes.size() * 2);
    for (const NoteSpan &n : notes) {
        if (n.channel < 0 || n.channel >= 16)
            continue;
        int onTick = n.startTick;
        int offTick = n.endTick;
        if (offTick <= onTick)
            offTick = onTick + 1;

        if (opts.simulateSampleTail && opts.tickAtMs) {
            int onMs = msAtTick(onTick);
            int offMs = msAtTick(offTick);
            int durMs = std::max(1, offMs - onMs);
            int tailMs = sampleTailMs(n.program, n.pitch,
                                      n.isDrumChannel, durMs);
            int extendedEndMs = onMs + std::max(durMs, tailMs);
            int extendedEndTick = opts.tickAtMs(extendedEndMs);
            if (extendedEndTick > offTick)
                offTick = extendedEndTick;
        }

        edges.push_back({onTick,  +1, n.channel});
        edges.push_back({offTick, -1, n.channel});
    }
    return computeFromEdges(edges, msAtTick);
}

Result computeFromEdges(const QVector<NoteEdge> &edgesIn,
                        const std::function<int(int)> &msAtTick)
{
    Result r;

    QVector<NoteEdge> deltas = edgesIn;

    // Sort: by tick, then OFFs (-1) before ONs (+1) at the same tick so the
    // reported peak reflects only truly simultaneous voices, not the
    // instantaneous ON-then-OFF artefact at note boundaries.
    std::sort(deltas.begin(), deltas.end(),
              [](const NoteEdge &a, const NoteEdge &b) {
                  if (a.tick != b.tick)
                      return a.tick < b.tick;
                  return a.delta < b.delta;
              });

    std::deque<int> noteOnTicksByChannel[16];
    QVector<RateHotspot> hotspots;

    int active = 0;
    int peak = 0;
    int peakTick = 0;
    int overflowEvents = 0;
    r.voiceSamples.reserve(deltas.size());

    for (const NoteEdge &d : deltas) {
        if (d.delta > 0) {
            if (d.channel >= 0 && d.channel < 16) {
                std::deque<int> &q = noteOnTicksByChannel[d.channel];
                q.push_back(d.tick);
                int windowStartMs = msAtTick(d.tick) - kNoteRateWindowMs;
                while (!q.empty() && msAtTick(q.front()) < windowStartMs)
                    q.pop_front();
                if (static_cast<int>(q.size()) > kNoteRateCeilingPerChannel) {
                    int notesInWindow = static_cast<int>(q.size());
                    double secs = double(kNoteRateWindowMs) / 1000.0;
                    double nps = notesInWindow / secs;
                    if (!hotspots.isEmpty()
                        && hotspots.last().channel == d.channel
                        && hotspots.last().endTick >= q.front()) {
                        hotspots.last().endTick = d.tick;
                        hotspots.last().notesInWindow =
                            std::max(hotspots.last().notesInWindow, notesInWindow);
                        hotspots.last().notesPerSecond =
                            std::max(hotspots.last().notesPerSecond, nps);
                    } else {
                        RateHotspot hs;
                        hs.channel = d.channel;
                        hs.startTick = q.front();
                        hs.endTick = d.tick;
                        hs.notesInWindow = notesInWindow;
                        hs.notesPerSecond = nps;
                        hotspots.push_back(hs);
                    }
                }
            }

            if (active >= kVoiceCeiling)
                ++overflowEvents;
        }

        active += d.delta;
        if (active < 0)
            active = 0;

        if (active > peak) {
            peak = active;
            peakTick = d.tick;
        }

        if (!r.voiceSamples.isEmpty() && r.voiceSamples.last().tick == d.tick) {
            r.voiceSamples.last().voiceCount = active;
        } else {
            r.voiceSamples.push_back({d.tick, active});
        }
    }

    r.rateHotspots = hotspots;
    r.globalPeak = peak;
    r.globalPeakTick = peakTick;
    r.overflowEvents = overflowEvents;
    r.valid = true;
    return r;
}

} // namespace FfxivVoiceLoad
