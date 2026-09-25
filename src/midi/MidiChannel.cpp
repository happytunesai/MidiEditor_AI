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

/**
 * \file midi/MidiChannel.cpp
 *
 * \brief Implements the class MidiChannel.
 */

#include "MidiChannel.h"

#include "../gui/Appearance.h"
#include "../gui/ChannelVisibilityManager.h"
#include "../MidiEvent/MidiEvent.h"
#include "../MidiEvent/NoteOnEvent.h"
#include "../MidiEvent/OffEvent.h"
#include "../MidiEvent/ProgChangeEvent.h"
#include "../gui/EventWidget.h"
#include "MidiFile.h"
#include "MidiTrack.h"

MidiChannel::MidiChannel(MidiFile *f, int num) {
    _midiFile = f;
    _num = num;

    _visible = true;
    _mute = false;
    _solo = false;

    _events = new QMultiMap<int, MidiEvent *>;
}

MidiChannel::MidiChannel(MidiChannel &other) {
    _midiFile = other._midiFile;
    _visible = other._visible;
    _mute = other._mute;
    _solo = other._solo;
    _events = new QMultiMap<int, MidiEvent *>(*(other._events));
    _num = other._num;
}

MidiChannel::~MidiChannel() {
    // The container only - the events are the document's. A snapshot whose map
    // reloadState() adopted has _events == nullptr by then (see below).
    delete _events;
}

ProtocolEntry *MidiChannel::copy() {
    // v2.2 #3 (undo-memory instrumentation): this is the SINGLE heavy snapshot
    // factory of the undo system - every protocolled channel mutation clones
    // the whole event map here (tens of bytes per event per snapshot), which
    // dwarfs every other undo cost by orders of magnitude. Count instead of
    // estimating: the live channel keeps a running count and node sum, and the
    // status-bar sampler turns the sum into structural bytes. The counters are
    // NOT copied into the snapshot (the copy ctor lists its fields explicitly)
    // and reloadState() does not touch them, so they survive undo and stay
    // monotonic for the life of this document.
    ++_snapshotCount;
    _snapshotNodeSum += _events->size();
    return new MidiChannel(*this);
}

void MidiChannel::reloadState(ProtocolEntry *entry) {
    MidiChannel *other = dynamic_cast<MidiChannel *>(entry);
    if (!other) {
        return;
    }
    _midiFile = other->_midiFile;
    _visible = other->_visible;
    _mute = other->_mute;
    _solo = other->_solo;
    // Free the container we are about to drop. copy() always allocates an
    // independent QMultiMap, so this live channel is the sole owner of its
    // current _events; the snapshot we adopt (other->_events) is a different
    // container. Without this, every channel-level undo/redo orphaned one full
    // event map. Guarded against a self-reload (other == this).
    if (_events != other->_events) {
        delete _events;
    }
    _events = other->_events;
    // Ownership moved to this live channel: the snapshot is deleted right
    // after this call (ProtocolItem::release) and must not free the map we
    // just adopted (review R231-08).
    if (other != this) {
        other->_events = nullptr;
    }
    _num = other->_num;

    // visible() resolves through ChannelVisibilityManager, not through the
    // _visible mirror restored above - so undo/redo of Hide/Show channel (and
    // of Show all / Hide all) was a no-op on screen. Push the restored state
    // into the manager for THIS document. (Full-review F185)
    if (_num >= 0 && _num <= 18) {
        ChannelVisibilityManager::instance().setChannelVisible(_num, _visible, _midiFile);
    }

    // Phase 48: undo/redo swaps the whole event map in. If either side of the
    // swap is the tempo channel, MidiFile's tempo cache no longer describes
    // this map - and the swap can restore a map of the SAME size, so the size
    // safety net alone would not catch it.
    if (_num == 17 || other->_num == 17) {
        bumpTempoRevision();
    }

    // The tempo channel decides the file's total length in ms. Undo/redo of a
    // channel-level tempo action restores only THIS snapshot - without the
    // recompute the timeline keeps the stale length until the file is
    // reloaded (the file-level reloadState already does the same).
    if (_num == 17 && _midiFile) {
        _midiFile->calcMaxTime();
    }
}

MidiFile *MidiChannel::file() {
    return _midiFile;
}

bool MidiChannel::visible() {
    // File-scoped (split view): this channel belongs to a specific document,
    // which is not necessarily the globally-active one.
    return ChannelVisibilityManager::instance().isChannelVisible(_num, _midiFile);
}


void MidiChannel::setVisible(bool b) {
    if (_num < 0 || _num > 18) return;
    // Snapshot BEFORE mutating (same order as setMute/setSolo): the snapshot is
    // what undo restores, and the copy ctor carries _visible - taken after the
    // write it held the NEW value, so undo of Hide/Show was a visible no-op.
    ProtocolEntry *toCopy = copy();
    ChannelVisibilityManager::instance().setChannelVisible(_num, b, _midiFile);
    _visible = b;
    protocol(toCopy, this);
}

void MidiChannel::setVisibleSilent(bool b) {
    if (_num < 0 || _num > 18) return;
    // Update both: the singleton (where visible() actually reads
    // from) AND the local _visible mirror (kept for consistency
    // with the regular setVisible path). No protocol() call so the
    // viewer's undo history stays clean.
    ChannelVisibilityManager::instance().setChannelVisible(_num, b, _midiFile);
    _visible = b;
}

bool MidiChannel::mute() {
    return _mute;
}

void MidiChannel::setMute(bool b) {
    ProtocolEntry *toCopy = copy();
    _mute = b;
    protocol(toCopy, this);
}

bool MidiChannel::solo() {
    return _solo;
}

void MidiChannel::setSolo(bool b) {
    ProtocolEntry *toCopy = copy();
    _solo = b;
    protocol(toCopy, this);
}

int MidiChannel::number() {
    if (_num < 0 || _num > 18) {
        return 0;
    }
    return _num;
}

QMultiMap<int, MidiEvent *> *MidiChannel::eventMap() {
    return _events;
}

QColor *MidiChannel::color() {
    return Appearance::channelColor(number());
}

NoteOnEvent *MidiChannel::insertNote(int note, int startTick, int endTick, int velocity, MidiTrack *track,
                                     bool toProtocol) {
    ProtocolEntry *toCopy = toProtocol ? copy() : nullptr;
    NoteOnEvent *onEvent = new NoteOnEvent(note, velocity, number(), track);

    OffEvent *off = new OffEvent(number(), 127 - note, track);

    off->setFile(file());
    off->setMidiTime(endTick, false);
    onEvent->setFile(file());
    onEvent->setMidiTime(startTick, false);

    if (toProtocol) {
        protocol(toCopy, this);
    }

    return onEvent;
}

bool MidiChannel::removeEvent(MidiEvent *event, bool toProtocol) {
    // if its once TimeSig / TempoChange at 0, dont delete event
    if (number() == 18 || number() == 17) {
        if ((event->midiTime() == 0) && (_events->count(0) == 1)) {
            return false;
        }
    }

    // remove from track if its the trackname
    if (number() == 16 && (MidiEvent *) (event->track()->nameEvent()) == event) {
        event->track()->setNameEvent(0);
    }

    ProtocolEntry *toCopy = nullptr;
    if (toProtocol) {
        toCopy = copy();
    }
    _events->remove(event->midiTime(), event);
    OnEvent *on = dynamic_cast<OnEvent *>(event);
    if (on && on->offEvent()) {
        _events->remove(on->offEvent()->midiTime(), on->offEvent());
    }
    // Phase 48: every removal from the tempo map invalidates MidiFile's tempo
    // cache. Sits here, below the protocol branch, so the bulk paths that pass
    // toProtocol=false are covered too.
    if (_num == 17) {
        bumpTempoRevision();
    }
    if (toProtocol) {
        protocol(toCopy, this);
    }

    //if(MidiEvent::eventWidget()->events().contains(event)){
    //	MidiEvent::eventWidget()->removeEvent(event);
    //}
    return true;
}

void MidiChannel::insertEvent(MidiEvent *event, int tick, bool toProtocol) {
    ProtocolEntry *toCopy = nullptr;
    if (toProtocol) {
        toCopy = copy();
    }
    event->setFile(file());
    event->setMidiTime(tick, false);

    // Phase 48: MidiEvent::setMidiTime() already bumps for an event whose own
    // channel number is 17; this catches the case where the event carries a
    // different channel number but lands in the tempo channel's map.
    if (_num == 17) {
        bumpTempoRevision();
    }

    if (toProtocol) {
        protocol(toCopy, this);
    }
}

void MidiChannel::deleteAllEvents() {
    ProtocolEntry *toCopy = copy();
    _events->clear();
    if (_num == 17) {
        bumpTempoRevision();
    }
    protocol(toCopy, this);
}

int MidiChannel::progAtTick(int tick) {
    if (_events->count() == 0)
        return 0;
    // search for the last ProgChangeEvent at or before tick. Several program
    // changes can share that tick (the FFXIV fixer writes one per track). The
    // one in effect is the most recently inserted: QMultiMap keeps it FIRST
    // among equal keys, and playback and the saved file apply it last. Walking
    // backwards reaches it last, so keep going to the start of that tick - the
    // old early return answered with the OLDEST one, and the channel view then
    // named another instrument than the one that played (fixer review CF-03).
    QMultiMap<int, MidiEvent *>::iterator it = _events->upperBound(tick);
    int foundTick = -1;
    int program = 0;
    while (it != _events->begin()) {
        --it;
        if (foundTick >= 0 && it.key() != foundTick) {
            break;
        }
        ProgChangeEvent *ev = dynamic_cast<ProgChangeEvent *>(it.value());
        if (ev && it.key() <= tick) {
            foundTick = it.key();
            program = ev->program();
        }
    }
    return program;
}
