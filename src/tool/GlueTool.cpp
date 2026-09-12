/*
 * MidiEditor
 * Copyright (C) 2010  Markus Schwenk
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "GlueTool.h"

#include "../MidiEvent/MidiEvent.h"
#include "../MidiEvent/NoteOnEvent.h"
#include "../MidiEvent/OffEvent.h"
#include "../gui/MatrixWidget.h"
#include "../midi/MidiChannel.h"
#include "../midi/MidiFile.h"
#include "../midi/MidiTrack.h"
#include "../protocol/Protocol.h"
#include "Selection.h"
#include "StandardTool.h"

#include <QMap>
#include <algorithm>

GlueTool::GlueTool()
    : EventTool() {
    setImage(":/run_environment/graphics/tool/glue.png");
    setToolTipText("Glue notes");
}

GlueTool::GlueTool(GlueTool &other)
    : EventTool(other) {
}

void GlueTool::draw(QPainter *painter) {
    paintSelectedEvents(painter);
}

bool GlueTool::press(bool leftClick) {
    Q_UNUSED(leftClick);
    return true;
}

bool GlueTool::release() {
    if (!file()) {
        return false;
    }

    performGlueOperation();

    // Return to standard tool if set
    if (_standardTool) {
        Tool::setCurrentTool(_standardTool);
        _standardTool->move(mouseX, mouseY);
        _standardTool->release();
    }

    return true;
}

ProtocolEntry *GlueTool::copy() {
    return new GlueTool(*this);
}

void GlueTool::reloadState(ProtocolEntry *entry) {
    EventTool::reloadState(entry);
    GlueTool *other = dynamic_cast<GlueTool *>(entry);
    if (!other) {
        return;
    }
}

bool GlueTool::showsSelection() {
    return true;
}

void GlueTool::performGlueOperation(bool respectChannels) {
    // Only work on selected events
    QList<MidiEvent *> eventsToProcess = Selection::instance()->selectedEvents();

    if (eventsToProcess.isEmpty()) {
        return; // Nothing to glue
    }

    // Group notes by pitch and track, optionally by channel
    QMap<QString, QList<NoteOnEvent *> > noteGroups = groupNotes(eventsToProcess, respectChannels);

    if (noteGroups.isEmpty()) {
        return;
    }

    // Start protocol action
    currentProtocol()->startNewAction(QObject::tr("Glue notes"), image());

    bool anyNotesGlued = false;

    // Process each group
    for (auto it = noteGroups.begin(); it != noteGroups.end(); ++it) {
        QList<NoteOnEvent *> notes = it.value();

        if (notes.size() < 2) continue;

        // Sort notes by start time
        std::sort(notes.begin(), notes.end(), [](NoteOnEvent *a, NoteOnEvent *b) {
            return a->midiTime() < b->midiTime();
        });

        // In Cubase, all selected notes of the same pitch get merged into one note
        // No need to check for adjacency - just merge all notes in the group
        // Only a group that really merged may claim the selection wipe below:
        // mergeNoteGroup() bails out on a group whose first note has no off event.
        if (mergeNoteGroup(notes)) {
            anyNotesGlued = true;
        }
    }

    if (anyNotesGlued) {
        // Clear selection since some notes were deleted. This has to run BEFORE
        // endAction(): with no step open the Protocol drops the item (leaking
        // its Selection copy) and the clear becomes invisible to undo.
        Selection::instance()->clearSelection();
    }

    currentProtocol()->endAction();
}

QMap<QString, QList<NoteOnEvent *> > GlueTool::groupNotes(const QList<MidiEvent *> &events, bool respectChannels) {
    QMap<QString, QList<NoteOnEvent *> > groups;

    for (MidiEvent *event: events) {
        NoteOnEvent *noteOn = dynamic_cast<NoteOnEvent *>(event);
        if (!noteOn) continue;

        // Create grouping key: always include pitch and track
        QString key = QString("pitch_%1_track_%2").arg(noteOn->note()).arg((quintptr) noteOn->track());

        // Optionally include channel
        if (respectChannels) {
            key += QString("_channel_%1").arg(noteOn->channel());
        }

        groups[key].append(noteOn);
    }

    return groups;
}


bool GlueTool::mergeNoteGroup(const QList<NoteOnEvent *> &noteGroup) {
    if (noteGroup.size() < 2) return false;

    NoteOnEvent *firstNote = noteGroup.first();
    if (!firstNote->offEvent()) return false;

    // The merged note ends at the LATEST off across the whole group - not the
    // last note by start order. A long note followed by a shorter one would
    // otherwise truncate the merge to the short note's end (data loss).
    int newEndTime = firstNote->offEvent()->midiTime();
    for (NoteOnEvent *n : noteGroup) {
        if (n->offEvent() && n->offEvent()->midiTime() > newEndTime)
            newEndTime = n->offEvent()->midiTime();
    }

    // Extend the first note to the new end time
    firstNote->offEvent()->setMidiTime(newEndTime);

    // Remove all other notes in the group
    for (int i = 1; i < noteGroup.size(); i++) {
        NoteOnEvent *noteToRemove = noteGroup[i];

        // Remove from selection if selected
        deselectEvent(noteToRemove);

        // Remove the note from the channel. MidiChannel::removeEvent() cascades
        // from the OnEvent to its paired OffEvent, so removing the off event a
        // second time only buys another full channel-map undo snapshot.
        MidiChannel *channel = file()->channel(noteToRemove->channel());
        channel->removeEvent(noteToRemove);
    }
    return true;
}
