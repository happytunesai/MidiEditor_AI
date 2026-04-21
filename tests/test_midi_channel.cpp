/*
 * test_midi_channel
 *
 * Unit tests for src/midi/MidiChannel — the per-channel event store that
 * every edit ultimately routes through. Covers:
 *   - QMultiMap insertion / iteration ordering by MIDI tick
 *   - removeEvent semantics inside vs. outside a Protocol action
 *   - the channel-17 / channel-18 "single anchor event at tick 0" guard
 *   - removeEvent cascading delete of a paired NoteOn/Off pair
 *   - progAtTick walking
 *   - deleteAllEvents
 *   - number()'s out-of-range clamping
 *   - visible() delegating to ChannelVisibilityManager
 *
 * Strategy
 * --------
 * MidiChannel.cpp drags in MidiFile / MidiTrack / Appearance / EventWidget
 * / the full event hierarchy via header includes plus calls inside
 * insertNote / removeEvent / setVisible. Linking the production tree is
 * the §3.1 monolith we must avoid.
 *
 * We instead:
 *   - link the four event .cpp files we directly construct
 *     (NoteOnEvent, OffEvent, OnEvent, ProgChangeEvent) — they only call
 *     into the MidiEvent base, which we ODR-shim;
 *   - link MidiChannel.cpp + ChannelVisibilityManager.cpp;
 *   - link the four protocol .cpp files (same pattern as test_protocol /
 *     test_midi_track) so Protocol::startNewAction / enterUndoStep work;
 *   - ODR-shim MidiFile, MidiTrack, MidiEvent base, GraphicObject,
 *     EventWidget, Appearance — defined out-of-line so the linker emits
 *     real symbols.
 *
 * Because MidiEvent::setMidiTime is shimmed (it just sets timePos and
 * does NOT route through file()->channelEvents()), the helper
 * MidiChannel::insertEvent / insertNote do not auto-populate the channel
 * map. The tests therefore push events directly via
 * `channel.eventMap()->insert(tick, ev)` — which is exactly what
 * production MidiEvent::setMidiTime does internally.
 */

#include <QtTest/QtTest>
#include <QObject>
#include <QList>
#include <QMultiMap>

// ---- Forward decls used by ODR shims ------------------------------------
class MidiTrack;
class MidiFile;
class Protocol;
class MidiChannel;
class MidiEvent;

// ---- ODR shims: Appearance ----------------------------------------------
#include <QColor>
class Appearance {
public:
    static QColor *channelColor(int n);
};
QColor *Appearance::channelColor(int /*n*/) {
    static QColor c(Qt::black);
    return &c;
}

// ---- ODR shims: MidiFile (never constructed directly) -------------------
class MidiFile {
public:
    MidiFile();
    Protocol *protocol();
    void setSaved(bool v);
    void setProtocol(Protocol *p);   // test-only helper
private:
    Protocol *_protocol;
};
MidiFile::MidiFile() : _protocol(nullptr) {}
Protocol *MidiFile::protocol() { return _protocol; }
void MidiFile::setSaved(bool /*v*/) {}
void MidiFile::setProtocol(Protocol *p) { _protocol = p; }

// ---- ODR shims: MidiTrack (vtable + a couple of accessors) --------------
// MidiChannel::removeEvent calls event->track()->setNameEvent(0) on the
// channel-16 branch, and references nameEvent() to compare. Our tests
// never enter that branch (we only ever set channel 0/17/18 in tests),
// but the symbols must link. Real signatures take TextEvent*, so we
// forward-declare it.
#include "../src/protocol/ProtocolEntry.h"
class TextEvent;
class MidiTrack : public ProtocolEntry {
public:
    virtual ~MidiTrack();
    void setNameEvent(TextEvent *e);
    TextEvent *nameEvent();
    bool hidden();
};
MidiTrack::~MidiTrack() = default;
void MidiTrack::setNameEvent(TextEvent * /*e*/) {}
TextEvent *MidiTrack::nameEvent() { return nullptr; }
bool MidiTrack::hidden() { return false; }

// ---- ODR shims: GraphicObject / EventWidget -----------------------------
#include "../src/gui/GraphicObject.h"
GraphicObject::GraphicObject() {}
void GraphicObject::draw(QPainter *, QColor) {}
bool GraphicObject::shown() { return false; }

#include "../src/gui/EventWidget.h"
void EventWidget::setEvents(QList<MidiEvent *>) {}
void EventWidget::reload() {}

// ---- ODR shims: MidiEvent base ------------------------------------------
// The .cpp files for NoteOnEvent / OffEvent / OnEvent / ProgChangeEvent
// only call into the MidiEvent base via these symbols; replicating just
// enough behaviour (timePos / channel / track storage) is plenty.
#include "../src/MidiEvent/MidiEvent.h"

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
void MidiEvent::setFile(MidiFile *f) { midiFile = f; }
int MidiEvent::line() { return UNKNOWN_LINE; }
QString MidiEvent::toMessage() { return QString(); }
QByteArray MidiEvent::save() { return QByteArray(); }
void MidiEvent::draw(QPainter *, QColor) {}
ProtocolEntry *MidiEvent::copy() { return nullptr; }
void MidiEvent::reloadState(ProtocolEntry *) {}
QString MidiEvent::typeString() { return QString(); }
bool MidiEvent::isOnEvent() { return false; }
// The production setMidiTime would route through file()->channelEvents();
// we just store the tick. Tests that need an event at a given tick push
// it into MidiChannel::eventMap() themselves — that is what production
// setMidiTime ultimately does.
void MidiEvent::setMidiTime(int t, bool) { timePos = t; }
int MidiEvent::midiTime() { return timePos; }
void MidiEvent::moveToChannel(int channel, bool) { numChannel = channel; }
int MidiEvent::channel() {
    if (numChannel < 0 || numChannel > 18) return 0;
    return numChannel;
}
MidiTrack *MidiEvent::track() { return _track; }

// ---- Real headers (after shims so our shim names win) -------------------
#include "../src/midi/MidiChannel.h"
#include "../src/MidiEvent/NoteOnEvent.h"
#include "../src/MidiEvent/OffEvent.h"
#include "../src/MidiEvent/OnEvent.h"
#include "../src/MidiEvent/ProgChangeEvent.h"
#include "../src/protocol/Protocol.h"
#include "../src/gui/ChannelVisibilityManager.h"

// =========================================================================

class TestMidiChannel : public QObject {
    Q_OBJECT

private:
    void drainStaticOnEventMap() {
        OffEvent::clearOnEvents();
    }

private slots:

    void init() { drainStaticOnEventMap(); }
    void cleanup() { drainStaticOnEventMap(); }

    // -----------------------------------------------------------------
    void freshChannel_thenEventMapIsEmpty() {
        MidiFile file;
        MidiChannel ch(&file, 0);
        QCOMPARE(ch.eventMap()->size(), 0);
        QCOMPARE(ch.number(), 0);
        QCOMPARE(ch.file(), &file);
        QCOMPARE(ch.mute(), false);
        QCOMPARE(ch.solo(), false);
    }

    // -----------------------------------------------------------------
    void number_outOfRange_thenClampedToZero() {
        // The ctor stores the raw value, but number() guards against
        // anything outside [0, 18]. Documents the current behaviour.
        MidiFile file;
        MidiChannel ch(&file, 99);
        QCOMPARE(ch.number(), 0);

        MidiChannel ch2(&file, -5);
        QCOMPARE(ch2.number(), 0);
    }

    // -----------------------------------------------------------------
    void eventMap_insertedAtMonotonicTicks_thenIteratedInTickOrder() {
        // QMultiMap iterates by ascending key — the foreach over
        // eventMap()->values() in production code therefore walks
        // events in tick order.
        MidiFile file;
        MidiChannel ch(&file, 0);

        NoteOnEvent *a = new NoteOnEvent(60, 100, 0, nullptr);
        NoteOnEvent *b = new NoteOnEvent(62, 100, 0, nullptr);
        NoteOnEvent *c = new NoteOnEvent(64, 100, 0, nullptr);
        ch.eventMap()->insert(200, b);
        ch.eventMap()->insert(100, a);
        ch.eventMap()->insert(300, c);

        QList<int> ticks;
        for (auto it = ch.eventMap()->begin(); it != ch.eventMap()->end(); ++it) {
            ticks.append(it.key());
        }
        QCOMPARE(ticks, (QList<int>{100, 200, 300}));

        // Cleanup: just delete; we did not pair OffEvents so the dtors
        // drain the static map cleanly.
        delete a;
        delete b;
        delete c;
    }

    // -----------------------------------------------------------------
    void eventMap_multipleAtSameTick_thenAllPreserved() {
        MidiFile file;
        MidiChannel ch(&file, 0);

        NoteOnEvent *a = new NoteOnEvent(60, 100, 0, nullptr);
        NoteOnEvent *b = new NoteOnEvent(62, 100, 0, nullptr);
        NoteOnEvent *c = new NoteOnEvent(64, 100, 0, nullptr);
        ch.eventMap()->insert(100, a);
        ch.eventMap()->insert(100, b);
        ch.eventMap()->insert(100, c);

        QCOMPARE(ch.eventMap()->size(), 3);
        QCOMPARE(ch.eventMap()->count(100), 3);

        delete a;
        delete b;
        delete c;
    }

    // -----------------------------------------------------------------
    void removeEvent_outsideProtocol_thenMapShrinksAndNoSnapshotTaken() {
        // toProtocol=false: removeEvent must not allocate a copy or
        // route through ProtocolEntry::protocol(). We can't directly
        // inspect "did it copy" but we can verify the event leaves the
        // map and the call returns true.
        MidiFile file;
        MidiChannel ch(&file, 0);

        NoteOnEvent *on = new NoteOnEvent(60, 100, 0, nullptr);
        on->setMidiTime(500, false);
        ch.eventMap()->insert(500, on);

        const bool removed = ch.removeEvent(on, /*toProtocol=*/false);
        QVERIFY(removed);
        QCOMPARE(ch.eventMap()->size(), 0);

        delete on;
    }

    // -----------------------------------------------------------------
    void removeEvent_insideProtocolAction_thenItemPushedOntoUndoStack() {
        // The protocol() member walks file()->protocol(); when both are
        // non-null it appends a ProtocolItem to the open step. We start
        // an action explicitly so the item lands on the undo stack.
        MidiFile file;
        Protocol protocol(&file);
        file.setProtocol(&protocol);
        QCOMPARE(file.protocol(), &protocol);

        MidiChannel ch(&file, 0);
        NoteOnEvent *on = new NoteOnEvent(60, 100, 0, nullptr);
        on->setMidiTime(500, false);
        ch.eventMap()->insert(500, on);

        protocol.startNewAction("remove");
        const bool removed = ch.removeEvent(on, /*toProtocol=*/true);
        protocol.endAction();

        QVERIFY(removed);
        QCOMPARE(ch.eventMap()->size(), 0);
        // Exactly one undo step should have landed on the stack.
        QCOMPARE(protocol.stepsBack(), 1);

        delete on;
    }

    // -----------------------------------------------------------------
    void removeEvent_channel18_singleAnchorAtTickZero_thenRefused() {
        // Time signature anchor protection: if channel 18 has exactly
        // one event at tick 0, removeEvent must return false without
        // touching the map.
        MidiFile file;
        MidiChannel ch(&file, 18);

        // We use a NoteOnEvent as a stand-in placeholder; the guard is
        // purely on tick + count, not on event type.
        NoteOnEvent *anchor = new NoteOnEvent(60, 100, 0, nullptr);
        anchor->setMidiTime(0, false);
        ch.eventMap()->insert(0, anchor);

        const bool removed = ch.removeEvent(anchor, /*toProtocol=*/false);
        QCOMPARE(removed, false);
        QCOMPARE(ch.eventMap()->size(), 1);

        // Cleanup — bypass the guard.
        ch.eventMap()->remove(0, anchor);
        delete anchor;
    }

    // -----------------------------------------------------------------
    void removeEvent_channel17_singleAnchorAtTickZero_thenRefused() {
        // Same guard for channel 17 (tempo).
        MidiFile file;
        MidiChannel ch(&file, 17);
        NoteOnEvent *anchor = new NoteOnEvent(60, 100, 0, nullptr);
        anchor->setMidiTime(0, false);
        ch.eventMap()->insert(0, anchor);

        const bool removed = ch.removeEvent(anchor, /*toProtocol=*/false);
        QCOMPARE(removed, false);
        QCOMPARE(ch.eventMap()->size(), 1);

        ch.eventMap()->remove(0, anchor);
        delete anchor;
    }

    // -----------------------------------------------------------------
    void removeEvent_channel18_secondEventAtTickZero_thenAllowed() {
        // Guard fires only when count(0) == 1. With two events at tick
        // 0, removing one is permitted.
        MidiFile file;
        MidiChannel ch(&file, 18);

        NoteOnEvent *a = new NoteOnEvent(60, 100, 0, nullptr);
        NoteOnEvent *b = new NoteOnEvent(62, 100, 0, nullptr);
        a->setMidiTime(0, false);
        b->setMidiTime(0, false);
        ch.eventMap()->insert(0, a);
        ch.eventMap()->insert(0, b);

        const bool removed = ch.removeEvent(a, /*toProtocol=*/false);
        QVERIFY(removed);
        QCOMPARE(ch.eventMap()->size(), 1);

        ch.eventMap()->remove(0, b);
        delete a;
        delete b;
    }

    // -----------------------------------------------------------------
    void removeEvent_pairedNoteOn_thenAlsoRemovesPairedOffEvent() {
        // dynamic_cast<OnEvent *>(event) succeeds for NoteOnEvent; if
        // the OnEvent has an attached OffEvent, removeEvent must
        // cascade and remove both halves of the pair.
        MidiFile file;
        MidiChannel ch(&file, 0);

        NoteOnEvent *on = new NoteOnEvent(60, 100, 0, nullptr);
        OffEvent *off = new OffEvent(0, 127 - 60, nullptr);
        // OffEvent ctor pairs with the NoteOn from the static map.
        QCOMPARE(off->onEvent(), static_cast<OnEvent *>(on));

        on->setMidiTime(100, false);
        off->setMidiTime(300, false);
        ch.eventMap()->insert(100, on);
        ch.eventMap()->insert(300, off);
        QCOMPARE(ch.eventMap()->size(), 2);

        const bool removed = ch.removeEvent(on, /*toProtocol=*/false);
        QVERIFY(removed);
        QCOMPARE(ch.eventMap()->size(), 0);

        delete on;
        delete off;
    }

    // -----------------------------------------------------------------
    void progAtTick_emptyChannel_thenReturnsZero() {
        MidiFile file;
        MidiChannel ch(&file, 0);
        QCOMPARE(ch.progAtTick(0), 0);
        QCOMPARE(ch.progAtTick(10000), 0);
    }

    // -----------------------------------------------------------------
    void progAtTick_returnsLastProgChangeAtOrBeforeTick() {
        MidiFile file;
        MidiChannel ch(&file, 0);

        ProgChangeEvent *p1 = new ProgChangeEvent(0, 5, nullptr);
        ProgChangeEvent *p2 = new ProgChangeEvent(0, 24, nullptr);
        ProgChangeEvent *p3 = new ProgChangeEvent(0, 73, nullptr);
        p1->setMidiTime(0, false);
        p2->setMidiTime(500, false);
        p3->setMidiTime(1000, false);
        ch.eventMap()->insert(0, p1);
        ch.eventMap()->insert(500, p2);
        ch.eventMap()->insert(1000, p3);

        QCOMPARE(ch.progAtTick(0), 5);
        QCOMPARE(ch.progAtTick(499), 5);
        QCOMPARE(ch.progAtTick(500), 24);
        QCOMPARE(ch.progAtTick(999), 24);
        QCOMPARE(ch.progAtTick(1000), 73);
        QCOMPARE(ch.progAtTick(50000), 73);

        delete p1;
        delete p2;
        delete p3;
    }

    // -----------------------------------------------------------------
    void progAtTick_noProgChangePresent_thenReturnsZero() {
        // A channel filled only with NoteOnEvents has no program info;
        // the search walks all entries and returns 0.
        MidiFile file;
        MidiChannel ch(&file, 0);

        NoteOnEvent *on = new NoteOnEvent(60, 100, 0, nullptr);
        on->setMidiTime(100, false);
        ch.eventMap()->insert(100, on);

        QCOMPARE(ch.progAtTick(50), 0);
        QCOMPARE(ch.progAtTick(200), 0);

        delete on;
    }

    // -----------------------------------------------------------------
    void deleteAllEvents_emptiesTheMap() {
        MidiFile file;
        MidiChannel ch(&file, 0);

        NoteOnEvent *a = new NoteOnEvent(60, 100, 0, nullptr);
        NoteOnEvent *b = new NoteOnEvent(62, 100, 0, nullptr);
        ch.eventMap()->insert(100, a);
        ch.eventMap()->insert(200, b);
        QCOMPARE(ch.eventMap()->size(), 2);

        ch.deleteAllEvents();
        QCOMPARE(ch.eventMap()->size(), 0);

        // deleteAllEvents only clears the map, it does not delete the
        // events themselves — the protocol system relies on this.
        delete a;
        delete b;
    }

    // -----------------------------------------------------------------
    void visible_delegatesToChannelVisibilityManager() {
        MidiFile file;
        MidiChannel ch(&file, 5);

        ChannelVisibilityManager::instance().resetAllVisible();
        QCOMPARE(ch.visible(), true);

        ChannelVisibilityManager::instance().setChannelVisible(5, false);
        QCOMPARE(ch.visible(), false);

        ChannelVisibilityManager::instance().resetAllVisible();
        QCOMPARE(ch.visible(), true);
    }

    // -----------------------------------------------------------------
    void copy_clonesEventMapByValueButSharesEventPointers() {
        // The MidiChannel copy ctor allocates a NEW QMultiMap and copy-
        // constructs it from the source's. Event pointers are shared.
        // This is what the protocol system snapshots before mutation.
        MidiFile file;
        MidiChannel ch(&file, 0);
        NoteOnEvent *on = new NoteOnEvent(60, 100, 0, nullptr);
        on->setMidiTime(100, false);
        ch.eventMap()->insert(100, on);

        ProtocolEntry *snapshot = ch.copy();
        MidiChannel *clone = dynamic_cast<MidiChannel *>(snapshot);
        QVERIFY(clone != nullptr);
        QCOMPARE(clone->eventMap()->size(), 1);
        QCOMPARE(clone->eventMap()->value(100), static_cast<MidiEvent *>(on));
        // The map is a deep copy, but pointers are shared.
        QVERIFY(clone->eventMap() != ch.eventMap());

        // Mutating the clone's map must not bleed into the original.
        clone->eventMap()->clear();
        QCOMPARE(ch.eventMap()->size(), 1);

        delete clone;
        delete on;
    }
};

QTEST_APPLESS_MAIN(TestMidiChannel)
#include "test_midi_channel.moc"
