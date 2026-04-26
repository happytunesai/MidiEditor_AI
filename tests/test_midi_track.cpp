/*
 * test_midi_track
 *
 * Unit tests for src/midi/MidiTrack — track number, name accessor (default
 * "Untitled track" when no name event is attached), assigned channel,
 * hidden/muted state, and the copy()/reloadState() pair used by the undo
 * protocol.
 *
 * Strategy
 * --------
 * MidiTrack.cpp pulls in MidiFile / MidiChannel / TextEvent / Appearance via
 * setName / color / copyToFile, and the Q_OBJECT machinery references them
 * unconditionally. Release /OPT:REF does NOT strip them in this project's
 * link config (verified — same situation as MidiEventSerializer in §2.6 of
 * the roadmap). We therefore ODR-shim every external symbol MidiTrack.cpp
 * touches: MidiFile (the four methods called from copyToFile + protocol +
 * setSaved), MidiChannel::insertEvent, the TextEvent stub (full class so
 * setName can construct one without UB even though no test reaches it),
 * and Appearance::trackColor. The protocol/* TUs are linked the same way
 * test_protocol does, so ProtocolEntry::protocol → enterUndoStep resolves;
 * our null-Protocol shim then makes the runtime path take the
 * `delete oldObj` branch and never touches a real Protocol.
 */

#include <QtTest/QtTest>
#include <QObject>
#include <QColor>
#include <QList>
#include <QString>

// ---- ODR shims for MidiTrack.cpp's external dependencies ------------------
// All member functions are defined OUT-OF-LINE so MSVC emits real (non-
// COMDAT, non-inline) symbols regardless of whether this TU calls them.
// The linker then resolves the external references from MidiTrack.cpp.obj.

class MidiTrack;
class MidiEvent;
class MidiChannel;
class Protocol;

class MidiFile {
public:
    MidiFile();
    void setSaved(bool v);
    Protocol *protocol();
    MidiChannel *channel(int ch);
    void addTrack();
    QList<MidiTrack *> *tracks();
    void registerCopiedTrack(MidiTrack *src, MidiTrack *dst, MidiFile *from);
private:
    QList<MidiTrack *> _tracks;
};

MidiFile::MidiFile() {}
void MidiFile::setSaved(bool /*v*/) {}
Protocol *MidiFile::protocol() { return nullptr; }
MidiChannel *MidiFile::channel(int /*ch*/) { return nullptr; }
void MidiFile::addTrack() {}
QList<MidiTrack *> *MidiFile::tracks() { return &_tracks; }
void MidiFile::registerCopiedTrack(MidiTrack * /*src*/, MidiTrack * /*dst*/, MidiFile * /*from*/) {}

class MidiChannel {
public:
    void insertEvent(MidiEvent *ev, int tick, bool ignoreSnapping = false);
};
void MidiChannel::insertEvent(MidiEvent * /*ev*/, int /*tick*/, bool /*ignoreSnapping*/) {}

class Appearance {
public:
    static QColor *trackColor(int number);
};
QColor *Appearance::trackColor(int /*number*/) {
    static QColor c(Qt::black);
    return &c;
}

// Real TextEvent header is too heavy (drags MidiEvent + GraphicObject +
// EventWidget). We only need a class named TextEvent in the global namespace
// with the exact symbols MidiTrack.cpp calls. None of these are reached by
// our tests, but they have to link.
class TextEvent {
public:
    enum { TRACKNAME = 1, TEXT = 5 };
    TextEvent(int channel, MidiTrack *track);
    QString text();
    void setText(QString t);
    int type();
    void setType(int t);
};
TextEvent::TextEvent(int /*channel*/, MidiTrack * /*track*/) {}
QString TextEvent::text() { return QString(); }
void TextEvent::setText(QString /*t*/) {}
int TextEvent::type() { return TRACKNAME; }
void TextEvent::setType(int /*t*/) {}

// ---- Real MidiTrack header (after shims so our shim names win) ------------

#include "../src/midi/MidiTrack.h"
#include "../src/protocol/ProtocolEntry.h"

// A second ProtocolEntry-derived type so reloadState() can be exercised
// with a non-MidiTrack pointer (the dynamic_cast inside reloadState should
// fall through and leave state untouched).
class OtherEntry : public ProtocolEntry {
public:
    ProtocolEntry *copy() override { return new OtherEntry(); }
    void reloadState(ProtocolEntry *) override {}
    MidiFile *file() override { return nullptr; }
};

// ---------------------------------------------------------------------------

class TestMidiTrack : public QObject {
    Q_OBJECT

private slots:

    // -----------------------------------------------------------------
    void defaultConstruction_thenAccessorsReturnDefaults() {
        MidiFile file;
        MidiTrack track(&file);

        QCOMPARE(track.number(), 0);
        QCOMPARE(track.hidden(), false);
        QCOMPARE(track.muted(), false);
        QCOMPARE(track.assignedChannel(), -1);
        QCOMPARE(track.file(), &file);
        QVERIFY(track.nameEvent() == nullptr);
        // No name event → fallback string.
        QCOMPARE(track.name(), QStringLiteral("Untitled track"));
    }

    // -----------------------------------------------------------------
    void setNumber_storesAndIsReadBackByNumber() {
        MidiFile file;
        MidiTrack track(&file);

        track.setNumber(7);
        QCOMPARE(track.number(), 7);

        // Negative / large values are accepted as-is — number() is just a
        // raw int identifier.
        track.setNumber(-3);
        QCOMPARE(track.number(), -3);
        track.setNumber(255);
        QCOMPARE(track.number(), 255);
    }

    // -----------------------------------------------------------------
    void setHidden_togglesHiddenState() {
        MidiFile file;
        MidiTrack track(&file);

        QCOMPARE(track.hidden(), false);
        track.setHidden(true);
        QCOMPARE(track.hidden(), true);
        track.setHidden(false);
        QCOMPARE(track.hidden(), false);
    }

    // -----------------------------------------------------------------
    void setMuted_togglesMutedState() {
        MidiFile file;
        MidiTrack track(&file);

        QCOMPARE(track.muted(), false);
        track.setMuted(true);
        QCOMPARE(track.muted(), true);
        track.setMuted(false);
        QCOMPARE(track.muted(), false);
    }

    // -----------------------------------------------------------------
    void assignChannel_storesValueAndIsReadBack() {
        MidiFile file;
        MidiTrack track(&file);

        QCOMPARE(track.assignedChannel(), -1);
        track.assignChannel(0);
        QCOMPARE(track.assignedChannel(), 0);
        track.assignChannel(15);
        QCOMPARE(track.assignedChannel(), 15);
        // Bounds are not enforced by assignChannel — out-of-range is
        // stored verbatim. Documents the current behaviour.
        track.assignChannel(99);
        QCOMPARE(track.assignedChannel(), 99);
    }

    // -----------------------------------------------------------------
    void copy_clonesNumberHiddenMutedAndFile() {
        MidiFile file;
        MidiTrack track(&file);
        track.setNumber(4);
        track.setHidden(true);
        track.setMuted(true);
        track.assignChannel(7);

        ProtocolEntry *entry = track.copy();
        MidiTrack *clone = dynamic_cast<MidiTrack *>(entry);
        QVERIFY2(clone != nullptr, "copy() must return a MidiTrack instance");

        QCOMPARE(clone->number(), 4);
        QCOMPARE(clone->hidden(), true);
        QCOMPARE(clone->muted(), true);
        QCOMPARE(clone->file(), &file);
        // TEST-001 (fixed): the copy ctor now propagates `_assignedChannel`.
        // Before the fix this field was left uninitialised, so the clone
        // returned garbage (e.g. 513 in one observed run). After the fix we
        // must see the source value.
        QCOMPARE(clone->assignedChannel(), 7);

        delete clone;
    }

    // -----------------------------------------------------------------
    void reloadState_fromMidiTrackEntry_restoresAllFields() {
        MidiFile file;
        MidiTrack track(&file);
        track.setNumber(2);
        track.setHidden(false);
        track.setMuted(false);
        track.assignChannel(5);

        // Snapshot the "old" state.
        ProtocolEntry *snapshot = track.copy();

        // Mutate.
        track.setNumber(99);
        track.setHidden(true);
        track.setMuted(true);
        track.assignChannel(12);
        QCOMPARE(track.number(), 99);
        QCOMPARE(track.hidden(), true);
        QCOMPARE(track.muted(), true);
        QCOMPARE(track.assignedChannel(), 12);

        // Restore.
        track.reloadState(snapshot);
        QCOMPARE(track.number(), 2);
        QCOMPARE(track.hidden(), false);
        QCOMPARE(track.muted(), false);
        QCOMPARE(track.file(), &file);
        QCOMPARE(track.assignedChannel(), 5); // TEST-001

        delete snapshot;
    }

    // -----------------------------------------------------------------
    void reloadState_fromForeignEntry_isNoOp() {
        MidiFile file;
        MidiTrack track(&file);
        track.setNumber(11);
        track.setHidden(true);

        OtherEntry foreign;
        // dynamic_cast<MidiTrack*> on a non-MidiTrack must return nullptr
        // and reloadState() must early-return without modifying state.
        track.reloadState(&foreign);

        QCOMPARE(track.number(), 11);
        QCOMPARE(track.hidden(), true);
    }

    // -----------------------------------------------------------------
    void setNameEvent_withNullEventOnFreshTrack_doesNotCrash() {
        MidiFile file;
        MidiTrack track(&file);
        // Existing _nameEvent is nullptr — setNameEvent must not
        // dereference it. New event is also nullptr — same.
        track.setNameEvent(nullptr);
        QVERIFY(track.nameEvent() == nullptr);
        // Name fallback still applies.
        QCOMPARE(track.name(), QStringLiteral("Untitled track"));
    }

    // -----------------------------------------------------------------
    void setNumber_withNullProtocol_dropsCopiedSnapshotWithoutCrashing() {
        // Regression guard: ProtocolEntry::protocol() takes the
        // `delete oldObj` branch when file()->protocol() returns nullptr.
        // setNumber must not leak or crash when called many times.
        MidiFile file;
        MidiTrack track(&file);
        for (int i = 0; i < 100; ++i) {
            track.setNumber(i);
        }
        QCOMPARE(track.number(), 99);
    }
};

QTEST_APPLESS_MAIN(TestMidiTrack)
#include "test_midi_track.moc"
