#include "MmlConverter.h"
#include "MmlLexer.h"
#include "MmlParser.h"

int MmlConverter::extractTempo(const QString& mmlText) {
    // Scan for the first  t<digits>  in the raw text
    for (int i = 0; i < mmlText.length(); ++i) {
        if (mmlText[i].toLower() == 't' && i + 1 < mmlText.length() &&
            mmlText[i + 1].isDigit()) {
            ++i;
            QString num;
            while (i < mmlText.length() && mmlText[i].isDigit())
                num += mmlText[i++];
            int tempo = num.toInt();
            if (tempo > 0 && tempo <= 999)
                return tempo;
        }
    }
    return 120; // default BPM
}

MmlSong MmlConverter::convert(const QString& mmlText, int ticksPerQuarter) {
    MmlSong song;
    song.ticksPerQuarter = ticksPerQuarter;
    song.tempo = extractTempo(mmlText);

    // Standard MML: tracks separated by semicolons
    QStringList trackTexts = mmlText.split(';', Qt::SkipEmptyParts);

    for (int i = 0; i < trackTexts.size(); ++i) {
        QString text = trackTexts[i].trimmed();
        if (text.isEmpty())
            continue;

        QList<MmlToken> tokens = MmlLexer::tokenize(text);
        MmlTrack track = MmlParser::parse(tokens, ticksPerQuarter);
        track.name = QString("Track %1").arg(i + 1);
        track.channel = i % 16;

        if (!track.notes.isEmpty())
            song.tracks.append(track);
    }

    return song;
}
