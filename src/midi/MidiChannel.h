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

#ifndef MIDICHANNEL_H_
#define MIDICHANNEL_H_

// Project includes
#include "../protocol/ProtocolEntry.h"

// Qt includes
#include <QMultiMap>

// Standard includes
#include <atomic>

// Forward declarations
class MidiFile;
class MidiEvent;
class QColor;
class MidiTrack;
class NoteOnEvent;

/**
 * \class MidiChannel
 *
 * \brief Represents a MIDI channel containing events for a specific instrument.
 *
 * MidiChannel manages all MIDI events for a specific channel within the MIDI file.
 * The MIDI editor uses 18 channels in total:
 *
 * - **Channels 0-15**: Standard MIDI channels for instruments
 * - **Channel 16**: General events (like PitchBend)
 * - **Channel 17**: Tempo change events
 * - **Channel 18**: Time signature events
 *
 * Key features:
 * - **Event management**: Stores and organizes all events for the channel
 * - **Visibility control**: Show/hide channel in the matrix widget
 * - **Mute control**: Enable/disable channel playback
 * - **Solo mode**: Play only this channel (mutes all others)
 * - **Color coding**: Visual identification in the editor
 * - **Program tracking**: Maintains current instrument program
 * - **Note insertion**: Convenient methods for adding notes
 *
 * Events are stored in a QMultiMap organized by MIDI tick time, allowing
 * efficient time-based access and manipulation.
 */
class MidiChannel : public ProtocolEntry {
public:
    /**
     * \brief Creates a new MidiChannel with the specified number.
     * \param f The MidiFile this channel belongs to
     * \param num The channel number (0-18)
     */
    MidiChannel(MidiFile *f, int num);

    /**
     * \brief Creates a new MidiChannel copying another instance.
     * \param other The MidiChannel instance to copy
     */
    MidiChannel(MidiChannel &other);

    /**
     * \brief Frees the event map container (not the events - they are owned
     *        by the document). Undo snapshots are MidiChannel copies: without
     *        this the map copy of every discarded snapshot leaked (review
     *        R231-08, the leak F012 named).
     */
    ~MidiChannel();

    // === Basic Properties ===

    /**
     * \brief Gets the parent MIDI file.
     * \return Pointer to the MidiFile containing this channel
     */
    MidiFile *file();

    /**
     * \brief Gets the channel number.
     * \return Channel number: 0-15 for MIDI channels, 16 for general events,
     *         17 for tempo changes, 18 for time signature events
     */
    int number();

    /**
     * \brief Gets the channel's display color.
     * \return Pointer to the QColor used for visual representation
     *
     * The color is determined by the channel number and provides
     * consistent visual identification across the editor.
     */
    QColor *color();

    // === Event Management ===

    /**
     * \brief Gets the event map containing all channel events.
     * \return Pointer to QMultiMap organized by MIDI tick time
     */
    QMultiMap<int, MidiEvent *> *eventMap();

    /**
     * \brief v2.2 #3 (undo-memory instrumentation): how many full-channel
     *  snapshots this LIVE channel has produced via copy() since the document
     *  was opened. Monotonic; snapshots themselves report 0.
     */
    qint64 snapshotCount() const { return _snapshotCount; }

    /**
     * \brief v2.2 #3: running sum of eventMap()->size() over all snapshots -
     *  i.e. the total number of map nodes the undo history was charged for.
     *  Multiply by the measured node size (48 B on MSVC x64) for structural
     *  bytes. Counts at copy() time, so shared-until-detach COW trees are
     *  charged in full (they detach on the next repaint or save anyway).
     */
    qint64 snapshotNodeSum() const { return _snapshotNodeSum; }

    // === Tempo-map cache invalidation (Phase 48) ===

    /**
     * \brief Revision counter of the TEMPO map (channel 17).
     *
     *  MidiFile keeps a binary-searchable cache of the channel-17 tempo map
     *  (see MidiFile::msOfTick()). That cache is only correct as long as it
     *  can tell that channel 17 changed, and EVERY channel-17 mutation must
     *  be visible to it - including the bulk paths that pass
     *  toProtocol=false, which is why the hook lives on the mutating methods
     *  here and not in the protocol layer.
     *
     *  The counter is deliberately PROCESS-WIDE rather than per document:
     *  the mutation sites that have to bump it live in TUs
     *  (MidiEvent.cpp, TempoChangeEvent.cpp) that several test harnesses
     *  link against an ODR-shimmed MidiFile, so they can neither call a new
     *  MidiFile method nor reach the owning MidiChannel through
     *  MidiFile::channel(). A shared counter over-invalidates - a tempo edit
     *  in one open document costs every other document one cache rebuild -
     *  which is a linear walk of a map that is normally a handful of events,
     *  and is always the SAFE direction to err in: a stale cache would mean
     *  wrong note positions and wrong playback timing.
     *
     *  Both accessors are inline on purpose: they must not create a link
     *  dependency on MidiChannel.cpp for the event TUs above.
     *
     *  ATOMIC because it is written on the document thread and read by the
     *  cache-rebuild check, and a plain quint64 read/written from two threads
     *  is a data race even where the hardware would have made it look benign.
     *  Acquire/release, not relaxed: the reader must see the map mutation that
     *  came BEFORE the bump, not just the new counter value.
     */
    static quint64 tempoRevision() { return _tempoRevision.load(std::memory_order_acquire); }

    /**
     * \brief Marks the tempo map as changed. Call after ANY mutation of a
     *  channel-17 event map or of a tempo event's BPM/position.
     */
    static void bumpTempoRevision() { _tempoRevision.fetch_add(1, std::memory_order_release); }

    /**
     * \brief Inserts a new note into this channel.
     * \param note MIDI note number (0-127)
     * \param startTick Start time in MIDI ticks
     * \param endTick End time in MIDI ticks
     * \param velocity Note velocity (0-127)
     * \param track The MIDI track to associate with the note
     * \param toProtocol when false, skips the full-channel snapshot - callers
     *        doing BULK inserts must take one MidiChannel::copy() per touched
     *        channel themselves and commit it via protocol() afterwards (see
     *        MidiFile::removeTrack for the pattern); otherwise each call
     *        deep-clones the whole event map into the open undo step.
     * \return Pointer to the created NoteOnEvent
     */
    NoteOnEvent *insertNote(int note, int startTick, int endTick, int velocity, MidiTrack *track,
                            bool toProtocol = true);

    /**
     * \brief Inserts an event into the channel's event map.
     * \param event The MIDI event to insert
     * \param tick The time position in MIDI ticks
     * \param toProtocol Whether to record this change in the protocol
     */
    void insertEvent(MidiEvent *event, int tick, bool toProtocol = true);

    /**
     * \brief Removes an event from the channel's event map.
     * \param event The MIDI event to remove
     * \param toProtocol Whether to record this change in the protocol
     * \return True if the event was found and removed
     */
    bool removeEvent(MidiEvent *event, bool toProtocol = true);

    /**
     * \brief Gets the program number active at the specified tick.
     *
     * Among several program changes at the same tick the most recently
     * inserted one wins - the one playback and the saved file apply last.
     * \param tick The time position to query
     * \return The MIDI program number (0-127) active at that time, 0 when
     *         the channel has no program change at or before \a tick
     */
    int progAtTick(int tick);

    /**
     * \brief Removes all events from the channel.
     */
    void deleteAllEvents();

    // === Display and Playback Control ===

    /**
     * \brief Gets the channel's visibility state.
     * \return True if the channel is visible in the MatrixWidget
     */
    bool visible();

    /**
     * \brief Sets the channel's visibility state.
     * \param b True to show the channel, false to hide it
     */
    void setVisible(bool b);

    /**
     * \brief Phase 9.9f §15.2 (Show-Mode follow-the-host): flip the
     * visibility WITHOUT recording a Protocol step. Used on the
     * viewer side to apply the presenter's view state silently —
     * viewers shouldn't have a hat-pass land in their undo history.
     * Caller must trigger a repaint manually when applying a batch.
     *
     * Implementation lives in the .cpp because visibility is stored
     * in the ChannelVisibilityManager singleton (NOT in _visible
     * directly — visible() reads through the manager). Bugfix
     * 2026-05-21: an earlier version only updated _visible, which
     * is dead state for visibility lookups.
     */
    void setVisibleSilent(bool b);

    /**
     * \brief Gets the channel's mute state.
     * \return True if the channel is muted (no sound output)
     */
    bool mute();

    /**
     * \brief Sets the channel's mute state.
     * \param b True to mute the channel, false to unmute it
     */
    void setMute(bool b);

    /**
     * \brief Gets the channel's solo state.
     * \return True if the channel is in solo mode
     *
     * When a channel is in solo mode, all other channels are effectively muted.
     */
    bool solo();

    /**
     * \brief Sets the channel's solo state.
     * \param b True to enable solo mode, false to disable it
     */
    void setSolo(bool b);

    // === Protocol System Integration ===

    /**
     * \brief Creates a copy of this channel for the protocol system.
     * \return A new ProtocolEntry representing this channel's state
     */
    ProtocolEntry *copy();

    /**
     * \brief Reloads the channel's state from a protocol entry.
     * \param entry The protocol entry to restore state from
     */
    void reloadState(ProtocolEntry *entry);

protected:
    /** \brief The parent MIDI file */
    MidiFile *_midiFile;

    /** \brief Channel state flags */
    bool _visible, _mute, _solo;

    /** \brief Event map organized by MIDI tick time */
    QMultiMap<int, MidiEvent *> *_events;

    /** \brief The channel number (0-18) */
    int _num;

    /** \brief v2.2 #3: undo-snapshot counters, see snapshotCount(). Not
     *  copied by the copy ctor (it lists its fields explicitly), not touched
     *  by reloadState(). */
    qint64 _snapshotCount = 0;
    qint64 _snapshotNodeSum = 0;

    /** \brief Phase 48: see tempoRevision(). Inline static so no TU needs a
     *  link dependency on MidiChannel.cpp just to bump it. */
    inline static std::atomic<quint64> _tempoRevision{0};
};

#endif // MIDICHANNEL_H_
