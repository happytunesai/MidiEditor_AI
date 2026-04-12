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

#ifndef LYRICMANAGER_H
#define LYRICMANAGER_H

#include "LyricBlock.h"

#include <QList>
#include <QObject>
#include <QString>

class MidiFile;

/**
 * \struct LyricMetadata
 * \brief Metadata for lyric export (LRC header tags).
 */
struct LyricMetadata {
    QString artist;
    QString title;
    QString album;
    QString lyricsBy;
    int offsetMs = 0;

    bool isEmpty() const {
        return artist.isEmpty() && title.isEmpty() && album.isEmpty() &&
               lyricsBy.isEmpty() && offsetMs == 0;
    }
};

/**
 * \class LyricManager
 *
 * \brief Manages lyric blocks for a MIDI file.
 *
 * LyricManager provides a clean data model for lyric phrases that can
 * be imported from MIDI TextEvents (type 0x05 Lyric / 0x01 Text),
 * from SRT subtitle files, or from plain text. It maintains an ordered
 * list of LyricBlocks sorted by startTick.
 *
 * Key features:
 * - Import lyrics from existing MIDI TextEvents
 * - Add/remove/edit/move/resize individual blocks
 * - Export blocks back to MIDI TextEvents
 * - Undo support via Protocol system
 * - Signals for UI updates
 */
class LyricManager : public QObject {
    Q_OBJECT

public:
    explicit LyricManager(MidiFile *file, QObject *parent = nullptr);

    // === Access ===

    /** \brief Returns all lyric blocks, sorted by startTick */
    const QList<LyricBlock> &allBlocks() const;

    /** \brief Returns the block at the given index, or an invalid block if out of range */
    LyricBlock blockAt(int index) const;

    /** \brief Returns the block that contains the given tick, or an invalid block if none */
    LyricBlock blockAtTick(int tick) const;

    /** \brief Returns the index of the block that contains the given tick, or -1 */
    int indexAtTick(int tick) const;

    /** \brief Returns the number of blocks */
    int count() const;

    /** \brief Returns true if there are any lyric blocks */
    bool hasLyrics() const;

    // === Metadata ===

    /** \brief Returns the lyric metadata (artist, title, album, etc.) */
    const LyricMetadata &metadata() const;

    /** \brief Sets the lyric metadata */
    void setMetadata(const LyricMetadata &meta);

    /** \brief Returns true if any metadata fields are filled */
    bool hasMetadata() const;

    // === Editing (with undo support) ===

    /**
     * \brief Adds a new lyric block. Inserted in sorted order by startTick.
     *        Wraps in a Protocol action for undo support.
     */
    void addBlock(const LyricBlock &block);

    /**
     * \brief Removes the block at the given index.
     *        Also removes the underlying TextEvent from the MIDI file if linked.
     */
    void removeBlock(int index);

    /**
     * \brief Moves a block to a new start tick (preserves duration).
     */
    void moveBlock(int index, int newStartTick);

    /**
     * \brief Resizes a block by changing its end tick.
     */
    void resizeBlock(int index, int newEndTick);

    /**
     * \brief Edits the text of a block.
     *        Also updates the underlying TextEvent if linked.
     */
    void editBlockText(int index, const QString &newText);

    // === Direct editing (no Protocol wrapping — for batch/drag operations) ===

    /** \brief Moves a block without Protocol wrapping. Does NOT re-sort. */
    void moveBlockDirect(int index, int newStartTick);

    /** \brief Resizes a block without Protocol wrapping. */
    void resizeBlockDirect(int index, int newEndTick);

    /** \brief Edits block text without Protocol wrapping. */
    void editBlockTextDirect(int index, const QString &newText);

    /** \brief Removes a block without Protocol wrapping. */
    void removeBlockDirect(int index);

    /** \brief Inserts a block in sorted order AND emits lyricsChanged. No Protocol wrapping. (P3-007) */
    void addBlockDirect(const LyricBlock &block);

    // === Import ===

    /**
     * \brief Imports lyrics from existing MIDI TextEvents (type 0x05 Lyric and 0x01 Text).
     *        Scans all channels and builds sorted LyricBlock list.
     *        Does NOT wrap in Protocol (read-only scan).
     */
    void importFromTextEvents();

    /**
     * \brief Imports lyrics from plain text. Each non-empty line becomes one LyricBlock.
     * \param text The plain text with one phrase per line
     * \param startTick Where the first phrase begins
     * \param defaultDurationTicks Default duration per phrase
     * \param skipEmptyLines If true, empty lines are skipped (treated as separators)
     */
    void importFromPlainText(const QString &text, int startTick = 0,
                             int defaultDurationTicks = 960, bool skipEmptyLines = true);

    /**
     * \brief Imports lyrics from an SRT subtitle file.
     *        Creates TextEvents for each SRT entry and adds them to the block list.
     *        Wraps in a Protocol action for undo support.
     * \param srtPath Path to the SRT file
     */
    void importFromSrt(const QString &srtPath);

    // === Export ===

    /**
     * \brief Exports all lyric blocks as MIDI TextEvents (type 0x05 Lyric).
     *        Removes existing lyric events first, then creates new ones.
     *        Wraps in a Protocol action for undo support.
     */
    void exportToTextEvents();

    /**
     * \brief Exports all lyric blocks to an SRT subtitle file.
     * \param srtPath Path to write the SRT file
     * \return True if export was successful
     */
    bool exportToSrt(const QString &srtPath);

    // === Bulk Operations ===

    /**
     * \brief Removes all lyric blocks and their linked TextEvents.
     */
    void clearAllBlocks();

    /**
     * \brief Sets the associated MIDI file (called when file changes).
     */
    void setFile(MidiFile *file);

signals:
    /** \brief Emitted when the block list changes (add, remove, clear, import) */
    void lyricsChanged();

    /** \brief Emitted when a specific block is added */
    void blockAdded(int index);

    /** \brief Emitted when a specific block is removed */
    void blockRemoved(int index);

    /** \brief Emitted when a specific block is modified (text, position, size) */
    void blockModified(int index);

public:
    /** \brief Inserts a block at the correct sorted position. Returns the index. */
    int insertSorted(const LyricBlock &block);

    /** \brief Re-sorts blocks by startTick. Call after batch moveBlockDirect operations. (P3-005) */
    void sortBlocks();

private:
    MidiFile *_file;
    QList<LyricBlock> _blocks;
    LyricMetadata _metadata;
};

#endif // LYRICMANAGER_H
