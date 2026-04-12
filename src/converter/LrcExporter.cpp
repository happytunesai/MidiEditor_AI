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

#include "LrcExporter.h"

#include "../midi/MidiFile.h"

#include <QFile>
#include <QRegularExpression>
#include <QTextStream>
#include <algorithm>

bool LrcExporter::exportLrc(const QString &filePath,
                            const QList<LyricBlock> &blocks,
                            MidiFile *file,
                            const LyricMetadata &metadata)
{
    if (!file || blocks.isEmpty())
        return false;

    QFile out(filePath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream stream(&out);
    stream.setEncoding(QStringConverter::Utf8);

    // Write header tags
    if (!metadata.artist.isEmpty())
        stream << "[ar:" << metadata.artist << "]\n";
    if (!metadata.title.isEmpty())
        stream << "[ti:" << metadata.title << "]\n";
    if (!metadata.album.isEmpty())
        stream << "[al:" << metadata.album << "]\n";
    if (!metadata.lyricsBy.isEmpty())
        stream << "[by:" << metadata.lyricsBy << "]\n";
    if (metadata.offsetMs != 0)
        stream << "[offset:" << metadata.offsetMs << "]\n";

    // Write lyric lines
    for (const LyricBlock &block : blocks) {
        QString timestamp = tickToLrcTimestamp(block.startTick, file);
        stream << "[" << timestamp << "]" << block.text << "\n";
    }

    out.close();
    return true;
}

QList<LyricBlock> LrcExporter::importLrc(const QString &filePath, MidiFile *file,
                                         LyricMetadata *outMetadata)
{
    QList<LyricBlock> blocks;
    if (!file)
        return blocks;

    QFile in(filePath);
    if (!in.open(QIODevice::ReadOnly | QIODevice::Text))
        return blocks;

    QTextStream stream(&in);
    stream.setEncoding(QStringConverter::Utf8);

    // Regex for LRC timestamp lines: [MM:SS.cc]Text or [MM:SS.ccc]Text
    // Supports multi-timestamp lines like [00:12.34][01:56.78]Text (P3-011)
    static QRegularExpression timestampRx(R"(\[(\d{2}:\d{2}\.\d{2,3})\])");
    // Regex for header tags: [tag:value]
    static QRegularExpression headerRx(R"(^\[([a-z]+):(.+)\]$)");

    while (!stream.atEnd()) {
        QString line = stream.readLine().trimmed();
        if (line.isEmpty())
            continue;

        // Collect all timestamps on this line
        QRegularExpressionMatchIterator it = timestampRx.globalMatch(line);
        QStringList timestamps;
        int lastMatchEnd = 0;
        while (it.hasNext()) {
            QRegularExpressionMatch m = it.next();
            timestamps.append(m.captured(1));
            lastMatchEnd = m.capturedEnd(0);
        }

        if (!timestamps.isEmpty()) {
            // Text is everything after the last timestamp tag
            QString text = line.mid(lastMatchEnd).trimmed();
            if (text.isEmpty())
                continue;

            // Create one block per timestamp (same text)
            for (const QString &timestamp : timestamps) {
                int ms = lrcTimestampToMs(timestamp);
                int startTick = file->tick(ms);

                LyricBlock block;
                block.startTick = startTick;
                block.endTick = startTick + 480; // Default, will be adjusted
                block.text = text;
                blocks.append(block);
            }
        } else if (outMetadata) {
            // Parse header tags into metadata
            QRegularExpressionMatch hm = headerRx.match(line);
            if (hm.hasMatch()) {
                QString tag = hm.captured(1).toLower();
                QString val = hm.captured(2).trimmed();
                if (tag == "ar")
                    outMetadata->artist = val;
                else if (tag == "ti")
                    outMetadata->title = val;
                else if (tag == "al")
                    outMetadata->album = val;
                else if (tag == "by")
                    outMetadata->lyricsBy = val;
                else if (tag == "offset")
                    outMetadata->offsetMs = val.toInt();
            }
        }
    }

    in.close();

    // Sort blocks by startTick (multi-timestamp lines may produce out-of-order blocks)
    std::sort(blocks.begin(), blocks.end(),
              [](const LyricBlock &a, const LyricBlock &b) {
                  return a.startTick < b.startTick;
              });

    // Adjust end ticks: each block ends when the next begins
    for (int i = 0; i < blocks.size() - 1; i++) {
        blocks[i].endTick = blocks[i + 1].startTick;
    }
    // Last block: default 960 ticks (2 beats)
    if (!blocks.isEmpty()) {
        blocks.last().endTick = blocks.last().startTick + 960;
    }

    return blocks;
}

QString LrcExporter::tickToLrcTimestamp(int tick, MidiFile *file)
{
    int ms = file->msOfTick(tick);
    int totalCs = ms / 10; // centiseconds
    int cs = totalCs % 100;
    int totalSec = ms / 1000;
    int sec = totalSec % 60;
    int min = totalSec / 60;

    return QString("%1:%2.%3")
        .arg(min, 2, 10, QChar('0'))
        .arg(sec, 2, 10, QChar('0'))
        .arg(cs, 2, 10, QChar('0'));
}

int LrcExporter::lrcTimestampToMs(const QString &ts)
{
    // Format: MM:SS.cc or MM:SS.ccc
    static QRegularExpression tsRx(R"((\d{2}):(\d{2})\.(\d{2,3}))");
    QRegularExpressionMatch match = tsRx.match(ts);
    if (!match.hasMatch())
        return 0;

    int min = match.captured(1).toInt();
    int sec = match.captured(2).toInt();
    QString fracStr = match.captured(3);
    int ms;
    if (fracStr.length() == 2)
        ms = fracStr.toInt() * 10; // centiseconds → ms
    else
        ms = fracStr.toInt();      // milliseconds

    return (min * 60 + sec) * 1000 + ms;
}
