/*
 * test_midi_event
 *
 * Unit tests for the MidiEvent base class in src/MidiEvent/MidiEvent.cpp.
 *
 * Scope
 * -----
 * Directly exercises the base-class behaviour that every event subclass
 * inherits: field initialisation, copy construction, channel clamping,
 * track/file/widget accessors, the knownMetaTypes() table, the line-enum
 * constants, and the shownInEventWidget / temporaryRecordID round-trips.
 *
 * The subclasses' line() overrides (TEMPO_CHANGE_EVENT_LINE = 128, etc.)
 * are covered transitively via test_meta_events / test_text_event /
 * test_sysex_event / test_channel_events. Here we pin down the base:
 *   - MidiEvent::line() default (returns 0 — distinct from UNKNOWN_LINE)
 *   - MidiEvent::isOnEvent() default (returns true)
 *   - enum ordering / numeric values of the line constants
 *
 * Strategy
 * --------
 * Links the REAL MidiEvent.cpp plus every event subclass .cpp plus
 * GraphicObject.cpp. Every event .cpp is needed because MidiEvent.cpp's
 * static loadMidiEvent factory references every subclass ctor/vtable,
 * and /OPT:REF does not strip loadMidiEvent (static-but-exported) in this
 * project's link config. ODR-shimmed the remaining external surface:
 *   - ProtocolEntry base (never hits a real Protocol)
 *   - Appearance::borderColor (MidiEvent::draw is in the vtable)
 *   - EventWidget::events/setEvents/reload
 *   - MidiFile::{channelEvents, endTick, setMaxLengthMs, msOfTick,
 *     channel, variableLengthvalue, writeVariableLengthValue,
 *     ticksPerQuarter, calcMaxTime}
 *   - MidiChannel::{eventMap, insertEvent, removeEvent}
 *
 * None of the shims are called at runtime — they just satisfy the linker
 * for virtual members reachable from the live vtables. All MidiFile /
 * MidiChannel pointers in the test are either nullptr or sentinel values
 * used purely for accessor round-trip checks.
 *
 * MidiTrack is forward-declared; the test only passes pointers through
 * the base class without dereferencing.
 */

#include <QtTest/QtTest>
#include <QObject>
#include <QList>
#include <QColor>

#include "../src/MidiEvent/MidiEvent.h"

// ---- ODR shims: ProtocolEntry ------------------------------------------
// MidiEvent inherits ProtocolEntry; we provide the base-class symbols here
// so ProtocolEntry.cpp does not need to link (it would drag in Protocol /
// ProtocolItem / MidiFile::protocol). The production protocol() routes to
// file()->protocol()->enterUndoStep(...); with a null file it deletes the
// snapshot — we mirror that so setTrack(..., true) paths would not leak,
// even though this test only calls the toProtocol=false overloads.
ProtocolEntry::~ProtocolEntry() {}
ProtocolEntry *ProtocolEntry::copy() { return nullptr; }
void ProtocolEntry::reloadState(ProtocolEntry *) {}
MidiFile *ProtocolEntry::file() { return nullptr; }
void ProtocolEntry::protocol(ProtocolEntry *oldObj, ProtocolEntry *) {
    delete oldObj;
}

// ---- ODR shims: Appearance (static used by MidiEvent::draw) -------------
#include "../src/gui/Appearance.h"
QColor Appearance::borderColor() { return QColor(); }

// ---- ODR shims: GraphicObject ------------------------------------------
// (None needed — GraphicObject.cpp is linked below.)

// ---- ODR shims: EventWidget --------------------------------------------
#include "../src/gui/EventWidget.h"
void EventWidget::setEvents(QList<MidiEvent *>) {}
void EventWidget::reload() {}
QList<MidiEvent *> EventWidget::events() { return {}; }

// ---- ODR shims: MidiFile -----------------------------------------------
// MidiEvent.cpp's virtuals (setMidiTime / setChannel / reloadState) call
// into MidiFile and MidiChannel. We never hit those paths in this test
// (toProtocol=false guards them, or we simply never invoke them), but the
// linker still needs the symbols because they are reachable from the live
// vtable. We DO NOT define MidiFile::~MidiFile — defining the out-of-line
// dtor would anchor MidiFile's vtable in this TU and drag every other
// MidiFile virtual symbol in with it.
class MidiFile;
#include "../src/midi/MidiFile.h"
QMultiMap<int, MidiEvent *> *MidiFile::channelEvents(int) { return nullptr; }
int MidiFile::endTick() { return 0; }
void MidiFile::setMaxLengthMs(int) {}
int MidiFile::msOfTick(int, QList<MidiEvent *> *, int) { return 0; }
MidiChannel *MidiFile::channel(int) { return nullptr; }
int MidiFile::variableLengthvalue(QDataStream *) { return 0; }
QByteArray MidiFile::writeVariableLengthValue(int) { return QByteArray(); }
int MidiFile::ticksPerQuarter() { return 192; }
void MidiFile::calcMaxTime() {}

// ---- ODR shims: MidiChannel --------------------------------------------
#include "../src/midi/MidiChannel.h"
QMultiMap<int, MidiEvent *> *MidiChannel::eventMap() { return nullptr; }
void MidiChannel::insertEvent(MidiEvent *, int, bool) {}
bool MidiChannel::removeEvent(MidiEvent *, bool) { return false; }

// =========================================================================

class TestMidiEvent : public QObject {
    Q_OBJECT

private slots:

    // -----------------------------------------------------------------
    // Constructor / field initialisation
    // -----------------------------------------------------------------
    void ctor_withChannelAndTrack_thenFieldsInitialised() {
        MidiEvent ev(5, nullptr);

        QCOMPARE(ev.channel(), 5);
        QCOMPARE(ev.track(), static_cast<MidiTrack *>(nullptr));
        QCOMPARE(ev.midiTime(), 0);
        QCOMPARE(ev.file(), static_cast<MidiFile *>(nullptr));
        QCOMPARE(ev.temporaryRecordID(), -1);
    }

    // -----------------------------------------------------------------
    // Copy constructor
    // -----------------------------------------------------------------
    void copyCtor_whenConstructed_thenFieldsMirrored() {
        MidiEvent original(7, nullptr);
        original.setTemporaryRecordID(42);

        MidiEvent copy(original);

        QCOMPARE(copy.channel(), 7);
        QCOMPARE(copy.track(), static_cast<MidiTrack *>(nullptr));
        QCOMPARE(copy.midiTime(), 0);
        QCOMPARE(copy.file(), static_cast<MidiFile *>(nullptr));
        QCOMPARE(copy.temporaryRecordID(), 42);
    }

    void copyCtor_thenOriginalAndCopyAreIndependent() {
        MidiEvent original(3, nullptr);
        original.setTemporaryRecordID(10);

        MidiEvent copy(original);

        // Mutating the original must not bleed into the copy.
        original.setTemporaryRecordID(99);
        original.setChannel(8, /*toProtocol=*/false);

        QCOMPARE(copy.channel(), 3);
        QCOMPARE(copy.temporaryRecordID(), 10);
    }

    // -----------------------------------------------------------------
    // channel() clamping — mirrors the production invariant:
    // "valid channels are 0..18 (16 meta, 17 tempo, 18 time sig);
    // anything else is a corrupted read and must return 0".
    // -----------------------------------------------------------------
    void channel_withNegativeValue_thenClampsToZero() {
        MidiEvent ev(0, nullptr);
        ev.setChannel(-1, /*toProtocol=*/false);  // setter clamps to 0
        QCOMPARE(ev.channel(), 0);
    }

    void channel_withValueAboveEighteen_thenClampsToZero() {
        MidiEvent ev(0, nullptr);
        ev.setChannel(19, /*toProtocol=*/false);  // setter clamps to 0
        QCOMPARE(ev.channel(), 0);
    }

    void channel_withBoundaryValues_thenReturnedAsIs() {
        // 0..18 are all legal, including the three special channels:
        //   16 = meta (key sig, text, lyrics)
        //   17 = tempo
        //   18 = time signature
        for (int ch : {0, 1, 15, 16, 17, 18}) {
            MidiEvent ev(0, nullptr);
            ev.setChannel(ch, /*toProtocol=*/false);
            QCOMPARE(ev.channel(), ch);
        }
    }

    // -----------------------------------------------------------------
    // Track / file accessors (toProtocol=false path — no protocol entry
    // is pushed, no file deref).
    // -----------------------------------------------------------------
    void setTrack_withToProtocolFalse_thenAccessorUpdated() {
        MidiEvent ev(0, nullptr);
        auto *sentinel = reinterpret_cast<MidiTrack *>(0xDEADBEEF);

        ev.setTrack(sentinel, /*toProtocol=*/false);

        QCOMPARE(ev.track(), sentinel);
    }

    void setFile_thenAccessorReturnsSameFile() {
        MidiEvent ev(0, nullptr);
        auto *sentinel = reinterpret_cast<MidiFile *>(0xFEEDFACE);

        ev.setFile(sentinel);

        QCOMPARE(ev.file(), sentinel);
    }

    // -----------------------------------------------------------------
    // Default virtuals on the base (each subclass overrides these;
    // here we pin the base defaults so a regression in the header
    // defaults surfaces).
    // -----------------------------------------------------------------
    void line_forBaseInstance_thenZero() {
        MidiEvent ev(0, nullptr);
        QCOMPARE(ev.line(), 0);
    }

    void isOnEvent_forBaseInstance_thenTrue() {
        // The base class historically returns true so generic tools
        // treat an unknown event as an on-type placeholder. Subclasses
        // (OffEvent, TextEvent, TempoChangeEvent, ...) override to false.
        MidiEvent ev(0, nullptr);
        QVERIFY(ev.isOnEvent());
    }

    void typeString_forBaseInstance_thenMidiEvent() {
        MidiEvent ev(0, nullptr);
        QCOMPARE(ev.typeString(), QStringLiteral("Midi Event"));
    }

    // -----------------------------------------------------------------
    // Piano-roll line index mapping — the enum that subclasses return
    // from line(). The numeric values are load-bearing: MatrixWidget
    // and the event widget index into arrays by line number, and the
    // line constants start at 128 (above the 0..127 range used by
    // note events, where line = 127 - noteNumber).
    // -----------------------------------------------------------------
    void lineEnumConstants_inExpectedOrder() {
        // First non-note line starts at 128 and the enum is sequential.
        QCOMPARE(static_cast<int>(MidiEvent::TEMPO_CHANGE_EVENT_LINE), 128);
        QCOMPARE(static_cast<int>(MidiEvent::TIME_SIGNATURE_EVENT_LINE), 129);
        QCOMPARE(static_cast<int>(MidiEvent::KEY_SIGNATURE_EVENT_LINE), 130);
        QCOMPARE(static_cast<int>(MidiEvent::PROG_CHANGE_LINE), 131);
        QCOMPARE(static_cast<int>(MidiEvent::CONTROLLER_LINE), 132);
        QCOMPARE(static_cast<int>(MidiEvent::KEY_PRESSURE_LINE), 133);
        QCOMPARE(static_cast<int>(MidiEvent::CHANNEL_PRESSURE_LINE), 134);
        QCOMPARE(static_cast<int>(MidiEvent::TEXT_EVENT_LINE), 135);
        QCOMPARE(static_cast<int>(MidiEvent::PITCH_BEND_LINE), 136);
        QCOMPARE(static_cast<int>(MidiEvent::SYSEX_LINE), 137);
        QCOMPARE(static_cast<int>(MidiEvent::UNKNOWN_LINE), 138);
    }

    // -----------------------------------------------------------------
    // knownMetaTypes — the meta-type lookup table used by the raw
    // hex editor for human-readable labels.
    // -----------------------------------------------------------------
    void knownMetaTypes_containsExpectedKeys() {
        QMap<int, QString> meta = MidiEvent::knownMetaTypes();

        // Text events: 0x01..0x07 all map to "Text Event"
        for (int i = 1; i < 8; ++i) {
            QVERIFY2(meta.contains(i),
                     QString("missing text meta key 0x%1").arg(i, 0, 16).toUtf8());
            QCOMPARE(meta.value(i), QStringLiteral("Text Event"));
        }

        QCOMPARE(meta.value(0x51), QStringLiteral("Tempo Change Event"));
        QCOMPARE(meta.value(0x58), QStringLiteral("Time Signature Event"));
        QCOMPARE(meta.value(0x59), QStringLiteral("Key Signature Event"));
        QCOMPARE(meta.value(0x2F), QStringLiteral("End of Track"));

        // Total: 7 text + 4 named = 11 entries, nothing more.
        QCOMPARE(meta.size(), 11);
    }

    // -----------------------------------------------------------------
    // Static event widget registration.
    // -----------------------------------------------------------------
    void eventWidget_roundtrip() {
        // Save / restore so a failure does not poison sibling tests.
        EventWidget *saved = MidiEvent::eventWidget();

        MidiEvent::setEventWidget(nullptr);
        QCOMPARE(MidiEvent::eventWidget(), static_cast<EventWidget *>(nullptr));

        auto *sentinel = reinterpret_cast<EventWidget *>(0xA1A1A1A1);
        MidiEvent::setEventWidget(sentinel);
        QCOMPARE(MidiEvent::eventWidget(), sentinel);

        MidiEvent::setEventWidget(saved);
    }

    void shownInEventWidget_withNullStaticWidget_thenFalse() {
        EventWidget *saved = MidiEvent::eventWidget();
        MidiEvent::setEventWidget(nullptr);

        MidiEvent ev(0, nullptr);
        QVERIFY(!ev.shownInEventWidget());

        MidiEvent::setEventWidget(saved);
    }

    // -----------------------------------------------------------------
    // Temporary record ID (used during live MIDI recording to correlate
    // note-on / note-off pairs before they are committed to a track).
    // -----------------------------------------------------------------
    void temporaryRecordID_roundtrip() {
        MidiEvent ev(0, nullptr);
        QCOMPARE(ev.temporaryRecordID(), -1);

        ev.setTemporaryRecordID(7);
        QCOMPARE(ev.temporaryRecordID(), 7);

        ev.setTemporaryRecordID(-1);
        QCOMPARE(ev.temporaryRecordID(), -1);
    }
};

QTEST_APPLESS_MAIN(TestMidiEvent)
#include "test_midi_event.moc"
