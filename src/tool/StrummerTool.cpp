/*
 * MidiEditor
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "StrummerTool.h"

#include "../midi/MidiFile.h"
#include "../MidiEvent/NoteOnEvent.h"
#include "../MidiEvent/OffEvent.h"
#include "../midi/MidiTrack.h"
#include "../protocol/Protocol.h"
#include "Selection.h"
#include "StandardTool.h"
#include <QMap>
#include <algorithm>

StrummerTool::StrummerTool()
    : EventTool() {
    setToolTipText("Strummer");
}

StrummerTool::StrummerTool(StrummerTool &other)
    : EventTool(other) {
}

bool StrummerTool::release() {
    return true;
}

ProtocolEntry *StrummerTool::copy() {
    return new StrummerTool(*this);
}

void StrummerTool::reloadState(ProtocolEntry *entry) {
    EventTool::reloadState(entry);
}

bool StrummerTool::showsSelection() {
    return true;
}

void StrummerTool::performStrum(int startStrengthMs, double startTension, int endStrengthMs, double endTension, int velocityStrength, double velocityTension, bool preserveEnd, bool alternateDirection, bool useStepStrength, bool ignoreTrack) {
    QList<MidiEvent *> eventsToProcess = Selection::instance()->selectedEvents();

    if (eventsToProcess.isEmpty()) {
        return;
    }

    // Filter to only NoteOnEvents and Group by Track
    QMap<MidiTrack *, QList<NoteOnEvent *>> notesByTrack;
    
    for (MidiEvent *event: eventsToProcess) {
        NoteOnEvent *noteOn = dynamic_cast<NoteOnEvent *>(event);
        if (noteOn && noteOn->offEvent()) {
            if (ignoreTrack) {
                notesByTrack[nullptr].append(noteOn);
            } else {
                notesByTrack[noteOn->track()].append(noteOn);
            }
        }
    }

    if (notesByTrack.isEmpty()) {
        return;
    }

    // Start protocol action
    currentProtocol()->startNewAction(QObject::tr("Strum notes"), image());

    // Process each track independently
    for (auto it = notesByTrack.begin(); it != notesByTrack.end(); ++it) {
        QList<NoteOnEvent *> &notes = it.value();
        
        if (notes.isEmpty()) continue;

        // 1. Sort notes in this track by time to facilitate grouping
        std::sort(notes.begin(), notes.end(), [](NoteOnEvent *a, NoteOnEvent *b) {
            if (a->midiTime() != b->midiTime())
                return a->midiTime() < b->midiTime();
            return a->note() < b->note();
        });

        // 2. Group notes into chords
        // WHY: a chord is a set of notes that start together. Grouping by overlap let one
        // sustained note swallow every later chord that starts while it rings, so those chords
        // were re-sorted and re-timed as one big cluster. Group by onset proximity instead: a
        // note joins the chord when it starts within a 32nd note of the chord's first onset.
        QList<QList<NoteOnEvent *>> chords;
        if (!notes.isEmpty()) {
            const int onsetTolerance = qMax(1, file()->ticksPerQuarter() / 8);
            QList<NoteOnEvent *> currentChord;
            currentChord.append(notes[0]);
            int currentChordStart = notes[0]->midiTime();

            for (int i = 1; i < notes.size(); i++) {
                NoteOnEvent *note = notes[i];
                if (note->midiTime() - currentChordStart <= onsetTolerance) {
                    currentChord.append(note);
                } else {
                    // Later onset, finalize current chord and start new one
                    chords.append(currentChord);
                    currentChord.clear();
                    currentChord.append(note);
                    currentChordStart = note->midiTime();
                }
            }
            if (!currentChord.isEmpty()) {
                chords.append(currentChord);
            }
        }

        // 3. Process each chord in this track
        bool currentDirectionUp = (startStrengthMs >= 0);
        
        for (QList<NoteOnEvent *> &chord : chords) {
            if (chord.size() > 1) {
                strumChord(chord, std::abs(startStrengthMs), startTension, endStrengthMs, endTension, velocityStrength, velocityTension, preserveEnd, currentDirectionUp, useStepStrength);
                
                if (alternateDirection) {
                    currentDirectionUp = !currentDirectionUp;
                }
            }
        }
    }

    currentProtocol()->endAction();
}

void StrummerTool::strumChord(QList<NoteOnEvent *> &chordNotes, int startStrengthMs, double startTension, int endStrengthMs, double endTension, int velocityStrength, double velocityTension, bool preserveEnd, bool directionUp, bool useStepStrength) {
    // Sort notes by pitch based on direction
    if (directionUp) {
        // Up: Low pitch -> High pitch
        std::sort(chordNotes.begin(), chordNotes.end(), [](NoteOnEvent *a, NoteOnEvent *b) {
            return a->note() < b->note();
        });
    } else {
        // Down: High pitch -> Low pitch
        std::sort(chordNotes.begin(), chordNotes.end(), [](NoteOnEvent *a, NoteOnEvent *b) {
            return a->note() > b->note();
        });
    }

    int count = chordNotes.size();
    if (count < 2) return;

    for (int i = 0; i < count; i++) {
        NoteOnEvent *note = chordNotes[i];
        
        // Calculate ms offsets
        int startOffsetMs = calculateOffset(i, count, startStrengthMs, startTension, useStepStrength);
        int endOffsetMs = calculateOffset(i, count, endStrengthMs, endTension, useStepStrength);
        
        // Calculate velocity offset
        int velOffset = calculateOffset(i, count, velocityStrength, velocityTension, false); // Velocity always total range? Or should it be step? Usually total range makes sense for velocity (crescendo over chord).
        
        int oldStartTick = note->midiTime();
        int oldEndTick = note->offEvent()->midiTime();
        
        // Convert to MS
        int oldStartMs = file()->timeMS(oldStartTick);
        int oldEndMs = file()->timeMS(oldEndTick);
        
        int newStartMs = oldStartMs + startOffsetMs;
        int newEndMs;
        
        if (preserveEnd) {
            // End stays fixed, only start moves
            // Ensure note has at least 1 ms duration
            if (newStartMs >= oldEndMs) {
                newStartMs = oldEndMs - 1;
            }
            newEndMs = oldEndMs;
        } else {
            // End moves with start (to preserve length) + end offset
            newEndMs = oldEndMs + startOffsetMs + endOffsetMs;
        }
        
        // Convert back to ticks
        int newStartTick = file()->tick(newStartMs);
        int newEndTick = file()->tick(newEndMs);
        
        // Ensure valid note length in ticks
        if (newStartTick >= newEndTick) {
            newEndTick = newStartTick + 1;
        }
        
        // Apply changes
        note->setMidiTime(newStartTick);
        note->offEvent()->setMidiTime(newEndTick);
        
        // Apply velocity changes
        if (velocityStrength != 0) {
            int newVel = note->velocity() + velOffset;
            // Clamp velocity to 1-127 (0 is note off)
            newVel = qBound(1, newVel, 127);
            note->setVelocity(newVel);
        }
    }
}

int StrummerTool::calculateOffset(int index, int count, int strength, double tension, bool useStepStrength) {
    if (count <= 1) return 0;
    if (strength == 0) return 0;
    
    if (useStepStrength) {
        strength = strength * (count - 1);
    }
    
    double t = (double)index / (double)(count - 1);
    
    // Apply tension
    // Map tension [-1, 1] to exponent
    // 0 -> 1 (Linear)
    // 1 -> 2 (Quadratic)
    // -1 -> 0.5 (Square root)
    // Formula: exponent = 2^tension
    double exponent = std::pow(2.0, tension);
    
    double t_curved = std::pow(t, exponent);
    
    return (int)(strength * t_curved);
}
