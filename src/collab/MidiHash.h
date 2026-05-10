/*
 * MidiEditor AI
 *
 * SHA-256 hash of a MIDI file on disk. The hash identifies a file's exact
 * byte content and is used as the commit ID in the collaboration history.
 *
 * Hashing the file on disk (rather than re-serializing the in-memory
 * MidiFile) avoids any non-determinism from the serializer and matches
 * what other tools that read the .mid would see.
 */

#ifndef MIDIHASH_H
#define MIDIHASH_H

#include <QString>

class MidiHash {
public:
    /**
     * \brief Compute the SHA-256 of the file at \a path, hex-encoded.
     *
     * Returns an empty string if the file cannot be opened.
     * Streams the file in chunks, so large MIDI files are handled
     * without loading the whole content into memory.
     */
    static QString sha256OfFile(const QString &path);
};

#endif // MIDIHASH_H
