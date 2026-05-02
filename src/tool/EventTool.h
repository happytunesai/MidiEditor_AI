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

#ifndef EVENTTOOL_H_
#define EVENTTOOL_H_

#include "EditorTool.h"

#include <QPointer>

// Forward declarations
class MidiEvent;
class MidiFile;
class MidiTrack;
class SharedClipboard;

/**
 * \class EventTool
 *
 * \brief Base class for tools that work with MIDI events.
 *
 * EventTool provides common functionality for all tools that manipulate MIDI events.
 * It serves as the base class for most editing tools in the MIDI editor and provides:
 *
 * - Event selection and deselection management
 * - Copy/paste operations for events
 * - Shared clipboard functionality for cross-instance operations
 * - Grid snapping (magnet) functionality
 * - Event positioning and timing utilities
 * - Visual feedback for selected events
 *
 * Key features:
 * - Static selection management shared across all event tools
 * - Integration with the protocol system for undo/redo
 * - Support for both local and shared clipboard operations
 * - Grid alignment assistance for precise editing
 */
class EventTool : public EditorTool {
public:
    /**
     * \brief Creates a new EventTool.
     */
    EventTool();

    /**
     * \brief Creates a new EventTool copying another instance.
     * \param other The EventTool instance to copy
     */
    EventTool(EventTool &other);

    // === Selection Management ===

    /**
     * \brief Selects a MIDI event.
     * \param event The event to select
     * \param single If true, clear other selections first
     * \param ignoreStr Ignore string comparison for selection
     * \param setSelection Update the selection state
     */
    static void selectEvent(MidiEvent *event, bool single, bool ignoreStr = false, bool setSelection = true);

    /**
     * \brief Deselects a MIDI event.
     * \param event The event to deselect
     */
    static void deselectEvent(MidiEvent *event);

    /**
     * \brief Clears all selected events.
     */
    static void clearSelection();

    /**
     * \brief Efficiently selects multiple events in batch.
     * \param events List of events to select
     *
     * This method is optimized for selecting large numbers of events
     * by minimizing UI updates and protocol overhead.
     */
    static void batchSelectEvents(const QList<MidiEvent *> &events);

    /**
     * \brief Paints visual feedback for selected events.
     * \param painter The QPainter to draw with
     */
    void paintSelectedEvents(QPainter *painter);

    // === Event Manipulation ===

    /**
     * \brief Changes the timing of an event.
     * \param event The event to modify
     * \param shiftX The time shift in pixels
     */
    void changeTick(MidiEvent *event, int shiftX);

    // === Clipboard Operations ===

    /**
     * \brief Copies selected events to the local clipboard.
     */
    static void copyAction();

    /**
     * \brief Pastes events from the local clipboard.
     */
    static void pasteAction();

    /**
     * \brief Copies selected events to the shared clipboard.
     * \return True if copy was successful
     */
    static bool copyToSharedClipboard();

    /**
     * \brief Pastes events from the shared clipboard.
     * \return True if paste was successful
     */
    static bool pasteFromSharedClipboard();

    /**
     * \brief Phase 34 \u2014 paste from the shared clipboard with explicit
     * routing options (track / channel assignment policy).
     * \param opts See PasteSpecialDialog.h.
     * \param allowSameProcess When true the cross-process guard is
     *        relaxed so the same MidiEditor instance can paste its own
     *        clipboard via the Paste Special flow. This is used when a
     *        copy from a previously-loaded file should be routed into a
     *        newly-opened file in the same window (Phase 36.x).
     */
    static bool pasteFromSharedClipboardWithOptions(const struct PasteSpecialOptions &opts,
                                                    bool allowSameProcess = false);

    /**
     * \brief Checks if shared clipboard has data.
     * \return True if shared clipboard contains events
     */
    static bool hasSharedClipboardData();

    /**
     * \brief Returns the MidiFile that owned the selection at the time of
     * the most recent copyAction(). May be null when nothing has been
     * copied yet, or when the original file has been destroyed (QPointer
     * tracks lifetime safely).
     */
    static MidiFile *copiedSourceFile();

    /**
     * \brief True when the local copy buffer is non-empty AND originated
     * from a different MidiFile than the one passed in. Used by
     * MainWindow::paste() to route Ctrl+V through Paste Special when the
     * user copies in file A, opens file B, and then pastes \u2014 mirroring
     * the cross-process behaviour but for the in-process \"open another
     * file\" workflow.
     */
    static bool localCopyIsForeignTo(MidiFile *current);

    /**
     * \brief Phase 36 -- duplicate the current selection onto another
     * track, leaving the originals in place. The new copies become the
     * active selection so the user can immediately transpose / re-edit.
     * Wraps a single Protocol step. No-op (returns false) when the
     * selection is empty or the target track is null.
     */
    static bool copySelectionToTrack(MidiTrack *target);

    /**
     * \brief Phase 36 -- duplicate the current selection onto another
     * channel (0..15). Originals are kept; the new copies become the
     * active selection. Refuses meta channels (16/17/18) and returns
     * false. Wraps a single Protocol step.
     */
    static bool copySelectionToChannel(int channel);

    /**
     * \brief Recalculates existing note positions after tempo/time signature changes.
     * \param tempoEvents List of tempo/time signature events that were pasted
     */
    static void recalculateExistingNotesAfterTempoChange(const QList<MidiEvent *> &tempoEvents);

    /**
     * \brief Returns whether this tool shows selection highlights.
     * \return True if selection should be shown
     */
    virtual bool showsSelection();

    // === Paste Target Configuration ===

    /**
     * \brief Sets the target track for paste operations.
     * \param track The track index to paste to
     */
    static void setPasteTrack(int track);

    /**
     * \brief Gets the target track for paste operations.
     * \return The track index to paste to
     */
    static int pasteTrack();

    /**
     * \brief Sets the target channel for paste operations.
     * \param channel The channel index to paste to
     */
    static void setPasteChannel(int channel);

    /**
     * \brief Gets the target channel for paste operations.
     * \return The channel index to paste to
     */
    static int pasteChannel();

    // === Grid Snapping ===

    /**
     * \brief Snaps X coordinate to grid if magnet is enabled.
     * \param x The X coordinate to snap
     * \param tick Optional pointer to receive the corresponding tick
     * \return The snapped X coordinate
     */
    int rasteredX(int x, int *tick = 0);

    /**
     * \brief Enables or disables grid snapping (magnet).
     * \param enable True to enable grid snapping
     */
    static void enableMagnet(bool enable);

    /**
     * \brief Checks if grid snapping (magnet) is enabled.
     * \return True if grid snapping is enabled
     */
    static bool magnetEnabled();

    /** \brief Static list of copied events */
    static QList<MidiEvent *> *copiedEvents;

    /** \brief Ticks per quarter of the source file at copy time */
    static int _copiedTicksPerQuarter;

    /** \brief MidiFile that owned the selection at the time of the last
     *  copyAction(). Tracked via QPointer so it null-out\u2019s automatically
     *  if the originating file is destroyed (e.g. closed without saving).
     *  Compared in MainWindow::paste() to detect the
     *  \"copy in file A, open file B, paste\" workflow.
     */
    static QPointer<MidiFile> _copiedSourceFile;

protected:
    /** \brief Flag indicating if the last clipboard operation was a cut */
    static bool isCutAction;

    /** \brief Target channel for paste operations */
    static int _pasteChannel;

    /** \brief Target track for paste operations */
    static int _pasteTrack;

    /** \brief Flag indicating if grid snapping is enabled */
    static bool _magnet;
};

#endif // EVENTTOOL_H_
