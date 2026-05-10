/*
 * MidiEditor AI
 *
 * Snapshot helper: convert a MidiFile to the QJsonArray representation
 * consumed by MidiDiff.
 *
 * The output format matches MidiEventSerializer::serialize() byte-for-byte
 * (note / cc / pitch_bend / program events; OffEvents implicit via the
 * paired NoteOn's duration). Meta-channel events (Tempo / TimeSig /
 * KeySig / Text) are not yet covered — they will be added when
 * MidiEventSerializer is extended.
 *
 * Lives in src/collab so that the runtime dependency direction stays
 * clean: collab depends on midi/MidiEvent/ai/MidiEventSerializer, but
 * not the other way round.
 */

#ifndef MIDISNAPSHOT_H
#define MIDISNAPSHOT_H

#include <QJsonArray>

class MidiFile;

class MidiSnapshot {
public:
    /**
     * \brief Serialize every supported event in \a file to a QJsonArray.
     *
     * Iterates channels 0–15 (meta channels skipped for v1) and gathers
     * every MidiEvent on each channel. Returns an empty array on a null
     * file or one with no supported events.
     */
    static QJsonArray ofFile(MidiFile *file);
};

#endif // MIDISNAPSHOT_H
