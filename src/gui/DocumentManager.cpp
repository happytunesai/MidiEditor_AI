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

#include "DocumentManager.h"

DocumentManager::~DocumentManager() {
    qDeleteAll(_documents);
    _documents.clear();
}

Document *DocumentManager::open(MidiFile *file, const QString &title) {
    Document *doc = new Document(file, title);
    _documents.append(doc);
    if (_activeIndex < 0) {
        _activeIndex = 0;
    }
    return doc;
}

Document *DocumentManager::openAndActivate(MidiFile *file, const QString &title) {
    Document *doc = open(file, title);
    _activeIndex = _documents.indexOf(doc);
    return doc;
}

Document *DocumentManager::active() const {
    if (_activeIndex < 0 || _activeIndex >= _documents.size()) {
        return nullptr;
    }
    return _documents.at(_activeIndex);
}

void DocumentManager::setActiveIndex(int index) {
    if (index < 0 || index >= _documents.size()) {
        return;
    }
    _activeIndex = index;
}

void DocumentManager::setActive(Document *doc) {
    int idx = _documents.indexOf(doc);
    if (idx >= 0) {
        _activeIndex = idx;
    }
}

Document *DocumentManager::at(int index) const {
    if (index < 0 || index >= _documents.size()) {
        return nullptr;
    }
    return _documents.at(index);
}

int DocumentManager::indexOfFile(MidiFile *file) const {
    for (int i = 0; i < _documents.size(); ++i) {
        if (_documents.at(i)->file() == file) {
            return i;
        }
    }
    return -1;
}

MidiFile *DocumentManager::removeAt(int index) {
    if (index < 0 || index >= _documents.size()) {
        return nullptr;
    }
    Document *doc = _documents.takeAt(index);
    MidiFile *file = doc->file();
    delete doc;

    // Keep the active index valid and pointing at a sensible neighbour.
    if (_documents.isEmpty()) {
        _activeIndex = -1;
    } else if (index < _activeIndex) {
        // A document before the active one was removed: shift left to follow.
        _activeIndex--;
    } else if (index == _activeIndex) {
        // The active document was removed: clamp to the previous neighbour
        // (or 0 when the first tab was the active one being closed).
        if (_activeIndex >= _documents.size()) {
            _activeIndex = _documents.size() - 1;
        }
    }
    // index > _activeIndex: active index unchanged.
    return file;
}

MidiFile *DocumentManager::closeActive() {
    return removeAt(_activeIndex);
}

Document *DocumentManager::insert(int index, MidiFile *file, const QString &title) {
    if (index < 0) {
        index = 0;
    }
    if (index > _documents.size()) {
        index = _documents.size();
    }
    Document *activeDoc = active(); // may be null when the manager is empty
    Document *doc = new Document(file, title);
    _documents.insert(index, doc);
    if (!activeDoc) {
        // Was empty: the inserted document is now the only (active) one.
        _activeIndex = index;
    } else {
        // Keep the same document active; its index may have shifted right.
        _activeIndex = _documents.indexOf(activeDoc);
    }
    return doc;
}

void DocumentManager::move(int from, int to) {
    if (from < 0 || from >= _documents.size()) {
        return;
    }
    if (to < 0 || to >= _documents.size()) {
        return;
    }
    if (from == to) {
        return;
    }
    Document *activeDoc = active();
    _documents.move(from, to);
    if (activeDoc) {
        _activeIndex = _documents.indexOf(activeDoc);
    }
}
