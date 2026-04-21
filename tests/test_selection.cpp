/*
 * test_selection
 *
 * Unit tests for src/tool/Selection — the global singleton that tracks
 * the currently selected MIDI events.
 *
 * Strategy
 * --------
 * Selection.cpp depends on:
 *   - ProtocolEntry (light header; calls ProtocolEntry::protocol() which
 *     in turn would use MidiFile + Protocol + ProtocolItem).
 *   - EventWidget   (heavy QWidget header; only its setEvents() / reload()
 *     are called, and only when the static _eventWidget pointer is non-null).
 *
 * Rather than link the entire undo/redo + GUI tree, this test TU provides
 * its own out-of-line definitions of the three ProtocolEntry methods and
 * the two EventWidget methods Selection needs. Selection.cpp links against
 * those stubs (ODR shims). The static Selection::_eventWidget stays at its
 * default value of nullptr, so the EventWidget bodies are unreachable —
 * they exist only to satisfy the linker.
 *
 * MidiEvent pointers are sentinel values cast from quintptr — Selection
 * never dereferences them, it only stores and returns the pointers.
 */

#include <QtTest/QtTest>
#include <QObject>
#include <QList>

#include "../src/protocol/ProtocolEntry.h"

// ---- ODR shims for ProtocolEntry ----------------------------------------
// Selection inherits ProtocolEntry. Its setSelection() calls protocol(copy(),
// this), which on the real codebase routes to file->protocol()->enterUndoStep.
// We short-circuit it by deleting the snapshot directly (Selection::file()
// returns nullptr in this test, matching the real behaviour for an unbound
// selection — the production ProtocolEntry::protocol() takes the same
// `delete oldObj` branch when file() is null).

ProtocolEntry::~ProtocolEntry() {}
ProtocolEntry *ProtocolEntry::copy() { return nullptr; }
void ProtocolEntry::reloadState(ProtocolEntry *) {}
MidiFile *ProtocolEntry::file() { return nullptr; }
void ProtocolEntry::protocol(ProtocolEntry *oldObj, ProtocolEntry *) {
    delete oldObj;
}

// ---- ODR shim for EventWidget -------------------------------------------
// Selection.cpp references EventWidget::setEvents() and EventWidget::reload()
// (the latter is wrapped in `if (_eventWidget)` guards). Define empty bodies
// so the static references resolve at link time. We never construct an
// EventWidget — the AUTOMOC-generated metaobject for it relies only on
// Qt6::Widgets symbols.
#include "../src/gui/EventWidget.h"
void EventWidget::setEvents(QList<MidiEvent *>) {}
void EventWidget::reload() {}

// ---- Real Selection header -----------------------------------------------
#include "../src/tool/Selection.h"


class TestSelection : public QObject {
    Q_OBJECT

private:
    static QList<MidiEvent *> makeFakeList(std::initializer_list<quintptr> ids) {
        QList<MidiEvent *> out;
        for (quintptr v : ids) {
            out.append(reinterpret_cast<MidiEvent *>(v));
        }
        return out;
    }

    void resetSelection() {
        // setFile(nullptr) deletes the current singleton and creates a fresh
        // one — handy for isolating tests from each other.
        Selection::setFile(nullptr);
    }

private slots:
    void instance_returnsSingleton_thenSamePointerEachCall() {
        QVERIFY(Selection::instance() != nullptr);
        QCOMPARE(Selection::instance(), Selection::instance());
    }

    void selectedEvents_freshInstance_thenEmpty() {
        resetSelection();
        QVERIFY(Selection::instance()->selectedEvents().isEmpty());
    }

    void setSelection_replacesPriorSelection_thenContainsOnlyNew() {
        resetSelection();
        Selection::instance()->setSelection(makeFakeList({0xA1, 0xA2, 0xA3}));
        QCOMPARE(Selection::instance()->selectedEvents().size(), 3);

        QList<MidiEvent *> b = makeFakeList({0xB1, 0xB2});
        Selection::instance()->setSelection(b);
        const auto sel = Selection::instance()->selectedEvents();
        QCOMPARE(sel.size(), 2);
        QCOMPARE(sel.at(0), b.at(0));
        QCOMPARE(sel.at(1), b.at(1));
    }

    void setSelection_preservesOrder_thenSameOrderAsInput() {
        resetSelection();
        QList<MidiEvent *> input = makeFakeList({0x05, 0x01, 0x04, 0x02, 0x03});
        Selection::instance()->setSelection(input);
        const auto sel = Selection::instance()->selectedEvents();
        QCOMPARE(sel.size(), input.size());
        for (int i = 0; i < input.size(); ++i) {
            QCOMPARE(sel.at(i), input.at(i));
        }
    }

    void setSelection_withSameListTwice_thenContentsUnchanged() {
        resetSelection();
        QList<MidiEvent *> input = makeFakeList({0x10, 0x20});
        Selection::instance()->setSelection(input);
        Selection::instance()->setSelection(input);
        const auto sel = Selection::instance()->selectedEvents();
        QCOMPARE(sel.size(), 2);
        QCOMPARE(sel.at(0), input.at(0));
        QCOMPARE(sel.at(1), input.at(1));
    }

    void clearSelection_afterSetSelection_thenEmpty() {
        resetSelection();
        Selection::instance()->setSelection(makeFakeList({0xC1, 0xC2}));
        QCOMPARE(Selection::instance()->selectedEvents().size(), 2);
        Selection::instance()->clearSelection();
        QVERIFY(Selection::instance()->selectedEvents().isEmpty());
    }

    void clearSelection_onEmptySelection_thenStillEmpty() {
        resetSelection();
        Selection::instance()->clearSelection();
        QVERIFY(Selection::instance()->selectedEvents().isEmpty());
    }

    void selectedEvents_returnsByValue_callerMutationDoesNotAffectSingleton() {
        // The v1.3.2 contract: selectedEvents() returns a copy. Mutating the
        // returned list must NOT change what the singleton holds.
        resetSelection();
        QList<MidiEvent *> input = makeFakeList({0xD1, 0xD2, 0xD3});
        Selection::instance()->setSelection(input);

        QList<MidiEvent *> copy = Selection::instance()->selectedEvents();
        copy.clear();
        copy.append(reinterpret_cast<MidiEvent *>(quintptr(0xDEAD)));

        const auto sel = Selection::instance()->selectedEvents();
        QCOMPARE(sel.size(), 3);
        QCOMPARE(sel.at(0), input.at(0));
        QCOMPARE(sel.at(1), input.at(1));
        QCOMPARE(sel.at(2), input.at(2));
    }

    void setSelection_withEmptyList_thenSelectionBecomesEmpty() {
        resetSelection();
        Selection::instance()->setSelection(makeFakeList({0x01, 0x02}));
        Selection::instance()->setSelection(QList<MidiEvent *>());
        QVERIFY(Selection::instance()->selectedEvents().isEmpty());
    }

    void setSelection_largeList_takesMoveSemanticsBranch_thenAllEntriesPresent() {
        // Selection switches to std::move when the input has 100+ entries
        // (Selection.cpp:62). Verify the result is still a faithful copy of
        // the input, regardless of which branch ran.
        resetSelection();
        QList<MidiEvent *> big;
        for (int i = 0; i < 150; ++i) {
            big.append(reinterpret_cast<MidiEvent *>(quintptr(0x1000 + i)));
        }
        QList<MidiEvent *> expected = big; // snapshot before move
        Selection::instance()->setSelection(big);
        const auto sel = Selection::instance()->selectedEvents();
        QCOMPARE(sel.size(), expected.size());
        for (int i = 0; i < expected.size(); ++i) {
            QCOMPARE(sel.at(i), expected.at(i));
        }
    }

    void setFile_replacesSingleton_thenSelectionIsEmptyAfterRebuild() {
        resetSelection();
        Selection::instance()->setSelection(makeFakeList({0x77, 0x88}));
        QCOMPARE(Selection::instance()->selectedEvents().size(), 2);
        // setFile() destroys the old instance and creates a fresh one.
        Selection::setFile(nullptr);
        QVERIFY(Selection::instance() != nullptr);
        QVERIFY(Selection::instance()->selectedEvents().isEmpty());
    }
};

QTEST_APPLESS_MAIN(TestSelection)
#include "test_selection.moc"
