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
#include "../MidiEvent/TempoChangeEvent.h"
#include "../MidiEvent/TimeSignatureEvent.h"
#include "../MidiEvent/KeySignatureEvent.h"
#include "../MidiEvent/TextEvent.h"
#include "../MidiEvent/ChannelPressureEvent.h"
#include "../MidiEvent/KeyPressureEvent.h"

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

        // Plan §11.10j — meta-channel events for live-sync coverage.
        TempoChangeEvent *tempo = dynamic_cast<TempoChangeEvent *>(event);
        if (tempo) {
            obj = serializeTempoEvent(event);
            obj[QStringLiteral("id")] = id++;
            arr.append(obj);
            continue;
        }

        TimeSignatureEvent *ts = dynamic_cast<TimeSignatureEvent *>(event);
        if (ts) {
            obj = serializeTimeSigEvent(event);
            obj[QStringLiteral("id")] = id++;
            arr.append(obj);
            continue;
        }

        KeySignatureEvent *ks = dynamic_cast<KeySignatureEvent *>(event);
        if (ks) {
            obj = serializeKeySigEvent(event);
            obj[QStringLiteral("id")] = id++;
            arr.append(obj);
            continue;
        }

        TextEvent *te = dynamic_cast<TextEvent *>(event);
        if (te) {
            obj = serializeTextEvent(event);
            obj[QStringLiteral("id")] = id++;
            arr.append(obj);
            continue;
        }

        ChannelPressureEvent *chp = dynamic_cast<ChannelPressureEvent *>(event);
        if (chp) {
            obj = serializeChannelPressureEvent(event);
            obj[QStringLiteral("id")] = id++;
            arr.append(obj);
            continue;
        }

        KeyPressureEvent *kp = dynamic_cast<KeyPressureEvent *>(event);
        if (kp) {
            obj = serializeKeyPressureEvent(event);
            obj[QStringLiteral("id")] = id++;
            arr.append(obj);
            continue;
        }

        // Skip OffEvents (they are implicit via duration) and other unsupported
        // types (SysEx, UnknownEvent — deliberate per Plan §11.10j out-of-scope).
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
    // BUG-COLLAB-031: live-sync of channel-property events (cc, pb,
    // prog_change) lost the track number because the serializer
    // skipped it. PrApply::insertEvent then refused trackIdx=-1 and
    // the receiver's channel ended up with NO program-change at all
    // (default Acoustic Piano). Round-trip the track field like the
    // meta-event serializers below already do.
    if (cc->track()) obj[QStringLiteral("track")] = cc->track()->number();
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
    if (pb->track()) obj[QStringLiteral("track")] = pb->track()->number();
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
    if (pc->track()) obj[QStringLiteral("track")] = pc->track()->number();
    return obj;
}

// --- Plan §11.10j meta + aftertouch event serializers ---------------------
//
// Channel/track are taken from the event's runtime values rather than
// hard-coded constants; tempo lives on channel 17, time-sig on 18, text /
// key-sig typically on 16, but this codebase does not enforce that and we
// must round-trip whatever the source file used.

QJsonObject MidiEventSerializer::serializeTempoEvent(MidiEvent *event)
{
    TempoChangeEvent *t = dynamic_cast<TempoChangeEvent *>(event);
    QJsonObject obj;
    obj[QStringLiteral("type")] = QStringLiteral("tempo");
    obj[QStringLiteral("tick")] = t->midiTime();
    obj[QStringLiteral("channel")] = t->channel();
    if (t->track()) obj[QStringLiteral("track")] = t->track()->number();
    obj[QStringLiteral("bpm")] = t->beatsPerQuarter();
    return obj;
}

QJsonObject MidiEventSerializer::serializeTimeSigEvent(MidiEvent *event)
{
    TimeSignatureEvent *ts = dynamic_cast<TimeSignatureEvent *>(event);
    QJsonObject obj;
    obj[QStringLiteral("type")] = QStringLiteral("time_sig");
    obj[QStringLiteral("tick")] = ts->midiTime();
    obj[QStringLiteral("channel")] = ts->channel();
    if (ts->track()) obj[QStringLiteral("track")] = ts->track()->number();
    obj[QStringLiteral("num")] = ts->num();
    // denom() returns the MIDI power-of-2 form (0=whole, 1=half, 2=quarter…)
    // — that's what the constructor expects, so we round-trip it as-is.
    obj[QStringLiteral("denomMidi")] = ts->denom();
    obj[QStringLiteral("midiClocks")] = ts->midiClocks();
    obj[QStringLiteral("num32In4")] = ts->num32In4();
    return obj;
}

QJsonObject MidiEventSerializer::serializeKeySigEvent(MidiEvent *event)
{
    KeySignatureEvent *k = dynamic_cast<KeySignatureEvent *>(event);
    QJsonObject obj;
    obj[QStringLiteral("type")] = QStringLiteral("key_sig");
    obj[QStringLiteral("tick")] = k->midiTime();
    obj[QStringLiteral("channel")] = k->channel();
    if (k->track()) obj[QStringLiteral("track")] = k->track()->number();
    obj[QStringLiteral("tonality")] = k->tonality();
    obj[QStringLiteral("minor")] = k->minor();
    return obj;
}

QJsonObject MidiEventSerializer::serializeTextEvent(MidiEvent *event)
{
    TextEvent *t = dynamic_cast<TextEvent *>(event);
    QJsonObject obj;
    obj[QStringLiteral("type")] = QStringLiteral("text");
    obj[QStringLiteral("tick")] = t->midiTime();
    obj[QStringLiteral("channel")] = t->channel();
    if (t->track()) obj[QStringLiteral("track")] = t->track()->number();
    obj[QStringLiteral("textType")] = t->type();
    obj[QStringLiteral("text")] = t->text();
    return obj;
}

QJsonObject MidiEventSerializer::serializeChannelPressureEvent(MidiEvent *event)
{
    ChannelPressureEvent *c = dynamic_cast<ChannelPressureEvent *>(event);
    QJsonObject obj;
    obj[QStringLiteral("type")] = QStringLiteral("chan_pressure");
    obj[QStringLiteral("tick")] = c->midiTime();
    obj[QStringLiteral("channel")] = c->channel();
    if (c->track()) obj[QStringLiteral("track")] = c->track()->number();
    obj[QStringLiteral("value")] = c->value();
    return obj;
}

QJsonObject MidiEventSerializer::serializeKeyPressureEvent(MidiEvent *event)
{
    KeyPressureEvent *k = dynamic_cast<KeyPressureEvent *>(event);
    QJsonObject obj;
    obj[QStringLiteral("type")] = QStringLiteral("key_pressure");
    obj[QStringLiteral("tick")] = k->midiTime();
    obj[QStringLiteral("channel")] = k->channel();
    if (k->track()) obj[QStringLiteral("track")] = k->track()->number();
    obj[QStringLiteral("note")] = k->note();
    obj[QStringLiteral("value")] = k->value();
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
    } else if (type == QStringLiteral("note_on")) {
        if (!eventObj.contains(QStringLiteral("tick")) ||
            !eventObj.contains(QStringLiteral("note")) ||
            !eventObj.contains(QStringLiteral("velocity"))) {
            errorMsg = QStringLiteral("note_on event missing required fields (tick, note, velocity)");
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
    } else if (type == QStringLiteral("note_off")) {
        if (!eventObj.contains(QStringLiteral("tick")) ||
            !eventObj.contains(QStringLiteral("note"))) {
            errorMsg = QStringLiteral("note_off event missing required fields (tick, note)");
            return false;
        }
    } else if (type == QStringLiteral("tempo")) {
        if (!eventObj.contains(QStringLiteral("tick")) ||
            !eventObj.contains(QStringLiteral("bpm"))) {
            errorMsg = QStringLiteral("tempo event missing required fields (tick, bpm)");
            return false;
        }
        int bpm = eventObj[QStringLiteral("bpm")].toInt();
        if (bpm < 1 || bpm > 999) {
            errorMsg = QStringLiteral("BPM out of range (1-999): %1").arg(bpm);
            return false;
        }
    } else if (type == QStringLiteral("time_sig")) {
        if (!eventObj.contains(QStringLiteral("tick")) ||
            !eventObj.contains(QStringLiteral("num")) ||
            !eventObj.contains(QStringLiteral("denomMidi"))) {
            errorMsg = QStringLiteral("time_sig event missing required fields (tick, num, denomMidi)");
            return false;
        }
    } else if (type == QStringLiteral("key_sig")) {
        if (!eventObj.contains(QStringLiteral("tick")) ||
            !eventObj.contains(QStringLiteral("tonality"))) {
            errorMsg = QStringLiteral("key_sig event missing required fields (tick, tonality)");
            return false;
        }
    } else if (type == QStringLiteral("text")) {
        if (!eventObj.contains(QStringLiteral("tick")) ||
            !eventObj.contains(QStringLiteral("text"))) {
            errorMsg = QStringLiteral("text event missing required fields (tick, text)");
            return false;
        }
    } else if (type == QStringLiteral("chan_pressure")) {
        if (!eventObj.contains(QStringLiteral("tick")) ||
            !eventObj.contains(QStringLiteral("value"))) {
            errorMsg = QStringLiteral("chan_pressure event missing required fields (tick, value)");
            return false;
        }
    } else if (type == QStringLiteral("key_pressure")) {
        if (!eventObj.contains(QStringLiteral("tick")) ||
            !eventObj.contains(QStringLiteral("note")) ||
            !eventObj.contains(QStringLiteral("value"))) {
            errorMsg = QStringLiteral("key_pressure event missing required fields (tick, note, value)");
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
                                       QList<MidiEvent *> &createdEvents,
                                       QStringList *skippedErrors)
{
    if (!file || !defaultTrack) return false;

    for (int i = 0; i < eventsJson.size(); ++i) {
        const QJsonValue &val = eventsJson[i];
        if (!val.isObject()) continue;
        QJsonObject obj = val.toObject();

        QString errorMsg;
        if (!validateEventJson(obj, errorMsg)) {
            if (skippedErrors)
                skippedErrors->append(QStringLiteral("Event %1: %2").arg(i).arg(errorMsg));
            continue; // Skip invalid events
        }

        QString type = obj[QStringLiteral("type")].toString();
        int tick = obj[QStringLiteral("tick")].toInt();
        // Channel range allows 0-18 to accommodate meta channels (16 = text /
        // key-sig, 17 = tempo, 18 = time-sig). Per-channel events still bound
        // to 0-15 by their own type branches below.
        int ch = (obj.contains(QStringLiteral("channel")) && obj[QStringLiteral("channel")].isDouble())
                     ? qBound(0, obj[QStringLiteral("channel")].toInt(), 18)
                     : channel;
        // Voice events (notes, CC, pitch bend, program change, aftertouch) must
        // live on channels 0-15; only meta events (tempo=17, time_sig=18,
        // key_sig/text=16) may use 16-18. Without this, a malformed channel:17
        // note would land on the tempo meta channel and corrupt the saved file.
        if (type != QStringLiteral("tempo") && type != QStringLiteral("time_sig")
            && type != QStringLiteral("key_sig") && type != QStringLiteral("text")) {
            ch = qMin(ch, 15);
        }

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
        } else if (type == QStringLiteral("note_on")) {
            int note = qBound(0, obj[QStringLiteral("note")].toInt(), 127);
            int vel = qBound(1, obj[QStringLiteral("velocity")].toInt(), 127);

            // Find matching note_off to compute duration
            int duration = 192; // Default: 1 quarter note
            for (int j = i + 1; j < eventsJson.size(); ++j) {
                if (!eventsJson[j].isObject()) continue;
                QJsonObject next = eventsJson[j].toObject();
                if (next[QStringLiteral("type")].toString() == QStringLiteral("note_off") &&
                    next[QStringLiteral("note")].toInt() == obj[QStringLiteral("note")].toInt()) {
                    int offTick = next[QStringLiteral("tick")].toInt();
                    if (offTick > tick) {
                        duration = offTick - tick;
                    }
                    break;
                }
            }

            MidiChannel *midiCh = file->channel(ch);
            if (midiCh) {
                NoteOnEvent *noteEvent = midiCh->insertNote(note, tick, tick + duration, vel, track);
                if (noteEvent) {
                    createdEvents.append(noteEvent);
                }
            }
        } else if (type == QStringLiteral("note_off")) {
            // Handled by note_on pairing above
            continue;
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
        } else if (type == QStringLiteral("tempo")) {
            // BPM ↔ microsPerQuarter conversion mirrors MidiPilotWidget's
            // applyTempoAction so behavioural parity is exact.
            int bpm = qBound(1, obj[QStringLiteral("bpm")].toInt(), 999);
            int microsPerQuarter = 60000000 / bpm;
            TempoChangeEvent *ev = new TempoChangeEvent(ch, microsPerQuarter, track);
            file->channel(ch)->insertEvent(ev, tick);
            createdEvents.append(ev);
        } else if (type == QStringLiteral("time_sig")) {
            int num = qBound(1, obj[QStringLiteral("num")].toInt(), 32);
            int denomMidi = qBound(0, obj[QStringLiteral("denomMidi")].toInt(), 5);
            int midiClocks = obj.value(QStringLiteral("midiClocks")).toInt(24);
            int num32In4 = obj.value(QStringLiteral("num32In4")).toInt(8);
            TimeSignatureEvent *ev = new TimeSignatureEvent(ch, num, denomMidi,
                                                            midiClocks, num32In4, track);
            file->channel(ch)->insertEvent(ev, tick);
            createdEvents.append(ev);
        } else if (type == QStringLiteral("key_sig")) {
            int tonality = qBound(-7, obj[QStringLiteral("tonality")].toInt(), 7);
            bool minor = obj.value(QStringLiteral("minor")).toBool();
            KeySignatureEvent *ev = new KeySignatureEvent(ch, tonality, minor, track);
            file->channel(ch)->insertEvent(ev, tick);
            createdEvents.append(ev);
        } else if (type == QStringLiteral("text")) {
            int textType = obj.value(QStringLiteral("textType")).toInt(TextEvent::TEXT);
            QString text = obj.value(QStringLiteral("text")).toString();
            TextEvent *ev = new TextEvent(ch, track);
            ev->setType(textType);
            ev->setText(text);
            file->channel(ch)->insertEvent(ev, tick);
            createdEvents.append(ev);
            // Plan §11.10o: when this text is the track-name marker
            // (TextEvent::TRACKNAME), bind it to the MidiTrack so the
            // track widget actually shows the synced name. Without
            // this, the event gets inserted on channel 16 but the
            // track's `_nameEvent` pointer stays null and the
            // displayed name stays "Track N". MidiFile::readTrack does
            // the same binding when loading from disk.
            if (textType == TextEvent::TRACKNAME && track) {
                track->setNameEvent(ev);
            }
        } else if (type == QStringLiteral("chan_pressure")) {
            int value = qBound(0, obj[QStringLiteral("value")].toInt(), 127);
            ChannelPressureEvent *ev = new ChannelPressureEvent(ch, value, track);
            file->channel(ch)->insertEvent(ev, tick);
            createdEvents.append(ev);
        } else if (type == QStringLiteral("key_pressure")) {
            int note = qBound(0, obj[QStringLiteral("note")].toInt(), 127);
            int value = qBound(0, obj[QStringLiteral("value")].toInt(), 127);
            KeyPressureEvent *ev = new KeyPressureEvent(ch, value, note, track);
            file->channel(ch)->insertEvent(ev, tick);
            createdEvents.append(ev);
        }
    }

    return !createdEvents.isEmpty();
}
