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

#include "SrtParser.h"

#include "../midi/MidiFile.h"

#include <QFile>
#include <QRegularExpression>
#include <QTextStream>

QList<LyricBlock> SrtParser::importSrt(const QString &filePath, MidiFile *file)
{
    QList<LyricBlock> blocks;
    if (!file) return blocks;

    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return blocks;
    }

    QTextStream stream(&f);
    stream.setEncoding(QStringConverter::Utf8);

    // SRT state machine: expect sequence number, then timing, then text, then blank
    enum State { ExpectSeq, ExpectTiming, ExpectText };
    State state = ExpectSeq;

    static const QRegularExpression timingRe(
        R"((\d{2}):(\d{2}):(\d{2})[,.](\d{3})\s*-->\s*(\d{2}):(\d{2}):(\d{2})[,.](\d{3}))");

    int startMs = 0, endMs = 0;
    QStringList textLines;

    // Strip BOM if present
    QString firstLine;
    bool firstLineRead = false;

    while (!stream.atEnd()) {
        QString line = stream.readLine();

        // Strip BOM from first line
        if (!firstLineRead) {
            firstLineRead = true;
            if (line.startsWith(QChar(0xFEFF))) {
                line = line.mid(1);
            }
        }

        switch (state) {
        case ExpectSeq:
            // Skip blank lines between entries
            if (line.trimmed().isEmpty()) continue;
            // Expect a sequence number (just skip it)
            if (line.trimmed().toInt() > 0 || line.trimmed() == "0") {
                state = ExpectTiming;
            }
            break;

        case ExpectTiming: {
            QRegularExpressionMatch match = timingRe.match(line);
            if (match.hasMatch()) {
                startMs = timeStringToMs(match.captured(1) + ":" +
                                         match.captured(2) + ":" +
                                         match.captured(3) + "," +
                                         match.captured(4));
                endMs = timeStringToMs(match.captured(5) + ":" +
                                       match.captured(6) + ":" +
                                       match.captured(7) + "," +
                                       match.captured(8));
                textLines.clear();
                state = ExpectText;
            } else {
                // Malformed — reset
                state = ExpectSeq;
            }
            break;
        }

        case ExpectText:
            if (line.trimmed().isEmpty()) {
                // End of this entry
                if (!textLines.isEmpty() && startMs >= 0 && endMs > startMs) {
                    LyricBlock block;
                    block.startTick = file->tick(startMs);
                    block.endTick = file->tick(endMs);
                    block.text = textLines.join(" ");
                    block.trackIndex = -1;
                    blocks.append(block);
                }
                state = ExpectSeq;
            } else {
                textLines.append(line.trimmed());
            }
            break;
        }
    }

    // Handle last entry (file may not end with blank line)
    if (state == ExpectText && !textLines.isEmpty() && startMs >= 0 && endMs > startMs) {
        LyricBlock block;
        block.startTick = file->tick(startMs);
        block.endTick = file->tick(endMs);
        block.text = textLines.join(" ");
        block.trackIndex = -1;
        blocks.append(block);
    }

    f.close();
    return blocks;
}

bool SrtParser::exportSrt(const QString &filePath, const QList<LyricBlock> &blocks, MidiFile *file)
{
    if (blocks.isEmpty() || !file) return false;

    QFile f(filePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }

    QTextStream stream(&f);
    stream.setEncoding(QStringConverter::Utf8);

    for (int i = 0; i < blocks.size(); i++) {
        const LyricBlock &block = blocks[i];

        int startMs = file->msOfTick(block.startTick);
        int endMs = file->msOfTick(block.endTick);

        stream << (i + 1) << "\n";
        stream << msToTimeString(startMs) << " --> " << msToTimeString(endMs) << "\n";
        stream << block.text << "\n";
        stream << "\n";
    }

    f.close();
    return true;
}

int SrtParser::timeStringToMs(const QString &timeStr)
{
    // Format: "HH:MM:SS,mmm"
    static const QRegularExpression re(R"((\d{2}):(\d{2}):(\d{2})[,.](\d{3}))");
    QRegularExpressionMatch match = re.match(timeStr);
    if (!match.hasMatch()) return -1;

    int hours = match.captured(1).toInt();
    int minutes = match.captured(2).toInt();
    int seconds = match.captured(3).toInt();
    int millis = match.captured(4).toInt();

    return hours * 3600000 + minutes * 60000 + seconds * 1000 + millis;
}

QString SrtParser::msToTimeString(int ms)
{
    if (ms < 0) ms = 0;

    int hours = ms / 3600000;
    ms %= 3600000;
    int minutes = ms / 60000;
    ms %= 60000;
    int seconds = ms / 1000;
    int millis = ms % 1000;

    return QString("%1:%2:%3,%4")
        .arg(hours, 2, 10, QChar('0'))
        .arg(minutes, 2, 10, QChar('0'))
        .arg(seconds, 2, 10, QChar('0'))
        .arg(millis, 3, 10, QChar('0'));
}
