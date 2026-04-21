/*
 * test_protocol
 *
 * Unit tests for src/protocol/Protocol — undo/redo stack semantics.
 *
 * Strategy
 * --------
 * Protocol.cpp depends on MidiFile only via `_file->setSaved(false)` inside
 * endAction() and via the protocol() member used by ProtocolEntry::protocol()
 * (which we never call from this test). ProtocolItem.cpp does a
 * `dynamic_cast<MidiTrack*>(...)` and references MidiTrack typeinfo.
 *
 * Rather than link the whole MidiFile / MidiTrack tree, we provide ODR
 * shims: minimal `MidiFile` and `MidiTrack` classes whose only purpose is
 * to satisfy the linker. The Protocol* TUs compile against the real
 * headers; the linker resolves the symbols the test TU emits.
 *
 * The MidiTrack stub gets an out-of-line virtual destructor so its
 * vtable + RTTI are emitted — that is what the dynamic_cast in
 * ProtocolItem::release() needs at link time. Since our test entries are
 * never instances of MidiTrack, the dynamic_cast just returns nullptr at
 * run time and the symbol contents do not matter.
 */

#include <QtTest/QtTest>
#include <QObject>
#include <QSignalSpy>
#include <QString>

#include "../src/protocol/ProtocolEntry.h"

// ---- ODR shims ------------------------------------------------------------
// Must be defined out-of-line so the symbols are externally linkable.

class Protocol; // forward — real header included after the shim block

class MidiFile {
public:
    MidiFile() : savedCalls(0), lastSavedArg(true) {}
    void setSaved(bool v);
    Protocol *protocol(); // referenced by ProtocolEntry::protocol()
    int savedCalls;
    bool lastSavedArg;
};

void MidiFile::setSaved(bool v) {
    ++savedCalls;
    lastSavedArg = v;
}

Protocol *MidiFile::protocol() { return nullptr; }

// Stub MidiTrack: only needs to exist as a ProtocolEntry-derived type with
// a vtable so dynamic_cast<MidiTrack*> in ProtocolItem.cpp links.
class MidiTrack : public ProtocolEntry {
public:
    virtual ~MidiTrack();
};

MidiTrack::~MidiTrack() = default;

// ---- Real Protocol headers (after shims, so the shim names win) -----------

#include "../src/protocol/Protocol.h"
#include "../src/protocol/ProtocolStep.h"
#include "../src/protocol/ProtocolItem.h"

// ---- Fake protocol entry --------------------------------------------------
// Carries a single int "value". copy() snapshots, reloadState() restores.
// file() returns nullptr so ProtocolItem::release() will choose to delete
// the old object (the `_oldObject->file() != _oldObject` branch evaluates
// to true since nullptr != heap pointer).

class FakeEntry : public ProtocolEntry {
public:
    int value;
    static int liveCount;

    FakeEntry() : value(0) { ++liveCount; }
    explicit FakeEntry(int v) : value(v) { ++liveCount; }
    ~FakeEntry() override { --liveCount; }

    ProtocolEntry *copy() override {
        return new FakeEntry(value);
    }
    void reloadState(ProtocolEntry *entry) override {
        FakeEntry *e = static_cast<FakeEntry *>(entry);
        value = e->value;
    }
    MidiFile *file() override { return nullptr; }
};

int FakeEntry::liveCount = 0;

// ---------------------------------------------------------------------------

class TestProtocol : public QObject {
    Q_OBJECT

private:
    // Helper: capture the current state of `target` and push one
    // ProtocolItem inside a freshly opened action.
    static void changeValue(Protocol *p, FakeEntry *target, int newValue,
                            const QString &description) {
        ProtocolEntry *snapshot = target->copy();
        target->value = newValue;
        p->startNewAction(description);
        p->enterUndoStep(new ProtocolItem(snapshot, target));
        p->endAction();
    }

private slots:

    void initTestCase() {
        FakeEntry::liveCount = 0;
    }

    // -----------------------------------------------------------------
    void emptyAction_endAction_doesNotPushOntoUndoStack() {
        MidiFile file;
        Protocol p(&file);

        QCOMPARE(p.stepsBack(), 0);
        QCOMPARE(p.stepsForward(), 0);

        p.startNewAction("noop");
        p.endAction();

        QCOMPARE(p.stepsBack(), 0);
        QCOMPARE(p.stepsForward(), 0);
        // setSaved is called by endAction unconditionally, and
        // startNewAction internally calls endAction() to close any
        // prior open step — so a startNewAction + endAction pair
        // emits two setSaved calls even when no items were added.
        QCOMPARE(file.savedCalls, 2);
        QCOMPARE(file.lastSavedArg, false);
    }

    // -----------------------------------------------------------------
    void enterUndoStep_withoutOpenAction_silentlyDiscardsItem() {
        MidiFile file;
        Protocol p(&file);

        FakeEntry target;
        ProtocolEntry *snapshot = target.copy();
        const int liveBefore = FakeEntry::liveCount;

        // No startNewAction → enterUndoStep must delete the item.
        p.enterUndoStep(new ProtocolItem(snapshot, &target));

        // The snapshot held by the dropped ProtocolItem is leaked by
        // ProtocolItem (no dtor). We only verify the stack is unaffected.
        QCOMPARE(p.stepsBack(), 0);
        QCOMPARE(p.stepsForward(), 0);
        Q_UNUSED(liveBefore);
    }

    // -----------------------------------------------------------------
    void singleAction_endAction_pushesOntoUndoStackWithDescription() {
        MidiFile file;
        Protocol p(&file);

        FakeEntry target;
        changeValue(&p, &target, 42, "set 42");

        QCOMPARE(p.stepsBack(), 1);
        QCOMPARE(p.stepsForward(), 0);
        QCOMPARE(target.value, 42);
        QCOMPARE(p.undoStep(0)->description(), QString("set 42"));
    }

    // -----------------------------------------------------------------
    void undo_singleAction_restoresOldStateAndMovesStepToRedo() {
        MidiFile file;
        Protocol p(&file);

        FakeEntry target;
        changeValue(&p, &target, 7, "set 7");

        p.undo();

        QCOMPARE(target.value, 0);
        QCOMPARE(p.stepsBack(), 0);
        QCOMPARE(p.stepsForward(), 1);
        QCOMPARE(p.redoStep(0)->description(), QString("set 7"));
    }

    // -----------------------------------------------------------------
    void redo_afterUndo_restoresNewStateAndMovesStepBack() {
        MidiFile file;
        Protocol p(&file);

        FakeEntry target;
        changeValue(&p, &target, 11, "set 11");
        p.undo();
        QCOMPARE(target.value, 0);

        p.redo();

        QCOMPARE(target.value, 11);
        QCOMPARE(p.stepsBack(), 1);
        QCOMPARE(p.stepsForward(), 0);
    }

    // -----------------------------------------------------------------
    void undoRedo_multipleActions_lifoOrder() {
        MidiFile file;
        Protocol p(&file);

        FakeEntry target;
        changeValue(&p, &target, 1, "a");
        changeValue(&p, &target, 2, "b");
        changeValue(&p, &target, 3, "c");
        QCOMPARE(target.value, 3);
        QCOMPARE(p.stepsBack(), 3);

        // Undo most recent first.
        p.undo();
        QCOMPARE(target.value, 2);
        p.undo();
        QCOMPARE(target.value, 1);
        p.undo();
        QCOMPARE(target.value, 0);
        QCOMPARE(p.stepsBack(), 0);
        QCOMPARE(p.stepsForward(), 3);

        // Redo in reverse order.
        p.redo();
        QCOMPARE(target.value, 1);
        p.redo();
        QCOMPARE(target.value, 2);
        p.redo();
        QCOMPARE(target.value, 3);
        QCOMPARE(p.stepsForward(), 0);
    }

    // -----------------------------------------------------------------
    void startNewAction_afterUndo_clearsRedoStack() {
        MidiFile file;
        Protocol p(&file);

        FakeEntry target;
        changeValue(&p, &target, 10, "ten");
        changeValue(&p, &target, 20, "twenty");
        p.undo();
        QCOMPARE(p.stepsForward(), 1);

        // New action invalidates redo history.
        changeValue(&p, &target, 99, "branch");

        QCOMPARE(p.stepsForward(), 0);
        QCOMPARE(p.stepsBack(), 2);
        QCOMPARE(target.value, 99);
    }

    // -----------------------------------------------------------------
    void undo_emptyStack_isNoOpAndDoesNotEmit() {
        MidiFile file;
        Protocol p(&file);

        QSignalSpy spy(&p, &Protocol::undoRedoPerformed);
        p.undo();
        p.redo();

        QCOMPARE(spy.count(), 0);
        QCOMPARE(p.stepsBack(), 0);
        QCOMPARE(p.stepsForward(), 0);
    }

    // -----------------------------------------------------------------
    void undo_emitsUndoRedoPerformedSignal() {
        MidiFile file;
        Protocol p(&file);

        FakeEntry target;
        changeValue(&p, &target, 5, "x");

        QSignalSpy spy(&p, &Protocol::undoRedoPerformed);
        p.undo();
        QCOMPARE(spy.count(), 1);

        p.redo();
        QCOMPARE(spy.count(), 2);
    }

    // -----------------------------------------------------------------
    void endAction_marksFileDirty() {
        MidiFile file;
        Protocol p(&file);

        FakeEntry target;
        changeValue(&p, &target, 1, "first");

        QVERIFY(file.savedCalls >= 1);
        QCOMPARE(file.lastSavedArg, false);
    }

    // -----------------------------------------------------------------
    void startNewAction_whileActionOpen_endsPriorActionImplicitly() {
        MidiFile file;
        Protocol p(&file);

        FakeEntry target;
        ProtocolEntry *snap1 = target.copy();
        target.value = 1;
        p.startNewAction("first");
        p.enterUndoStep(new ProtocolItem(snap1, &target));

        // Did not call endAction — startNewAction("second") must close it.
        ProtocolEntry *snap2 = target.copy();
        target.value = 2;
        p.startNewAction("second");
        p.enterUndoStep(new ProtocolItem(snap2, &target));
        p.endAction();

        QCOMPARE(p.stepsBack(), 2);
        QCOMPARE(p.undoStep(0)->description(), QString("first"));
        QCOMPARE(p.undoStep(1)->description(), QString("second"));
    }

    // -----------------------------------------------------------------
    void addEmptyAction_appendsDescriptionWithoutItems() {
        MidiFile file;
        Protocol p(&file);

        p.addEmptyAction("File opened");
        QCOMPARE(p.stepsBack(), 1);
        QCOMPARE(p.undoStep(0)->description(), QString("File opened"));
    }

    // -----------------------------------------------------------------
    void goTo_undoStep_undoesUntilTargetIsLast() {
        MidiFile file;
        Protocol p(&file);

        FakeEntry target;
        changeValue(&p, &target, 1, "a");
        changeValue(&p, &target, 2, "b");
        changeValue(&p, &target, 3, "c");

        ProtocolStep *first = p.undoStep(0); // "a"
        p.goTo(first);

        // After goTo, the target step is the LAST entry on the undo stack
        // (i.e. the most recent action that has not been undone). In this
        // implementation goTo stops while there is still more than one
        // step on the stack, so "a" remains.
        QCOMPARE(p.stepsBack(), 1);
        QCOMPARE(p.undoStep(0)->description(), QString("a"));
        QCOMPARE(target.value, 1);
    }
};

QTEST_APPLESS_MAIN(TestProtocol)
#include "test_protocol.moc"
