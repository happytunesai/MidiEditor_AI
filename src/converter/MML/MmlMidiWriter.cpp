#include "MmlMidiWriter.h"
#include <algorithm>

QByteArray MmlMidiWriter::writeVariableLength(int value) {
    QByteArray result;
    quint32 buf = value & 0x7F;
    while ((value >>= 7) > 0) {
        buf <<= 8;
        buf |= ((value & 0x7F) | 0x80);
    }
    for (;;) {
        result.append(static_cast<char>(buf & 0xFF));
        if (buf & 0x80)
            buf >>= 8;
        else
            break;
    }
    return result;
}

void MmlMidiWriter::appendUInt16BE(QByteArray& d, quint16 v) {
    d.append(static_cast<char>((v >> 8) & 0xFF));
    d.append(static_cast<char>(v & 0xFF));
}

void MmlMidiWriter::appendUInt32BE(QByteArray& d, quint32 v) {
    d.append(static_cast<char>((v >> 24) & 0xFF));
    d.append(static_cast<char>((v >> 16) & 0xFF));
    d.append(static_cast<char>((v >> 8) & 0xFF));
    d.append(static_cast<char>(v & 0xFF));
}

QByteArray MmlMidiWriter::writeTrack(const MmlTrack& track, int channel,
                                     int tempo, bool includeTempo) {
    QByteArray ev;
    int ch = channel & 0x0F;

    // Track name meta-event
    if (!track.name.isEmpty()) {
        QByteArray name = track.name.toUtf8();
        ev.append(writeVariableLength(0));
        ev.append(static_cast<char>(0xFF));
        ev.append(static_cast<char>(0x03));
        ev.append(writeVariableLength(name.size()));
        ev.append(name);
    }

    // Tempo meta-event (first track only)
    if (includeTempo && tempo > 0) {
        int usPerBeat = 60000000 / tempo;
        ev.append(writeVariableLength(0));
        ev.append(static_cast<char>(0xFF));
        ev.append(static_cast<char>(0x51));
        ev.append(static_cast<char>(0x03));
        ev.append(static_cast<char>((usPerBeat >> 16) & 0xFF));
        ev.append(static_cast<char>((usPerBeat >> 8) & 0xFF));
        ev.append(static_cast<char>(usPerBeat & 0xFF));
    }

    // Program change
    if (track.instrument > 0) {
        ev.append(writeVariableLength(0));
        ev.append(static_cast<char>(0xC0 | ch));
        ev.append(static_cast<char>(track.instrument & 0x7F));
    }

    // Build note-on / note-off pairs and sort
    struct Evt {
        int tick;
        bool on;
        int pitch;
        int vel;
    };
    QList<Evt> list;
    for (const MmlNote& n : track.notes) {
        list.append({n.startTick, true, n.pitch, n.velocity});
        list.append({n.startTick + n.duration, false, n.pitch, 0});
    }
    std::sort(list.begin(), list.end(), [](const Evt& a, const Evt& b) {
        if (a.tick != b.tick) return a.tick < b.tick;
        return !a.on && b.on; // note-off before note-on at same tick
    });

    int lastTick = 0;
    for (const Evt& e : list) {
        int delta = e.tick - lastTick;
        ev.append(writeVariableLength(delta));
        ev.append(static_cast<char>((e.on ? 0x90 : 0x80) | ch));
        ev.append(static_cast<char>(e.pitch & 0x7F));
        ev.append(static_cast<char>(e.vel & 0x7F));
        lastTick = e.tick;
    }

    // End-of-track
    ev.append(writeVariableLength(0));
    ev.append(static_cast<char>(0xFF));
    ev.append(static_cast<char>(0x2F));
    ev.append(static_cast<char>(0x00));

    // Wrap in MTrk chunk
    QByteArray chunk;
    chunk.append("MTrk", 4);
    appendUInt32BE(chunk, static_cast<quint32>(ev.size()));
    chunk.append(ev);
    return chunk;
}

QByteArray MmlMidiWriter::write(const MmlSong& song) {
    if (song.tracks.isEmpty())
        return {};

    int nTracks = song.tracks.size();

    // MThd header
    QByteArray midi;
    midi.append("MThd", 4);
    appendUInt32BE(midi, 6);
    appendUInt16BE(midi, nTracks > 1 ? 1 : 0); // format 0 or 1
    appendUInt16BE(midi, static_cast<quint16>(nTracks));
    appendUInt16BE(midi, static_cast<quint16>(song.ticksPerQuarter));

    // Track chunks
    for (int i = 0; i < nTracks; i++) {
        const MmlTrack& t = song.tracks[i];
        int ch = (t.channel >= 0) ? t.channel : (i % 16);
        midi.append(writeTrack(t, ch, song.tempo, i == 0));
    }

    return midi;
}
