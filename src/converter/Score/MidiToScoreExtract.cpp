/*
 * MidiEditor AI - MidiFile -> ScoreInput extraction (see MidiToScore.h).
 *
 * The MidiFile-coupled half of the engraver: it flattens the live file into the
 * plain ScoreInput that the pure buildScore() (MidiToScore.cpp) engraves. Split
 * into its own TU so the engraver core can be unit-tested without dragging in
 * the MidiFile / event / GUI dependency tree.
 */
#include "MidiToScore.h"

#include "../../midi/MidiFile.h"
#include "../../midi/MidiChannel.h"
#include "../../midi/MidiTrack.h"
#include "../../MidiEvent/NoteOnEvent.h"
#include "../../MidiEvent/OffEvent.h"
#include "../../MidiEvent/TempoChangeEvent.h"
#include "../../MidiEvent/TimeSignatureEvent.h"
#include "../../MidiEvent/KeySignatureEvent.h"

#include <QHash>

namespace score {

ScoreInput extractInput(MidiFile *file) {
    ScoreInput in;
    if (!file) return in;
    in.divisions = file->ticksPerQuarter() > 0 ? file->ticksPerQuarter() : 480;
    in.endTick = file->endTick(); // ticks — NOT maxTime(), which returns milliseconds

    // Time signatures.
    {
        QMultiMap<int, MidiEvent *> *map = file->timeSignatureEvents();
        for (auto it = map->begin(); it != map->end(); ++it) {
            // denom() is the SMF power-of-two EXPONENT (4/4 stores 2), while
            // MetaTimeSig::denominator is a real note value - convert here or
            // the engraved meter and measure length are both wrong. Clamped to
            // 0..6 (whole .. 64th), the range MusicXML <beat-type> allows.
            if (auto *ts = dynamic_cast<TimeSignatureEvent *>(it.value()))
                in.timeSigs.append({ ts->midiTime(), ts->num(),
                                     1 << qBound(0, ts->denom(), 6) });
        }
    }
    // Tempos.
    {
        QMultiMap<int, MidiEvent *> *map = file->tempoEvents();
        for (auto it = map->begin(); it != map->end(); ++it) {
            if (auto *tc = dynamic_cast<TempoChangeEvent *>(it.value()))
                in.tempos.append({ tc->midiTime(), static_cast<double>(tc->beatsPerQuarter()) });
        }
    }
    // Key signatures live on the meta channel (16).
    {
        QMultiMap<int, MidiEvent *> *map = file->channel(16)->eventMap();
        for (auto it = map->begin(); it != map->end(); ++it) {
            if (auto *ks = dynamic_cast<KeySignatureEvent *>(it.value()))
                in.keySigs.append({ ks->midiTime(), ks->tonality(), ks->minor() });
        }
    }

    // Notes, bucketed by track (channels 0..15 only), and the channel each
    // track plays on: the one with most of its notes. The part's instrument is
    // the program in effect on that channel at its first note there - not the
    // first program change the track owns: Fix X|V Channels gives every track
    // a program change for every channel, so that one belonged to channel 0
    // and every part came out with channel 0's instrument.
    struct Acc {
        QList<RawNote> notes;
        QHash<int, int> notesPerChannel;
        QHash<int, int> firstTickOnChannel;
    };
    QHash<MidiTrack *, Acc> acc;
    for (int ch = 0; ch < 16; ++ch) {
        QMultiMap<int, MidiEvent *> *map = file->channel(ch)->eventMap();
        for (auto it = map->begin(); it != map->end(); ++it) {
            auto *on = dynamic_cast<NoteOnEvent *>(it.value());
            if (!on) continue;
            OffEvent *off = on->offEvent();
            if (!off) continue;
            Acc &a = acc[on->track()];
            a.notes.append({ on->midiTime(), off->midiTime() - on->midiTime(),
                             on->note(), on->velocity() });
            ++a.notesPerChannel[ch];
            if (!a.firstTickOnChannel.contains(ch)) // map order: the earliest
                a.firstTickOnChannel.insert(ch, on->midiTime());
        }
    }

    // Emit parts in track order; skip tracks with no notes.
    QList<MidiTrack *> *tracks = file->tracks();
    int idx = 0;
    for (MidiTrack *tr : *tracks) {
        ++idx;
        if (!tr || !acc.contains(tr)) continue;
        const Acc &a = acc[tr];
        if (a.notes.isEmpty()) continue;
        RawPart rp;
        rp.name = tr->name();
        if (rp.name.isEmpty()) rp.name = QStringLiteral("Track %1").arg(idx);
        int channel = 0, most = -1;
        for (int ch = 0; ch < 16; ++ch) { // ties: the lowest channel
            const int n = a.notesPerChannel.value(ch, 0);
            if (n > most) { most = n; channel = ch; }
        }
        const int program = file->channel(channel)->progAtTick(a.firstTickOnChannel.value(channel));
        rp.channel = channel;
        rp.program = program >= 0 ? program : 0;
        rp.notes = a.notes;
        in.parts.append(rp);
    }
    return in;
}

Score build(MidiFile *file) {
    return buildScore(extractInput(file));
}

} // namespace score
