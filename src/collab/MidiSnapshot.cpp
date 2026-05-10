/*
 * MidiEditor AI - MidiSnapshot implementation.
 */

#include "MidiSnapshot.h"

#include <QList>
#include <QMultiMap>

#include "../ai/MidiEventSerializer.h"
#include "../midi/MidiChannel.h"
#include "../midi/MidiFile.h"
#include "../MidiEvent/MidiEvent.h"

QJsonArray MidiSnapshot::ofFile(MidiFile *file) {
    if (!file) return QJsonArray();

    // Channels 0–15 carry instrument events; channel 16 is the general
    // meta bin (text, key-sig); channel 17 holds tempo changes; channel
    // 18 holds time-signature events. All four ranges need to ride the
    // wire for live-sync to cover BPM / time-sig edits (Plan §11.10j).
    QList<MidiEvent *> all;
    for (int ch = 0; ch < 19; ++ch) {
        MidiChannel *channel = file->channel(ch);
        if (!channel) continue;
        QMultiMap<int, MidiEvent *> *map = channel->eventMap();
        if (!map) continue;
        for (auto it = map->begin(); it != map->end(); ++it) {
            all.append(it.value());
        }
    }

    return MidiEventSerializer::serialize(all, file);
}
