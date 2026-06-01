/*
 * MidiEditor AI - notation export IR (Phase 43, 1.8.0).
 *
 * A small intermediate representation that sits between the in-memory MidiFile
 * and a notation writer (MusicXML now; Guitar Pro later). It is richer than the
 * MusicXML *import* model (MusicXmlModels.h, which is near-MIDI-flat): it carries
 * measures, rests, note values + dots, ties, chords, clefs and enharmonic
 * spelling - the things MIDI doesn't store and a score writer needs.
 *
 * Plain C++/Qt-containers only (no GUI), so it is unit-testable in isolation.
 */
#ifndef SCOREMODEL_H
#define SCOREMODEL_H

#include <QList>
#include <QString>

namespace score {

// ---- raw input (a MidiFile flattened to what the engraver needs) -----------

struct RawNote {
    int startTick = 0;
    int durTicks  = 0;   // endTick - startTick (>0)
    int pitch     = 60;  // MIDI 0..127
    int velocity  = 80;
};

struct RawPart {
    QString        name;
    int            channel = 0;   // 0..15 (used for the MIDI instrument hint)
    int            program = 0;   // GM 0..127
    QList<RawNote> notes;
};

struct MetaTimeSig { int tick = 0; int numerator = 4; int denominator = 4; };
struct MetaTempo   { int tick = 0; double bpm = 120.0; };
struct MetaKey     { int tick = 0; int fifths = 0; bool minor = false; };

// The full engraver input. build(MidiFile*) fills this; tests build it directly.
struct ScoreInput {
    QString            title;
    int                divisions = 480;   // ticks per quarter (== MusicXML divisions)
    int                endTick   = 0;     // length to engrave to (>= last note end)
    QList<MetaTimeSig> timeSigs;
    QList<MetaTempo>   tempos;
    QList<MetaKey>     keySigs;
    QList<RawPart>     parts;
};

// ---- engraved output -------------------------------------------------------

// MusicXML note-type names map 1:1 to this enum's spelling.
enum class NoteType { Whole, Half, Quarter, Eighth, Sixteenth };

inline const char *noteTypeName(NoteType t) {
    switch (t) {
        case NoteType::Whole:     return "whole";
        case NoteType::Half:      return "half";
        case NoteType::Quarter:   return "quarter";
        case NoteType::Eighth:    return "eighth";
        case NoteType::Sixteenth: return "16th";
    }
    return "quarter";
}

enum class Clef { Treble, Bass };

// One notated element in a voice: a rest, a note, or a chord member.
struct ScoreEvent {
    int      durDivs   = 0;          // duration in divisions
    NoteType type      = NoteType::Quarter;
    int      dots      = 0;          // 0 or 1
    bool     isRest    = false;
    bool     isChord   = false;      // MusicXML <chord/> - stacks on the previous note
    // pitch (ignored when isRest):
    char     step      = 'C';        // 'A'..'G'
    int      alter     = 0;          // -1 flat, 0 natural, +1 sharp
    int      octave    = 4;          // MusicXML octave (middle C = C4)
    int      velocity  = 80;
    // ties (a note split across a barline or note-value boundary):
    bool     tieStart  = false;
    bool     tieStop   = false;
};

struct ScoreMeasure {
    int                number   = 1;   // 1-based
    int                divLen   = 0;   // total divisions in the measure
    int                beats    = 4;   // time-sig numerator in effect
    int                beatType = 4;   // time-sig denominator in effect
    bool               timeChanged = true;  // emit <time> in <attributes>?
    bool               keyChanged  = false; // emit <key> in <attributes>?
    int                keyFifths   = 0;
    bool               keyMinor    = false;
    double             tempoBpm    = 0.0;    // >0 => emit a <sound tempo>/<direction>
    QList<ScoreEvent>  voice;          // v1: a single voice per part
};

struct ScorePart {
    QString             name;
    int                 channel  = 0;
    int                 program  = 0;
    Clef                clef     = Clef::Treble;
    QList<ScoreMeasure> measures;
};

struct Score {
    QString           title;
    int               divisions = 480;
    QList<ScorePart>  parts;
};

} // namespace score

#endif // SCOREMODEL_H
