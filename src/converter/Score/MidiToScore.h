/*
 * MidiEditor AI - MIDI -> notation engraver (Phase 43, 1.8.0).
 *
 * Reconstructs notation (measures, note values, rests, ties, chords, enharmonic
 * spelling) from flat MIDI - the work a notation export needs because MIDI does
 * not store any of it. Output is a score::Score that a writer (MusicXML, later
 * Guitar Pro) serialises.
 *
 * buildScore() is pure (takes a ScoreInput) so it can be unit-tested without a
 * MidiFile; build() is the app-facing convenience that extracts a ScoreInput
 * from the live MidiFile first.
 */
#ifndef MIDITOSCORE_H
#define MIDITOSCORE_H

#include "ScoreModel.h"

class MidiFile;

namespace score {

/// Pure engraver: ScoreInput -> engraved Score. Unit-testable.
Score buildScore(const ScoreInput &in);

/// Flatten the live MidiFile into a ScoreInput (notes per track, meta, etc.).
ScoreInput extractInput(MidiFile *file);

/// Convenience: extractInput(file) then buildScore(...).
Score build(MidiFile *file);

} // namespace score

#endif // MIDITOSCORE_H
