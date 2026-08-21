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

#ifndef MIDIFILE_H_
#define MIDIFILE_H_

// Project includes
#include "../protocol/ProtocolEntry.h"

// Qt includes
#include <QHash>
#include <QMultiMap>
#include <QMutex>
#include <QObject>

// Standard includes
#include <vector>

// Forward declarations
class MidiEvent;
class TimeSignatureEvent;
class TempoChangeEvent;
class Protocol;
class MidiChannel;
class MidiTrack;
class LyricManager;

/**
 * \class MidiFile
 *
 * \brief Core class representing a complete MIDI file with all its data and functionality.
 *
 * MidiFile is the central class of the MIDI editor, managing all aspects of MIDI
 * file handling, playback, and editing. It provides comprehensive functionality for:
 *
 * - **File I/O**: Loading and saving MIDI files in standard format
 * - **Track management**: Multiple MIDI tracks with independent content
 * - **Channel management**: 16 MIDI channels per track
 * - **Event management**: All types of MIDI events and meta-events
 * - **Timing**: Tempo changes, time signatures, and timing calculations
 * - **Protocol integration**: Full undo/redo support for all operations
 * - **Playback support**: Integration with MIDI playback systems
 *
 * Key features:
 * - Standard MIDI file format support (Type 0 and Type 1)
 * - Real-time event manipulation and editing
 * - Comprehensive timing and measure calculations
 * - Multi-track and multi-channel organization
 * - Protocol-based undo/redo for all changes
 * - Event filtering and selection capabilities
 * - Tempo and time signature management
 *
 * The class serves as both a data container and a controller, managing
 * the relationships between tracks, channels, events, and timing information.
 *
 * \par Thread safety
 * MidiFile is a GUI-thread object with ONE documented exception: the timing
 * conversions are also called from the playback thread. PlayerThread::run()
 * asks msOfTick() for its start position and PlayerThread::timeout() asks
 * tick(ms) every 15 ms, both on the player thread, while the user keeps
 * editing on the GUI thread. Those conversions read a cached tempo map that
 * any edit can invalidate, so the cache is guarded by an internal mutex and
 * the following methods are safe to call concurrently from any thread:
 *
 *   - msOfTick(tick) (the events == nullptr form)
 *   - tick(ms)
 *   - tick(startms, endms, ...)
 *   - calcMaxTime()
 *   - invalidateTempoCache()
 *
 * Nothing else on this class is thread-safe: every other event/channel
 * accessor reads the channel maps without synchronisation, and no thread but
 * the GUI thread may MUTATE the file. PlayerThread also calls measure(), which
 * walks channel 18 unguarded - that predates the tempo cache and is not
 * covered here; keep it in mind before moving more work onto the player
 * thread.
 */
class MidiFile : public QObject, public ProtocolEntry {
    Q_OBJECT

public:
    /**
     * \brief Creates a new MidiFile by loading from a file path.
     * \param path Path to the MIDI file to load
     * \param ok Pointer to bool indicating success/failure
     * \param log Optional string list to receive loading messages
     */
    MidiFile(QString path, bool *ok, QStringList *log = 0);

    /**
     * \brief Creates a new empty MidiFile.
     */
    MidiFile();

    /**
     * \brief Creates a new MidiFile for protocol operations.
     * \param maxTime Maximum time in ticks
     * \param p Protocol instance for undo/redo support
     */
    MidiFile(int maxTime, Protocol *p);

    /**
     * \brief Destroys the MidiFile and cleans up all resources.
     */
    ~MidiFile();

    // === File I/O Operations ===

    /**
     * \brief Saves the MIDI file to the specified path.
     * \param path File path to save to
     * \param skipMutedTrackEvents When true, tracks marked as muted are
     *        written as empty track chunks (header + end-of-track only).
     *        Used by the audio export path so muted tracks are silent in
     *        the rendered file, mirroring live-playback mute behaviour.
     * \param drumProgramByTrackName When non-empty, every NoteOn on
     *        channel 9 from a track whose name is a key in this map is
     *        prefixed with a Program Change event (channel 9, value =
     *        the mapped program). Used by the audio export path so
     *        offline FluidSynth picks the same FFXIV percussion preset
     *        for each drum track that the live MidiOutput injects per
     *        NoteOn — without this a Snare Drum track whose hits land
     *        on a non-GM key would fall through to the bongo preset.
     * \return True if save was successful, false otherwise
     */
    bool save(QString path, bool skipMutedTrackEvents = false,
              const QHash<QString, int> &drumProgramByTrackName = QHash<QString, int>());

    /**
     * \brief Writes a delta time value to a byte array.
     * \param time The time value to encode
     * \return QByteArray containing the encoded delta time
     */
    QByteArray writeDeltaTime(int time);

    // === Timing and Measurement ===

    /**
     * \brief Gets the maximum time of all events in the file.
     * \return Maximum time in MIDI ticks
     */
    int maxTime();

    /**
     * \brief Gets the end tick of the file (last event + duration).
     * \return End time in MIDI ticks
     */
    int endTick();

    /**
     * \brief Set the file's end tick directly. Protocol-aware (push/pop a
     *  state for undo) and triggers a maxTime recompute. Used by the
     *  collaboration sync layer to propagate Insert-measures changes
     *  that grow the file length without adding any events on the
     *  shifted side. No-op if \a tick equals the current value.
     */
    void setEndTick(int tick);

    /**
     * \brief Converts MIDI time to milliseconds.
     * \param midiTime Time in MIDI ticks
     * \return Time in milliseconds
     */
    int timeMS(int midiTime);

    /**
     * \brief Gets measure information for a given tick position.
     * \param startTick The tick position to query
     * \param startTickOfMeasure Pointer to receive measure start tick.
     *        MUST NOT be null - it is dereferenced unconditionally.
     * \param endTickOfMeasure Pointer to receive measure end tick.
     *        MUST NOT be null - it is dereferenced unconditionally.
     * \return The measure number, 1-BASED: tick 0 is in measure 1. This is
     *         the number the status bar, the time display and the measure
     *         tool show, so it is what the user sees - never add 1 to it.
     *         Matches startTickOfMeasure(), which is 1-based as well.
     */
    int measure(int startTick, int *startTickOfMeasure, int *endTickOfMeasure);

    /**
     * \brief Number of measures the song occupies - the 1-based bar number of
     *        the last SOUNDING tick, i.e. measure(endTick() - 1).
     *
     *        Do NOT use measure(endTick()) for this. endTick() is the EXCLUSIVE
     *        end of the song, so a file that ends exactly on a bar line (File >
     *        New: 7680 ticks = 10 bars of 4/4 at 192 ticks/quarter) has its end
     *        tick in the first tick of the NEXT bar and measure() then reports
     *        one bar too many. Every "how many bars does this song have?"
     *        caller must go through here so they cannot drift apart.
     * \return The bar count, always >= 1 (an empty file still shows one bar).
     */
    int measureCount();

    // === Event Access and Management ===

    /**
     * \brief Gets the map of tempo change events.
     * \return Pointer to QMap containing tempo events organized by tick
     */
    QMultiMap<int, MidiEvent *> *tempoEvents();

    /**
     * \brief Gets the map of time signature events.
     * \return Pointer to QMultiMap containing time signature events organized by tick
     */
    QMultiMap<int, MidiEvent *> *timeSignatureEvents();

    /**
     * \brief Recalculates the maximum time of all events in the file.
     *
     *  Reads the tempo cache under the cache mutex, then emits
     *  recalcWidgetSize() with the lock released - a directly connected slot
     *  repaints, and a repaint asks msOfTick() again.
     */
    void calcMaxTime();

    // === Time Conversion Methods ===

    /**
     * \brief Converts milliseconds to MIDI ticks.
     *
     *  Thread-safe (see the class-level "Thread safety" note): PlayerThread
     *  calls this every 15 ms while the GUI thread edits.
     * \param ms Time in milliseconds
     * \return Time in MIDI ticks
     */
    int tick(int ms);

    /**
     * \brief Gets events and timing information for a time range.
     *
     *  Thread-safe. The returned list holds pointers to the file's own tempo
     *  events, so it is only valid as long as the caller does not let the GUI
     *  thread delete them - the same lifetime rule as before the cache.
     * \param startms Start time in milliseconds
     * \param endms End time in milliseconds
     * \param events Pointer to receive list of events in range
     * \param endTick Pointer to receive end tick
     * \param msOfFirstEvent Pointer to receive timing of first event
     * \return Start tick of the time range
     */
    int tick(int startms, int endms, QList<MidiEvent *> **events, int *endTick, int *msOfFirstEvent);

    /**
     * \brief Gets measure information for a tick range.
     * \param startTick Start tick position
     * \param endTick End tick position
     * \param eventList Pointer to receive list of time signature events
     * \param tickInMeasure Optional pointer to receive tick within measure
     * \return The measure number
     */
    int measure(int startTick, int endTick, QList<TimeSignatureEvent *> **eventList, int *tickInMeasure = 0);

    /**
     * \brief Converts MIDI ticks to milliseconds with event context.
     * \param tick Time in MIDI ticks
     * \param events Optional list of events for timing context. When null
     *        (the normal case - grid lines, notes, cursor, playback prep)
     *        the answer comes out of the O(log n) tempo cache; when a list
     *        is given the original linear walk over THAT list is used
     *        unchanged, because the list is the caller's own window into
     *        the tempo map and carries its own time origin.
     *
     *        The cached form is thread-safe (PlayerThread::run() calls it for
     *        its start position). The \a events form is NOT: it walks a list
     *        the caller owns, so the caller keeps that list alive itself.
     * \param msOfFirstEventInList Timing reference for first event
     * \return Time in milliseconds
     */
    int msOfTick(int tick, QList<MidiEvent *> *events = 0, int msOfFirstEventInList = 0);

    /**
     * \brief Drops the cached tempo map, forcing a rebuild on the next
     *        timing query.
     *
     *  Normal mutations do not need this: every channel-17 mutation path
     *  bumps MidiChannel::tempoRevision(), and the cache also re-checks the
     *  size of the tempo map on every query. It exists for the wholesale
     *  state swaps MidiFile itself performs (undo/redo, loading).
     *
     *  Thread-safe, but note what it does NOT promise: it only marks the cache
     *  stale. A concurrent player-thread query may still be mid-lookup and
     *  will finish against the old anchors before the rebuild happens - which
     *  is the pre-existing behaviour of editing during playback, not a new
     *  hazard.
     */
    void invalidateTempoCache();

    /**
     * \brief Gets all events between two tick positions.
     * \param start Start tick position
     * \param end End tick position
     * \return Pointer to list of events in the specified range
     */
    QList<MidiEvent *> *eventsBetween(int start, int end);

    /**
     * \brief Gets the ticks per quarter note resolution.
     * \return Number of ticks per quarter note
     */
    int ticksPerQuarter();

    // === Channel and Protocol Access ===

    /**
     * \brief Gets all events for a specific MIDI channel.
     * \param channel The MIDI channel number (0-15)
     * \return Pointer to QMultiMap containing channel events organized by tick
     */
    QMultiMap<int, MidiEvent *> *channelEvents(int channel);

    /**
     * \brief Gets the protocol system for undo/redo operations.
     * \return Pointer to the Protocol instance
     */
    Protocol *protocol();

    /**
     * \brief Gets the lyric manager for this file.
     * \return Pointer to the LyricManager instance
     */
    LyricManager *lyricManager();

    /**
     * \brief Gets a specific MIDI channel.
     * \param i The channel number (0-18, where 16-18 are special channels)
     * \return Pointer to the MidiChannel instance
     */
    MidiChannel *channel(int i);

    // === Playback Support ===

    /**
     * \brief Prepares player data starting from a specific tick.
     * \param tickFrom The starting tick position for playback preparation
     */
    void preparePlayerData(int tickFrom);

    /**
     * \brief Gets the prepared player data.
     * \return Pointer to QMultiMap containing events organized for playback
     */
    QMultiMap<int, MidiEvent *> *playerData();

    // === Static Utility Methods ===

    /**
     * @brief Gets the name of a General MIDI instrument.
     * @param prog The program number (0-127)
     * @return String name of the instrument
     */
    static QString instrumentName(int prog);

    /**
     * @brief Gets the default GM name of an instrument (ignoring custom definitions).
     * @param prog The program number (0-127)
     * @return String name of the instrument
     */
    static QString gmInstrumentName(int prog);

    /**
     * \brief Gets the name of a MIDI control change.
     * \param control The control change number (0-127)
     * \return String name of the control change
     */
    static QString controlChangeName(int control);

    // === Cursor and Position Management ===

    /**
     * \brief Gets the current cursor position.
     * \return Cursor position in MIDI ticks
     */
    int cursorTick();

    /**
     * \brief Gets the pause position.
     * \return Pause position in MIDI ticks
     */
    int pauseTick();

    /**
     * \brief Sets the cursor position.
     * \param tick New cursor position in MIDI ticks
     */
    void setCursorTick(int tick);

    /**
     * \brief Sets the pause position.
     * \param tick New pause position in MIDI ticks
     */
    void setPauseTick(int tick);

    // === File Management ===

    /**
     * \brief Gets the file path.
     * \return String path to the MIDI file
     */
    QString path();

    /**
     * \brief Checks if the file has been saved.
     * \return True if the file is saved, false if modified
     */
    bool saved();

    /**
     * \brief Sets the saved state of the file.
     * \param b True to mark as saved, false to mark as modified
     */
    void setSaved(bool b);

    /**
     * \brief Sets the file path.
     * \param path New file path string
     */
    void setPath(QString path);

    // === Channel and Track Management ===

    /**
     * \brief Checks if a channel is muted.
     * \param ch The channel number to check
     * \return True if the channel is muted
     */
    bool channelMuted(int ch);

    /**
     * \brief Gets the number of tracks in the file.
     * \return Number of MIDI tracks
     */
    int numTracks();

    /**
     * \brief Gets the list of all tracks.
     * \return Pointer to QList containing all MidiTrack instances
     */
    QList<MidiTrack *> *tracks();

    /**
     * \brief Adds a new track to the file.
     */
    void addTrack();

    /**
     * \brief Removes a track from the file.
     * \param track The MidiTrack to remove
     * \return True if the track was successfully removed
     */
    bool removeTrack(MidiTrack *track);

    /**
     * \brief Moves a track one slot up/down (delta -1 / +1) in the track list
     *        and renumbers all tracks, as one protocolled operation (the file
     *        snapshot restores the LIST ORDER on undo, not just the numbers).
     * \return False when the move is out of range.
     */
    bool moveTrack(MidiTrack *track, int delta);

    // === File Structure Modification ===

    /**
     * \brief Sets the maximum length of the file in milliseconds.
     * \param ms Maximum length in milliseconds
     */
    void setMaxLengthMs(int ms);

    /**
     * \brief Deletes a range of measures from the file.
     * \param from Starting measure number
     * \param to Ending measure number
     */
    void deleteMeasures(int from, int to);

    /**
     * \brief Inserts empty measures into the file.
     * \param after Measure number to insert after
     * \param numMeasures Number of measures to insert
     */
    void insertMeasures(int after, int numMeasures);

    // === Protocol System Integration ===

    /**
     * \brief Creates a copy of this file for the protocol system.
     * \return A new ProtocolEntry representing this file's state
     */
    ProtocolEntry *copy();

    /**
     * \brief Reloads the file's state from a protocol entry.
     * \param entry The protocol entry to restore state from
     */
    void reloadState(ProtocolEntry *entry);

    /**
     * \brief Gets this file instance (for ProtocolEntry interface).
     * \return Pointer to this MidiFile
     */
    MidiFile *file();

    /**
     * \brief Gets a track by its number.
     * \param number The track number to retrieve
     * \return Pointer to the MidiTrack, or nullptr if not found
     */
    MidiTrack *track(int number);

    /**
     * \brief Gets the tonality (key signature) at a specific tick.
     * \param tick The tick position to query
     * \return The tonality value (positive for sharps, negative for flats)
     */
    int tonalityAt(int tick);

    /**
     * \brief Gets the time signature at a specific tick.
     * \param tick The tick position to query
     * \param num Pointer to receive the numerator
     * \param denum Pointer to receive the denominator as the SMF POWER-OF-TWO
     *        EXPONENT, not the printed number: 2 means /4, 3 means /8. To show
     *        or divide by it, use `1 << *denum`.
     * \param lastTimeSigEvent Optional pointer to receive the time signature
     *        event the reported meter comes from. Set to NULLPTR when channel
     *        18 holds no event at or before \a tick - and that null is the ONLY
     *        way to tell that case apart, because the fallback then reported in
     *        num/denum is a valid 4/4 (4, 2). Never decide "is the meter still
     *        anchored?" by comparing num/denum.
     */
    void meterAt(int tick, int *num, int *denum, TimeSignatureEvent **lastTimeSigEvent = 0);

    // === Static Utility Methods ===

    /**
     * \brief Reads a variable-length value from a MIDI data stream.
     * \param content The data stream to read from
     * \return The decoded variable-length value
     */
    static int variableLengthvalue(QDataStream *content);

    /**
     * \brief Encodes a value as a variable-length MIDI value.
     * \param value The value to encode
     * \return QByteArray containing the encoded variable-length value
     */
    static QByteArray writeVariableLengthValue(int value);

    /** \brief Default ticks per quarter note for new files */
    static int defaultTimePerQuarter;

    // === Copy/Paste Support ===

    /**
     * \brief Registers a track copy operation for paste functionality.
     * \param source The source track that was copied
     * \param destination The destination track for pasting
     * \param fileFrom The source file
     */
    void registerCopiedTrack(MidiTrack *source, MidiTrack *destination, MidiFile *fileFrom);

    /**
     * \brief Gets the appropriate track for pasting copied content.
     * \param source The source track that was copied
     * \param fileFrom The source file
     * \return Pointer to the track to paste into
     */
    MidiTrack *getPasteTrack(MidiTrack *source, MidiFile *fileFrom);

    // === Quantization and Timing ===

    /**
     * \brief Gets quantization tick values for a given fraction size.
     * \param fractionSize The fraction size for quantization
     * \return List of tick values for quantization grid
     */
    QList<int> quantization(int fractionSize);

    /**
     * \brief Gets the starting tick of a specific measure.
     * \param measure The measure number
     * \return The tick position where the measure starts
     */
    int startTickOfMeasure(int measure);

signals:
    /**
     * \brief Emitted when the cursor position changes.
     */
    void cursorPositionChanged();

    /**
     * \brief Emitted when widgets need to recalculate their size.
     */
    void recalcWidgetSize();

    /**
     * \brief Emitted when track information changes.
     */
    void trackChanged();

private:
    // === File Reading Methods ===

    /**
     * \brief Reads a complete MIDI file from a data stream.
     * \param content The data stream containing MIDI data
     * \param log Optional string list to receive loading messages
     * \return True if reading was successful
     */
    bool readMidiFile(QDataStream *content, QStringList *log);

    /**
     * \brief Reads a single MIDI track from a data stream.
     * \param content The data stream containing track data
     * \param num The track number being read
     * \param log Optional string list to receive loading messages
     * \return True if reading was successful
     */
    bool readTrack(QDataStream *content, int num, QStringList *log);

    /**
     * \brief Reads a delta time value from a data stream.
     * \param content The data stream to read from
     * \return The delta time value
     */
    int deltaTime(QDataStream *content);

    /**
     * \brief Prints log messages to debug output.
     * \param log The string list containing log messages
     */
    void printLog(QStringList *log);

    /**
     * \brief Length of one measure in ticks for a meter given as the
     *        (numerator, power-of-two denominator EXPONENT) pair that
     *        meterAt() reports - i.e. 4 * num * ticksPerQuarter() / (1 <<
     *        denumPow).
     *
     *        Used wherever channel 18 holds no TimeSignatureEvent to ask:
     *        those paths used to dereference a null (or, in deleteMeasures()
     *        and insertMeasures(), an uninitialised) TimeSignatureEvent*.
     * \return The measure length, always >= 1.
     */
    int ticksPerMeasureOfMeter(int num, int denumPow);

    // === Tempo map cache (Phase 48) ===

    /**
     * \brief One entry of the cached tempo map: a tempo change, the time in
     *        milliseconds at which it takes effect, and the ms-per-tick that
     *        is valid from there until the next anchor.
     *
     *  The vector is sorted by \a tick (the tempo map is an ordered
     *  QMultiMap) and, because msPerTick is always positive, by \a msAtTick
     *  as well - so both directions of the conversion are a binary search.
     */
    struct TempoAnchor {
        int tick;
        double msAtTick;
        double msPerTick;
        TempoChangeEvent *event;
    };

    /**
     * \brief Rebuilds the tempo cache if it can no longer be trusted.
     *
     *  PRECONDITION: the caller already holds \a _tempoCacheMutex - hence the
     *  name. It must never call a public (locking) method of this class, or a
     *  query would deadlock on the non-recursive mutex; the only outward call
     *  it makes is TempoChangeEvent::msPerTick(), which reads
     *  ticksPerQuarter() and takes no lock.
     *
     *  Two independent triggers, both cheap:
     *   - MidiChannel::tempoRevision() differs from the value the cache was
     *     built at (every channel-17 mutation bumps it), and
     *   - the size of the tempo map differs from the size recorded at build
     *     time. This is the safety net for mutation sites that bypass
     *     MidiChannel entirely and write into eventMap() directly. It cannot
     *     replace the revision counter - same-size mutations exist (a BPM
     *     edit, a moved tempo event) - but it catches drift for free.
     *
     *  The revision counter is process-wide, so an edit in ANY open document
     *  can force a rebuild here. That only ever costs a wasted walk: the walk
     *  reads THIS file's own tempo map, which only this file's GUI thread
     *  writes.
     *
     *  A rebuild is ONE linear walk, i.e. exactly the work the old
     *  msOfTick() did on every single query.
     */
    void ensureTempoCacheLocked();

    /**
     * \brief Index of the anchor that governs \a tick, or -1 when the file
     *        has no tempo events at all. Ticks before the first anchor are
     *        governed by that first anchor (extrapolated backwards), which
     *        is what the original linear walk did.
     *
     *  PRECONDITION: \a _tempoCacheMutex held and the cache already ensured.
     *  The returned index is only meaningful while that lock is held.
     */
    int tempoAnchorIndexForTickLocked(int tick) const;

    /** \brief Index of the anchor that governs \a ms, or -1 if there is none.
     *  Same precondition as tempoAnchorIndexForTickLocked(). */
    int tempoAnchorIndexForMsLocked(double ms) const;

    /** \brief Cached msOfTick() in full double precision. Takes the cache
     *  mutex itself; do not call it with the lock already held. */
    double msOfTickCached(int tick);

    // === Private Member Variables ===

    /**
     * \brief Serialises every access to the tempo cache below.
     *
     *  Phase 48 turned the timing conversions into calls that may REBUILD
     *  _tempoCache, and PlayerThread runs them concurrently with GUI-thread
     *  edits (see the class-level "Thread safety" note). Without this, one
     *  thread could iterate the vector while the other reallocated it.
     *
     *  Held for the WHOLE query, not just the rebuild - a lookup that returned
     *  an index or a reference and then released the lock would still be
     *  reading a vector another thread is free to clear. Every lookup therefore
     *  copies the one 24-byte anchor it needs out while the lock is held.
     *  Uncontended it is a few nanoseconds and allocates nothing, which is what
     *  the hot path (one call per grid line, note and cursor per paint)
     *  requires.
     *
     *  Mutable so the const lookups can lock; recursive locking is NOT
     *  supported, so no method that holds it may call a public method.
     */
    mutable QMutex _tempoCacheMutex;

    /** \brief Phase 48: sorted tempo anchors; see ensureTempoCacheLocked().
     *  Guarded by _tempoCacheMutex.
     *  Deliberately NOT part of copy()/reloadState(): protocol snapshots must
     *  stay cheap, and reloadState() invalidates instead. */
    std::vector<TempoAnchor> _tempoCache;
    bool _tempoCacheValid = false;
    quint64 _tempoCacheRevision = 0;
    int _tempoCacheEventCount = -1;

    /** \brief Ticks per quarter note resolution */
    int timePerQuarter;

    /** \brief Array of MIDI channels (0-15 standard, 16-18 special) */
    MidiChannel *channels[19];

    /** \brief File path and basic properties */
    QString _path;
    int midiTicks, maxTimeMS, _cursorTick, _pauseTick, _midiFormat;

    /** \brief Protocol system for undo/redo */
    Protocol *prot;

    /** \brief Lyric manager for lyric block operations */
    LyricManager *_lyricManager;

    /** \brief Player data and state */
    QMultiMap<int, MidiEvent *> *playerMap;
    bool _saved;

    /** \brief Track management */
    QList<MidiTrack *> *_tracks;
    QMap<MidiFile *, QMap<MidiTrack *, MidiTrack *> > pasteTracks;
};

#endif // MIDIFILE_H_
