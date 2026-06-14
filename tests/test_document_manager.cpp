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
};

QTEST_APPLESS_MAIN(TestDocumentManager)
#include "test_document_manager.moc"
