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

#include "TempoChangeEvent.h"
#include "../midi/MidiChannel.h"
#include "../midi/MidiFile.h"

TempoChangeEvent::TempoChangeEvent(int channel, int value, MidiTrack *track)
    : MidiEvent(channel, track) {
    if (value <= 0) {
        value = 500000; // Default 120 BPM
    }
    // Keep the exact microseconds-per-quarter the file carried. _beats is a
    // truncated integer BPM, so re-deriving the microseconds in save() rewrote
    // every fractional tempo (100.5 BPM came back as 100) on an untouched
    // load/save round trip.
    _microsPerQuarter = value;
    _beats = 60000000 / value;
}

TempoChangeEvent::TempoChangeEvent(TempoChangeEvent &other)
    : MidiEvent(other) {
    _beats = other._beats;
    _microsPerQuarter = other._microsPerQuarter;
}

int TempoChangeEvent::beatsPerQuarter() {
    return _beats;
}

double TempoChangeEvent::msPerTick() {
    if (!file() || _beats <= 0 || file()->ticksPerQuarter() <= 0)
        return 1.0;
    double quarters_per_second = (double) _beats / 60;
    double ticks_per_second = (double) (file()->ticksPerQuarter()) * quarters_per_second;
    return 1000 / (ticks_per_second);
}

ProtocolEntry *TempoChangeEvent::copy() {
    return new TempoChangeEvent(*this);
}

void TempoChangeEvent::reloadState(ProtocolEntry *entry) {
    TempoChangeEvent *other = dynamic_cast<TempoChangeEvent *>(entry);
    if (!other) {
        return;
    }
    MidiEvent::reloadState(entry);
    _beats = other->_beats;
    _microsPerQuarter = other->_microsPerQuarter;
    // Phase 48: undo/redo of a BPM edit restores _beats without changing the
    // size of the tempo map, so nothing else would tell MidiFile's tempo cache
    // that its ms-per-tick values are stale.
    MidiChannel::bumpTempoRevision();
}

int TempoChangeEvent::line() {
    return MidiEvent::TEMPO_CHANGE_EVENT_LINE;
}

QByteArray TempoChangeEvent::save() {
    QByteArray array = QByteArray();

    array.append(char(0xFF));
    array.append(char(0x51));
    array.append(char(0x03));
    // Write back the exact microseconds-per-quarter this event carries; deriving
    // it from the truncated integer BPM lost up to a full BPM per round trip.
    int value = _microsPerQuarter;
    if (value <= 0) {
        int beats = (_beats > 0) ? _beats : 120;
        value = 60000000 / beats;
    }
    for (int i = 2; i >= 0; i--) {
        array.append((value & (0xFF << 8 * i)) >> 8 * i);
    }

    return array;
}

int TempoChangeEvent::microsPerQuarter() const {
    if (_microsPerQuarter > 0) {
        return _microsPerQuarter;
    }
    return (_beats > 0) ? (60000000 / _beats) : 500000;
}

void TempoChangeEvent::setMicrosPerQuarter(int value) {
    if (value <= 0) {
        value = 500000;
    }
    ProtocolEntry *toCopy = copy();
    _microsPerQuarter = value;
    _beats = 60000000 / value;
    MidiChannel::bumpTempoRevision();
    file()->calcMaxTime();
    protocol(toCopy, this);
}

void TempoChangeEvent::setBeats(int beats) {
    ProtocolEntry *toCopy = copy();
    _beats = beats;
    // The user picked a BPM, so re-deriving the microsecond value here is the
    // intended loss - unlike the load/save path, which must stay lossless.
    _microsPerQuarter = (beats > 0) ? (60000000 / beats) : 500000;
    // Phase 48: a BPM change rewrites the timing of the whole map behind this
    // event while leaving the map itself the same size - bump BEFORE the
    // length recompute, which reads the tempo cache back.
    MidiChannel::bumpTempoRevision();
    file()->calcMaxTime();
    protocol(toCopy, this);
}

QString TempoChangeEvent::typeString() {
    return "Tempo Change Event";
}
