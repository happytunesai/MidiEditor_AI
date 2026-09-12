/*
 * test_sysex_event
 *
 * Unit tests for SysExEvent's byte encoding and accessors.
 *
 * What the production code actually does
 * --------------------------------------
 *   QByteArray SysExEvent::save() emits the SMF form:
 *       F0 <varlen length> <data...> F7   (the length counts the F7)
 *
 * _data holds the payload only; the length is re-derived on every save()
 * through MidiFile::writeVariableLengthValue, so setData() can never write
 * back a stale prefix and a load/save round trip stays byte-identical.
 * (The loader reads SysEx by that length as well; the wire form
 * F0 <data> F7 is produced by MidiOutput for playback and re-framed by
 * MidiInput for recording.)
 *
 * These tests verify:
 *   - leading byte is 0xF0, then the var-int length, trailing byte 0xF7
 *   - the user-supplied bytes appear verbatim between length and F7
 *   - empty data emits F0 01 F7
 *
 * Strategy
 * --------
 * Compiles only SysExEvent.cpp directly. Every other dependency
 * (MidiEvent base, ProtocolEntry, GraphicObject, EventWidget, MidiTrack,
 * MidiFile) is ODR-shimmed in this TU exactly like test_text_event.
 */

#include <QtTest/QtTest>
#include <QObject>
#include <QByteArray>
#include <QPainter>

#include "../src/MidiEvent/SysExEvent.h"

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
// SysExEvent::save() derives its SMF length prefix through this helper;
// the target links no MidiFile.cpp, so mirror the standard var-int writer.
QByteArray MidiFile::writeVariableLengthValue(int value) {
    QList<quint8> groups;
    do {
        groups.prepend(static_cast<quint8>(value & 0x7F));
        value >>= 7;
    } while (value > 0);
    QByteArray out;
    for (int i = 0; i < groups.size(); ++i)
        out.append(char(groups[i] | (i < groups.size() - 1 ? 0x80 : 0x00)));
    return out;
}

// =========================================================================

class TestSysExEvent : public QObject {
    Q_OBJECT

private slots:

    void ctor_storesDataAndExposesIt() {
        const QByteArray payload = QByteArray::fromHex("4304"); // arbitrary
        SysExEvent ev(0, payload, nullptr);
        QCOMPARE(ev.data(), payload);
        QCOMPARE(ev.line(), int(MidiEvent::SYSEX_LINE));
    }

    void save_emptyPayload_thenEmitsFramingAndLengthOnly() {
        // Empty payload: F0, a length of 1 (the F7 alone), F7.
        SysExEvent ev(0, QByteArray(), nullptr);
        QByteArray expected;
        expected.append(char(0xF0));
        expected.append(char(0x01));
        expected.append(char(0xF7));
        QCOMPARE(ev.save(), expected);
        QCOMPARE(ev.save().size(), 3);
    }

    void save_singleBytePayload_thenF0LengthPayloadF7() {
        SysExEvent ev(0, QByteArray(1, char(0x42)), nullptr);
        QByteArray expected;
        expected.append(char(0xF0));
        expected.append(char(0x02));
        expected.append(char(0x42));
        expected.append(char(0xF7));
        QCOMPARE(ev.save(), expected);
    }

    void save_realisticGmReset_thenExactByteSequence() {
        // Common GM Reset SysEx as stored in an SMF: F0 05 7E 7F 09 01 F7
        // (length 5 = four payload bytes plus the F7).
        const QByteArray inner = QByteArray::fromHex("7E7F0901");
        SysExEvent ev(0, inner, nullptr);
        QByteArray expected;
        expected.append(char(0xF0));
        expected.append(char(0x05));
        expected.append(inner);
        expected.append(char(0xF7));
        QCOMPARE(ev.save(), expected);
        QCOMPARE(ev.save().toHex().toUpper(),
                 QByteArray("F0057E7F0901F7"));
    }

    void save_largePayload_thenAllBytesPreservedVerbatim() {
        // Stress: 256-byte payload. Length 257 needs two var-int bytes
        // (82 01), so we expect F0 + 82 01 + 256 bytes + F7 = 260 total.
        QByteArray payload;
        payload.reserve(256);
        for (int i = 0; i < 256; ++i) {
            payload.append(char(i & 0x7F)); // keep < 0x80 to avoid ambiguity
        }
        SysExEvent ev(0, payload, nullptr);
        const QByteArray bytes = ev.save();
        QCOMPARE(bytes.size(), 260);
        QCOMPARE(bytes.at(0), char(0xF0));
        QCOMPARE(bytes.mid(1, 2), QByteArray::fromHex("8201"));
        QCOMPARE(bytes.at(bytes.size() - 1), char(0xF7));
        QCOMPARE(bytes.mid(3, 256), payload);
    }

    void save_payloadContainingF7Byte_thenStillEmittedVerbatim() {
        // Payloads should not be filtered or escaped — even an
        // accidental F7 byte mid-stream is preserved (the production
        // code does no scrubbing). The real-world MIDI parser side
        // would have to handle this but that is out of scope for save().
        const QByteArray payload = QByteArray::fromHex("01F702");
        SysExEvent ev(0, payload, nullptr);
        QByteArray expected;
        expected.append(char(0xF0));
        expected.append(char(0x04));
        expected.append(payload);
        expected.append(char(0xF7));
        QCOMPARE(ev.save(), expected);
    }

    void setData_replacesPayloadAndIsRoundTripped() {
        SysExEvent ev(0, QByteArray::fromHex("AA"), nullptr);
        QCOMPARE(ev.data(), QByteArray::fromHex("AA"));
        const QByteArray replacement = QByteArray::fromHex("DEADBEEF");
        ev.setData(replacement);
        QCOMPARE(ev.data(), replacement);
        QCOMPARE(ev.save().mid(2, 4), replacement); // after F0 and the 1-byte length
    }

    void typeString_isConstantHumanReadableLabel() {
        SysExEvent ev(0, QByteArray(), nullptr);
        QCOMPARE(ev.typeString(),
                 QStringLiteral("System Exclusive Message (SysEx)"));
    }
};

QTEST_APPLESS_MAIN(TestSysExEvent)
#include "test_sysex_event.moc"
