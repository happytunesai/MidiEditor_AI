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

#include "TimeSignatureEvent.h"
#include "../midi/MidiFile.h"
#include "math.h"

TimeSignatureEvent::TimeSignatureEvent(int channel, int num, int denom,
                                       int midiClocks, int num32In4, MidiTrack *track)
    : MidiEvent(channel, track) {
    numerator = num;
    denominator = denom;
    midiClocksPerMetronome = midiClocks;
    num32In4th = num32In4;
}

TimeSignatureEvent::TimeSignatureEvent(TimeSignatureEvent &other)
    : MidiEvent(other) {
    numerator = other.numerator;
    denominator = other.denominator;
    midiClocksPerMetronome = other.midiClocksPerMetronome;
    num32In4th = other.num32In4th;
}

int TimeSignatureEvent::num() {
    return numerator;
}

int TimeSignatureEvent::denom() {
    return denominator;
}

int TimeSignatureEvent::midiClocks() {
    return midiClocksPerMetronome;
}

int TimeSignatureEvent::num32In4() {
    return num32In4th;
}

int TimeSignatureEvent::ticksPerMeasure() {
    if (!file() || numerator <= 0 || denominator < 0)
        return 1;
    // Both inputs arrive unvalidated from the file: the 0x58 denominator byte is
    // read as a full 0..255 value and the MThd division word may be 0. A shift of
    // 12 or more already truncates the division below to 0 - the old `denom == 0`
    // check could never catch that - and callers use the result raw, so a 0 here
    // means an integer division by zero (MidiFile::startTickOfMeasure) or a paint
    // loop that never advances (MatrixWidget). Keep the result strictly positive.
    if (denominator > 15) {
        int fallback = 4 * file()->ticksPerQuarter();
        return fallback > 0 ? fallback : 1;
    }
    int denom = 1 << denominator;
    int tpm = (4 * numerator * file()->ticksPerQuarter()) / denom;
    return tpm > 0 ? tpm : 1;
}

int TimeSignatureEvent::measures(int ticks, int *ticksLeft) {
    int tpm = ticksPerMeasure();
    if (tpm <= 0) tpm = 1;
    if (ticksLeft) {
        *ticksLeft = ticks % tpm;
    }
    return ticks / tpm;
}

ProtocolEntry *TimeSignatureEvent::copy() {
    return new TimeSignatureEvent(*this);
}

void TimeSignatureEvent::reloadState(ProtocolEntry *entry) {
    TimeSignatureEvent *other = dynamic_cast<TimeSignatureEvent *>(entry);
    if (!other) {
        return;
    }
    MidiEvent::reloadState(entry);
    numerator = other->numerator;
    denominator = other->denominator;
    midiClocksPerMetronome = other->midiClocksPerMetronome;
    num32In4th = other->num32In4th;
}

int TimeSignatureEvent::line() {
    return MidiEvent::TIME_SIGNATURE_EVENT_LINE;
}

void TimeSignatureEvent::setNumerator(int n) {
    ProtocolEntry *toCopy = copy();
    numerator = n;
    protocol(toCopy, this);
}

void TimeSignatureEvent::setDenominator(int d) {
    ProtocolEntry *toCopy = copy();
    denominator = d;
    protocol(toCopy, this);
}

QByteArray TimeSignatureEvent::save() {
    QByteArray array = QByteArray();
    array.append(char(0xFF));
    array.append(char(0x58));
    array.append(char(0x04));
    array.append(numerator);
    array.append(denominator);
    array.append(midiClocksPerMetronome);
    array.append(num32In4th);
    return array;
}

QString TimeSignatureEvent::typeString() {
    return "Time Signature Event";
}
