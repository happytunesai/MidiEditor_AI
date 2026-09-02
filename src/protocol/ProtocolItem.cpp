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

#include "ProtocolItem.h"
#include "../midi/MidiFile.h"
#include "../midi/MidiTrack.h"
#include "ProtocolEntry.h"

ProtocolItem::ProtocolItem(ProtocolEntry *oldObj, ProtocolEntry *newObj) {
    _oldObject = oldObj;
    _newObject = newObj;
}

ProtocolItem::~ProtocolItem() {
    // WHY: an item destroyed without release() (discarded redo stack, item
    // entered with no open action, ~Protocol) owned its old snapshot but freed
    // only the 16-byte wrapper - the snapshot (a Selection list, an event copy,
    // a whole MidiChannel event map) leaked. Same ownership rule as release().
    if (_oldObject && !dynamic_cast<MidiTrack *>(_oldObject)) {
        if (_oldObject->file() != _oldObject) {
            delete _oldObject;
        }
    }
}

ProtocolItem *ProtocolItem::release() {
    ProtocolEntry *entry = _newObject->copy();
    _newObject->reloadState(_oldObject);

    //files can be protocolled too but they must not be deleted
    if (!dynamic_cast<MidiTrack *>(_oldObject)) {
        if (_oldObject->file() != _oldObject) {
            delete _oldObject;
        }
    }
    // Ownership of the old snapshot ends here either way; the destructor that
    // follows in ProtocolStep::releaseStep() must not touch it again.
    _oldObject = nullptr;
    return new ProtocolItem(entry, _newObject);
}
