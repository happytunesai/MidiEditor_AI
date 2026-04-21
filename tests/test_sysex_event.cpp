/*
 * test_sysex_event
 *
 * Unit tests for SysExEvent's byte encoding and accessors.
 *
 * What the production code actually does
 * --------------------------------------
 *   QByteArray SysExEvent::save() emits exactly:  F0 <data...> F7
 *
 * Notable absence (documented, not patched here): the production save()
 * does NOT prepend an SMF variable-length data-length field. The full
 * SMF spec for an in-track SysEx event is `F0 <varlen> <data...> F7`
 * (or `F7 <varlen> <data...>` for the "escape" form). The MidiEditor
 * tree appears to add the var-int length elsewhere when serialising the
 * full file rather than inside SysExEvent itself; SysExEvent::save()
 * returns only the F0/F7-framed payload.
 *
 * These tests therefore verify the *current* contract:
 *   - leading byte is 0xF0
 *   - trailing byte is 0xF7
 *   - the user-supplied bytes appear verbatim between the framing bytes
 *   - empty data still emits the two framing bytes
 * and document the missing var-int length prefix as a known gap rather
 * than failing on it.
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

    void save_emptyPayload_thenEmitsOnlyTheTwoFramingBytes() {
        // Guard against the empty-payload edge case: save() must still
        // emit valid F0/F7 framing.
        SysExEvent ev(0, QByteArray(), nullptr);
        QByteArray expected;
        expected.append(char(0xF0));
        expected.append(char(0xF7));
        QCOMPARE(ev.save(), expected);
        QCOMPARE(ev.save().size(), 2);
    }

    void save_singleBytePayload_thenF0PayloadF7() {
        SysExEvent ev(0, QByteArray(1, char(0x42)), nullptr);
        QByteArray expected;
        expected.append(char(0xF0));
        expected.append(char(0x42));
        expected.append(char(0xF7));
        QCOMPARE(ev.save(), expected);
    }

    void save_realisticGmReset_thenExactByteSequence() {
        // Common GM Reset SysEx: F0 7E 7F 09 01 F7 (the manufacturer +
        // device + sub-id + value bytes appear verbatim between the
        // framing bytes).
        const QByteArray inner = QByteArray::fromHex("7E7F0901");
        SysExEvent ev(0, inner, nullptr);
        QByteArray expected;
        expected.append(char(0xF0));
        expected.append(inner);
        expected.append(char(0xF7));
        QCOMPARE(ev.save(), expected);
        QCOMPARE(ev.save().toHex().toUpper(),
                 QByteArray("F07E7F0901F7"));
    }

    void save_largePayload_thenAllBytesPreservedVerbatim() {
        // Stress: 256-byte payload. The current save() does not insert
        // a length prefix, so we expect F0 + 256 bytes + F7 = 258 total.
        QByteArray payload;
        payload.reserve(256);
        for (int i = 0; i < 256; ++i) {
            payload.append(char(i & 0x7F)); // keep < 0x80 to avoid ambiguity
        }
        SysExEvent ev(0, payload, nullptr);
        const QByteArray bytes = ev.save();
        QCOMPARE(bytes.size(), 258);
        QCOMPARE(bytes.at(0), char(0xF0));
        QCOMPARE(bytes.at(bytes.size() - 1), char(0xF7));
        QCOMPARE(bytes.mid(1, 256), payload);
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
        QCOMPARE(ev.save().mid(1, 4), replacement);
    }

    void typeString_isConstantHumanReadableLabel() {
        SysExEvent ev(0, QByteArray(), nullptr);
        QCOMPARE(ev.typeString(),
                 QStringLiteral("System Exclusive Message (SysEx)"));
    }
};

QTEST_APPLESS_MAIN(TestSysExEvent)
#include "test_sysex_event.moc"
