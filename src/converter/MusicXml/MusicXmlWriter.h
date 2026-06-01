/*
 * MidiEditor AI - MusicXML export writer (Phase 43, 1.8.0).
 *
 * Serialises an engraved score::Score (from MidiToScore) into MusicXML 4.0
 * score-partwise text. Plain text (.musicxml); the compressed .mxl container is
 * deferred. Output opens in MuseScore / Finale / Sibelius / Dorico.
 */
#ifndef MUSICXMLWRITER_H
#define MUSICXMLWRITER_H

#include <QByteArray>

namespace score { struct Score; }

class MusicXmlWriter {
public:
    /// Serialise @p score to MusicXML 4.0 partwise bytes (UTF-8).
    static QByteArray write(const score::Score &score);
};

#endif // MUSICXMLWRITER_H
