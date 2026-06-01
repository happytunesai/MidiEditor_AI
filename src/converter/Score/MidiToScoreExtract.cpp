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
#include "../../MidiEvent/ProgChangeEvent.h"
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
            if (auto *ts = dynamic_cast<TimeSignatureEvent *>(it.value()))
                in.timeSigs.append({ ts->midiTime(), ts->num(), ts->denom() });
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

    // Notes + program, bucketed by track (channels 0..15 only).
    struct Acc {
        QList<RawNote> notes;
        int  program = 0; bool hasProg = false;
        int  channel = 0; bool hasChan = false;
    };
    QHash<MidiTrack *, Acc> acc;
    for (int ch = 0; ch < 16; ++ch) {
        QMultiMap<int, MidiEvent *> *map = file->channel(ch)->eventMap();
        for (auto it = map->begin(); it != map->end(); ++it) {
            MidiEvent *e = it.value();
            if (auto *on = dynamic_cast<NoteOnEvent *>(e)) {
                OffEvent *off = on->offEvent();
                if (!off) continue;
                MidiTrack *tr = on->track();
                Acc &a = acc[tr];
                a.notes.append({ on->midiTime(), off->midiTime() - on->midiTime(),
                                 on->note(), on->velocity() });
                if (!a.hasChan) { a.channel = ch; a.hasChan = true; }
            } else if (auto *pc = dynamic_cast<ProgChangeEvent *>(e)) {
                Acc &a = acc[pc->track()];
                if (!a.hasProg) {
                    a.program = pc->program(); a.hasProg = true;
                    if (!a.hasChan) { a.channel = ch; a.hasChan = true; }
                }
            }
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
        rp.channel = a.channel;
        rp.program = a.program;
        rp.notes = a.notes;
        in.parts.append(rp);
    }
    return in;
}

Score build(MidiFile *file) {
    return buildScore(extractInput(file));
}

} // namespace score
