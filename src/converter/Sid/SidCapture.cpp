/*
 * MidiEditor AI - SID register capture implementation (Phase 42).
 */

#include "SidCapture.h"

#include "Mos6502.h"

#include <cstdint>

namespace sid {

namespace {

// Per-frame "musical signature": gate + coarse pitch (>>6 absorbs vibrato
// jitter) + waveform class, per voice, packed into 36 bits. Real SID players
// don't repeat their register stream byte-exactly across loops (LFO/vibrato
// phase drifts), so matching on this musical essence is far more robust than
// exact register equality.
uint32_t voiceSig(uint8_t ctrl, int freq) {
    if (ctrl & 0x08) return 0xFFFu;        // TEST: oscillator reset state
    if (!(ctrl & 0x01)) return 0;          // gate off / silent
    uint32_t pitch = (freq >> 6) & 0x1FFu; // coarse pitch
    uint32_t wc = (ctrl >> 4) & 0x7u;
    return 0x800u | (wc << 9) | pitch;
}

uint64_t frameSig(const SidFrame &fr) {
    uint32_t v0 = voiceSig(fr.regs[4],  fr.regs[0]  | (fr.regs[1]  << 8));
    uint32_t v1 = voiceSig(fr.regs[11], fr.regs[7]  | (fr.regs[8]  << 8));
    uint32_t v2 = voiceSig(fr.regs[18], fr.regs[14] | (fr.regs[15] << 8));
    return uint64_t(v0) | (uint64_t(v1) << 12) | (uint64_t(v2) << 24);
}

} // namespace

int detectLoopEnd(const std::vector<SidFrame> &frames) {
    const int N = static_cast<int>(frames.size());
    const int minPeriod = 50;  // >= ~1 s; ignore trivial periodicity
    const double tol = 0.05;   // tolerate up to 5% jittered frames per period
    if (N < 2 * minPeriod)
        return N;

    std::vector<uint64_t> sig(static_cast<std::size_t>(N));
    for (int i = 0; i < N; ++i)
        sig[i] = frameSig(frames[i]);

    auto blockMatches = [&](int a, int b, int P) {
        int lim = static_cast<int>(P * tol);
        if (lim < 2) lim = 2;
        int mism = 0;
        for (int k = 0; k < P; ++k) {
            if (sig[a + k] != sig[b + k] && ++mism > lim)
                return false;
        }
        return true;
    };

    // Smallest period whose two trailing windows match (within tolerance) is
    // the fundamental loop.
    int period = -1;
    for (int P = minPeriod; P <= N / 2; ++P) {
        if (blockMatches(N - 2 * P, N - P, P)) { period = P; break; }
    }
    if (period < 0)
        return N;

    // Walk the loop start back in whole-period blocks; keep intro + one loop.
    int M = N - period;
    while (M - period >= 0 && blockMatches(M - period, M, period))
        M -= period;
    return M + period;
}

CaptureResult captureSid(const SidFile &sf, int song, int frameCount) {
    CaptureResult r;
    if (!sf.valid) {
        r.error = "Invalid SID file.";
        return r;
    }

    // Resolve song selection (1-based as stored).
    int s = (song > 0) ? song : sf.startSong;
    if (s < 1 || s > sf.songs) s = sf.startSong;
    if (s < 1) s = 1;
    r.song = s;

    // PAL vs NTSC timing for downstream pitch/duration maths.
    if (sf.clock == Clock::NTSC) {
        r.framesPerSecond = kNtscFramesPerSecond;
        r.clockHz = kNtscClockHz;
    } else {
        r.framesPerSecond = kPalFramesPerSecond;
        r.clockHz = kPalClockHz;
    }

    Mos6502 cpu;
    cpu.loadProgram(sf.programData.data(), int(sf.programData.size()), sf.loadAddress);

    // PSID init convention: the song number (0-based) is passed in A.
    cpu.a = uint8_t(s - 1);
    cpu.x = 0;
    cpu.y = 0;

    if (frameCount < 1) frameCount = 1;
    r.frames.reserve(static_cast<std::size_t>(frameCount));

    auto snapshot = [&]() {
        SidFrame fr;
        for (int reg = 0; reg < kSidRegisterCount; ++reg)
            fr.regs[reg] = cpu.mem[uint16_t(0xD400 + reg)];
        r.frames.push_back(fr);
    };

    if (sf.playAddress == 0) {
        // RSID tune with a self-installed interrupt player (no play address).
        // Per the SID format spec these "strictly require a true C64
        // environment ... a cycle-accurate emulator like VICE", which our
        // from-scratch 6502 is not - the captured registers come out as noise.
        // Rather than import garbage we decline RSID-IRQ tunes; accurate import
        // will arrive with the cycle-accurate libsidplayfp backend.
        r.error = "This is an RSID tune with a self-installed interrupt player. "
                  "Importing it accurately needs a cycle-accurate C64 engine "
                  "(libsidplayfp), which isn't available yet.";
        return r;
    }

    // PSID: a plain subroutine init, then call the play routine once per
    // (50/60 Hz) frame.
    if (!cpu.callSubroutine(sf.initAddress)) {
        r.error = "Init routine did not return (ran past the instruction guard).";
        return r;
    }
    for (int frame = 0; frame < frameCount; ++frame) {
        if (!cpu.callSubroutine(sf.playAddress)) {
            r.error = "Play routine did not return on frame " + std::to_string(frame) + ".";
            break;
        }
        snapshot();
    }

    // Trim to the natural length (intro + exactly one loop) when the tune's
    // register stream turns out to be periodic - no repeated iterations.
    int keep = detectLoopEnd(r.frames);
    if (keep > 0 && keep < static_cast<int>(r.frames.size())) {
        r.frames.resize(static_cast<std::size_t>(keep));
        r.loopDetected = true;
    }

    r.ok = !r.frames.empty();
    return r;
}

} // namespace sid
