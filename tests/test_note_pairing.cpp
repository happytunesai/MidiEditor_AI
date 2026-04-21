/*
 * test_note_pairing
 *
 * Unit tests for the NoteOnEvent / OffEvent pairing invariant maintained
 * via the process-wide static `OffEvent::onEvents` map.
 *
 * What the production code does
 * -----------------------------
 *   - NoteOnEvent's ctor calls OffEvent::enterOnEvent(this), which inserts
 *     the OnEvent into a static QMultiMap<int line, OnEvent *> keyed by
 *     `event->line()` (== 127 - note for NoteOnEvent).
 *   - NoteOnEvent's dtor (V131-P2-03) calls OffEvent::removeOnEvent(this)
 *     to drain the entry — symmetric to the ctor — so a NoteOnEvent freed
 *     before being matched does not leave a dangling pointer.
 *   - OffEvent's ctor scans onEvents->values(line()) and pairs with the
 *     first OnEvent whose channel matches the OffEvent's channel.
 *
 * Strategy
 * --------
 * Compiles only NoteOnEvent.cpp + OffEvent.cpp + OnEvent.cpp directly.
 * The MidiEvent base + ProtocolEntry + GraphicObject + EventWidget +
 * MidiFile bits are ODR-shimmed exactly like test_meta_events. No real
 * MidiFile / MidiChannel / MidiTrack tree is linked.
 *
 * MidiTrack is forward-declared and we always pass nullptr — none of the
 * exercised paths dereference it.
 */

#include <QtTest/QtTest>
#include <QObject>
#include <QList>

// Headers under test (these transitively include MidiEvent.h, which
// includes EventWidget.h and GraphicObject.h).
#include "../src/MidiEvent/NoteOnEvent.h"
#include "../src/MidiEvent/OffEvent.h"
#include "../src/MidiEvent/OnEvent.h"

// ---- ODR shims: ProtocolEntry -------------------------------------------
ProtocolEntry::~ProtocolEntry() {}
ProtocolEntry *ProtocolEntry::copy() { return nullptr; }
void ProtocolEntry::reloadState(ProtocolEntry *) {}
MidiFile *ProtocolEntry::file() { return nullptr; }
void ProtocolEntry::protocol(ProtocolEntry *oldObj, ProtocolEntry *) {
    // Mirror production behaviour for the no-file branch: caller
    // deletes the old snapshot. We never enter a real undo step.
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
// We must NOT define MidiFile::~MidiFile() — that would anchor its vtable
// in this TU and pull every other MidiFile virtual symbol in with it.
class MidiFile;
#include "../src/midi/MidiFile.h"

// =========================================================================

class TestNotePairing : public QObject {
    Q_OBJECT

private:
    void drainStaticMap() {
        // Belt-and-braces: every test should start from an empty static
        // map. clearOnEvents is the production helper for this.
        OffEvent::clearOnEvents();
    }

private slots:

    void init() { drainStaticMap(); }
    void cleanup() { drainStaticMap(); }

    // -----------------------------------------------------------------
    void noteOn_constructed_thenAppearsInCorruptedOnEvents() {
        // A NoteOnEvent that never sees an OffEvent is, by definition,
        // a "corrupted" entry in the static onEvents map.
        NoteOnEvent on(60, 100, 0, nullptr);

        QList<OnEvent *> corrupt = OffEvent::corruptedOnEvents();
        QCOMPARE(corrupt.size(), 1);
        QCOMPARE(corrupt.first(), static_cast<OnEvent *>(&on));
    }

    // -----------------------------------------------------------------
    void noteOn_destroyed_thenRemovedFromOnEventsMap() {
        // V131-P2-03 regression: destructor must symmetrically drain
        // the static map so the next OffEvent ctor cannot dereference
        // a stale pointer.
        {
            NoteOnEvent on(60, 100, 0, nullptr);
            QCOMPARE(OffEvent::corruptedOnEvents().size(), 1);
        }
        QCOMPARE(OffEvent::corruptedOnEvents().size(), 0);
    }

    // -----------------------------------------------------------------
    void offEvent_matchingChannelAndLine_thenPairsAndDrainsMap() {
        NoteOnEvent on(60, 100, 0, nullptr);
        // line of NoteOnEvent(note=60) is 127-60 = 67. OffEvent ctor
        // takes the line directly.
        OffEvent off(0, 127 - 60, nullptr);

        QCOMPARE(off.onEvent(), static_cast<OnEvent *>(&on));
        QCOMPARE(on.offEvent(), static_cast<OffEvent *>(&off));
        // Map is drained: the NoteOn is no longer "corrupt".
        QCOMPARE(OffEvent::corruptedOnEvents().size(), 0);
    }

    // -----------------------------------------------------------------
    void offEvent_channelMismatch_thenLeavesNoteOnUnpaired() {
        NoteOnEvent on(60, 100, /*ch*/ 0, nullptr);
        // Same line, different channel → no pairing.
        OffEvent off(/*ch*/ 1, 127 - 60, nullptr);

        QVERIFY(off.onEvent() == nullptr);
        QVERIFY(on.offEvent() == nullptr);
        QCOMPARE(OffEvent::corruptedOnEvents().size(), 1);
    }

    // -----------------------------------------------------------------
    void offEvent_lineMismatch_thenLeavesNoteOnUnpaired() {
        NoteOnEvent on(60, 100, 0, nullptr);
        // Different line (different note) → no pairing even on same channel.
        OffEvent off(0, 127 - 61, nullptr);

        QVERIFY(off.onEvent() == nullptr);
        QVERIFY(on.offEvent() == nullptr);
        QCOMPARE(OffEvent::corruptedOnEvents().size(), 1);
    }

    // -----------------------------------------------------------------
    void offEvent_pairsByChannelEvenWhenLineEntryIsForeign_thenSkipsForeign() {
        // Two NoteOns on the same line but different channels. An
        // OffEvent for channel 1 must pair with the channel-1 NoteOn,
        // not the channel-0 one inserted first.
        NoteOnEvent onCh0(60, 100, 0, nullptr);
        NoteOnEvent onCh1(60, 100, 1, nullptr);
        QCOMPARE(OffEvent::corruptedOnEvents().size(), 2);

        OffEvent off(1, 127 - 60, nullptr);

        QCOMPARE(off.onEvent(), static_cast<OnEvent *>(&onCh1));
        QVERIFY(onCh0.offEvent() == nullptr);
        QCOMPARE(onCh1.offEvent(), static_cast<OffEvent *>(&off));
        // Only the channel-1 entry was drained.
        QCOMPARE(OffEvent::corruptedOnEvents().size(), 1);
        QCOMPARE(OffEvent::corruptedOnEvents().first(),
                 static_cast<OnEvent *>(&onCh0));
    }

    // -----------------------------------------------------------------
    void multipleOffEvents_sameChannelAndLine_thenPairFifo() {
        // Two NoteOns on identical (channel, line). QMultiMap returns
        // values for a key in last-in-first-out order in Qt 6, but the
        // production code uses values(line()) and walks the list from
        // index 0 forward — whatever ordering QMultiMap chose is the
        // ordering we observe. Asserting "off1 pairs with one of the
        // two; off2 pairs with the other; both are drained" is the
        // contract that matters.
        NoteOnEvent onA(60, 100, 0, nullptr);
        NoteOnEvent onB(60, 100, 0, nullptr);
        QCOMPARE(OffEvent::corruptedOnEvents().size(), 2);

        OffEvent off1(0, 127 - 60, nullptr);
        OffEvent off2(0, 127 - 60, nullptr);

        QVERIFY(off1.onEvent() != nullptr);
        QVERIFY(off2.onEvent() != nullptr);
        QVERIFY(off1.onEvent() != off2.onEvent());
        QCOMPARE(OffEvent::corruptedOnEvents().size(), 0);
    }

    // -----------------------------------------------------------------
    void copyConstructedNoteOn_doesNotRegisterInMap() {
        // Only the original NoteOnEvent ctor calls enterOnEvent. The
        // copy ctor (used by ProtocolItem snapshots) must not push a
        // duplicate, otherwise undo would corrupt the pairing map.
        NoteOnEvent original(60, 100, 0, nullptr);
        QCOMPARE(OffEvent::corruptedOnEvents().size(), 1);

        NoteOnEvent copy(original);
        // Still exactly one entry — the copy did not register.
        QList<OnEvent *> corrupt = OffEvent::corruptedOnEvents();
        QCOMPARE(corrupt.size(), 1);
        QCOMPARE(corrupt.first(), static_cast<OnEvent *>(&original));
    }

    // -----------------------------------------------------------------
    void copyConstructedNoteOn_destroyed_doesNotRemoveOriginalEntry() {
        // The dtor's removeOnEvent on a copy must be a no-op (the copy
        // was never inserted) and must NOT take the original's entry
        // along with it. QMultiMap::remove uses both key and value, so
        // a removal targeting the copy pointer cannot match the
        // original's pointer — verify that explicitly.
        NoteOnEvent original(60, 100, 0, nullptr);
        {
            NoteOnEvent copy(original);
            QCOMPARE(OffEvent::corruptedOnEvents().size(), 1);
        }
        // Original is still in the map.
        QList<OnEvent *> corrupt = OffEvent::corruptedOnEvents();
        QCOMPARE(corrupt.size(), 1);
        QCOMPARE(corrupt.first(), static_cast<OnEvent *>(&original));
    }

    // -----------------------------------------------------------------
    void clearOnEvents_drainsAllPendingEntries() {
        NoteOnEvent on1(60, 100, 0, nullptr);
        NoteOnEvent on2(64, 100, 1, nullptr);
        NoteOnEvent on3(67, 100, 2, nullptr);
        QCOMPARE(OffEvent::corruptedOnEvents().size(), 3);

        OffEvent::clearOnEvents();
        QCOMPARE(OffEvent::corruptedOnEvents().size(), 0);

        // Still safe to destroy on1..on3 — their dtor's removeOnEvent
        // is a no-op for keys not present. (Implicit: this slot exits
        // without crashing.)
    }

    // -----------------------------------------------------------------
    void offEventConstructed_beforeNoteOn_doesNotPair() {
        // Order matters: OffEvent only pairs with NoteOns already in
        // the map. A late NoteOn cannot retroactively pair with a
        // prior unpaired OffEvent.
        OffEvent off(0, 127 - 60, nullptr);
        NoteOnEvent on(60, 100, 0, nullptr);

        QVERIFY(off.onEvent() == nullptr);
        QVERIFY(on.offEvent() == nullptr);
        // The NoteOn is still pending in the map.
        QCOMPARE(OffEvent::corruptedOnEvents().size(), 1);
    }

    // -----------------------------------------------------------------
    void offEventLine_beforePairing_returnsCtorLine() {
        // OffEvent::line() returns onEvent()->line() once paired, but
        // falls back to the line passed to the ctor when unpaired.
        OffEvent off(0, 42, nullptr);
        QCOMPARE(off.line(), 42);
    }
};

QTEST_APPLESS_MAIN(TestNotePairing)
#include "test_note_pairing.moc"
