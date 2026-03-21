#include "MidiEventSerializer.h"

#include "../midi/MidiFile.h"
#include "../midi/MidiTrack.h"
#include "../midi/MidiChannel.h"
#include "../MidiEvent/MidiEvent.h"
#include "../MidiEvent/NoteOnEvent.h"
#include "../MidiEvent/OnEvent.h"
#include "../MidiEvent/OffEvent.h"
#include "../MidiEvent/ControlChangeEvent.h"
#include "../MidiEvent/PitchBendEvent.h"
#include "../MidiEvent/ProgChangeEvent.h"

const char *MidiEventSerializer::NOTE_NAMES[] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

QString MidiEventSerializer::noteName(int note)
{
    if (note < 0 || note > 127) return QStringLiteral("?");
    int octave = (note / 12) - 1;
    int noteIdx = note % 12;
    return QStringLiteral("%1%2").arg(QString::fromLatin1(NOTE_NAMES[noteIdx])).arg(octave);
}

QJsonArray MidiEventSerializer::serialize(const QList<MidiEvent *> &events, MidiFile *file)
{
    QJsonArray arr;
    int id = 0;

    for (MidiEvent *event : events) {
        QJsonObject obj;

        // Try each event type
        NoteOnEvent *noteOn = dynamic_cast<NoteOnEvent *>(event);
        if (noteOn) {
            obj = serializeNoteEvent(event, file);
            obj[QStringLiteral("id")] = id++;
            arr.append(obj);
            continue;
        }

        ControlChangeEvent *cc = dynamic_cast<ControlChangeEvent *>(event);
        if (cc) {
            obj = serializeControlChangeEvent(event);
            obj[QStringLiteral("id")] = id++;
            arr.append(obj);
            continue;
        }

        PitchBendEvent *pb = dynamic_cast<PitchBendEvent *>(event);
        if (pb) {
            obj = serializePitchBendEvent(event);
            obj[QStringLiteral("id")] = id++;
            arr.append(obj);
            continue;
        }

        ProgChangeEvent *pc = dynamic_cast<ProgChangeEvent *>(event);
        if (pc) {
            obj = serializeProgChangeEvent(event);
            obj[QStringLiteral("id")] = id++;
            arr.append(obj);
            continue;
        }

        // Skip OffEvents (they are implicit via duration) and other unsupported types
    }

    return arr;
}

QJsonObject MidiEventSerializer::serializeNoteEvent(MidiEvent *event, MidiFile *file)
{
    NoteOnEvent *noteOn = dynamic_cast<NoteOnEvent *>(event);
    QJsonObject obj;
    obj[QStringLiteral("type")] = QStringLiteral("note");
    obj[QStringLiteral("tick")] = noteOn->midiTime();
    obj[QStringLiteral("note")] = noteOn->note();
    obj[QStringLiteral("noteName")] = noteName(noteOn->note());
    obj[QStringLiteral("velocity")] = noteOn->velocity();
    obj[QStringLiteral("channel")] = noteOn->channel();

    // Calculate duration from offEvent
    OffEvent *off = noteOn->offEvent();
    if (off) {
        obj[QStringLiteral("duration")] = off->midiTime() - noteOn->midiTime();
    } else {
        obj[QStringLiteral("duration")] = file->ticksPerQuarter(); // fallback: 1 quarter note
    }

    if (noteOn->track()) {
        obj[QStringLiteral("track")] = noteOn->track()->number();
    }

    return obj;
}

QJsonObject MidiEventSerializer::serializeControlChangeEvent(MidiEvent *event)
{
    ControlChangeEvent *cc = dynamic_cast<ControlChangeEvent *>(event);
    QJsonObject obj;
    obj[QStringLiteral("type")] = QStringLiteral("cc");
    obj[QStringLiteral("tick")] = cc->midiTime();
    obj[QStringLiteral("control")] = cc->control();
    obj[QStringLiteral("controlName")] = MidiFile::controlChangeName(cc->control());
    obj[QStringLiteral("value")] = cc->value();
    obj[QStringLiteral("channel")] = cc->channel();
    return obj;
}

QJsonObject MidiEventSerializer::serializePitchBendEvent(MidiEvent *event)
{
    PitchBendEvent *pb = dynamic_cast<PitchBendEvent *>(event);
    QJsonObject obj;
    obj[QStringLiteral("type")] = QStringLiteral("pitch_bend");
    obj[QStringLiteral("tick")] = pb->midiTime();
    obj[QStringLiteral("value")] = pb->value();
    obj[QStringLiteral("channel")] = pb->channel();
    return obj;
}

QJsonObject MidiEventSerializer::serializeProgChangeEvent(MidiEvent *event)
{
    ProgChangeEvent *pc = dynamic_cast<ProgChangeEvent *>(event);
    QJsonObject obj;
    obj[QStringLiteral("type")] = QStringLiteral("program_change");
    obj[QStringLiteral("tick")] = pc->midiTime();
    obj[QStringLiteral("program")] = pc->program();
    obj[QStringLiteral("programName")] = MidiFile::instrumentName(pc->program());
    obj[QStringLiteral("channel")] = pc->channel();
    return obj;
}

bool MidiEventSerializer::validateEventJson(const QJsonObject &eventObj, QString &errorMsg)
{
    if (!eventObj.contains(QStringLiteral("type"))) {
        errorMsg = QStringLiteral("Missing 'type' field");
        return false;
    }

    QString type = eventObj[QStringLiteral("type")].toString();

    if (type == QStringLiteral("note")) {
        if (!eventObj.contains(QStringLiteral("tick")) ||
            !eventObj.contains(QStringLiteral("note")) ||
            !eventObj.contains(QStringLiteral("velocity")) ||
            !eventObj.contains(QStringLiteral("duration"))) {
            errorMsg = QStringLiteral("Note event missing required fields (tick, note, velocity, duration)");
            return false;
        }
        int note = eventObj[QStringLiteral("note")].toInt();
        int vel = eventObj[QStringLiteral("velocity")].toInt();
        if (note < 0 || note > 127) {
            errorMsg = QStringLiteral("Note value out of range (0-127): %1").arg(note);
            return false;
        }
        if (vel < 0 || vel > 127) {
            errorMsg = QStringLiteral("Velocity out of range (0-127): %1").arg(vel);
            return false;
        }
    } else if (type == QStringLiteral("cc")) {
        if (!eventObj.contains(QStringLiteral("tick")) ||
            !eventObj.contains(QStringLiteral("control")) ||
            !eventObj.contains(QStringLiteral("value"))) {
            errorMsg = QStringLiteral("CC event missing required fields (tick, control, value)");
            return false;
        }
        int ctrl = eventObj[QStringLiteral("control")].toInt();
        int val = eventObj[QStringLiteral("value")].toInt();
        if (ctrl < 0 || ctrl > 127) {
            errorMsg = QStringLiteral("Control number out of range (0-127): %1").arg(ctrl);
            return false;
        }
        if (val < 0 || val > 127) {
            errorMsg = QStringLiteral("Control value out of range (0-127): %1").arg(val);
            return false;
        }
    } else if (type == QStringLiteral("pitch_bend")) {
        if (!eventObj.contains(QStringLiteral("tick")) ||
            !eventObj.contains(QStringLiteral("value"))) {
            errorMsg = QStringLiteral("PitchBend event missing required fields (tick, value)");
            return false;
        }
        int val = eventObj[QStringLiteral("value")].toInt();
        if (val < 0 || val > 16383) {
            errorMsg = QStringLiteral("PitchBend value out of range (0-16383): %1").arg(val);
            return false;
        }
    } else if (type == QStringLiteral("program_change")) {
        if (!eventObj.contains(QStringLiteral("tick")) ||
            !eventObj.contains(QStringLiteral("program"))) {
            errorMsg = QStringLiteral("ProgramChange event missing required fields (tick, program)");
            return false;
        }
        int prog = eventObj[QStringLiteral("program")].toInt();
        if (prog < 0 || prog > 127) {
            errorMsg = QStringLiteral("Program number out of range (0-127): %1").arg(prog);
            return false;
        }
    } else {
        errorMsg = QStringLiteral("Unknown event type: %1").arg(type);
        return false;
    }

    int tick = eventObj[QStringLiteral("tick")].toInt();
    if (tick < 0) {
        errorMsg = QStringLiteral("Tick must be non-negative: %1").arg(tick);
        return false;
    }

    return true;
}

bool MidiEventSerializer::deserialize(const QJsonArray &eventsJson,
                                       MidiFile *file,
                                       MidiTrack *defaultTrack,
                                       int channel,
                                       QList<MidiEvent *> &createdEvents)
{
    if (!file || !defaultTrack) return false;

    for (const QJsonValue &val : eventsJson) {
        if (!val.isObject()) continue;
        QJsonObject obj = val.toObject();

        QString errorMsg;
        if (!validateEventJson(obj, errorMsg)) {
            continue; // Skip invalid events
        }

        QString type = obj[QStringLiteral("type")].toString();
        int tick = obj[QStringLiteral("tick")].toInt();
        int ch = obj.contains(QStringLiteral("channel"))
                     ? qBound(0, obj[QStringLiteral("channel")].toInt(), 15)
                     : channel;

        // Per-event track override: if the AI specifies a track index, use it
        MidiTrack *track = defaultTrack;
        if (obj.contains(QStringLiteral("track"))) {
            int trackIdx = obj[QStringLiteral("track")].toInt(-1);
            if (trackIdx >= 0 && trackIdx < file->numTracks()) {
                track = file->track(trackIdx);
            }
        }

        if (type == QStringLiteral("note")) {
            int note = qBound(0, obj[QStringLiteral("note")].toInt(), 127);
            int vel = qBound(1, obj[QStringLiteral("velocity")].toInt(), 127);
            int duration = qMax(1, obj[QStringLiteral("duration")].toInt());

            // Use MidiChannel::insertNote which handles NoteOn + OffEvent pairing
            MidiChannel *midiCh = file->channel(ch);
            if (midiCh) {
                NoteOnEvent *noteEvent = midiCh->insertNote(note, tick, tick + duration, vel, track);
                if (noteEvent) {
                    createdEvents.append(noteEvent);
                }
            }
        } else if (type == QStringLiteral("cc")) {
            int ctrl = qBound(0, obj[QStringLiteral("control")].toInt(), 127);
            int value = qBound(0, obj[QStringLiteral("value")].toInt(), 127);

            ControlChangeEvent *ccEvent = new ControlChangeEvent(ch, ctrl, value, track);
            file->channel(ch)->insertEvent(ccEvent, tick);
            createdEvents.append(ccEvent);
        } else if (type == QStringLiteral("pitch_bend")) {
            int value = qBound(0, obj[QStringLiteral("value")].toInt(), 16383);

            PitchBendEvent *pbEvent = new PitchBendEvent(ch, value, track);
            file->channel(ch)->insertEvent(pbEvent, tick);
            createdEvents.append(pbEvent);
        } else if (type == QStringLiteral("program_change")) {
            int prog = qBound(0, obj[QStringLiteral("program")].toInt(), 127);

            ProgChangeEvent *pcEvent = new ProgChangeEvent(ch, prog, track);
            file->channel(ch)->insertEvent(pcEvent, tick);
            createdEvents.append(pcEvent);
        }
    }

    return !createdEvents.isEmpty();
}
