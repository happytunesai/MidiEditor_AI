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

#ifndef LYRICBLOCK_H
#define LYRICBLOCK_H

#include <QString>

class MidiEvent;

/**
 * \struct LyricBlock
 *
 * \brief Represents a single lyric phrase with timing information.
 *
 * LyricBlock stores one line or syllable of lyrics along with its
 * start and end positions in MIDI ticks. Blocks can exist independently
 * (e.g., imported from plain text) or be linked to an underlying TextEvent.
 */
struct LyricBlock {
    int startTick = 0;      ///< Start position in MIDI ticks
    int endTick = 0;        ///< End position in MIDI ticks
    QString text;            ///< The lyric text (one line/syllable/phrase)
    int trackIndex = -1;     ///< Assigned track (-1 = global/meta channel 16)

    /**
     * \brief Pointer to the underlying TextEvent, if this block was imported
     *        from MIDI. nullptr for blocks created externally (SRT, plain text).
     */
    MidiEvent *sourceEvent = nullptr;

    /** \brief Duration in ticks */
    int durationTicks() const { return endTick - startTick; }

    /** \brief Whether this block has valid timing data */
    bool isValid() const { return endTick > startTick && !text.trimmed().isEmpty(); }
};

#endif // LYRICBLOCK_H
