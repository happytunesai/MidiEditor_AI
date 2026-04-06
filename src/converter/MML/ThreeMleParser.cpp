#include "ThreeMleParser.h"
#include "MmlLexer.h"
#include "MmlParser.h"

#include <QIODevice>
#include <QMap>
#include <QRegularExpression>
#include <QTextStream>

MmlSong ThreeMleParser::parse(const QString& text, int ticksPerQuarter) {
    MmlSong song;
    song.ticksPerQuarter = ticksPerQuarter;
    song.tempo = 120;

    // ── Pass 1: split into INI-style sections ─────────────────
    QString currentSection;
    QMap<QString, QString> settings;       // key → value  (lower-cased keys)
    QMap<int, QString>     channelMml;     // 1-based channel → accumulated MML

    QTextStream stream(const_cast<QString*>(&text), QIODevice::ReadOnly);

    while (!stream.atEnd()) {
        QString line = stream.readLine().trimmed();

        if (line.isEmpty() || line.startsWith("//"))
            continue;

        // Section header  [SomeSection]
        if (line.startsWith('[') && line.endsWith(']')) {
            currentSection = line.mid(1, line.length() - 2).trimmed();
            continue;
        }

        // Inside [Settings]
        if (currentSection.compare("Settings", Qt::CaseInsensitive) == 0) {
            int eq = line.indexOf('=');
            if (eq > 0) {
                QString key   = line.left(eq).trimmed().toLower();
                QString value = line.mid(eq + 1).trimmed();
                settings[key] = value;
            }
            continue;
        }

        // Inside [Channel<N>]
        if (currentSection.startsWith("Channel", Qt::CaseInsensitive)) {
            static QRegularExpression rxNum(QStringLiteral("\\d+"));
            QRegularExpressionMatch m = rxNum.match(currentSection);
            if (m.hasMatch()) {
                int chNum = m.captured(0).toInt();
                if (channelMml.contains(chNum))
                    channelMml[chNum] += QChar(' ') + line;
                else
                    channelMml[chNum] = line;
            }
        }
    }

    // ── Extract global settings ───────────────────────────────
    if (settings.contains("tempo")) {
        int t = settings["tempo"].toInt();
        if (t > 0 && t <= 999)
            song.tempo = t;
    }
    QString title = settings.value("title", QString());

    // ── Pass 2: parse each channel's MML ──────────────────────
    QList<int> chNums = channelMml.keys();
    std::sort(chNums.begin(), chNums.end());

    for (int chNum : chNums) {
        QString mml = channelMml[chNum].trimmed();
        if (mml.isEmpty())
            continue;

        QList<MmlToken> tokens = MmlLexer::tokenize(mml);
        MmlTrack track = MmlParser::parse(tokens, ticksPerQuarter);

        if (!title.isEmpty())
            track.name = QString("%1 - Ch%2").arg(title).arg(chNum);
        else
            track.name = QString("Channel %1").arg(chNum);

        track.channel = (chNum - 1) % 16; // 3MLE channels are 1-based

        if (!track.notes.isEmpty())
            song.tracks.append(track);
    }

    return song;
}
