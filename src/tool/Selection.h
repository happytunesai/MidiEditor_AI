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

#ifndef SELECTION_H_
#define SELECTION_H_

// Project includes
#include "../protocol/ProtocolEntry.h"

// Qt includes
#include <QList>

// Forward declarations
class MidiEvent;
class EventWidget;

/**
 * \class Selection
 *
 * \brief Manages the current selection of MIDI events in the editor.
 *
 * Selection is a singleton class that maintains the current set of selected
 * MIDI events across the entire application. It provides centralized selection
 * management with protocol support for undo/redo operations.
 *
 * Key features:
 * - **Singleton pattern**: Global access to the current selection
 * - **Protocol integration**: Selection changes can be undone/redone
 * - **Event management**: Add, remove, and clear selected events
 * - **Widget integration**: Synchronizes with EventWidget display
 * - **Multi-selection**: Supports selecting multiple events simultaneously
 *
 * The selection is used by various tools and widgets to determine which
 * events should be affected by operations like move, copy, delete, etc.
 * It maintains consistency across all views and provides a unified
 * interface for selection management.
 */
class Selection : public ProtocolEntry {
public:
    /**
     * \brief Creates a new Selection for the given file.
     * \param file The MidiFile this selection belongs to
     */
    Selection(MidiFile *file);

    /**
     * \brief Creates a new Selection copying another instance.
     * \param other The Selection instance to copy
     */
    Selection(Selection &other);

    /**
     * \brief Creates a copy of this selection for the protocol system.
     * \return A new ProtocolEntry representing this selection's state
     */
    virtual ProtocolEntry *copy();

    /**
     * \brief Reloads the selection's state from a protocol entry.
     * \param entry The protocol entry to restore state from
     */
    virtual void reloadState(ProtocolEntry *entry);

    /**
     * \brief Gets the MIDI file associated with this selection.
     * \return Pointer to the MidiFile
     */
    virtual MidiFile *file();

    // === Active-document selection management ===
    //
    // Phase 28 (multi-document tabs): selection is now per-document. Each
    // MidiFile keeps its own Selection so switching documents/tabs restores
    // that document's selection. instance() resolves to the *active*
    // document's selection. With a single open document this behaves exactly
    // as the old global singleton did. A null file (no active document) gets
    // a fresh, transient selection that is not retained.

    /**
     * \brief Gets the selection of the currently active document.
     * \return Pointer to the active Selection (never null)
     */
    static Selection *instance();

    /**
     * \brief Makes the given file's selection the active one, creating it on
     *        first use. Existing per-file selections are preserved (not
     *        recreated), so switching back to a document restores its
     *        selection. Passing nullptr activates a fresh transient selection.
     * \param file The MidiFile whose selection should become active.
     */
    static void setFile(MidiFile *file);

    /**
     * \brief Drops the retained selection for a file that is being closed.
     *        Call this when a document/MidiFile is destroyed (e.g. tab close
     *        or the single-document swap) to avoid leaking its selection or
     *        leaving a dangling active pointer.
     * \param file The MidiFile being forgotten.
     */
    static void forgetFile(MidiFile *file);

    // === Selection Management ===

    /**
     * \brief Gets the list of currently selected events.
     * \return Copy of the list of selected MidiEvent pointers
     */
    QList<MidiEvent *> selectedEvents();

    /**
     * \brief Sets the selection to the given list of events.
     * \param selections List of MidiEvent pointers to select
     */
    void setSelection(QList<MidiEvent *> selections);

    /**
     * \brief Phase 9.9f §15.2 (Show-Mode follow-the-host): set the
     * selection WITHOUT recording a Protocol step. Used on viewers
     * to mirror the presenter's selection — the viewer's own undo
     * history shouldn't get polluted by presenter-side selection
     * changes (and the viewer can't undo away the host's selection
     * even if it tried). Still updates the EventWidget so the
     * "selected" list in the sidebar follows the host.
     */
    void setSelectionSilent(QList<MidiEvent *> selections);

    /**
     * \brief Clears all selected events.
     */
    void clearSelection();

    /** \brief Static reference to the event widget for display updates */
    static EventWidget *_eventWidget;

private:
    /** \brief List of currently selected events */
    QList<MidiEvent *> _selectedEvents;

    /** \brief Associated MIDI file */
    MidiFile *_file;
};

#endif // SELECTION_H_
