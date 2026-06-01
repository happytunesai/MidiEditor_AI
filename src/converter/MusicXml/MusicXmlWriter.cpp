/*
 * MidiEditor AI - MusicXML export writer implementation (see MusicXmlWriter.h).
 */
#include "MusicXmlWriter.h"

#include "../Score/ScoreModel.h"

#include <QXmlStreamWriter>
#include <cmath>

using namespace score;

namespace {

void writeClef(QXmlStreamWriter &w, Clef clef) {
    w.writeStartElement("clef");
    if (clef == Clef::Bass) { w.writeTextElement("sign", "F"); w.writeTextElement("line", "4"); }
    else                    { w.writeTextElement("sign", "G"); w.writeTextElement("line", "2"); }
    w.writeEndElement();
}

void writeKey(QXmlStreamWriter &w, int fifths, bool minor) {
    w.writeStartElement("key");
    w.writeTextElement("fifths", QString::number(fifths));
    w.writeTextElement("mode", minor ? "minor" : "major");
    w.writeEndElement();
}

void writeTime(QXmlStreamWriter &w, int beats, int beatType) {
    w.writeStartElement("time");
    w.writeTextElement("beats", QString::number(beats));
    w.writeTextElement("beat-type", QString::number(beatType));
    w.writeEndElement();
}

void writeTempo(QXmlStreamWriter &w, double bpm) {
    w.writeStartElement("direction");
    w.writeAttribute("placement", "above");
    w.writeStartElement("direction-type");
    w.writeStartElement("metronome");
    w.writeTextElement("beat-unit", "quarter");
    w.writeTextElement("per-minute", QString::number(static_cast<int>(std::lround(bpm))));
    w.writeEndElement(); // metronome
    w.writeEndElement(); // direction-type
    w.writeStartElement("sound");
    w.writeAttribute("tempo", QString::number(bpm, 'f', 2));
    w.writeEndElement(); // sound
    w.writeEndElement(); // direction
}

void writeNote(QXmlStreamWriter &w, const ScoreEvent &e) {
    w.writeStartElement("note");
    if (e.isChord) w.writeEmptyElement("chord");

    if (e.isRest) {
        w.writeEmptyElement("rest");
    } else {
        w.writeStartElement("pitch");
        w.writeTextElement("step", QString(QChar(e.step)));
        if (e.alter != 0) w.writeTextElement("alter", QString::number(e.alter));
        w.writeTextElement("octave", QString::number(e.octave));
        w.writeEndElement(); // pitch
    }

    w.writeTextElement("duration", QString::number(e.durDivs));

    if (!e.isRest) {
        if (e.tieStart) { w.writeEmptyElement("tie"); w.writeAttribute("type", "start"); }
        if (e.tieStop)  { w.writeEmptyElement("tie"); w.writeAttribute("type", "stop");  }
    }

    w.writeTextElement("voice", "1");
    w.writeTextElement("type", noteTypeName(e.type));
    for (int d = 0; d < e.dots; ++d) w.writeEmptyElement("dot");

    if (!e.isRest && (e.tieStart || e.tieStop)) {
        w.writeStartElement("notations");
        if (e.tieStop)  { w.writeEmptyElement("tied"); w.writeAttribute("type", "stop");  }
        if (e.tieStart) { w.writeEmptyElement("tied"); w.writeAttribute("type", "start"); }
        w.writeEndElement(); // notations
    }

    w.writeEndElement(); // note
}

} // namespace

QByteArray MusicXmlWriter::write(const Score &score) {
    QByteArray out;
    QXmlStreamWriter w(&out);
    w.setAutoFormatting(true);

    w.writeStartDocument();
    w.writeDTD("<!DOCTYPE score-partwise PUBLIC "
               "\"-//Recordare//DTD MusicXML 4.0 Partwise//EN\" "
               "\"http://www.musicxml.org/dtds/partwise.dtd\">");

    w.writeStartElement("score-partwise");
    w.writeAttribute("version", "4.0");

    if (!score.title.isEmpty()) {
        w.writeStartElement("work");
        w.writeTextElement("work-title", score.title);
        w.writeEndElement();
    }

    // part-list
    w.writeStartElement("part-list");
    for (int i = 0; i < score.parts.size(); ++i) {
        const ScorePart &p = score.parts[i];
        const QString id = QStringLiteral("P%1").arg(i + 1);
        w.writeStartElement("score-part");
        w.writeAttribute("id", id);
        w.writeTextElement("part-name", p.name.isEmpty() ? QStringLiteral("Part %1").arg(i + 1) : p.name);
        w.writeStartElement("midi-instrument");
        w.writeAttribute("id", id + "-I1");
        w.writeTextElement("midi-channel", QString::number((p.channel & 0x0F) + 1));
        w.writeTextElement("midi-program", QString::number((p.program & 0x7F) + 1));
        w.writeEndElement(); // midi-instrument
        w.writeEndElement(); // score-part
    }
    w.writeEndElement(); // part-list

    // parts
    for (int i = 0; i < score.parts.size(); ++i) {
        const ScorePart &p = score.parts[i];
        w.writeStartElement("part");
        w.writeAttribute("id", QStringLiteral("P%1").arg(i + 1));

        for (const ScoreMeasure &m : p.measures) {
            w.writeStartElement("measure");
            w.writeAttribute("number", QString::number(m.number));

            const bool first = (m.number == 1);
            if (first || m.keyChanged || m.timeChanged) {
                w.writeStartElement("attributes");
                if (first) w.writeTextElement("divisions", QString::number(score.divisions));
                if (first || m.keyChanged)  writeKey(w, m.keyFifths, m.keyMinor);
                if (first || m.timeChanged) writeTime(w, m.beats, m.beatType);
                if (first) writeClef(w, p.clef);
                w.writeEndElement(); // attributes
            }

            if (m.tempoBpm > 0.0) writeTempo(w, m.tempoBpm);

            for (const ScoreEvent &e : m.voice) writeNote(w, e);

            w.writeEndElement(); // measure
        }
        w.writeEndElement(); // part
    }

    w.writeEndElement(); // score-partwise
    w.writeEndDocument();
    return out;
}
