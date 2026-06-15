/*
 * test_document_manager
 *
 * Unit tests for src/gui/DocumentManager - the Phase 28 (multi-document tabs)
 * bookkeeping that tracks open documents + the active index. DocumentManager
 * is pure pointer/list logic and never dereferences the MidiFile, so the tests
 * use sentinel MidiFile* values cast from quintptr. The trickiest contract is
 * removeAt()'s active-index adjustment, which most slots target.
 */

#include <QtTest/QtTest>
#include <QObject>

#include "../src/gui/DocumentManager.h"

class TestDocumentManager : public QObject {
    Q_OBJECT

private:
    static MidiFile *fakeFile(quintptr id) {
        return reinterpret_cast<MidiFile *>(id);
    }

private slots:
    void empty_thenNoActiveAndZeroCount() {
        DocumentManager m;
        QCOMPARE(m.count(), 0);
        QVERIFY(m.isEmpty());
        QCOMPARE(m.activeIndex(), -1);
        QVERIFY(m.active() == nullptr);
    }

    void open_firstDocument_thenBecomesActive() {
        DocumentManager m;
        Document *d = m.open(fakeFile(0x1), "A");
        QCOMPARE(m.count(), 1);
        QCOMPARE(m.activeIndex(), 0);
        QCOMPARE(m.active(), d);
        QCOMPARE(d->title(), QString("A"));
        QCOMPARE(d->file(), fakeFile(0x1));
    }

    void open_secondDocument_thenActiveStaysOnFirst() {
        DocumentManager m;
        Document *a = m.open(fakeFile(0x1));
        m.open(fakeFile(0x2));
        QCOMPARE(m.count(), 2);
        QCOMPARE(m.active(), a); // open() does not steal focus
    }

    void openAndActivate_thenNewDocIsActive() {
        DocumentManager m;
        m.open(fakeFile(0x1));
        Document *b = m.openAndActivate(fakeFile(0x2));
        QCOMPARE(m.activeIndex(), 1);
        QCOMPARE(m.active(), b);
    }

    void setActiveIndexAndSetActive_thenActiveFollows() {
        DocumentManager m;
        Document *a = m.open(fakeFile(0x1));
        Document *b = m.open(fakeFile(0x2));
        m.setActiveIndex(1);
        QCOMPARE(m.active(), b);
        m.setActive(a);
        QCOMPARE(m.active(), a);
        // Out-of-range / unmanaged are no-ops.
        m.setActiveIndex(99);
        QCOMPARE(m.active(), a);
    }

    void lookups_indexOfAndAt_thenConsistent() {
        DocumentManager m;
        Document *a = m.open(fakeFile(0xAA));
        Document *b = m.open(fakeFile(0xBB));
        QCOMPARE(m.indexOf(a), 0);
        QCOMPARE(m.indexOf(b), 1);
        QCOMPARE(m.at(1), b);
        QCOMPARE(m.indexOfFile(fakeFile(0xBB)), 1);
        QCOMPARE(m.indexOfFile(fakeFile(0xCC)), -1);
        QVERIFY(m.at(99) == nullptr);
    }

    void removeAt_beforeActive_thenActiveShiftsLeft() {
        DocumentManager m;
        m.open(fakeFile(0x1));
        m.open(fakeFile(0x2));
        Document *c = m.open(fakeFile(0x3));
        m.setActive(c); // active index 2
        MidiFile *removed = m.removeAt(0);
        QCOMPARE(removed, fakeFile(0x1));
        QCOMPARE(m.count(), 2);
        QCOMPARE(m.active(), c); // still c, now at index 1
        QCOMPARE(m.activeIndex(), 1);
    }

    void removeAt_afterActive_thenActiveUnchanged() {
        DocumentManager m;
        Document *a = m.open(fakeFile(0x1));
        m.open(fakeFile(0x2));
        m.open(fakeFile(0x3));
        m.setActive(a); // active index 0
        m.removeAt(2);
        QCOMPARE(m.activeIndex(), 0);
        QCOMPARE(m.active(), a);
    }

    void removeAt_activeMiddle_thenClampsToNeighbour() {
        DocumentManager m;
        m.open(fakeFile(0x1));
        Document *b = m.open(fakeFile(0x2));
        Document *c = m.open(fakeFile(0x3));
        m.setActive(b); // active index 1
        m.removeAt(1);  // remove the active one
        QCOMPARE(m.count(), 2);
        // index 1 removed; clamp keeps index 1 (now == c).
        QCOMPARE(m.activeIndex(), 1);
        QCOMPARE(m.active(), c);
    }

    void removeAt_lastRemaining_thenNoActive() {
        DocumentManager m;
        m.open(fakeFile(0x1));
        MidiFile *removed = m.closeActive();
        QCOMPARE(removed, fakeFile(0x1));
        QCOMPARE(m.count(), 0);
        QCOMPARE(m.activeIndex(), -1);
        QVERIFY(m.active() == nullptr);
    }

    void removeAt_activeIsLastIndex_thenClampsDown() {
        DocumentManager m;
        Document *a = m.open(fakeFile(0x1));
        m.open(fakeFile(0x2));
        m.setActiveIndex(1); // active is last
        m.removeAt(1);
        QCOMPARE(m.activeIndex(), 0);
        QCOMPARE(m.active(), a);
    }

    void removeAt_outOfRange_thenNullAndUnchanged() {
        DocumentManager m;
        m.open(fakeFile(0x1));
        QVERIFY(m.removeAt(5) == nullptr);
        QCOMPARE(m.count(), 1);
    }

    // ----- insert() (Phase 28 editor groups: drop a tab at a position) -------

    void insert_intoEmpty_thenBecomesActive() {
        DocumentManager m;
        Document *d = m.insert(0, fakeFile(0x1), "A");
        QCOMPARE(m.count(), 1);
        QCOMPARE(m.active(), d);
        QCOMPARE(m.activeIndex(), 0);
    }

    void insert_atFront_beforeActive_thenActiveShiftsRight() {
        DocumentManager m;
        Document *a = m.open(fakeFile(0x1));
        Document *b = m.open(fakeFile(0x2));
        m.setActive(b); // active index 1
        Document *n = m.insert(0, fakeFile(0x9), "N");
        QCOMPARE(m.count(), 3);
        QCOMPARE(m.at(0), n);
        QCOMPARE(m.at(1), a);
        QCOMPARE(m.at(2), b);
        // The inserted doc does NOT steal active; b stays active at its new index.
        QCOMPARE(m.active(), b);
        QCOMPARE(m.activeIndex(), 2);
    }

    void insert_afterActive_thenActiveIndexUnchanged() {
        DocumentManager m;
        Document *a = m.open(fakeFile(0x1));
        m.open(fakeFile(0x2));
        m.setActive(a); // active index 0
        m.insert(2, fakeFile(0x9));
        QCOMPARE(m.active(), a);
        QCOMPARE(m.activeIndex(), 0);
    }

    void insert_indexClampedToCount() {
        DocumentManager m;
        m.open(fakeFile(0x1));
        Document *n = m.insert(99, fakeFile(0x2)); // clamps to append
        QCOMPARE(m.indexOf(n), 1);
    }

    // ----- move() (Phase 28 editor groups: reorder tabs within a group) ------

    void move_keepsSameDocumentActive() {
        DocumentManager m;
        Document *a = m.open(fakeFile(0x1));
        Document *b = m.open(fakeFile(0x2));
        Document *c = m.open(fakeFile(0x3));
        m.setActive(a); // active index 0
        m.move(0, 2);   // a goes to the end
        QCOMPARE(m.at(0), b);
        QCOMPARE(m.at(1), c);
        QCOMPARE(m.at(2), a);
        QCOMPARE(m.active(), a); // still a, now at index 2
        QCOMPARE(m.activeIndex(), 2);
    }

    void move_nonActive_thenActiveIndexFollows() {
        DocumentManager m;
        Document *a = m.open(fakeFile(0x1));
        Document *b = m.open(fakeFile(0x2));
        Document *c = m.open(fakeFile(0x3));
        m.setActive(c); // active index 2
        m.move(0, 1);   // a and b swap; c untouched but shifts? no - c stays last
        QCOMPARE(m.at(0), b);
        QCOMPARE(m.at(1), a);
        QCOMPARE(m.at(2), c);
        QCOMPARE(m.active(), c);
        QCOMPARE(m.activeIndex(), 2);
    }

    void move_outOfRangeOrNoop_thenUnchanged() {
        DocumentManager m;
        Document *a = m.open(fakeFile(0x1));
        Document *b = m.open(fakeFile(0x2));
        m.move(0, 0);   // no-op
        m.move(5, 0);   // out of range
        m.move(0, 9);   // out of range
        QCOMPARE(m.at(0), a);
        QCOMPARE(m.at(1), b);
    }

    // ----- gapToMoveIndex() (drag-drop reorder: insertion gap -> move index) --

    void gapToMoveIndex_dropInOwnGapIsNoOp() {
        // Count 3, dragging tab `from`: a gap immediately around `from` must not
        // move it (to == from).
        QCOMPARE(DocumentManager::gapToMoveIndex(0, 0, 3), 0);
        QCOMPARE(DocumentManager::gapToMoveIndex(0, 1, 3), 0); // gap right after itself
        QCOMPARE(DocumentManager::gapToMoveIndex(1, 1, 3), 1);
        QCOMPARE(DocumentManager::gapToMoveIndex(1, 2, 3), 1); // gap right after itself
        QCOMPARE(DocumentManager::gapToMoveIndex(2, 2, 3), 2);
        QCOMPARE(DocumentManager::gapToMoveIndex(2, 3, 3), 2); // gap at the very end
    }

    void gapToMoveIndex_dragRightShiftsLeftByOne() {
        // Dragging tab 0 to a gap on its right: gap>from -> to = gap-1.
        QCOMPARE(DocumentManager::gapToMoveIndex(0, 2, 3), 1);
        QCOMPARE(DocumentManager::gapToMoveIndex(0, 3, 3), 2); // to the end
        QCOMPARE(DocumentManager::gapToMoveIndex(1, 3, 3), 2);
    }

    void gapToMoveIndex_dragLeftKeepsGap() {
        // Dragging a later tab to a gap on its left: gap<=from -> to = gap.
        QCOMPARE(DocumentManager::gapToMoveIndex(2, 0, 3), 0);
        QCOMPARE(DocumentManager::gapToMoveIndex(2, 1, 3), 1);
        QCOMPARE(DocumentManager::gapToMoveIndex(1, 0, 3), 0);
    }

    void gapToMoveIndex_clampsAndHandlesEdgeCounts() {
        QCOMPARE(DocumentManager::gapToMoveIndex(0, -1, 3), 2); // negative gap -> append
        QCOMPARE(DocumentManager::gapToMoveIndex(0, 99, 3), 2); // huge gap -> append
        QCOMPARE(DocumentManager::gapToMoveIndex(0, 0, 0), 0);  // empty
        QCOMPARE(DocumentManager::gapToMoveIndex(0, 5, 1), 0);  // single tab
    }

    void gapToMoveIndex_thenMoveProducesExpectedOrder() {
        // Integration: feed the gap result into move() and check the order.
        DocumentManager m;
        Document *a = m.open(fakeFile(0xA));
        Document *b = m.open(fakeFile(0xB));
        Document *c = m.open(fakeFile(0xC)); // [a,b,c]
        // Drag a (index 0) to the very end (gap 3).
        m.move(0, DocumentManager::gapToMoveIndex(0, 3, m.count()));
        QCOMPARE(m.at(0), b);
        QCOMPARE(m.at(1), c);
        QCOMPARE(m.at(2), a);
        // Drag it back to the front (gap 0).
        m.move(2, DocumentManager::gapToMoveIndex(2, 0, m.count()));
        QCOMPARE(m.at(0), a);
        QCOMPARE(m.at(1), b);
        QCOMPARE(m.at(2), c);
    }
};

QTEST_APPLESS_MAIN(TestDocumentManager)
#include "test_document_manager.moc"
