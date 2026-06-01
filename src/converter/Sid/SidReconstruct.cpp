/*
 * MidiEditor AI - SID note reconstruction implementation (Phase 42.1b).
 *
 * Heuristics tuned against real HVSC tunes:
 *  - release tail: a gate-off starts a release window (SR release nibble via
 *    the SID rate table, capped, ended early by the next note) so sustained
 *    bass/lead notes get real durations instead of 1-frame stabs;
 *  - percussion: a SID voice multiplexes drums (the noise waveform) over its
 *    bass/lead, so a gate-on noise frame is routed to a separate percussion
 *    stream (voice index 3) WITHOUT disturbing the melodic note on that voice;
 *  - hard-restart TEST-bit frames hold the oscillator in reset, so they are
 *    skipped (no tone) and don't chop the held note.
 */

#include "SidReconstruct.h"

#include <cmath>

namespace sid {

int sidFreqToMidiNote(int sidFreq, double clockHz) {
    if (sidFreq <= 0 || clockHz <= 0.0)
        return -1;
    const double hz = double(sidFreq) * clockHz / 16777216.0;
    if (hz < 8.0)
        return -1;
    const int note = int(std::lround(69.0 + 12.0 * std::log2(hz / 440.0)));
    if (note < 0 || note > 127)
        return -1;
    return note;
}

namespace {

struct VoiceState {
    bool    active = false;
    int     startFrame = 0;
    int     midiNote = -1;
    int     sidFreq = 0;
    uint8_t waveform = 0;
    int     velocity = 100;
    bool    releasing = false;
    int     releaseEnd = 0;
};

struct PercState {
    bool active = false;
    int  startFrame = 0;
    int  midiNote = 48;
    int  velocity = 100;
    int  length = 1;   ///< min frames the hit should sound (from its decay)
};

const int kVoiceBase[3] = {0, 7, 14};
const int kPercVoice = 3; // synthetic 4th "voice" = percussion track

int velocityFromSustain(uint8_t sustainRelease) {
    // SID notes attack to FULL peak regardless of sustain, so keep velocity in
    // a clearly-audible 80..127 band (sustain-0 tunes were silent otherwise).
    const int sustain = sustainRelease >> 4;
    return 80 + (sustain * 47) / 15;
}

int releaseFrames(uint8_t sustainRelease, double fps, int maxFrames) {
    static const int kMs[16] = {6, 24, 48, 72, 114, 168, 204, 240,
                                300, 750, 1500, 2400, 3000, 9000, 15000, 24000};
    int f = int(std::lround(kMs[sustainRelease & 0x0F] / 1000.0 * fps));
    if (f > maxFrames) f = maxFrames;
    return f;
}

} // namespace

std::vector<SidNote> reconstructNotes(const CaptureResult &cap) {
    std::vector<SidNote> notes;
    VoiceState vs[3];
    PercState perc[3];

    const double fps = cap.framesPerSecond > 0 ? cap.framesPerSecond : 50.0;
    const int maxRelease = int(fps * 2.0);
    const int frameCount = int(cap.frames.size());

    auto pushNote = [&](int v, int endFrame) {
        if (vs[v].active && endFrame > vs[v].startFrame) {
            SidNote n;
            n.voice = v; n.startFrame = vs[v].startFrame; n.endFrame = endFrame;
            n.midiNote = vs[v].midiNote; n.sidFreq = vs[v].sidFreq;
            n.waveform = vs[v].waveform; n.velocity = vs[v].velocity;
            notes.push_back(n);
        }
        vs[v].active = false;
        vs[v].releasing = false;
    };

    auto pushPerc = [&](int v, int endFrame) {
        if (perc[v].active) {
            // A drum hit gates for ~1 frame but rings out over its decay
            // envelope; give the note that length so the noise preset has time
            // to sound (a single-tick note is inaudible). Honour a longer
            // actual gate if there was one; clamp to the capture.
            int e = perc[v].startFrame + perc[v].length;
            if (endFrame > e) e = endFrame;
            if (e > frameCount) e = frameCount;
            if (e > perc[v].startFrame) {
                SidNote n;
                n.voice = kPercVoice;
                n.startFrame = perc[v].startFrame;
                n.endFrame = e;
                n.midiNote = perc[v].midiNote;
                n.sidFreq = 0;
                n.waveform = 0x80; // noise -> C64 Noise preset
                n.velocity = perc[v].velocity;
                notes.push_back(n);
            }
        }
        perc[v].active = false;
    };

    for (int f = 0; f < frameCount; ++f) {
        const SidFrame &fr = cap.frames[f];
        for (int v = 0; v < 3; ++v) {
            const int b = kVoiceBase[v];
            const uint8_t ctrl = fr.regs[b + 4];

            if (vs[v].active && vs[v].releasing && f >= vs[v].releaseEnd)
                pushNote(v, vs[v].releaseEnd);

            if (ctrl & 0x08) // TEST bit: oscillator reset, no tone this frame
                continue;

            const bool gate = (ctrl & 0x01) != 0;
            const int freq = fr.regs[b] | (fr.regs[b + 1] << 8);
            const uint8_t wave = ctrl & 0xF0;
            const uint8_t sr = fr.regs[b + 6];

            // Gate-on noise = a percussion hit on this voice. Route it to the
            // drum stream and leave the voice's melodic note alone.
            if (gate && (wave & 0x80)) {
                if (!perc[v].active) {
                    int pn = sidFreqToMidiNote(freq, cap.clockHz);
                    if (pn < 0) pn = 48;
                    // Drum length from the decay nibble of AD (regs b+5),
                    // clamped to a percussive 3..12 frames (~60-240 ms).
                    int len = releaseFrames(fr.regs[b + 5], fps, 12);
                    if (len < 3) len = 3;
                    perc[v] = PercState{true, f, pn, velocityFromSustain(sr), len};
                }
                continue;
            }
            // Not a noise hit -> close any open percussion hit on this voice.
            pushPerc(v, f);

            if (gate) {
                // Ring modulation (bit 0x04) replaces this oscillator's output
                // with triangle x the previous voice's oscillator, an
                // *inharmonic* spectrum whose perceived pitch is NOT the
                // frequency register. Reconstructing a MIDI note from freq here
                // yields phantom high notes (e.g. Commando's V1 ringmod intro
                // drone reads as a lone E7). So under ringmod we neither start
                // nor re-pitch a melodic note: we wait for a clean, non-ringmod
                // onset. (Sync, bit 0x02, still tracks a real pitch and is left
                // alone - V2's synced intro riff is genuine music.)
                const bool ringmod = (ctrl & 0x04) != 0;
                const int note = sidFreqToMidiNote(freq, cap.clockHz);
                if (note < 0) { pushNote(v, f); continue; }

                if (!vs[v].active) {
                    if (ringmod) continue; // don't start a pitched note under ringmod
                    vs[v] = VoiceState{true, f, note, freq, wave, velocityFromSustain(sr), false, 0};
                } else if (vs[v].releasing || note != vs[v].midiNote) {
                    if (ringmod) continue; // keep the active note; don't split on a ringmod pitch
                    pushNote(v, f);
                    vs[v] = VoiceState{true, f, note, freq, wave, velocityFromSustain(sr), false, 0};
                } else {
                    vs[v].waveform = wave;
                    vs[v].sidFreq = freq;
                }
            } else {
                if (vs[v].active && !vs[v].releasing) {
                    int rel = releaseFrames(sr, fps, maxRelease);
                    if (rel <= 0) {
                        pushNote(v, f);
                    } else {
                        vs[v].releasing = true;
                        vs[v].releaseEnd = f + rel;
                    }
                }
            }
        }
    }

    for (int v = 0; v < 3; ++v) {
        if (vs[v].active) {
            int end = (vs[v].releasing && vs[v].releaseEnd < frameCount)
                          ? vs[v].releaseEnd : frameCount;
            pushNote(v, end);
        }
        if (perc[v].active)
            pushPerc(v, frameCount);
    }

    return notes;
}

} // namespace sid
