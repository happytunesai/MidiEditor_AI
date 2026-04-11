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

#ifndef SRTPARSER_H
#define SRTPARSER_H

#include "../midi/LyricBlock.h"

#include <QList>
#include <QString>

class MidiFile;

/**
 * \class SrtParser
 *
 * \brief Imports and exports SRT subtitle files as LyricBlocks.
 *
 * SRT (SubRip Text) is a simple subtitle format with timestamps
 * and text. This class converts between SRT files and LyricBlocks
 * using MidiFile tick/ms conversion.
 *
 * SRT format:
 * \code
 * 1
 * 00:00:05,000 --> 00:00:10,500
 * First line of the verse
 *
 * 2
 * 00:00:10,800 --> 00:00:15,200
 * Second line continues here
 * \endcode
 */
class SrtParser {
public:
    /**
     * \brief Imports an SRT file and returns a list of LyricBlocks.
     * \param filePath Path to the SRT file
     * \param file MidiFile for tick/ms conversion
     * \return List of LyricBlocks with timing from the SRT file
     */
    static QList<LyricBlock> importSrt(const QString &filePath, MidiFile *file);

    /**
     * \brief Exports a list of LyricBlocks to an SRT file.
     * \param filePath Path to write the SRT file
     * \param blocks The lyric blocks to export
     * \param file MidiFile for tick/ms conversion
     * \return True if export was successful
     */
    static bool exportSrt(const QString &filePath, const QList<LyricBlock> &blocks, MidiFile *file);

private:
    /**
     * \brief Converts an SRT time string to milliseconds.
     * \param timeStr Time string in format "HH:MM:SS,mmm"
     * \return Time in milliseconds, or -1 on parse error
     */
    static int timeStringToMs(const QString &timeStr);

    /**
     * \brief Converts milliseconds to an SRT time string.
     * \param ms Time in milliseconds
     * \return Time string in format "HH:MM:SS,mmm"
     */
    static QString msToTimeString(int ms);
};

#endif // SRTPARSER_H
