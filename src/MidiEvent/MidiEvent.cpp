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

#include "MidiEvent.h"
#include "../gui/EventWidget.h"
#include "../gui/Appearance.h"
#include "../midi/MidiFile.h"
#include "ChannelPressureEvent.h"
#include "ControlChangeEvent.h"
#include "KeyPressureEvent.h"
#include "KeySignatureEvent.h"
#include "NoteOnEvent.h"
#include "OffEvent.h"
#include "PitchBendEvent.h"
#include "ProgChangeEvent.h"
#include "SysExEvent.h"
#include "TempoChangeEvent.h"
#include "TextEvent.h"
#include "TimeSignatureEvent.h"
#include "UnknownEvent.h"

#include <QByteArray>

#include "../midi/MidiChannel.h"

quint8 MidiEvent::_startByte = 0;
EventWidget *MidiEvent::_eventWidget = 0;

MidiEvent::MidiEvent(int channel, MidiTrack *track)
    : ProtocolEntry()
      , GraphicObject() {
    _track = track;
    numChannel = channel;
    timePos = 0;
    midiFile = 0;
    _tempID = -1;
}

MidiEvent::MidiEvent(MidiEvent &other)
    : ProtocolEntry(other)
      , GraphicObject() {
    _track = other._track;
    numChannel = other.numChannel;
    timePos = other.timePos;
    midiFile = other.midiFile;
    _tempID = other._tempID;
}

MidiEvent *MidiEvent::loadMidiEvent(QDataStream *content, bool *ok, bool *endEvent, MidiTrack *track, quint8 startByte, quint8 secondByte) {
    // first try to load the event. If this does not work try to use
    // old first byte as new first byte. This is implemented in the end of this
    // method using recursive calls.
    // if startByte (paramater) is not 0, this is the second call so first and
    // second byte must not be loaded from the stream but from the parameters.

    *ok = true;

    quint8 tempByte;

    quint8 prevStartByte = _startByte;

    // The running-status register is a class static shared by every parse, so a
    // failed event must not leave it pointing at the byte that failed: that value
    // survived into the next track - and into the next document - where the
    // running-status retry below could turn a stray data byte into a fabricated
    // event. Restore it whenever this call reports failure.
    struct RunningStatusGuard {
        quint8 saved;
        const bool *okFlag;
        ~RunningStatusGuard() {
            if (!*okFlag) {
                _startByte = saved;
            }
        }
    } runningStatusGuard{prevStartByte, ok};

    if (!startByte) {
        (*content) >> tempByte;
    } else {
        tempByte = startByte;
    }
    _startByte = tempByte;

    int channel = tempByte & 0x0F;

    // Safety check: ensure channel is in valid range for regular MIDI events
    if (channel < 0 || channel > 15) {
        channel = 0;
    }

    switch (tempByte & 0xF0) {
        case 0x80: {
            // Note Off
            if (!startByte) {
                (*content) >> tempByte;
            } else {
                tempByte = secondByte;
            }
            int note = tempByte;
            if (note < 0 || note > 127) {
                *ok = false;
                return 0;
            }
            // skip byte (velocity)
            (*content) >> tempByte;

            OffEvent *event = new OffEvent(channel, 127 - note, track);
            *ok = true;
            return event;
        }

        case 0x90: {
            // Note On
            if (!startByte) {
                (*content) >> tempByte;
            } else {
                tempByte = secondByte;
            }
            int note = tempByte;
            if (note < 0 || note > 127) {
                *ok = false;
                return 0;
            }
            (*content) >> tempByte;
            int velocity = tempByte;
            *ok = true;

            if (velocity > 0) {
                NoteOnEvent *event = new NoteOnEvent(note, velocity, channel, track);
                return event;
            } else {
                OffEvent *event = new OffEvent(channel, 127 - note, track);
                return event;
            }
        }

        case 0xA0: {
            // Key Pressure
            if (!startByte) {
                (*content) >> tempByte;
            } else {
                tempByte = secondByte;
            }
            int note = tempByte;
            if (note < 0 || note > 127) {
                *ok = false;
                return 0;
            }
            (*content) >> tempByte;
            int value = tempByte;

            *ok = true;

            return new KeyPressureEvent(channel, value, note, track);
        }

        case 0xB0: {
            // Controller
            if (!startByte) {
                (*content) >> tempByte;
            } else {
                tempByte = secondByte;
            }
            int control = tempByte;
            (*content) >> tempByte;
            int value = tempByte;
            *ok = true;
            return new ControlChangeEvent(channel, control, value, track);
        }

        case 0xC0: {
            // programm change
            if (!startByte) {
                (*content) >> tempByte;
            } else {
                tempByte = secondByte;
            }
            *ok = true;
            return new ProgChangeEvent(channel, tempByte, track);
        }

        case 0xD0: {
            // Key Pressure
            if (!startByte) {
                (*content) >> tempByte;
            } else {
                tempByte = secondByte;
            }
            int value = tempByte;

            *ok = true;

            return new ChannelPressureEvent(channel, value, track);
        }

        case 0xE0: {
            // Pitch Wheel
            if (!startByte) {
                (*content) >> tempByte;
            } else {
                tempByte = secondByte;
            }
            quint8 first = tempByte;
            (*content) >> tempByte;
            quint8 second = tempByte;

            int value = (second << 7) | first;

            *ok = true;

            return new PitchBendEvent(channel, value, track);
        }

        case 0xF0: {
            // System Message
            channel = 16; // 16 is channel without number

            switch (tempByte & 0x0F) {
                case 0x00: {
                    // SysEx: SMF stores F0 <varlen length> <bytes> with the length
                    // covering the terminating F7. Reading by length instead of
                    // scanning for F7 keeps the length prefix out of the payload
                    // (save() re-derives it) and stops an unterminated chunk from
                    // swallowing the rest of the track.
                    QIODevice *device = content->device();
                    const qint64 lengthPos = device ? device->pos() : -1;
                    // MidiEditor 2.3.0 and earlier saved an EMPTY sysex as the
                    // bare "F0 F7": a terminator where the length would be.
                    if (device && lengthPos >= 0 && !content->atEnd()) {
                        (*content) >> tempByte;
                        if (tempByte == 0xF7) {
                            *ok = true;
                            return new SysExEvent(channel, QByteArray(), track);
                        }
                        if (!device->seek(lengthPos)) {
                            *ok = false;
                            return 0;
                        }
                    }
                    int sysExLength = MidiFile::variableLengthvalue(content);
                    if (sysExLength < 0 || sysExLength > 65535) {
                        *ok = false;
                        return 0;
                    }
                    QByteArray array;
                    array.reserve(sysExLength);
                    bool truncated = false;
                    for (int i = 0; i < sysExLength; i++) {
                        if (content->atEnd()) {
                            truncated = true;
                            break;
                        }
                        (*content) >> tempByte;
                        array.append((char) tempByte);
                    }
                    // Compatibility (review R231-13): MidiEditor 2.3.0 and earlier
                    // wrote "F0 <data> F7" WITHOUT the length field, so the first
                    // data byte (a manufacturer id such as 7E) was just read as a
                    // length. Sysex data bytes are all below 0x80, so an F7 INSIDE
                    // the chunk (or a chunk running past the track) can only mean
                    // the old framing: rewind and scan to the terminator instead.
                    // A length-framed packet that merely lacks the trailing F7 is a
                    // legitimate multi-packet dump and is kept as read.
                    const int innerF7 = array.indexOf((char) 0xF7);
                    // A length of 0 never occurs in a standard file either: it is
                    // the first byte of an extended manufacturer id (00 xx yy).
                    bool oldFraming = (innerF7 >= 0 && innerF7 != array.size() - 1)
                                      || truncated
                                      || (sysExLength == 0 && !content->atEnd());
                    // No F7 inside the chunk and not at the end: EITHER the first
                    // packet of a standard multi-packet dump OR an old-framed
                    // message whose payload is longer than its first byte (e.g.
                    // "F0 01 02 03 F7", manufacturer id 01 read as length 1). The
                    // standard form is followed by a continuation packet:
                    // <delta> F7 <len> <data bytes < 0x80, last may be F7>, and
                    // meta events (FF ...) may sit between the packets - they are
                    // not MIDI data, so the SMF spec allows them there. Look ahead
                    // for exactly that structure (skipping a bounded number of
                    // meta events). Exhausting that look-ahead is inconclusive:
                    // keep the standard framing rather than treating a search
                    // limit as evidence of an old-framed message.
                    if (!oldFraming && innerF7 < 0 && device && !content->atEnd()) {
                        const qint64 afterChunk = device->pos();
                        bool continuationOk = false;
                        bool lookaheadLimitReached = false;
                        constexpr int kMaxLookaheadMetaEvents = 16;
                        for (int skippedMeta = 0; skippedMeta <= kMaxLookaheadMetaEvents; skippedMeta++) {
                            const int delta = MidiFile::variableLengthvalue(content);
                            if (delta < 0 || content->atEnd()) break;
                            (*content) >> tempByte;
                            if (tempByte == 0xFF) {
                                if (skippedMeta == kMaxLookaheadMetaEvents) {
                                    lookaheadLimitReached = true;
                                    break;
                                }
                                // <type> <len> <data>: skip and look at the next event
                                if (content->atEnd()) break;
                                (*content) >> tempByte;
                                const int metaLen = MidiFile::variableLengthvalue(content);
                                if (metaLen < 0 || metaLen > 65535) break;
                                bool metaComplete = true;
                                for (int i = 0; i < metaLen; i++) {
                                    if (content->atEnd()) { metaComplete = false; break; }
                                    (*content) >> tempByte;
                                }
                                if (!metaComplete) break;
                                continue;
                            }
                            if (tempByte == 0xF7) {
                                const int escLen = MidiFile::variableLengthvalue(content);
                                if (escLen > 0 && escLen <= 65535) {
                                    continuationOk = true;
                                    for (int i = 0; i < escLen; i++) {
                                        if (content->atEnd()) { continuationOk = false; break; }
                                        (*content) >> tempByte;
                                        if (tempByte >= 0x80
                                            && !(tempByte == 0xF7 && i == escLen - 1)) {
                                            continuationOk = false;
                                            break;
                                        }
                                    }
                                }
                            }
                            break;
                        }
                        if (!device->seek(afterChunk)) {
                            *ok = false;
                            return 0;
                        }
                        oldFraming = !continuationOk && !lookaheadLimitReached;
                    }
                    if (oldFraming) {
                        if (!device || lengthPos < 0 || !device->seek(lengthPos)) {
                            *ok = false;
                            return 0;
                        }
                        array.clear();
                        while (true) {
                            if (content->atEnd() || array.size() > 65535) {
                                *ok = false;
                                return 0;
                            }
                            (*content) >> tempByte;
                            if (tempByte == 0xF7) {
                                break;
                            }
                            array.append((char) tempByte);
                        }
                    } else if (!array.isEmpty() && (quint8) array.at(array.size() - 1) == 0xF7) {
                        array.chop(1);
                    }
                    *ok = true;
                    return new SysExEvent(channel, array, track);
                }

                case 0x07: {
                    // SMF escape / sysex continuation: F7 <varlen length> <bytes>.
                    // Without this case control fell out of both switches into the
                    // running-status retry below, which re-read the F7 status byte
                    // as a data byte, set *ok = false and made readTrack() discard
                    // every remaining event of the track. The payload is kept as an
                    // UnknownEvent so it survives a save round trip as an ignorable
                    // meta event, rather than as a live F0 sysex message.
                    int escLength = MidiFile::variableLengthvalue(content);
                    if (escLength < 0 || escLength > 65535) {
                        *ok = false;
                        return 0;
                    }
                    QByteArray escData;
                    escData.reserve(escLength);
                    for (int i = 0; i < escLength; i++) {
                        if (content->atEnd()) {
                            *ok = false;
                            return 0;
                        }
                        (*content) >> tempByte;
                        escData.append((char) tempByte);
                    }
                    *ok = true;
                    return new UnknownEvent(channel, (char) 0xF7, escData, track);
                }

                case 0x0F: {
                    // MetaEvent
                    if (!startByte) {
                        (*content) >> tempByte;
                    } else {
                        tempByte = secondByte;
                    }
                    switch (tempByte) {
                        case 0x51: {
                            // TempoChange
                            //(*content)>>tempByte;
                            //if(tempByte!=3){
                            //	*ok = false;
                            //	return 0;
                            //}
                            quint32 value;
                            (*content) >> value;
                            // Mask off the length byte (MSB) to get the 3-byte tempo value
                            value &= 0x00FFFFFF;
                            return new TempoChangeEvent(17, (int) value, track);
                        }
                        case 0x58: {
                            // TimeSignature
                            (*content) >> tempByte;
                            if (tempByte != 4) {
                                *ok = false;
                                return 0;
                            }

                            (*content) >> tempByte;
                            int num = (int) tempByte;
                            (*content) >> tempByte;
                            int denom = (int) tempByte;
                            (*content) >> tempByte;
                            int metronome = (int) tempByte;
                            (*content) >> tempByte;
                            int num32 = (int) tempByte;
                            return new TimeSignatureEvent(18, num, denom, metronome, num32, track);
                        }
                        case 0x59: {
                            // keysignature
                            (*content) >> tempByte;
                            if (tempByte != 2) {
                                *ok = false;
                                return 0;
                            }
                            qint8 t;
                            (*content) >> t;
                            int tonality = (int) t;
                            (*content) >> tempByte;
                            bool minor = true;
                            if (tempByte == 0) {
                                minor = false;
                            }
                            return new KeySignatureEvent(channel, tonality, minor, track);
                        }
                        case 0x2F: {
                            // end Event
                            *endEvent = true;
                            *ok = true;
                            return 0;
                        }
                        default: {
                            if (tempByte >= 0x01 && tempByte <= 0x07) {
                                // textevent
                                // read type
                                TextEvent *textEvent = new TextEvent(channel, track);
                                textEvent->setType(tempByte);
                                uint length = MidiFile::variableLengthvalue(content);

                                // Safety check: prevent unreasonably large text events that could cause memory issues
                                if (length > 65535) { // 64KB limit for text events
                                    *ok = false;
                                    delete textEvent;
                                    return 0;
                                }

                                // Additional safety: check for zero-length text events
                                if (length == 0) {
                                    textEvent->setText(QString());
                                    *ok = true;
                                    return textEvent;
                                }

                                // Use QByteArray for safe dynamic memory management
                                QByteArray textData;
                                textData.reserve(length);

                                for (uint i = 0; i < length; i++) {
                                    // Check if stream has ended unexpectedly
                                    if (content->atEnd()) {
                                        delete textEvent;
                                        *ok = false;
                                        return 0;
                                    }
                                    (*content) >> tempByte;
                                    textData.append((char) tempByte);
                                }

                                // Remove terminator null bytes which cause text truncation
                                // and render as "[]" boxes in Windows UI
                                int nullIdx = textData.indexOf('\0');
                                if (nullIdx != -1) {
                                    textData.truncate(nullIdx);
                                }

                                // Try UTF-8 first; if invalid sequences are found, fall back to Latin-1
                                // (most MIDI files, especially older European ones, use ISO 8859-1)
                                QString decodedText = QString::fromUtf8(textData);
                                if (decodedText.contains(QChar(0xFFFD))) {
                                    // UTF-8 decoding produced replacement chars → use Latin-1
                                    decodedText = QString::fromLatin1(textData);
                                }
                                textEvent->setText(decodedText.remove(QChar(0)).trimmed());
                                *ok = true;
                                return textEvent;
                            } else {
                                // tempByte is meta event type
                                int typeByte = ((char) tempByte);

                                // read length
                                int length = MidiFile::variableLengthvalue(content);

                                // Safety check for unknown events
                                if (length < 0) {
                                    *ok = false;
                                    return 0;
                                }

                                // 64KB limit for unknown events
                                if (length > 65535) {
                                    *ok = false;
                                    return 0;
                                }

                                // content
                                QByteArray array;
                                array.reserve(length);
                                for (int i = 0; i < length; i++) {
                                    if (content->atEnd()) {
                                        *ok = false;
                                        return 0;
                                    }
                                    (*content) >> tempByte;
                                    array.append((char) tempByte);
                                }
                                *ok = true;
                                return new UnknownEvent(channel, typeByte, array, track);
                            }
                        }
                    }
                }

                default: {
                    // F1..F6 and F8..FE are not legal inside an SMF track and can
                    // never be a data byte either, so the running-status retry
                    // below could only fabricate an event (a CC or ProgChange with
                    // a bogus payload) out of this status byte. Fail here instead.
                    *ok = false;
                    return 0;
                }
            }
        }
    }

    // if the event could not be loaded try to use old firstByte before the new
    // data.
    // To do this, pass prefFirstByte and secondByte (the current firstByte)
    // and use it recursive.
    // Guard against infinite recursion: only recurse if we have a valid
    // previous status byte and this isn't already a recursive call
    // (startByte == 0 means this is a fresh call, not recursive)
    if (startByte != 0 || prevStartByte == 0) {
        // Already in a recursive call or no valid running status - give up
        *ok = false;
        return nullptr;
    }
    _startByte = prevStartByte;
    return loadMidiEvent(content, ok, endEvent, track, _startByte, tempByte);
}

void MidiEvent::resetRunningStatus() {
    // Running status must not cross a track boundary (SMF spec) and must not
    // cross a document boundary either - the register is a class static shared
    // by every parse, including MIDI input.
    _startByte = 0;
}

void MidiEvent::setTrack(MidiTrack *track, bool toProtocol) {
    ProtocolEntry *toCopy = copy();

    _track = track;
    if (toProtocol) {
        protocol(toCopy, this);
    } else {
        delete toCopy;
    }
}

MidiTrack *MidiEvent::track() {
    return _track;
}

void MidiEvent::setChannel(int ch, bool toProtocol) {
    // Validate channel assignment - prevent regular events from using special channels
    if (ch < 0 || ch > 18) {
        ch = 0;
    }

    int oldChannel = channel();
    ProtocolEntry *toCopy = copy();
    numChannel = ch;
    if (toProtocol) {
        protocol(toCopy, this);
        file()->channelEvents(oldChannel)->remove(midiTime(), this);
        // tells the new channel to add this event
        setMidiTime(midiTime(), toProtocol);
    } else {
        delete toCopy;
    }
}

int MidiEvent::channel() {
    // Add validation to prevent crashes from corrupted channel numbers
    if (numChannel < 0 || numChannel > 18) {
        return 0; // Return a safe default
    }
    return numChannel;
}

QString MidiEvent::toMessage() {
    return "";
}

QByteArray MidiEvent::save() {
    return QByteArray();
}

void MidiEvent::setMidiTime(int t, bool toProtocol) {
    // if its once TimeSig / TempoChange at 0, dont delete event
    if (toProtocol && (channel() == 18 || channel() == 17)) {
        if (midiTime() == 0 && midiFile->channel(channel())->eventMap()->count(0) == 1) {
            return;
        }
    }

    ProtocolEntry *toCopy = nullptr;
    if (toProtocol) {
        toCopy = copy();
    }

    // Phase 48: this is the single place where an event actually moves inside
    // a channel's event map, so a tempo event moving in time is invalidated
    // here. The map is mutated twice (remove, then insert) and the length
    // recompute in between queries MidiFile::msOfTick(), so both mutations get
    // their own bump - a single bump before the msOfTick() call would let that
    // call re-validate the cache against a half-updated map.
    const bool onTempoChannel = (numChannel == 17);

    file()->channelEvents(numChannel)->remove(timePos, this);
    if (onTempoChannel) {
        MidiChannel::bumpTempoRevision();
    }
    timePos = t;
    if (timePos > file()->endTick()) {
        file()->setMaxLengthMs(file()->msOfTick(timePos) + 100);
    }
    if (toProtocol) {
        protocol(toCopy, this);
    }

    file()->channelEvents(numChannel)->insert(timePos, this);
    if (onTempoChannel) {
        MidiChannel::bumpTempoRevision();
    }
}

int MidiEvent::midiTime() {
    return timePos;
}

void MidiEvent::setFile(MidiFile *f) {
    midiFile = f;
}

MidiFile *MidiEvent::file() {
    return midiFile;
}

int MidiEvent::line() {
    return 0;
}

void MidiEvent::draw(QPainter *p, QColor c) {
    p->setPen(Appearance::borderColor());
    p->setBrush(c);
    p->drawRoundedRect(x(), y(), width(), height(), 1, 1);
}

ProtocolEntry *MidiEvent::copy() {
    return new MidiEvent(*this);
}

void MidiEvent::reloadState(ProtocolEntry *entry) {
    MidiEvent *other = dynamic_cast<MidiEvent *>(entry);
    if (!other) {
        return;
    }
    _track = other->_track;
    // A channel-level snapshot restored earlier in the same undo/redo step may
    // already hold this event at its target key (or still at the stale one):
    // unmap it at BOTH keys, in both channels, before re-inserting, so no map
    // ever holds the event twice and a stale entry never survives (review
    // R231-11 - Delete Overlaps redo doubled shortened notes).
    QMultiMap<int, MidiEvent *> *current = file()->channelEvents(numChannel);
    current->remove(timePos, this);
    current->remove(other->timePos, this);
    numChannel = other->numChannel;
    QMultiMap<int, MidiEvent *> *target = file()->channelEvents(numChannel);
    target->remove(timePos, this);
    target->remove(other->timePos, this);
    timePos = other->timePos;
    target->insert(timePos, this);
    midiFile = other->midiFile;
}

QString MidiEvent::typeString() {
    return "Midi Event";
}

void MidiEvent::setEventWidget(EventWidget *widget) {
    _eventWidget = widget;
}

EventWidget *MidiEvent::eventWidget() {
    return _eventWidget;
}

bool MidiEvent::shownInEventWidget() {
    if (!_eventWidget) {
        return false;
    }
    return _eventWidget->events().contains(this);
}

bool MidiEvent::isOnEvent() {
    return true;
}

QMap<int, QString> MidiEvent::knownMetaTypes() {
    QMap<int, QString> meta;
    for (int i = 1; i < 8; i++) {
        meta.insert(i, "Text Event");
    }
    meta.insert(0x51, "Tempo Change Event");
    meta.insert(0x58, "Time Signature Event");
    meta.insert(0x59, "Key Signature Event");
    meta.insert(0x2F, "End of Track");
    return meta;
}

void MidiEvent::setTemporaryRecordID(int id) {
    _tempID = id;
}

int MidiEvent::temporaryRecordID() {
    return _tempID;
}

void MidiEvent::moveToChannel(int ch, bool toProtocol) {
    int oldChannel = channel();

    if (oldChannel > 15) {
        return;
    }

    if (oldChannel == ch) {
        return;
    }

    midiFile->channel(oldChannel)->removeEvent(this, toProtocol);

    ProtocolEntry *toCopy = nullptr;
    if (toProtocol) {
        toCopy = copy();
    }

    numChannel = ch;

    if (toProtocol) {
        protocol(toCopy, this);
    }

    midiFile->channel(ch)->insertEvent(this, midiTime(), toProtocol);
}
