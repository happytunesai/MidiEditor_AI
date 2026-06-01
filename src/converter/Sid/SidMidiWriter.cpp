/*
 * MidiEditor AI - SID notes -> SMF bytes implementation (Phase 42.1).
 */

#include "SidMidiWriter.h"

#include <algorithm>
#include <cmath>

namespace sid {

namespace {

QByteArray vlq(int value) { // variable-length quantity (MIDI delta times)
    QByteArray out;
    quint32 buf = value & 0x7F;
    while ((value >>= 7) > 0) {
        buf <<= 8;
        buf |= ((value & 0x7F) | 0x80);
    }
    for (;;) {
        out.append(static_cast<char>(buf & 0xFF));
        if (buf & 0x80) buf >>= 8; else break;
    }
    return out;
}

void be16(QByteArray &d, quint16 v) {
    d.append(static_cast<char>((v >> 8) & 0xFF));
    d.append(static_cast<char>(v & 0xFF));
}
void be32(QByteArray &d, quint32 v) {
    d.append(static_cast<char>((v >> 24) & 0xFF));
    d.append(static_cast<char>((v >> 16) & 0xFF));
    d.append(static_cast<char>((v >> 8) & 0xFF));
    d.append(static_cast<char>(v & 0xFF));
}

QByteArray mtrk(const QByteArray &events) {
    QByteArray chunk;
    chunk.append("MTrk", 4);
    be32(chunk, static_cast<quint32>(events.size()));
    chunk.append(events);
    return chunk;
}

// SID waveform bits -> a sensible General MIDI program (0-based). The real
// "C64 sound" comes from the C64 SoundFont in Phase 42.2; these are just
// reasonable defaults for a stock GM soundfont.
int waveformToProgram(uint8_t w) {
    if (w & 0x40) return 80;  // pulse    -> Lead 1 (square)
    if (w & 0x20) return 81;  // sawtooth -> Lead 2 (sawtooth)
    if (w & 0x10) return 82;  // triangle -> Lead 3 (calliope)
    if (w & 0x80) return 122; // noise    -> Seashore (distinct, so C64 mode
                              //             can remap it to C64 Noise)
    return 80;
}

struct Evt { int tick; bool on; int pitch; int vel; };

} // namespace

QByteArray writeSidNotesToSmf(const std::vector<SidNote> &notes,
                              const CaptureResult &cap,
                              const QString &title) {
    const int ppq = 600;
    const int tempoUs = 500000; // 120 BPM
    const double fps = cap.framesPerSecond > 0 ? cap.framesPerSecond : 50.0;
    int ticksPerFrame = int(std::lround(ppq * 2.0 / fps)); // PAL 24, NTSC 20
    if (ticksPerFrame < 1) ticksPerFrame = 1;

    QByteArray midi;
    midi.append("MThd", 4);
    be32(midi, 6);
    be16(midi, 1);                          // format 1
    be16(midi, 5);                          // conductor + 3 voices + percussion
    be16(midi, static_cast<quint16>(ppq));

    // --- Track 0: conductor (name, tempo, time signature) ---
    {
        QByteArray ev;
        QByteArray nm = (QStringLiteral("SID: ") +
                         (title.isEmpty() ? QStringLiteral("Untitled") : title)).toUtf8();
        ev.append(vlq(0)); ev.append(char(0xFF)); ev.append(char(0x03)); ev.append(vlq(nm.size())); ev.append(nm);
        ev.append(vlq(0)); ev.append(char(0xFF)); ev.append(char(0x51)); ev.append(char(0x03));
        ev.append(char((tempoUs >> 16) & 0xFF)); ev.append(char((tempoUs >> 8) & 0xFF)); ev.append(char(tempoUs & 0xFF));
        ev.append(vlq(0)); ev.append(char(0xFF)); ev.append(char(0x58)); ev.append(char(0x04));
        ev.append(char(0x04)); ev.append(char(0x02)); ev.append(char(0x18)); ev.append(char(0x08)); // 4/4
        ev.append(vlq(0)); ev.append(char(0xFF)); ev.append(char(0x2F)); ev.append(char(0x00));
        midi.append(mtrk(ev));
    }

    // --- Tracks 1-3: SID voices; track 4: percussion (noise hits) ---
    for (int v = 0; v < 4; ++v) {
        QByteArray ev;
        const int ch = v & 0x0F;

        QByteArray nm = (v < 3 ? QStringLiteral("SID Voice %1").arg(v + 1)
                               : QStringLiteral("SID Percussion")).toUtf8();
        ev.append(vlq(0)); ev.append(char(0xFF)); ev.append(char(0x03)); ev.append(vlq(nm.size())); ev.append(nm);

        // Program from the voice's first note's waveform.
        for (const SidNote &n : notes) {
            if (n.voice == v) {
                int prog = waveformToProgram(n.waveform);
                ev.append(vlq(0)); ev.append(char(0xC0 | ch)); ev.append(char(prog & 0x7F));
                break;
            }
        }

        std::vector<Evt> list;
        for (const SidNote &n : notes) {
            if (n.voice != v) continue;
            int t0 = n.startFrame * ticksPerFrame;
            int t1 = n.endFrame * ticksPerFrame;
            if (t1 <= t0) t1 = t0 + 1;
            int vel = n.velocity < 1 ? 1 : (n.velocity > 127 ? 127 : n.velocity);
            list.push_back({t0, true, n.midiNote & 0x7F, vel});
            list.push_back({t1, false, n.midiNote & 0x7F, 0});
        }
        std::sort(list.begin(), list.end(), [](const Evt &a, const Evt &b) {
            if (a.tick != b.tick) return a.tick < b.tick;
            return !a.on && b.on; // note-off before note-on at the same tick
        });

        int lastTick = 0;
        for (const Evt &e : list) {
            ev.append(vlq(e.tick - lastTick));
            ev.append(char((e.on ? 0x90 : 0x80) | ch));
            ev.append(char(e.pitch & 0x7F));
            ev.append(char(e.vel & 0x7F));
            lastTick = e.tick;
        }

        ev.append(vlq(0)); ev.append(char(0xFF)); ev.append(char(0x2F)); ev.append(char(0x00));
        midi.append(mtrk(ev));
    }

    return midi;
}

} // namespace sid
