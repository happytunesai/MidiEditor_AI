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

#include "EventTool.h"

#include "../MidiEvent/MidiEvent.h"
#include "../MidiEvent/NoteOnEvent.h"
#include "../MidiEvent/OffEvent.h"
#include "../MidiEvent/OnEvent.h"
#include "../MidiEvent/TempoChangeEvent.h"
#include "../MidiEvent/TimeSignatureEvent.h"
#include "../MidiEvent/KeySignatureEvent.h"
#include "../gui/EventWidget.h"
#include "../gui/MainWindow.h"
#include "../gui/MatrixWidget.h"
#include "../gui/Appearance.h"
#include "../gui/ChannelVisibilityManager.h"
#include "../midi/MidiChannel.h"
#include "../midi/MidiFile.h"
#include "../midi/MidiPlayer.h"
#include "../midi/MidiTrack.h"
#include "../protocol/Protocol.h"
#include "../gui/PasteSpecialDialog.h"
#include "NewNoteTool.h"
#include "Selection.h"
#include "SharedClipboard.h"
#include "SnapMath.h"

#include <set>
#include <vector>
#include <cmath>
#include <QSet>
#include <QStatusBar>

QList<MidiEvent *> *EventTool::copiedEvents = new QList<MidiEvent *>;
int EventTool::_copiedTicksPerQuarter = 0;
QPointer<MidiFile> EventTool::_copiedSourceFile;

int EventTool::_pasteChannel = -1;
// -1 = "Keep track" (the default): same-file pastes stay on their source
// track. -2 = "Same as selected for new events": explicitly routes the paste
// onto the current edit track - it must be distinct from the default, or the
// explicit menu choice is dead for same-file pastes (review finding, v2.0).
int EventTool::_pasteTrack = -1;

bool EventTool::_magnet = false;
// Default to Modern (hard grid) snap; MainWindow overrides from the saved
// "snap_mode" setting at startup.
bool EventTool::_modernSnap = true;

EventTool::EventTool()
    : EditorTool() {
}

EventTool::EventTool(EventTool &other)
    : EditorTool(other) {
}

void EventTool::selectEvent(MidiEvent *event, bool single, bool ignoreStr, bool setSelection) {
    if (!ChannelVisibilityManager::instance().isChannelVisible(event->channel())) {
        return;
    }

    if (event->track()->hidden()) {
        return;
    }

    QList<MidiEvent *> selected = Selection::instance()->selectedEvents();

    OffEvent *offevent = dynamic_cast<OffEvent *>(event);
    if (offevent) {
        return;
    }

    if (single && !QApplication::keyboardModifiers().testFlag(Qt::ShiftModifier) && (!QApplication::keyboardModifiers().testFlag(Qt::ControlModifier) || ignoreStr)) {
        selected.clear();
        NoteOnEvent *on = dynamic_cast<NoteOnEvent *>(event);
        if (on) {
            MidiPlayer::play(on);
        }
    }
    if (!selected.contains(event) && (!QApplication::keyboardModifiers().testFlag(Qt::ControlModifier) || ignoreStr)) {
        selected.append(event);
    } else if (QApplication::keyboardModifiers().testFlag(Qt::ControlModifier) && !ignoreStr) {
        selected.removeAll(event);
    }

    if (setSelection) {
        Selection::instance()->setSelection(selected);
    }
    _mainWindow->eventWidget()->reportSelectionChangedByTool();
}

void EventTool::deselectEvent(MidiEvent *event) {
    QList<MidiEvent *> selected = Selection::instance()->selectedEvents();
    selected.removeAll(event);
    Selection::instance()->setSelection(selected);

    if (_mainWindow->eventWidget()->events().contains(event)) {
        _mainWindow->eventWidget()->removeEvent(event);
    }
}

void EventTool::clearSelection() {
    Selection::instance()->clearSelection();
    _mainWindow->eventWidget()->reportSelectionChangedByTool();
}

void EventTool::batchSelectEvents(const QList<MidiEvent *> &events) {
    if (events.isEmpty()) {
        return;
    }

    // Build a NEW local list — do NOT modify _selectedEvents via reference,
    // otherwise setSelection()'s early-return check always sees them as equal.
    QList<MidiEvent *> newSelection;
    newSelection.reserve(events.size());

    // Add all valid events to selection in batch
    foreach(MidiEvent* event, events) {
        // Check visibility using the global visibility manager
        if (!ChannelVisibilityManager::instance().isChannelVisible(event->channel())) {
            continue;
        }

        if (event->track()->hidden()) {
            continue;
        }

        // Skip OffEvents
        OffEvent *offevent = dynamic_cast<OffEvent *>(event);
        if (offevent) {
            continue;
        }

        newSelection.append(event);
    }

    // Update selection state once at the end
    Selection::instance()->setSelection(newSelection);
    _mainWindow->eventWidget()->reportSelectionChangedByTool();
}

void EventTool::paintSelectedEvents(QPainter *painter) {
    foreach(MidiEvent* event, Selection::instance()->selectedEvents()) {
        bool show = event->shown();

        if (!show) {
            OnEvent *ev = dynamic_cast<OnEvent *>(event);
            if (ev) {
                show = ev->offEvent() && ev->offEvent()->shown();
            }
        }

        if (event->track()->hidden()) {
            show = false;
        }
        if (!ChannelVisibilityManager::instance().isChannelVisible(event->channel())) {
            show = false;
        }

        if (show) {
            painter->setBrush(Appearance::noteSelectionColor());
            painter->setPen(Appearance::selectionBorderColor());
            painter->drawRoundedRect(event->x(), event->y(), event->width(),
                                     event->height(), 1, 1);
        }
    }
}

void EventTool::changeTick(MidiEvent *event, int shiftX) {
    // TODO: falls event gezeigt ist, über matrixWidget tick erfragen (effizienter)
    //int newMs = matrixWidget->msOfXPos(event->x()-shiftX);

    int newMs = file()->msOfTick(event->midiTime()) - matrixWidget->timeMsOfWidth(shiftX);
    int tick = file()->tick(newMs);

    if (tick < 0) {
        tick = 0;
    }

    // with magnet: set to div value if pixel refers to this tick
    if (magnetEnabled()) {
        int newX = matrixWidget->xPosOfMs(newMs);
        typedef QPair<int, int> TMPPair;
        foreach(TMPPair p, matrixWidget->divs()) {
            int xt = p.first;
            if (newX == xt) {
                tick = p.second;
                break;
            }
        }
    }
    event->setMidiTime(tick);
}

void EventTool::copyAction() {
    if (Selection::instance()->selectedEvents().size() > 0) {
        // clear old copied Events
        qDeleteAll(*copiedEvents);
        copiedEvents->clear();

        // Store source file's ticksPerQuarter before copying events
        MidiFile *sourceFile = currentFile();
        _copiedTicksPerQuarter = sourceFile ? sourceFile->ticksPerQuarter() : 192;
        _copiedSourceFile = sourceFile;

        foreach(MidiEvent* event, Selection::instance()->selectedEvents()) {
            // add the current Event
            MidiEvent *ev = dynamic_cast<MidiEvent *>(event->copy());
            if (ev) {
                // do not append off event here
                OffEvent *off = dynamic_cast<OffEvent *>(ev);
                if (!off) {
                    copiedEvents->append(ev);
                }
            }

            // if its onEvent, add a copy of the OffEvent
            OnEvent *onEv = dynamic_cast<OnEvent *>(ev);
            if (onEv) {
                OffEvent *offEv = dynamic_cast<OffEvent *>(onEv->offEvent()->copy());
                if (offEv) {
                    offEv->setOnEvent(onEv);
                    copiedEvents->append(offEv);
                }
            }
        }

        // Copy to shared clipboard BEFORE detaching file pointers
        copyToSharedClipboard();

        // Detach copied events from source file so stale pointers can't be dereferenced
        foreach(MidiEvent* event, *copiedEvents) {
            event->setFile(nullptr);
        }

        _mainWindow->copiedEventsChanged();
    }
}

void EventTool::pasteAction() {
    // Check if shared clipboard has newer data from a different process
    bool hasSharedData = hasSharedClipboardData();
    bool hasLocalData = (copiedEvents->size() > 0);

    if (hasSharedData) {
        // Always prefer shared clipboard data (cross-instance) when available
        if (pasteFromSharedClipboard()) {
            return;
        }
    }

    if (hasLocalData) {
        // Continue with local clipboard paste logic below
    } else {
        return;
    }

    // TODO what happens to TempoEvents??

    // copy copied events to insert unique events
    QList<MidiEvent *> copiedCopiedEvents;
    foreach(MidiEvent* event, *copiedEvents) {
        // add the current Event
        MidiEvent *ev = dynamic_cast<MidiEvent *>(event->copy());
        if (ev) {
            // do not append off event here
            OffEvent *off = dynamic_cast<OffEvent *>(ev);
            if (!off) {
                copiedCopiedEvents.append(ev);
            }
        }

        // if its onEvent, add a copy of the OffEvent
        OnEvent *onEv = dynamic_cast<OnEvent *>(ev);
        if (onEv && onEv->offEvent()) {
            OffEvent *offEv = dynamic_cast<OffEvent *>(onEv->offEvent()->copy());
            if (offEv) {
                offEv->setOnEvent(onEv);
                copiedCopiedEvents.append(offEv);
            }
        }
    }

    if (copiedCopiedEvents.count() > 0) {
        // Begin a new ProtocolAction
        currentFile()->protocol()->startNewAction(QObject::tr("Paste ") + QString::number(copiedCopiedEvents.count()) + QObject::tr(" events"));

        double tickscale = 1;
        if (_copiedTicksPerQuarter > 0 && currentFile()->ticksPerQuarter() != _copiedTicksPerQuarter) {
            tickscale = ((double) (currentFile()->ticksPerQuarter())) / ((double) _copiedTicksPerQuarter);
        }

        // get first Tick of the copied events
        int firstTick = -1;
        foreach(MidiEvent* event, copiedCopiedEvents) {
            if ((int) (tickscale * event->midiTime()) < firstTick || firstTick < 0) {
                firstTick = (int) (tickscale * event->midiTime());
            }
        }

        if (firstTick < 0)
            firstTick = 0;

        // calculate the difference of old/new events in MidiTicks
        int diff = currentFile()->cursorTick() - firstTick;

        // set the Positions and add the Events to the channels
        clearSelection();

        std::sort(copiedCopiedEvents.begin(), copiedCopiedEvents.end(), [](MidiEvent *a, MidiEvent *b) {return a->midiTime() < b->midiTime();});

        std::vector<std::pair<ProtocolEntry *, ProtocolEntry *> > channelCopies;
        std::set<int> copiedChannels;

        // Determine which channels are associated with the pasted events and copy them
        for (auto event: copiedCopiedEvents) {
            // get channel
            int channelNum = event->channel();
            if (_pasteChannel == -2) {
                channelNum = NewNoteTool::editChannel();
            }
            if ((_pasteChannel >= 0) && (channelNum < 16)) {
                channelNum = _pasteChannel;
            }

            if (copiedChannels.find(channelNum) == copiedChannels.end()) {
                MidiChannel *channel = currentFile()->channel(channelNum);
                ProtocolEntry *channelCopy = channel->copy();
                channelCopies.push_back(std::make_pair(channelCopy, channel));
                copiedChannels.insert(channelNum);
            }
        }

        // Build a NEW local selection list — do NOT rely on selectEvent() mutating
        // the internal Selection list through a reference. Since v1.3.2,
        // Selection::selectedEvents() returns by value, so selectEvent(..., setSelection=false)
        // would only modify throwaway local copies. We accumulate pasted events here and
        // call setSelection() once at the end. (PASTE-001)
        QList<MidiEvent *> pastedSelection;

        for (auto it = copiedCopiedEvents.rbegin(); it != copiedCopiedEvents.rend(); it++) {
            MidiEvent *event = *it;

            // get channel
            int channelNum = event->channel();
            if (_pasteChannel == -2) {
                channelNum = NewNoteTool::editChannel();
            }
            if ((_pasteChannel >= 0) && (channelNum < 16)) {
                channelNum = _pasteChannel;
            }

            // get track
            MidiTrack *track = event->track();
            if ((pasteTrack() >= 0) && (pasteTrack() < currentFile()->tracks()->size())) {
                // Explicit "paste to track N" override (Edit > Paste to track).
                track = currentFile()->track(pasteTrack());
            } else if (pasteTrack() == -2) {
                // Explicit "Same as selected for new events": route onto the
                // current edit track. Checked BEFORE the keep-branch so the
                // explicit choice also applies to same-file pastes (with the
                // old -2 DEFAULT this ordering collapsed every paste onto
                // track 0 - PASTE-TRACK-001 - which is why the default is now
                // -1/keep and -2 means only the deliberate menu choice).
                track = currentFile()->track(NewNoteTool::editTrack());
            } else if (track && currentFile()->tracks()->contains(track)) {
                // Default (-1 "Keep track") same-file paste: KEEP the event's
                // own track so the notes land on the track they came from.
            } else {
                // Cross-file paste or track from deleted file: use the
                // remembered source->target mapping when one exists, else the
                // current edit track (the 1.9.1 cross-file default).
                track = currentFile()->getPasteTrack(event->track(), event->file());
                if (!track) {
                    track = currentFile()->track(NewNoteTool::editTrack());
                }
                if (!track) {
                    track = currentFile()->track(0);
                }
            }

            if ((!track) || (track->file() != currentFile())) {
                track = currentFile()->track(0);
            }

            event->setFile(currentFile());
            event->setChannel(channelNum, false);
            event->setTrack(track, false);
            currentFile()->channel(channelNum)->insertEvent(event,
                                                            (int) (tickscale * event->midiTime()) + diff, false);

            // Respect channel/track visibility (same rules as selectEvent()); skip OffEvents.
            if (dynamic_cast<OffEvent *>(event)) {
                continue;
            }
            if (!ChannelVisibilityManager::instance().isChannelVisible(event->channel())) {
                continue;
            }
            if (event->track() && event->track()->hidden()) {
                continue;
            }
            pastedSelection.append(event);
        }
        Selection::instance()->setSelection(pastedSelection);
        _mainWindow->eventWidget()->reportSelectionChangedByTool();

        // Put the copied channels from before the event insertion onto the protocol stack
        for (auto channelPair: channelCopies) {
            ProtocolEntry *channel = channelPair.first;
            channel->protocol(channel, channelPair.second);
        }

        currentFile()->protocol()->endAction();
    }
}

bool EventTool::showsSelection() {
    return false;
}

void EventTool::setPasteTrack(int track) {
    _pasteTrack = track;
}

int EventTool::pasteTrack() {
    return _pasteTrack;
}

void EventTool::setPasteChannel(int channel) {
    _pasteChannel = channel;
}

int EventTool::pasteChannel() {
    return _pasteChannel;
}

int EventTool::rasteredX(int x, int *tick) {
    if (!_magnet) {
        if (tick) {
            *tick = _currentFile->tick(matrixWidget->msOfXPos(x));
        }
        return x;
    }
    int snappedX = x;
    int snappedTick = 0;
    if (SnapMath::snapToDiv(x, matrixWidget->divs(), _modernSnap, &snappedX, &snappedTick)) {
        if (tick) {
            *tick = snappedTick;
        }
        return snappedX;
    }
    // No snap (Legacy with nothing nearby, or no divisions): keep exact position.
    if (tick) {
        *tick = _currentFile->tick(matrixWidget->msOfXPos(x));
    }
    return x;
}

void EventTool::enableMagnet(bool enable) {
    _magnet = enable;
}

bool EventTool::magnetEnabled() {
    return _magnet;
}

void EventTool::setModernSnap(bool modern) {
    _modernSnap = modern;
}

bool EventTool::modernSnap() {
    return _modernSnap;
}

bool EventTool::copyToSharedClipboard() {
    SharedClipboard *clipboard = SharedClipboard::instance();
    if (!clipboard->initialize()) {
        return false;
    }

    // Use the same events that were copied to local clipboard
    if (copiedEvents->isEmpty()) {
        return false;
    }

    // Get the source file from the first event
    MidiFile *sourceFile = copiedEvents->first()->file();
    if (!sourceFile) {
        return false;
    }

    bool result = clipboard->copyEvents(*copiedEvents, sourceFile);
    return result;
}

bool EventTool::pasteFromSharedClipboard() {
    // Legacy entry: preserve existing 1.5.x behaviour by routing every
    // pasted event onto the current edit target. The Phase 34 dialog and
    // the new modes are reached via pasteFromSharedClipboardWithOptions().
    PasteSpecialOptions opts;
    opts.assignment = PasteAssignment::CurrentEditTarget;
    return pasteFromSharedClipboardWithOptions(opts);
}

bool EventTool::pasteFromSharedClipboardWithOptions(const PasteSpecialOptions &opts,
                                                    bool allowSameProcess) {
    SharedClipboard *clipboard = SharedClipboard::instance();
    if (!clipboard->initialize()) {
        return false;
    }

    if (allowSameProcess) {
        if (!clipboard->hasData()) {
            return false;
        }
    } else if (!clipboard->hasDataFromDifferentProcess()) {
        return false;
    }

    QList<MidiEvent *> sharedEvents;
    if (!clipboard->pasteEvents(currentFile(), sharedEvents, opts.applyTempoConversion,
                                opts.targetCursorTick)) {
        // deserializeEvents() can new[] and append events before bailing on a
        // mid-stream sanity check (truncated/corrupt buffer). Free them here so a
        // malformed cross-process buffer doesn't leak MidiEvents (and clearing the
        // list also drains any unpaired NoteOnEvent from OffEvent::onEvents via
        // ~NoteOnEvent). The probe path in pasteSpecial() already does this.
        qDeleteAll(sharedEvents);
        sharedEvents.clear();
        return false;
    }
    if (sharedEvents.isEmpty()) {
        return false;
    }

    // Now actually paste these events using the same logic as regular paste
    if (sharedEvents.count() > 0) {
        // Resolve target track FIRST so we can bail out without leaving an
        // open Protocol action (PHASE36-001). currentFile() is guaranteed
        // non-null at this point because pasteEvents() above already
        // dereferenced it.
        int targetChannel = NewNoteTool::editChannel();
        MidiTrack *targetTrack = currentFile()->track(NewNoteTool::editTrack());
        if (!targetTrack) {
            targetTrack = currentFile()->track(0);
        }
        if (!targetTrack) {
            for (MidiEvent *event: sharedEvents) {
                if (event) delete event;
            }
            return false;
        }

        // Repeat-paste guard: when requested, skip note events that would land
        // EXACTLY on an identical existing note (same channel + tick + pitch), so a
        // second identical paste doesn't silently stack invisible duplicates. Build
        // the skip set (duplicate NoteOns + their paired OffEvents) BEFORE opening
        // the protocol action.
        //
        // findDuplicatePasteNotes() and the insertion loop below BOTH index the
        // clipboard timing/source metadata by each event's position in the full
        // sharedEvents list (the loop via sharedIndexOf, built at the split below),
        // so a detected duplicate lands exactly where it was computed. This holds
        // for interleaved tempo/time-sig/key-sig + note clipboards too, not just the
        // notes-only fast path - earlier this was gated off for meta clipboards
        // because the loop used a tempo-first running index that diverged.
        QSet<MidiEvent *> skipSet;
        int skippedNotes = 0;
        if (opts.skipDuplicates) {
            const QList<MidiEvent *> dupOns =
                findDuplicatePasteNotes(currentFile(), sharedEvents,
                                        static_cast<int>(opts.assignment));
            skippedNotes = dupOns.size();
            for (MidiEvent *e : dupOns) {
                skipSet.insert(e);
                if (NoteOnEvent *on = dynamic_cast<NoteOnEvent *>(e)) {
                    if (on->offEvent()) skipSet.insert(on->offEvent());
                }
            }
        }
        const int insertCount = sharedEvents.count() - skipSet.size();
        if (insertCount <= 0) {
            // Everything is already present here - paste nothing (and don't open an
            // empty undo step). The events are freed here since none are inserted.
            // Clear the selection so the caller's "reveal" step doesn't scroll to a
            // stale (pre-paste) selection.
            clearSelection();
            qDeleteAll(sharedEvents);
            sharedEvents.clear();
            if (_mainWindow) {
                _mainWindow->statusBar()->showMessage(
                    QObject::tr("All pasted notes are already present here - nothing pasted"),
                    4000);
            }
            return true;
        }

        // Begin a new ProtocolAction
        currentFile()->protocol()->startNewAction(QObject::tr("Paste ") + QString::number(insertCount) + QObject::tr(" events from shared clipboard"));

        // Phase 36.x -- rescale source ticks into the target file's tick
        // grid when the two MidiFiles use different ticksPerQuarter. The
        // local clipboard path (pasteAction) does the same thing via
        // _copiedTicksPerQuarter; without it a copy from a 480-TPQ file
        // into a 192-TPQ file (or vice-versa) collapses every event onto
        // the first beat. Reported regression after the file-to-file
        // Paste Special routing landed.
        const int sourceTpq = SharedClipboard::sourceTicksPerQuarter();
        const int targetTpq = currentFile()->ticksPerQuarter();
        double tickscale = 1.0;
        if (sourceTpq > 0 && targetTpq > 0 && sourceTpq != targetTpq) {
            tickscale = static_cast<double>(targetTpq) / static_cast<double>(sourceTpq);
        }
        // PHASE36-006: round to nearest instead of truncating toward zero
        // so a leading event at originalTime=1 with a downscale of 0.4
        // doesn't get clamped onto tick 0 and lose its swing.
        auto scaleTick = [tickscale](int t) {
            return static_cast<int>(std::lround(tickscale * static_cast<double>(t)));
        };

        // Get first tick using the original timing information (not deserialized timing)
        int firstTick = -1;
        for (int i = 0; i < sharedEvents.size(); i++) {
            QPair<int, int> originalTiming = SharedClipboard::getOriginalTiming(i);
            int originalTime = originalTiming.first;

            if (originalTime != -1) {
                const int scaled = scaleTick(originalTime);
                if (scaled < firstTick || firstTick < 0) {
                    firstTick = scaled;
                }
            }
        }

        if (firstTick < 0)
            firstTick = 0;

        // Calculate the difference to paste at cursor position
        int diff = currentFile()->cursorTick() - firstTick;

        // ---- Phase 34: build source-track \u2192 target-track map -----------
        // Only meaningful when the assignment policy isn't CurrentEditTarget.
        // For CurrentEditTarget the map is empty and the legacy code path
        // below uses `targetTrack` / `targetChannel` directly.
        QHash<int, MidiTrack *> sourceTrackToTarget;
        const QList<QPair<int, QString>> sourceTracks = SharedClipboard::sourceTrackList();
        if (opts.assignment != PasteAssignment::CurrentEditTarget) {
            for (const auto &p : sourceTracks) {
                const int sourceId = p.first;
                const QString sourceName = p.second;
                MidiTrack *resolved = nullptr;

                // PreserveSourceMapping reuses an existing target track by name.
                // For an UNNAMED source track use a name derived from the (stable,
                // clipboard-provided) source id so a repeated paste reuses the SAME
                // track instead of spawning a fresh "Pasted Track" each time. The
                // matcher and the creator below MUST agree on this name, or the
                // match never succeeds and every paste appends another track.
                const QString preserveName = sourceName.isEmpty()
                    ? QObject::tr("Pasted Track %1").arg(sourceId)
                    : sourceName;

                if (opts.assignment == PasteAssignment::PreserveSourceMapping) {
                    // Try to reuse an existing target track by name.
                    if (currentFile()->tracks()) {
                        for (MidiTrack *t : *currentFile()->tracks()) {
                            if (t && t->name() == preserveName) {
                                resolved = t;
                                break;
                            }
                        }
                    }
                }

                if (!resolved) {
                    // Create a new track. addTrack() appends and is itself
                    // part of the open protocol action, so a single Ctrl+Z
                    // unwinds it together with the pasted events.
                    currentFile()->addTrack();
                    resolved = currentFile()->tracks()
                                   ? currentFile()->tracks()->last()
                                   : nullptr;
                    if (resolved) {
                        QString newName;
                        if (opts.assignment == PasteAssignment::NewTracksPerSource) {
                            newName = sourceName.isEmpty()
                                          ? QObject::tr("Pasted Track %1").arg(sourceId)
                                          : QObject::tr("Pasted: %1").arg(sourceName);
                        } else { // PreserveSourceMapping with no match
                            newName = preserveName;
                        }
                        resolved->setName(newName);
                    }
                }

                if (resolved) {
                    sourceTrackToTarget.insert(sourceId, resolved);
                }
            }
        }

        // Clear selection and paste events
        clearSelection();

        // Build a NEW local selection list — do NOT rely on selectEvent() mutating
        // the internal Selection list through a reference. (PASTE-001)
        QList<MidiEvent *> pastedSelection;

        // Separate tempo/time signature events from regular events first.
        // IMPORTANT: the clipboard timing/source metadata is indexed by each
        // event's position in the FULL sharedEvents list. Splitting into two lists
        // loses that position, so remember it per event (sharedIndexOf) and use it
        // for every getOriginalTiming()/getPasteSourceInfo() lookup below. A
        // per-list running index only matches when serialization is strictly
        // tempo-first; an interleaved clipboard would mis-time the regular events.
        QList<MidiEvent *> tempoEvents;
        QList<MidiEvent *> regularEvents;
        QHash<MidiEvent *, int> sharedIndexOf;

        for (int i = 0; i < sharedEvents.size(); ++i) {
            MidiEvent *event = sharedEvents.at(i);
            sharedIndexOf.insert(event, i);
            if (dynamic_cast<TempoChangeEvent *>(event) || dynamic_cast<TimeSignatureEvent *>(event) || dynamic_cast<KeySignatureEvent *>(event)) {
                tempoEvents.append(event);
            } else {
                regularEvents.append(event);
            }
        }

        // Create protocol entries for channels that will be modified
        std::vector<std::pair<ProtocolEntry *, ProtocolEntry *> > channelCopies;
        std::set<int> copiedChannels;

        auto snapshotChannelOnce = [&](int chan) {
            if (chan < 0 || chan > 18) return;
            if (copiedChannels.find(chan) != copiedChannels.end()) return;
            MidiChannel *channel = currentFile()->channel(chan);
            if (!channel) return;
            ProtocolEntry *channelCopy = channel->copy();
            channelCopies.push_back(std::make_pair(channelCopy, channel));
            copiedChannels.insert(chan);
        };

        // Add target channel for regular events.
        // - CurrentEditTarget mode: every regular event collapses onto
        //   targetChannel.
        // - NewTracks / Preserve modes: each regular event keeps its
        //   source channel; pre-snapshot every distinct source channel
        //   so the protocol covers all of them.
        if (!regularEvents.isEmpty()) {
            if (opts.assignment == PasteAssignment::CurrentEditTarget) {
                snapshotChannelOnce(targetChannel);
            } else {
                for (int i = 0; i < regularEvents.size(); ++i) {
                    const PasteSourceInfo info =
                        SharedClipboard::getPasteSourceInfo(sharedIndexOf.value(regularEvents.at(i)));
                    if (info.originalChannel >= 0 && info.originalChannel <= 15) {
                        snapshotChannelOnce(info.originalChannel);
                    } else {
                        // Unknown channel: fall back to the legacy target.
                        snapshotChannelOnce(targetChannel);
                    }
                }
            }
        }

        // Add channels 16, 17, and 18 for meta events
        if (!tempoEvents.isEmpty()) {
            // Channel 16 for key signature and other general events
            if (copiedChannels.find(16) == copiedChannels.end()) {
                MidiChannel *channel = currentFile()->channel(16);
                ProtocolEntry *channelCopy = channel->copy();
                channelCopies.push_back(std::make_pair(channelCopy, channel));
                copiedChannels.insert(16);
            }
            // Channel 17 for tempo events
            if (copiedChannels.find(17) == copiedChannels.end()) {
                MidiChannel *channel = currentFile()->channel(17);
                ProtocolEntry *channelCopy = channel->copy();
                channelCopies.push_back(std::make_pair(channelCopy, channel));
                copiedChannels.insert(17);
            }
            // Channel 18 for time signature events
            if (copiedChannels.find(18) == copiedChannels.end()) {
                MidiChannel *channel = currentFile()->channel(18);
                ProtocolEntry *channelCopy = channel->copy();
                channelCopies.push_back(std::make_pair(channelCopy, channel));
                copiedChannels.insert(18);
            }
        }

        // First, paste tempo/time signature events and integrate them into the file
        for (MidiEvent *event : tempoEvents) {
            if (!event) {
                continue;
            }

            try {
                // Get the original timing information (metadata is keyed by the
                // event's position in the full sharedEvents list, not a per-list index)
                QPair<int, int> originalTiming = SharedClipboard::getOriginalTiming(sharedIndexOf.value(event));
                int originalTime = originalTiming.first;

                if (originalTime == -1) {
                    originalTime = event->midiTime();
                }

                // Calculate new timing
                int newTime = scaleTick(originalTime) + diff;
                if (newTime < 0) newTime = 0;

                // Set event properties
                event->setFile(currentFile());
                event->setTrack(targetTrack, false);

                // Insert meta events into their correct channels
                TempoChangeEvent *tempoEvent = dynamic_cast<TempoChangeEvent *>(event);
                TimeSignatureEvent *timeSigEvent = dynamic_cast<TimeSignatureEvent *>(event);
                KeySignatureEvent *keySigEvent = dynamic_cast<KeySignatureEvent *>(event);
                
                if (tempoEvent) {
                    event->setChannel(17, false);
                    currentFile()->channel(17)->insertEvent(event, newTime, false);
                } else if (timeSigEvent) {
                    event->setChannel(18, false);
                    currentFile()->channel(18)->insertEvent(event, newTime, false);
                } else if (keySigEvent) {
                    event->setChannel(16, false);
                    currentFile()->channel(16)->insertEvent(event, newTime, false);
                } else {
                    // Fallback for other meta events - use General Events channel
                    event->setChannel(16, false);
                    currentFile()->channel(16)->insertEvent(event, newTime, false);
                }

                // Accumulate into local selection list (respecting visibility & skipping OffEvents).
                if (!dynamic_cast<OffEvent *>(event)
                    && ChannelVisibilityManager::instance().isChannelVisible(event->channel())
                    && (!event->track() || !event->track()->hidden())) {
                    pastedSelection.append(event);
                }
            } catch (...) {
                delete event;
            }
        }

        // Then paste regular events. Metadata is keyed by each event's position in
        // the full sharedEvents list (sharedIndexOf), independent of how tempo/meta
        // and regular events are interleaved in the clipboard.
        for (MidiEvent *event : regularEvents) {
            if (!event) {
                continue;
            }

            // Repeat-paste guard: drop events that duplicate an existing note (and
            // their paired OffEvents). Delete the (un-inserted) event.
            if (opts.skipDuplicates && skipSet.contains(event)) {
                delete event;
                continue;
            }

            try {
                const int metaIndex = sharedIndexOf.value(event);
                // Get the original timing information from SharedClipboard
                QPair<int, int> originalTiming = SharedClipboard::getOriginalTiming(metaIndex);
                int originalTime = originalTiming.first;

                if (originalTime == -1) {
                    // Fallback to event's current timing if no stored timing
                    originalTime = event->midiTime();
                }

                // Calculate new timing (preserve relative timing, paste at cursor)
                int newTime = scaleTick(originalTime) + diff;

                // Phase 34: route per-event according to opts.assignment.
                int eventChannel = targetChannel;
                MidiTrack *eventTrack = targetTrack;
                if (opts.assignment != PasteAssignment::CurrentEditTarget) {
                    const PasteSourceInfo info =
                        SharedClipboard::getPasteSourceInfo(metaIndex);
                    if (info.originalChannel >= 0 && info.originalChannel <= 15) {
                        eventChannel = info.originalChannel;
                    }
                    auto it = sourceTrackToTarget.constFind(info.sourceTrackId);
                    if (it != sourceTrackToTarget.constEnd() && it.value()) {
                        eventTrack = it.value();
                    }
                }

                // Set the event properties safely (timing is already restored in deserializeEvents)
                event->setFile(currentFile());

                event->setChannel(eventChannel, false);

                event->setTrack(eventTrack, false);

                // Insert into the (possibly per-event) target channel.
                currentFile()->channel(eventChannel)->insertEvent(event, newTime, false);

                // Accumulate into local selection list (respecting visibility & skipping OffEvents).
                if (!dynamic_cast<OffEvent *>(event)
                    && ChannelVisibilityManager::instance().isChannelVisible(event->channel())
                    && (!event->track() || !event->track()->hidden())) {
                    pastedSelection.append(event);
                }
            } catch (...) {
                delete event;
            }
        }

        // Put the copied channels from before the event insertion onto the protocol stack
        for (auto channelPair: channelCopies) {
            ProtocolEntry *channel = channelPair.first;
            channel->protocol(channel, channelPair.second);
        }

        // Pasted notes can extend past the song's previous end, so ALWAYS
        // recompute the file's max time - otherwise the timeline and scrollbars
        // don't grow to reach the new notes and they look "missing" off the right
        // edge. (Previously this only ran when tempo events were pasted.)
        currentFile()->calcMaxTime();

        // If tempo/time signature events were pasted, recalculate existing notes
        // This must happen AFTER protocol entries are committed so recalculation is included in undo
        if (!tempoEvents.isEmpty()) {
            recalculateExistingNotesAfterTempoChange(tempoEvents);
        }

        // Update the selection to show the pasted events
        Selection::instance()->setSelection(pastedSelection);
        _mainWindow->eventWidget()->reportSelectionChangedByTool();

        currentFile()->protocol()->endAction();

        if (skippedNotes > 0 && _mainWindow) {
            _mainWindow->statusBar()->showMessage(
                QObject::tr("Pasted %1 events (%2 already-present note(s) skipped)")
                    .arg(insertCount).arg(skippedNotes),
                4000);
        }
    }

    // Note: sharedEvents are now owned by the file/channels
    return true;
}

QList<MidiEvent *> EventTool::findDuplicatePasteNotes(MidiFile *target,
                                                      const QList<MidiEvent *> &events,
                                                      int assignment) {
    QList<MidiEvent *> dups;
    if (!target || events.isEmpty()) {
        return dups;
    }

    // Mirror the placement math of pasteFromSharedClipboardWithOptions so the
    // detected positions match where the events would actually land.
    const int sourceTpq = SharedClipboard::sourceTicksPerQuarter();
    const int targetTpq = target->ticksPerQuarter();
    double tickscale = 1.0;
    if (sourceTpq > 0 && targetTpq > 0 && sourceTpq != targetTpq) {
        tickscale = static_cast<double>(targetTpq) / static_cast<double>(sourceTpq);
    }
    auto scaleTick = [tickscale](int t) {
        return static_cast<int>(std::lround(tickscale * static_cast<double>(t)));
    };

    int firstTick = -1;
    for (int i = 0; i < events.size(); ++i) {
        const int ot = SharedClipboard::getOriginalTiming(i).first;
        if (ot != -1) {
            const int s = scaleTick(ot);
            if (s < firstTick || firstTick < 0) firstTick = s;
        }
    }
    if (firstTick < 0) firstTick = 0;
    const int diff = target->cursorTick() - firstTick;

    const bool collapse =
        (assignment == static_cast<int>(PasteAssignment::CurrentEditTarget));
    const int editChannel = NewNoteTool::editChannel();

    for (int i = 0; i < events.size(); ++i) {
        NoteOnEvent *on = dynamic_cast<NoteOnEvent *>(events.at(i));
        if (!on) continue;

        int ot = SharedClipboard::getOriginalTiming(i).first;
        if (ot == -1) ot = on->midiTime();
        const int newTime = scaleTick(ot) + diff;

        int channel = editChannel;
        if (!collapse) {
            const PasteSourceInfo info = SharedClipboard::getPasteSourceInfo(i);
            if (info.originalChannel >= 0 && info.originalChannel <= 15) {
                channel = info.originalChannel;
            }
        }

        QMultiMap<int, MidiEvent *> *chanEvents = target->channelEvents(channel);
        if (!chanEvents) continue;
        const QList<MidiEvent *> atTick = chanEvents->values(newTime);
        for (MidiEvent *existing : atTick) {
            NoteOnEvent *exOn = dynamic_cast<NoteOnEvent *>(existing);
            if (exOn && exOn->note() == on->note()) {
                dups.append(on);
                break;
            }
        }
    }
    return dups;
}

bool EventTool::hasSharedClipboardData() {
    SharedClipboard *clipboard = SharedClipboard::instance();
    if (!clipboard->initialize()) {
        return false;
    }
    // Only return true if data is from a different process
    return clipboard->hasDataFromDifferentProcess();
}

MidiFile *EventTool::copiedSourceFile() {
    return _copiedSourceFile.data();
}

bool EventTool::localCopyIsForeignTo(MidiFile *current) {
    if (!current) return false;
    if (copiedEvents->isEmpty()) return false;
    // QPointer null-outs when the original file is destroyed; in that
    // case the local copy is implicitly foreign to whatever is open now.
    MidiFile *src = _copiedSourceFile.data();
    return src != current;
}

// ---------------------------------------------------------------------------
// Phase 36 -- Copy to Track / Copy to Channel
//
// Mirrors the Move-to-Track / Move-to-Channel flow but DUPLICATES the
// selection instead of re-homing the originals. The new copies become
// the active selection so the user can immediately transpose, re-velocity,
// recolour, etc. without having to reselect them.
//
// Implementation notes:
//   * NoteOnEvents must drag their paired OffEvent along; we clone the
//     paired OffEvent the same way EventTool::pasteAction() does
//     (see line ~280 above).
//   * Each touched MidiChannel is snapshotted once via channel->copy()
//     so the whole action collapses into a single Protocol step
//     (channel->protocol(snapshot, channel) at the end). insertEvent()
//     is therefore called with toProtocol=false.
//   * Meta channels (16 / 17 / 18) refuse Copy-to-Channel because
//     "channel" doesn't apply to key sig / tempo / time-sig events.
//     Copy-to-Track has no such restriction -- meta events live on
//     dedicated tracks too.
// ---------------------------------------------------------------------------

namespace {

// Helper: clone one event onto the chosen track + channel and insert it
// into `targetFile`. Returns the newly inserted MidiEvent (NoteOn) or
// nullptr on failure. The paired OffEvent (when the source is a NoteOn)
// is inserted as a side effect; it is NOT returned because the caller
// should select only the on-events.
MidiEvent *cloneEventOnto(MidiEvent *src, MidiTrack *targetTrack,
                          int targetChannel, MidiFile *targetFile) {
    if (!src || !targetFile) return nullptr;

    MidiEvent *dup = dynamic_cast<MidiEvent *>(src->copy());
    if (!dup) return nullptr;

    dup->setFile(targetFile);
    if (targetTrack) {
        dup->setTrack(targetTrack, false);
    }
    if (targetChannel >= 0) {
        dup->setChannel(targetChannel, false);
    }

    const int insertChannel = (targetChannel >= 0)
        ? targetChannel
        : dup->channel();

    OnEvent *onDup = dynamic_cast<OnEvent *>(dup);
    if (onDup) {
        OnEvent *srcOn = dynamic_cast<OnEvent *>(src);
        OffEvent *srcOff = srcOn ? srcOn->offEvent() : nullptr;
        if (srcOff) {
            OffEvent *offDup = dynamic_cast<OffEvent *>(srcOff->copy());
            if (offDup) {
                offDup->setOnEvent(onDup);
                offDup->setFile(targetFile);
                if (targetTrack) {
                    offDup->setTrack(targetTrack, false);
                }
                if (targetChannel >= 0) {
                    offDup->setChannel(targetChannel, false);
                }
                targetFile->channel(insertChannel)
                    ->insertEvent(offDup, srcOff->midiTime(), false);
            }
        }
    }

    targetFile->channel(insertChannel)
        ->insertEvent(dup, src->midiTime(), false);
    return dup;
}

} // namespace

bool EventTool::copySelectionToTrack(MidiTrack *target) {
    if (!target) return false;
    MidiFile *file = currentFile();
    if (!file) return false;

    QList<MidiEvent *> selected = Selection::instance()->selectedEvents();
    if (selected.isEmpty()) return false;

    file->protocol()->startNewAction(
        QObject::tr("Copy %1 events to track %2")
            .arg(selected.size())
            .arg(target->number()));

    // Snapshot every channel that will receive an insert exactly once.
    std::vector<std::pair<ProtocolEntry *, ProtocolEntry *>> channelCopies;
    std::set<int> snapshotted;
    auto snapshotChannel = [&](int chan) {
        if (snapshotted.find(chan) != snapshotted.end()) return;
        MidiChannel *mc = file->channel(chan);
        if (!mc) return;
        ProtocolEntry *snap = mc->copy();
        channelCopies.push_back(std::make_pair(snap, mc));
        snapshotted.insert(chan);
    };
    for (MidiEvent *ev : selected) {
        if (!ev) continue;
        if (dynamic_cast<OffEvent *>(ev)) continue; // paired with its OnEvent
        // PHASE36-004: never duplicate meta events (tempo / time-sig /
        // key-sig live on channels 17 / 18 / 16). Duplicating them at
        // identical ticks corrupts the tempo map (last-writer-wins
        // inside QMultiMap) and silently shifts playback timing.
        if (ev->channel() >= 16) continue;
        snapshotChannel(ev->channel());
    }

    QList<MidiEvent *> newSelection;
    for (MidiEvent *ev : selected) {
        if (!ev) continue;
        if (dynamic_cast<OffEvent *>(ev)) continue;
        if (ev->channel() >= 16) continue; // see PHASE36-004 above
        MidiEvent *dup = cloneEventOnto(ev, target, /*targetChannel=*/-1, file);
        if (dup) newSelection.append(dup);
    }

    // Commit one channel-state diff per affected channel.
    for (auto &pair : channelCopies) {
        ProtocolEntry *snap = pair.first;
        snap->protocol(snap, pair.second);
    }

    Selection::instance()->setSelection(newSelection);
    if (_mainWindow) {
        _mainWindow->eventWidget()->reportSelectionChangedByTool();
    }

    file->protocol()->endAction();
    return !newSelection.isEmpty();
}

bool EventTool::copySelectionToChannel(int channel) {
    if (channel < 0 || channel > 15) {
        // Meta channels (16/17/18) refuse this operation.
        return false;
    }
    MidiFile *file = currentFile();
    if (!file) return false;

    QList<MidiEvent *> selected = Selection::instance()->selectedEvents();
    if (selected.isEmpty()) return false;

    file->protocol()->startNewAction(
        QObject::tr("Copy %1 events to channel %2")
            .arg(selected.size())
            .arg(channel));

    // Snapshot the target channel + every source channel.
    std::vector<std::pair<ProtocolEntry *, ProtocolEntry *>> channelCopies;
    std::set<int> snapshotted;
    auto snapshotChannel = [&](int chan) {
        if (snapshotted.find(chan) != snapshotted.end()) return;
        MidiChannel *mc = file->channel(chan);
        if (!mc) return;
        ProtocolEntry *snap = mc->copy();
        channelCopies.push_back(std::make_pair(snap, mc));
        snapshotted.insert(chan);
    };
    snapshotChannel(channel);

    QList<MidiEvent *> newSelection;
    for (MidiEvent *ev : selected) {
        if (!ev) continue;
        if (dynamic_cast<OffEvent *>(ev)) continue;
        // Skip events that already live on a meta channel -- they cannot
        // be re-routed onto a 0..15 voice channel.
        if (ev->channel() >= 16) continue;

        // Keep the original track. We only re-channel the duplicate.
        MidiEvent *dup = cloneEventOnto(ev, ev->track(), channel, file);
        if (dup) newSelection.append(dup);
    }

    for (auto &pair : channelCopies) {
        ProtocolEntry *snap = pair.first;
        snap->protocol(snap, pair.second);
    }

    Selection::instance()->setSelection(newSelection);
    if (_mainWindow) {
        _mainWindow->eventWidget()->reportSelectionChangedByTool();
    }

    file->protocol()->endAction();
    return !newSelection.isEmpty();
}

void EventTool::recalculateExistingNotesAfterTempoChange(const QList<MidiEvent *> &tempoEvents) {
    if (tempoEvents.isEmpty() || !currentFile()) {
        return;
    }

    // Find the earliest tempo/time signature change position
    int earliestChangePosition = INT_MAX;
    for (MidiEvent *event : tempoEvents) {
        if (event && event->midiTime() < earliestChangePosition) {
            earliestChangePosition = event->midiTime();
        }
    }

    if (earliestChangePosition == INT_MAX) {
        return;
    }

    // Collect all existing events that come after the earliest tempo change
    QList<QPair<MidiEvent *, int>> eventsToRecalculate; // event, original position
    
    for (int ch = 0; ch < 19; ch++) {
        // Skip tempo (17) and time signature (18) channels - they're already updated
        if (ch == 17 || ch == 18) {
            continue;
        }
        
        QMultiMap<int, MidiEvent *> *channelEvents = currentFile()->channelEvents(ch);
        if (!channelEvents) {
            continue;
        }
        
        QMultiMap<int, MidiEvent *>::iterator it = channelEvents->lowerBound(earliestChangePosition);
        while (it != channelEvents->end()) {
            MidiEvent *event = it.value();
            int originalPosition = it.key();
            
            // Skip the events we just pasted (they're already correctly positioned)
            bool isNewlyPasted = false;
            for (MidiEvent *pastedEvent : Selection::instance()->selectedEvents()) {
                if (pastedEvent == event) {
                    isNewlyPasted = true;
                    break;
                }
            }
            
            if (!isNewlyPasted) {
                eventsToRecalculate.append(QPair<MidiEvent *, int>(event, originalPosition));
            }
            
            ++it;
        }
    }

    // BULK-OP UNDO: snapshot each affected channel ONCE and run the per-event
    // moves with toProtocol=false. removeEvent's default (true) deep-clones the
    // ENTIRE channel map per moved event - O(moved x events) memory inside one
    // undo action on large files (the documented blowup pattern; see
    // MidiFile::removeTrack).
    QSet<int> affectedChannels;
    for (auto &pair : eventsToRecalculate) {
        if (pair.first) {
            affectedChannels.insert(pair.first->channel());
        }
    }
    QHash<int, ProtocolEntry *> channelSnapshots;
    for (int ch : affectedChannels) {
        MidiChannel *channel = currentFile()->channel(ch);
        if (channel) {
            channelSnapshots.insert(ch, channel->copy());
        }
    }

    // Now recalculate positions for all affected events
    for (auto &pair : eventsToRecalculate) {
        MidiEvent *event = pair.first;
        int oldPosition = pair.second;

        if (!event) {
            continue;
        }

        // Convert old position to real time using old tempo map
        int oldMs = currentFile()->msOfTick(oldPosition);

        // Convert back to ticks using new tempo map
        int newPosition = currentFile()->tick(oldMs);

        // Only update if position actually changed
        if (newPosition != oldPosition && newPosition >= 0) {
            // Remove from old position (unprotocolled - covered by the snapshot)
            currentFile()->channel(event->channel())->removeEvent(event, false);

            // Insert at new position
            currentFile()->channel(event->channel())->insertEvent(event, newPosition, false);
        }
    }

    // Commit one ProtocolItem per touched channel (undo restores the maps).
    for (auto it = channelSnapshots.constBegin(); it != channelSnapshots.constEnd(); ++it) {
        MidiChannel *channel = currentFile()->channel(it.key());
        if (channel) {
            channel->protocol(it.value(), channel);
        }
    }
    
    // Final recalculation to ensure everything is consistent
    currentFile()->calcMaxTime();
}
