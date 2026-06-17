#include "Selection.h"
#include "../gui/EventWidget.h"

#include <QHash>

EventWidget *Selection::_eventWidget = 0;

// Phase 28: per-document selections. Real (non-null) files keep a retained
// Selection in _perFileSelections so switching documents/tabs restores their
// selection; closing a document drops it via forgetFile(). The active pointer
// is what instance() returns. A null active (no document) is a transient
// selection that is freed when it is replaced. _active is never null.
static QHash<MidiFile *, Selection *> _perFileSelections;
static Selection *_active = new Selection(nullptr);

Selection::Selection(MidiFile *file) {
    _file = file;
    if (_eventWidget) {
        _eventWidget->setEvents(_selectedEvents);
        _eventWidget->reload();
    }
}

Selection::Selection(Selection &other) {
    _file = other._file;
    _selectedEvents = other._selectedEvents;
}

ProtocolEntry *Selection::copy() {
    return new Selection(*this);
}

void Selection::reloadState(ProtocolEntry *entry) {
    Selection *other = dynamic_cast<Selection *>(entry);
    if (!other) {
        return;
    }
    _selectedEvents = other->_selectedEvents;
    if (_eventWidget) {
        _eventWidget->setEvents(_selectedEvents);
        //_eventWidget->reload();
    }
}

MidiFile *Selection::file() {
    return _file;
}

Selection *Selection::instance() {
    return _active;
}

void Selection::setFile(MidiFile *file) {
    Selection *previous = _active;

    if (file) {
        // Reuse this document's retained selection, or create it on first use.
        Selection *s = _perFileSelections.value(file, nullptr);
        if (!s) {
            s = new Selection(file);
            _perFileSelections.insert(file, s);
        }
        _active = s;
    } else {
        // No active document: a fresh, transient (non-retained) selection.
        _active = new Selection(nullptr);
    }

    // Retire the previous active only if it was a transient (null-file)
    // selection. Retained per-file selections must survive the switch.
    if (previous && previous != _active && previous->file() == nullptr) {
        delete previous;
    }
}

void Selection::forgetFile(MidiFile *file) {
    if (!file) {
        return;
    }
    Selection *s = _perFileSelections.take(file);
    if (!s) {
        return;
    }
    // If the document being closed is the active one, fall back to a fresh
    // transient selection so instance() never dangles.
    if (_active == s) {
        _active = new Selection(nullptr);
    }
    delete s;
}

Selection *Selection::forFile(MidiFile *file) {
    if (!file) {
        return nullptr;
    }
    return _perFileSelections.value(file, nullptr);
}

QList<MidiEvent *> Selection::selectedEvents() {
    return _selectedEvents;
}

void Selection::setSelection(QList<MidiEvent *> selections) {
    protocol(copy(), this);

    // For large selections, use move semantics to avoid copying
    // Use move semantics for selections with 100+ events to improve performance
    if (selections.size() >= 100) {
        _selectedEvents = std::move(selections);
    } else {
        _selectedEvents = selections;
    }

    if (_eventWidget) {
        _eventWidget->setEvents(_selectedEvents);
        // Note: reload() is commented out for performance - it's called elsewhere when needed
        //_eventWidget->reload();
    }
}

void Selection::clearSelection() {
    setSelection(QList<MidiEvent *>());
    if (_eventWidget) {
        _eventWidget->setEvents(_selectedEvents);
        //_eventWidget->reload();
    }
}

void Selection::setSelectionSilent(QList<MidiEvent *> selections) {
    // Phase 9.9f §15.2: silent variant — no protocol(copy(), this)
    // call, so the change is invisible to the undo stack. Used on a
    // viewer to mirror the presenter's selection.
    if (selections.size() >= 100) {
        _selectedEvents = std::move(selections);
    } else {
        _selectedEvents = selections;
    }
    if (_eventWidget) {
        _eventWidget->setEvents(_selectedEvents);
    }
}
