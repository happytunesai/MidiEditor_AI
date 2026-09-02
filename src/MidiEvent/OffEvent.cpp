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

#include "OffEvent.h"
#include "OnEvent.h"

static QMultiMap<int, OnEvent *> onEventsStorage;
QMultiMap<int, OnEvent *> *OffEvent::onEvents = &onEventsStorage;

OffEvent::OffEvent(int ch, int l, MidiTrack *track)
    : MidiEvent(ch, track) {
    _line = l;
    _onEvent = 0;
    QList<OnEvent *> eventsToClose = onEvents->values(line());
    for (int i = 0; i < eventsToClose.length(); i++) {
        // The pending-OnEvent map is process-global and is only drained around
        // MidiFile::readMidiFile, so it can still hold OnEvents that belong to no
        // parse at all - MatrixWidget's piano preview note registers itself for
        // the whole session with a null track. An OffEvent's partner always comes
        // from the same track, so scope the search to it instead of adopting a
        // stranger and overwriting that stranger's own _offEvent pointer.
        if (eventsToClose.at(i)->track() != track) {
            continue;
        }
        if (eventsToClose.at(i)->channel() == channel()) {
            setOnEvent(eventsToClose.at(i));

            // remove entry
            removeOnEvent(eventsToClose.at(i));
            return;
        }
    }
}

QList<OnEvent *> OffEvent::corruptedOnEvents() {
    return onEvents->values();
}

void OffEvent::removeOnEvent(OnEvent *event) {
    onEvents->remove(event->line(), event);
    /*
	for(int j = 0; j<eventsToClose.length(); j++){
		if(i!=j){
			onEvents->insert(line(), eventsToClose.at(j));
		}
	}
	*/
}

OffEvent::OffEvent(OffEvent &other)
    : MidiEvent(other) {
    _onEvent = other._onEvent;
}

void OffEvent::setOnEvent(OnEvent *event) {
    _onEvent = event;
    event->setOffEvent(this);
}

OnEvent *OffEvent::onEvent() {
    return _onEvent;
}

void OffEvent::setMidiTime(int t, bool toProtocol) {
    MidiEvent::setMidiTime(t, toProtocol);
}

void OffEvent::enterOnEvent(OnEvent *event) {
    onEvents->insert(event->line(), event);
}

void OffEvent::clearOnEvents() {
    onEvents->clear();
}

void OffEvent::draw(QPainter *p, QColor c) {
    if (onEvent() && !onEvent()->shown()) {
        onEvent()->draw(p, c);
    }
}

ProtocolEntry *OffEvent::copy() {
    return new OffEvent(*this);
}

void OffEvent::reloadState(ProtocolEntry *entry) {
    OffEvent *other = dynamic_cast<OffEvent *>(entry);
    if (!other) {
        return;
    }
    MidiEvent::reloadState(entry);
    _onEvent = other->_onEvent;
}

QByteArray OffEvent::save() {
    if (onEvent()) {
        return onEvent()->saveOffEvent();
    } else {
        return QByteArray();
    }
}

QString OffEvent::toMessage() {
    if (onEvent()) {
        return onEvent()->offEventMessage();
    } else {
        return QString();
    }
}

int OffEvent::line() {
    if (onEvent()) {
        return onEvent()->line();
    }
    return _line;
}

bool OffEvent::isOnEvent() {
    return false;
}
