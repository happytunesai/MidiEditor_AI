#include "MmlParser.h"
#include <QtGlobal>

int MmlParser::noteOffset(QChar letter) {
    switch (letter.toLatin1()) {
    case 'c': return 0;
    case 'd': return 2;
    case 'e': return 4;
    case 'f': return 5;
    case 'g': return 7;
    case 'a': return 9;
    case 'b': return 11;
    default:  return 0;
    }
}

MmlTrack MmlParser::parse(const QList<MmlToken>& tokens, int ticksPerQuarter) {
    MmlTrack track;

    int octave = 4;
    int defaultLength = 4;   // quarter note
    int volume = 8;          // MML range 0-15
    int currentTick = 0;
    int idx = 0;
    bool tiePending = false;

    auto peekType = [&]() -> MmlTokenType {
        return (idx < tokens.size()) ? tokens[idx].type : MmlTokenType::EndOfTrack;
    };

    auto readNumber = [&]() -> int {
        if (peekType() == MmlTokenType::Number) {
            return tokens[idx++].value.toInt();
        }
        return -1; // no number present
    };

    auto calcDuration = [&](int lengthVal) -> int {
        if (lengthVal <= 0)
            lengthVal = defaultLength;
        int dur = ticksPerQuarter * 4 / lengthVal;
        int dotAdd = dur / 2;
        while (peekType() == MmlTokenType::Dot) {
            dur += dotAdd;
            dotAdd /= 2;
            idx++;
        }
        return dur;
    };

    while (idx < tokens.size() && tokens[idx].type != MmlTokenType::EndOfTrack) {
        MmlTokenType type = tokens[idx].type;

        // ── Note ──────────────────────────────────────────────
        if (type == MmlTokenType::NoteLetter) {
            QChar letter = tokens[idx].value[0];
            idx++;

            int semitoneAdj = 0;
            while (peekType() == MmlTokenType::Sharp || peekType() == MmlTokenType::Flat) {
                semitoneAdj += (tokens[idx].type == MmlTokenType::Sharp) ? 1 : -1;
                idx++;
            }

            int lengthVal = readNumber();
            int duration = calcDuration(lengthVal);

            int pitch = (octave + 1) * 12 + noteOffset(letter) + semitoneAdj;
            pitch = qBound(0, pitch, 127);
            int velocity = qBound(1, volume * 8, 127);

            // Tie: extend previous note of same pitch instead of creating new one
            if (tiePending && !track.notes.isEmpty() &&
                track.notes.last().pitch == pitch) {
                track.notes.last().duration += duration;
            } else {
                MmlNote note;
                note.pitch = pitch;
                note.startTick = currentTick;
                note.duration = duration;
                note.velocity = velocity;
                track.notes.append(note);
            }

            currentTick += duration;

            // Check if this note is tied to the next
            tiePending = (peekType() == MmlTokenType::Tie);
            if (tiePending)
                idx++;
        }
        // ── Rest ──────────────────────────────────────────────
        else if (type == MmlTokenType::Rest) {
            idx++;
            int lengthVal = readNumber();
            int duration = calcDuration(lengthVal);
            currentTick += duration;
            tiePending = false;
        }
        // ── Octave ────────────────────────────────────────────
        else if (type == MmlTokenType::OctaveCmd) {
            idx++;
            int val = readNumber();
            if (val >= 0)
                octave = qBound(0, val, 8);
        } else if (type == MmlTokenType::OctaveUp) {
            idx++;
            octave = qMin(octave + 1, 8);
        } else if (type == MmlTokenType::OctaveDown) {
            idx++;
            octave = qMax(octave - 1, 0);
        }
        // ── Tempo (consumed here but also extracted at song level) ─
        else if (type == MmlTokenType::TempoCmd) {
            idx++;
            readNumber();
        }
        // ── Default length ────────────────────────────────────
        else if (type == MmlTokenType::LengthCmd) {
            idx++;
            int val = readNumber();
            if (val > 0)
                defaultLength = val;
            while (peekType() == MmlTokenType::Dot)
                idx++; // consume trailing dots
        }
        // ── Volume ────────────────────────────────────────────
        else if (type == MmlTokenType::VolumeCmd) {
            idx++;
            int val = readNumber();
            if (val >= 0)
                volume = qBound(0, val, 15);
        }
        // ── Instrument / program change ───────────────────────
        else if (type == MmlTokenType::InstrumentCmd) {
            idx++;
            int val = readNumber();
            if (val >= 0)
                track.instrument = qBound(0, val, 127);
        }
        // ── Stray tie (between non-note tokens) ──────────────
        else if (type == MmlTokenType::Tie) {
            idx++;
            tiePending = true;
        }
        // ── Loops (simplified — just skip markers) ───────────
        else if (type == MmlTokenType::LoopStart || type == MmlTokenType::LoopEnd) {
            idx++;
            readNumber(); // optional loop count
        }
        // ── Unknown ───────────────────────────────────────────
        else {
            idx++;
        }
    }

    return track;
}
