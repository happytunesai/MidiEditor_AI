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

#include "NoteOnEvent.h"

#include "OffEvent.h"

NoteOnEvent::NoteOnEvent(int note, int velocity, int ch, MidiTrack *track)
    : OnEvent(ch, track) {
    _note = note;
    _velocity = velocity;
    // has to be done here because the line is not known in OnEvents constructor
    // before
    OffEvent::enterOnEvent(this);
}

NoteOnEvent::NoteOnEvent(NoteOnEvent &other)
    : OnEvent(other) {
    _note = other._note;
    _velocity = other._velocity;
}

NoteOnEvent::~NoteOnEvent() {
    // V131-P2-03: symmetric to the constructor's OffEvent::enterOnEvent(this).
    // Only the original (non-copy) NoteOnEvent registers itself in the static
    // onEvents map, and the map is drained as OffEvents are matched to their
    // OnEvents. But if a NoteOnEvent is destroyed before any OffEvent picks it
    // up (corrupt clipboard payload, truncated file load, cancelled parse),
    // it would be left dangling in the map and dereferenced by the next
    // OffEvent ctor on the same line. QMultiMap::remove is a no-op for keys
    // not present, so this is safe for copies too.
    OffEvent::removeOnEvent(this);
}

int NoteOnEvent::note() {
    return _note;
}

int NoteOnEvent::velocity() {
    return _velocity;
}

void NoteOnEvent::setVelocity(int v, bool toProtocol) {
    if (v < 0) {
        v = 0;
    }
    if (v > 127) {
        v = 127;
    }
    if (!toProtocol) {
        // Bulk-op fast path: caller (e.g. FFXIVChannelFixer) has already
        // snapshotted the owning channel, so per-event copy()+protocol()
        // would only burn memory.
        _velocity = v;
        return;
    }
    ProtocolEntry *toCopy = copy();
    _velocity = v;
    protocol(toCopy, this);
}

int NoteOnEvent::line() {
    return 127 - _note;
}

void NoteOnEvent::setNote(int n) {
    ProtocolEntry *toCopy = copy();
    _note = qBound(0, n, 127);
    protocol(toCopy, this);
}

ProtocolEntry *NoteOnEvent::copy() {
    return new NoteOnEvent(*this);
}

void NoteOnEvent::reloadState(ProtocolEntry *entry) {
    NoteOnEvent *other = dynamic_cast<NoteOnEvent *>(entry);
    if (!other) {
        return;
    }
    OnEvent::reloadState(entry);

    _note = other->_note;
    _velocity = other->_velocity;
}

QString NoteOnEvent::toMessage() {
    return "noteon " + QString::number(channel()) + " " + QString::number(note()) + " " + QString::number(velocity());
}

QString NoteOnEvent::offEventMessage() {
    return "noteoff " + QString::number(channel()) + " " + QString::number(note());
}

QByteArray NoteOnEvent::save() {
    QByteArray array = QByteArray();
    array.append(0x90 | channel());
    array.append(note());
    array.append(velocity());
    return array;
}

QByteArray NoteOnEvent::saveOffEvent() {
    QByteArray array = QByteArray();
    array.append(0x80 | channel());
    array.append(note());
    array.append((char) 0x0);
    return array;
}

QString NoteOnEvent::typeString() {
    return "Note On Event";
}
