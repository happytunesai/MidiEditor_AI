/*
 * test_meta_events
 *
 * Unit tests for the SMF byte encoding of three MIDI meta events:
 *   - KeySignatureEvent   (FF 59 02 sf mi)
 *   - TimeSignatureEvent  (FF 58 04 nn dd cc bb)
 *   - TempoChangeEvent    (FF 51 03 tt tt tt)
 * plus the BPM <-> microseconds-per-quarter conversion that lives inside
 * TempoChangeEvent's constructor / save() pair.
 *
 * Strategy
 * --------
 * All three event classes derive from MidiEvent, which itself derives from
 * ProtocolEntry + GraphicObject and pulls in the EventWidget header. The
 * production MidiEvent.cpp also drags in every other event type plus
 * MidiFile / MidiChannel / Appearance via loadMidiEvent().
 *
 * To keep the link surface tight, this test compiles ONLY:
 *   - KeySignatureEvent.cpp
 *   - TimeSignatureEvent.cpp
 *   - TempoChangeEvent.cpp
 * and provides ODR shims (defined in this TU) for every external symbol
 * those .cpp files reference:
 *   - ProtocolEntry::{ctor, dtor, copy, reloadState, file, protocol}
 *   - GraphicObject::{ctor, draw}
 *   - EventWidget::{setEvents, reload}                     (never invoked)
 *   - MidiEvent base ctors + non-overridden virtuals + statics
 *   - MidiFile::{~MidiFile, ticksPerQuarter, calcMaxTime}  (never invoked)
 *
 * MidiTrack is forward-declared everywhere; the test passes nullptr.
 */

#include <QtTest/QtTest>
#include <QObject>
#include <QByteArray>
#include <QPainter>

// Headers under test (these transitively include MidiEvent.h, which
// includes EventWidget.h and GraphicObject.h).
#include "../src/MidiEvent/KeySignatureEvent.h"
#include "../src/MidiEvent/TimeSignatureEvent.h"
#include "../src/MidiEvent/TempoChangeEvent.h"

// ---- ODR shims: ProtocolEntry -------------------------------------------
ProtocolEntry::~ProtocolEntry() {}
ProtocolEntry *ProtocolEntry::copy() { return nullptr; }
void ProtocolEntry::reloadState(ProtocolEntry *) {}
MidiFile *ProtocolEntry::file() { return nullptr; }
void ProtocolEntry::protocol(ProtocolEntry *oldObj, ProtocolEntry *) {
    // The production code defers to file()->protocol()->enterUndoStep(...);
    // when file() is null it deletes oldObj. We never enter a protocol
    // action in this test, but a few setters (e.g. setTonality) call it,
    // so keep the same delete-old behaviour.
    delete oldObj;
}

// ---- ODR shims: GraphicObject -------------------------------------------
#include "../src/gui/GraphicObject.h"
GraphicObject::GraphicObject() {}
void GraphicObject::draw(QPainter *, QColor) {}

// ---- ODR shims: EventWidget (only the symbols MidiEvent / Selection
// would call; not exercised here, but moc_EventWidget.cpp may reference
// reload() through qt_metacall).
#include "../src/gui/EventWidget.h"
void EventWidget::setEvents(QList<MidiEvent *>) {}
void EventWidget::reload() {}

// ---- ODR shims: MidiEvent base ------------------------------------------
// We re-implement MidiEvent's ctors and virtual surface directly in this
// TU so the real MidiEvent.cpp does not need to link.
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

// ---- ODR shims: MidiFile (never constructed; only the methods that
// TempoChange/TimeSignature .cpp directly reference need to link).
// The class definition itself comes from the real MidiFile.h, included
// transitively by the two .cpp files.
//
// IMPORTANT: do NOT define MidiFile::~MidiFile() here. Defining the
// out-of-line destructor would anchor MidiFile's vtable in this TU,
// which in turn references qt_metacall / qt_metacast / metaObject /
// ProtocolEntry overrides — none of which we link. We never construct
// or destroy a MidiFile in this test, so the vtable is never needed.
class MidiFile;
#include "../src/midi/MidiFile.h"
int MidiFile::ticksPerQuarter() { return 192; }
void MidiFile::calcMaxTime() {}


// =========================================================================

class TestMetaEvents : public QObject {
    Q_OBJECT

private slots:
    // ---- KeySignatureEvent ----------------------------------------------

    void keySig_save_cMajor_thenFiveByteHeaderWithZeroSfZeroMi() {
        KeySignatureEvent ev(16, 0, false, nullptr);
        QByteArray expected;
        expected.append(char(0xFF));
        expected.append(char(0x59));
        expected.append(char(0x02));
        expected.append(char(0x00)); // 0 sharps/flats
        expected.append(char(0x00)); // major
        QCOMPARE(ev.save(), expected);
    }

    void keySig_save_threeSharpsMinor_thenSfThreeMiOne() {
        // 3 sharps + minor = F# minor
        KeySignatureEvent ev(16, 3, true, nullptr);
        QByteArray expected;
        expected.append(char(0xFF));
        expected.append(char(0x59));
        expected.append(char(0x02));
        expected.append(char(0x03));
        expected.append(char(0x01));
        QCOMPARE(ev.save(), expected);
    }

    void keySig_save_fourFlatsMajor_thenSfStoredAsSignedByte() {
        // 4 flats + major = Ab major. Negative tonality must round-trip
        // as a signed byte (0xFC = -4).
        KeySignatureEvent ev(16, -4, false, nullptr);
        QByteArray expected;
        expected.append(char(0xFF));
        expected.append(char(0x59));
        expected.append(char(0x02));
        expected.append(char(0xFC));
        expected.append(char(0x00));
        QCOMPARE(ev.save(), expected);
    }

    void keySig_accessors_returnConstructorArguments() {
        KeySignatureEvent ev(16, -2, true, nullptr);
        QCOMPARE(ev.tonality(), -2);
        QCOMPARE(ev.minor(), true);
        QCOMPARE(ev.line(), int(MidiEvent::KEY_SIGNATURE_EVENT_LINE));
    }

    // ---- TimeSignatureEvent ---------------------------------------------

    void timeSig_save_4on4_thenSevenByteCanonicalHeader() {
        // 4/4, 24 MIDI clocks per metronome, 8 32nd notes per quarter
        // (the SMF default). Note: per the SMF spec, the second data byte
        // is the *power* of two for the denominator (4 -> 2).
        TimeSignatureEvent ev(18, 4, 2, 24, 8, nullptr);
        QByteArray expected;
        expected.append(char(0xFF));
        expected.append(char(0x58));
        expected.append(char(0x04));
        expected.append(char(0x04)); // numerator
        expected.append(char(0x02)); // denominator power (2 -> /4)
        expected.append(char(0x18)); // 24 MIDI clocks
        expected.append(char(0x08)); // 8 32nds per quarter
        QCOMPARE(ev.save(), expected);
    }

    void timeSig_save_6on8_thenDenominatorPowerEqualsThree() {
        TimeSignatureEvent ev(18, 6, 3, 36, 8, nullptr);
        QByteArray expected;
        expected.append(char(0xFF));
        expected.append(char(0x58));
        expected.append(char(0x04));
        expected.append(char(0x06)); // numerator
        expected.append(char(0x03)); // denominator power (3 -> /8)
        expected.append(char(0x24)); // 36 clocks
        expected.append(char(0x08));
        QCOMPARE(ev.save(), expected);
    }

    void timeSig_accessors_returnConstructorArguments() {
        TimeSignatureEvent ev(18, 7, 3, 18, 8, nullptr);
        QCOMPARE(ev.num(), 7);
        QCOMPARE(ev.denom(), 3);
        QCOMPARE(ev.midiClocks(), 18);
        QCOMPARE(ev.num32In4(), 8);
        QCOMPARE(ev.line(), int(MidiEvent::TIME_SIGNATURE_EVENT_LINE));
    }

    // ---- TempoChangeEvent -----------------------------------------------

    void tempoChange_construct_500000us_thenBeatsPerQuarterEquals120() {
        TempoChangeEvent ev(17, 500000, nullptr);
        QCOMPARE(ev.beatsPerQuarter(), 120);
    }

    void tempoChange_construct_1000000us_thenBeatsPerQuarterEquals60() {
        TempoChangeEvent ev(17, 1000000, nullptr);
        QCOMPARE(ev.beatsPerQuarter(), 60);
    }

    void tempoChange_construct_zeroValue_thenDefaultsTo120Bpm() {
        // Spec: a zero or negative value is replaced with 500000 us
        // (== 120 BPM). Guards against malformed input from importers.
        TempoChangeEvent ev(17, 0, nullptr);
        QCOMPARE(ev.beatsPerQuarter(), 120);
    }

    void tempoChange_save_120Bpm_thenThreeByteBigEndian500000() {
        TempoChangeEvent ev(17, 500000, nullptr);
        QByteArray expected;
        expected.append(char(0xFF));
        expected.append(char(0x51));
        expected.append(char(0x03));
        // 500000 == 0x07A120, big-endian
        expected.append(char(0x07));
        expected.append(char(0xA1));
        expected.append(char(0x20));
        QCOMPARE(ev.save(), expected);
    }

    void tempoChange_save_60Bpm_thenThreeByteBigEndian1000000() {
        TempoChangeEvent ev(17, 1000000, nullptr);
        QByteArray expected;
        expected.append(char(0xFF));
        expected.append(char(0x51));
        expected.append(char(0x03));
        // 1_000_000 == 0x0F4240, big-endian
        expected.append(char(0x0F));
        expected.append(char(0x42));
        expected.append(char(0x40));
        QCOMPARE(ev.save(), expected);
    }

    void tempoChange_save_roundTripValueMatchesConstructorInput() {
        // Pick a few BPMs that divide 60_000_000 cleanly so the round trip
        // is exact (the public API stores integer BPM only).
        const int bpms[] = {60, 90, 120, 150, 200, 240};
        for (int bpm : bpms) {
            TempoChangeEvent ev(17, 60000000 / bpm, nullptr);
            QByteArray bytes = ev.save();
            QCOMPARE(bytes.size(), 6);
            int decoded = (quint8(bytes.at(3)) << 16)
                        | (quint8(bytes.at(4)) << 8)
                        |  quint8(bytes.at(5));
            QCOMPARE(decoded, 60000000 / bpm);
        }
    }
};

QTEST_APPLESS_MAIN(TestMetaEvents)
#include "test_meta_events.moc"
