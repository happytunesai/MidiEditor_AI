/*
 * test_channel_events
 *
 * One happy-path encode-and-verify test per channel-voice MIDI event class:
 *
 *   - ControlChangeEvent     (Bn cc vv)
 *   - PitchBendEvent         (En lsb msb)   -- 14-bit value
 *   - ProgChangeEvent        (Cn pp)        -- 2-byte status (no value byte)
 *   - ChannelPressureEvent   (Dn vv)        -- 2-byte status
 *   - KeyPressureEvent       (An nn vv)
 *
 * Plus a small handful of high-value edge cases (>50 % chance of breaking
 * on real input):
 *   - PitchBendEvent low-byte / high-byte split for centre (8192) and max
 *     (16383) — the canonical place this encoder gets wrong
 *   - ControlChangeEvent on a non-zero channel (status nibble masking)
 *
 * Strategy
 * --------
 * Compiles only the five .cpp files under test. The MidiEvent base +
 * ProtocolEntry + GraphicObject + EventWidget + MidiTrack + MidiFile are
 * ODR-shimmed in this TU exactly like test_meta_events / test_text_event.
 */

#include <QtTest/QtTest>
#include <QObject>
#include <QByteArray>
#include <QPainter>

#include "../src/MidiEvent/ControlChangeEvent.h"
#include "../src/MidiEvent/PitchBendEvent.h"
#include "../src/MidiEvent/ProgChangeEvent.h"
#include "../src/MidiEvent/ChannelPressureEvent.h"
#include "../src/MidiEvent/KeyPressureEvent.h"

// ---- ODR shims: ProtocolEntry -------------------------------------------
ProtocolEntry::~ProtocolEntry() {}
ProtocolEntry *ProtocolEntry::copy() { return nullptr; }
void ProtocolEntry::reloadState(ProtocolEntry *) {}
MidiFile *ProtocolEntry::file() { return nullptr; }
void ProtocolEntry::protocol(ProtocolEntry *oldObj, ProtocolEntry *) {
    delete oldObj;
}

// ---- ODR shims: GraphicObject -------------------------------------------
#include "../src/gui/GraphicObject.h"
GraphicObject::GraphicObject() {}
void GraphicObject::draw(QPainter *, QColor) {}
bool GraphicObject::shown() { return false; }

// ---- ODR shims: EventWidget ---------------------------------------------
#include "../src/gui/EventWidget.h"
void EventWidget::setEvents(QList<MidiEvent *>) {}
void EventWidget::reload() {}

// ---- ODR shims: MidiEvent base ------------------------------------------
quint8 MidiEvent::_startByte = 0;
EventWidget *MidiEvent::_eventWidget = nullptr;

MidiEvent::MidiEvent(int channel, MidiTrack *track) {
    _track = track;
    numChannel = channel;
    timePos = 0;
    midiFile = nullptr;
    _tempID = -1;
}

MidiEvent::MidiEvent(MidiEvent &other)
    : ProtocolEntry(other), GraphicObject() {
    _track = other._track;
    numChannel = other.numChannel;
    timePos = other.timePos;
    midiFile = other.midiFile;
    _tempID = other._tempID;
}

MidiFile *MidiEvent::file() { return midiFile; }
int MidiEvent::line() { return UNKNOWN_LINE; }
QString MidiEvent::toMessage() { return QString(); }
QByteArray MidiEvent::save() { return QByteArray(); }
void MidiEvent::draw(QPainter *, QColor) {}
ProtocolEntry *MidiEvent::copy() { return nullptr; }
void MidiEvent::reloadState(ProtocolEntry *) {}
QString MidiEvent::typeString() { return QString(); }
bool MidiEvent::isOnEvent() { return false; }
void MidiEvent::setMidiTime(int t, bool) { timePos = t; }
void MidiEvent::moveToChannel(int channel, bool) { numChannel = channel; }
int MidiEvent::channel() {
    if (numChannel < 0 || numChannel > 18) return 0;
    return numChannel;
}
MidiTrack *MidiEvent::track() { return _track; }

// ---- ODR shims: MidiFile (never constructed) ----------------------------
class MidiFile;
#include "../src/midi/MidiFile.h"

// =========================================================================

class TestChannelEvents : public QObject {
    Q_OBJECT

private slots:

    // ---- ControlChangeEvent ---------------------------------------------

    void controlChange_save_channel0_thenStatusB0ControlValue() {
        // Modulation wheel (CC1) = 64 on channel 0:  B0 01 40
        ControlChangeEvent ev(0, 1, 64, nullptr);
        QByteArray expected;
        expected.append(char(0xB0));
        expected.append(char(0x01));
        expected.append(char(0x40));
        QCOMPARE(ev.save(), expected);
        QCOMPARE(ev.line(), int(MidiEvent::CONTROLLER_LINE));
        QCOMPARE(ev.control(), 1);
        QCOMPARE(ev.value(), 64);
    }

    void controlChange_save_channel9_thenStatusNibbleIsB9() {
        // CC7 (volume) = 100 on channel 9 (drums):  B9 07 64
        // Verifies the channel nibble OR is correct on a non-zero channel.
        ControlChangeEvent ev(9, 7, 100, nullptr);
        QByteArray expected;
        expected.append(char(0xB9));
        expected.append(char(0x07));
        expected.append(char(0x64));
        QCOMPARE(ev.save(), expected);
    }

    // ---- PitchBendEvent -------------------------------------------------

    void pitchBend_save_centerValue8192_thenLsb00Msb40() {
        // 8192 = 0x2000. SMF splits as low 7 bits then high 7 bits:
        //   lsb = 0x00, msb = 0x40  =>  E0 00 40   (centre, no bend)
        PitchBendEvent ev(0, 8192, nullptr);
        QByteArray expected;
        expected.append(char(0xE0));
        expected.append(char(0x00));
        expected.append(char(0x40));
        QCOMPARE(ev.save(), expected);
        QCOMPARE(ev.line(), int(MidiEvent::PITCH_BEND_LINE));
        QCOMPARE(ev.value(), 8192);
    }

    void pitchBend_save_maxValue16383_thenLsb7FMsb7F() {
        // 14-bit max = 0x3FFF: lsb = 0x7F, msb = 0x7F  => E0 7F 7F
        PitchBendEvent ev(3, 16383, nullptr);
        QByteArray expected;
        expected.append(char(0xE3));
        expected.append(char(0x7F));
        expected.append(char(0x7F));
        QCOMPARE(ev.save(), expected);
    }

    void pitchBend_save_zeroValue_thenE0_00_00() {
        // Lowest 14-bit value: full negative bend.
        PitchBendEvent ev(0, 0, nullptr);
        QByteArray expected;
        expected.append(char(0xE0));
        expected.append(char(0x00));
        expected.append(char(0x00));
        QCOMPARE(ev.save(), expected);
    }

    // ---- ProgChangeEvent ------------------------------------------------

    void progChange_save_channel0_thenTwoBytesC0AndProgram() {
        // Acoustic Grand Piano (program 0) on channel 0:  C0 00
        ProgChangeEvent ev(0, 0, nullptr);
        QByteArray expected;
        expected.append(char(0xC0));
        expected.append(char(0x00));
        QCOMPARE(ev.save(), expected);
        QCOMPARE(ev.line(), int(MidiEvent::PROG_CHANGE_LINE));
        QCOMPARE(ev.program(), 0);
    }

    void progChange_save_channel15_program127_thenCfAnd7F() {
        // Verifies the high program number + high channel nibble path.
        ProgChangeEvent ev(15, 127, nullptr);
        QByteArray expected;
        expected.append(char(0xCF));
        expected.append(char(0x7F));
        QCOMPARE(ev.save(), expected);
    }

    // ---- ChannelPressureEvent -------------------------------------------

    void channelPressure_save_channel0_thenTwoBytesD0AndValue() {
        // Aftertouch = 90 on channel 0:  D0 5A
        ChannelPressureEvent ev(0, 0x5A, nullptr);
        QByteArray expected;
        expected.append(char(0xD0));
        expected.append(char(0x5A));
        QCOMPARE(ev.save(), expected);
        QCOMPARE(ev.line(), int(MidiEvent::CHANNEL_PRESSURE_LINE));
        QCOMPARE(ev.value(), 0x5A);
    }

    // ---- KeyPressureEvent -----------------------------------------------

    void keyPressure_save_channel0_thenA0NoteValue() {
        // Polyphonic aftertouch on note 60 (middle C) = 100 on channel 0:
        //   A0 3C 64
        KeyPressureEvent ev(0, 100, 60, nullptr);
        QByteArray expected;
        expected.append(char(0xA0));
        expected.append(char(0x3C));
        expected.append(char(0x64));
        QCOMPARE(ev.save(), expected);
        QCOMPARE(ev.line(), int(MidiEvent::KEY_PRESSURE_LINE));
        QCOMPARE(ev.note(), 60);
        QCOMPARE(ev.value(), 100);
    }
};

QTEST_APPLESS_MAIN(TestChannelEvents)
#include "test_channel_events.moc"
