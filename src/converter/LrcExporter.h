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

#ifndef LRCEXPORTER_H
#define LRCEXPORTER_H

#include "../midi/LyricBlock.h"
#include "../midi/LyricManager.h"

#include <QList>
#include <QString>

class MidiFile;

/**
 * \class LrcExporter
 *
 * \brief Import/export lyrics in LRC format (compatible with MidiBard2).
 *
 * LRC format: [MM:SS.cc]Text
 * MidiBard2 extension: [MM:SS.cc]BardName:Text
 */
class LrcExporter {
public:
    static bool exportLrc(const QString &filePath,
                          const QList<LyricBlock> &blocks,
                          MidiFile *file,
                          const LyricMetadata &metadata = {});

    static QList<LyricBlock> importLrc(const QString &filePath, MidiFile *file,
                                       LyricMetadata *outMetadata = nullptr);

private:
    static QString tickToLrcTimestamp(int tick, MidiFile *file);
    static int lrcTimestampToMs(const QString &ts);
};

#endif // LRCEXPORTER_H
