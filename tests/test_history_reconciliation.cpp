// Unit tests for HistoryReconciliation — pure logic helpers for the
// returning-peer merge flow (Plan §11.10b, Phase 9.5g).
//
// Pure QJsonArray + QStringList — no MidiFile dep, no QSettings, no
// network. Links HistoryReconciliation.cpp + PrBundle.cpp because
// synthesizeBundle returns PrBundle by value.
//
// Run from a build configured with -DBUILD_TESTING=ON:
//     ctest --test-dir build -V -R HistoryReconciliation

#include "HistoryReconciliation.h"
#include "PrBundle.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QtTest/QtTest>

class TestHistoryReconciliation : public QObject {
    Q_OBJECT

private slots:
    // findMergeBase
    void findMergeBase_emptyInputsReturnEmpty();
    void findMergeBase_noOverlapReturnsEmpty();
    void findMergeBase_singleCommonReturnsIt();
    void findMergeBase_multipleCommonReturnsFirstFromLocal();
    void findMergeBase_skipsEmptyHashesInLocal();

    // tailHashes
    void tailHashes_emptyHistoryReturnsEmpty();
    void tailHashes_returnsNewestFirst();
    void tailHashes_capsAtMaxEntries();
    void tailHashes_skipsBlankHashEntries();
    void tailHashes_zeroOrNegativeMaxReturnsEmpty();

    // commitsSinceFork
    void commitsSinceFork_emptyHistoryReturnsEmpty();
    void commitsSinceFork_emptyAncestorReturnsWhole();
    void commitsSinceFork_ancestorAtTipReturnsEmptySlice();
    void commitsSinceFork_ancestorInMiddleReturnsTailOnly();
    void commitsSinceFork_ancestorNotFoundReturnsWhole();

    // synthesizeBundle
    void synthesizeBundle_emptySliceIsInvalid();
    void synthesizeBundle_singleCommitUsesTipMessage();
    void synthesizeBundle_multiCommitSummarisesCount();
    void synthesizeBundle_concatenatesHunksChronologically();

    // classify
    void classify_sameHeadShortCircuits();
    void classify_localBehindRemote();
    void classify_remoteBehindLocal();
    void classify_diverged();
    void classify_unrelated();

private:
    static QJsonObject commit(const QString &hash,
                              const QString &message = QString(),
                              const QString &author = QStringLiteral("Alice"),
                              qint64 ts = 1700000000) {
        QJsonObject o;
        o.insert(QStringLiteral("hash"), hash);
        o.insert(QStringLiteral("message"), message);
        o.insert(QStringLiteral("author"), author);
        o.insert(QStringLiteral("ts"), static_cast<double>(ts));
        o.insert(QStringLiteral("hunks"), QJsonArray());
        return o;
    }

    static QJsonObject commitWithHunks(const QString &hash,
                                       const QJsonArray &hunks,
                                       const QString &message = QString()) {
        QJsonObject o = commit(hash, message);
        o.insert(QStringLiteral("hunks"), hunks);
        return o;
    }

    static QJsonObject hunk(const QString &tag) {
        QJsonObject h;
        QJsonObject scope;
        scope.insert(QStringLiteral("tag"), tag);
        h.insert(QStringLiteral("scope"), scope);
        return h;
    }
};

// ---------------------------------------------------------------------
// findMergeBase
// ---------------------------------------------------------------------

void TestHistoryReconciliation::findMergeBase_emptyInputsReturnEmpty() {
    QCOMPARE(HistoryReconciliation::findMergeBase({}, {}), QString());
    QCOMPARE(HistoryReconciliation::findMergeBase({QStringLiteral("a")}, {}), QString());
    QCOMPARE(HistoryReconciliation::findMergeBase({}, {QStringLiteral("a")}), QString());
}

void TestHistoryReconciliation::findMergeBase_noOverlapReturnsEmpty() {
    QStringList local  = {QStringLiteral("L3"), QStringLiteral("L2"), QStringLiteral("L1")};
    QStringList remote = {QStringLiteral("R3"), QStringLiteral("R2"), QStringLiteral("R1")};
    QCOMPARE(HistoryReconciliation::findMergeBase(local, remote), QString());
}

void TestHistoryReconciliation::findMergeBase_singleCommonReturnsIt() {
    QStringList local  = {QStringLiteral("L2"), QStringLiteral("L1"), QStringLiteral("ROOT")};
    QStringList remote = {QStringLiteral("R2"), QStringLiteral("R1"), QStringLiteral("ROOT")};
    QCOMPARE(HistoryReconciliation::findMergeBase(local, remote),
             QStringLiteral("ROOT"));
}

// Local is the iteration driver - first local hash present in remote wins.
void TestHistoryReconciliation::findMergeBase_multipleCommonReturnsFirstFromLocal() {
    QStringList local  = {QStringLiteral("L3"), QStringLiteral("X"), QStringLiteral("Y")};
    QStringList remote = {QStringLiteral("Y"), QStringLiteral("X"), QStringLiteral("R0")};
    // X and Y are both common; first from local (after L3) is X.
    QCOMPARE(HistoryReconciliation::findMergeBase(local, remote),
             QStringLiteral("X"));
}

void TestHistoryReconciliation::findMergeBase_skipsEmptyHashesInLocal() {
    QStringList local  = {QString(), QStringLiteral("X"), QStringLiteral("Y")};
    QStringList remote = {QStringLiteral("Y"), QStringLiteral("X")};
    QCOMPARE(HistoryReconciliation::findMergeBase(local, remote),
             QStringLiteral("X"));
}

// ---------------------------------------------------------------------
// tailHashes
// ---------------------------------------------------------------------

void TestHistoryReconciliation::tailHashes_emptyHistoryReturnsEmpty() {
    QCOMPARE(HistoryReconciliation::tailHashes(QJsonArray(), 50),
             QStringList());
}

void TestHistoryReconciliation::tailHashes_returnsNewestFirst() {
    // history[] is oldest-first per the sidecar convention.
    QJsonArray h;
    h.append(commit(QStringLiteral("c1")));
    h.append(commit(QStringLiteral("c2")));
    h.append(commit(QStringLiteral("c3")));
    QStringList tail = HistoryReconciliation::tailHashes(h, 50);
    QCOMPARE(tail, QStringList({QStringLiteral("c3"),
                                 QStringLiteral("c2"),
                                 QStringLiteral("c1")}));
}

void TestHistoryReconciliation::tailHashes_capsAtMaxEntries() {
    QJsonArray h;
    for (int i = 0; i < 100; ++i) {
        h.append(commit(QStringLiteral("c%1").arg(i)));
    }
    QStringList tail = HistoryReconciliation::tailHashes(h, 10);
    QCOMPARE(tail.size(), 10);
    // Newest 10 of 100, in newest-first order.
    QCOMPARE(tail.first(), QStringLiteral("c99"));
    QCOMPARE(tail.last(),  QStringLiteral("c90"));
}

void TestHistoryReconciliation::tailHashes_skipsBlankHashEntries() {
    QJsonArray h;
    h.append(commit(QStringLiteral("c1")));
    h.append(commit(QString()));
    h.append(commit(QStringLiteral("c3")));
    QStringList tail = HistoryReconciliation::tailHashes(h, 50);
    QCOMPARE(tail, QStringList({QStringLiteral("c3"), QStringLiteral("c1")}));
}

void TestHistoryReconciliation::tailHashes_zeroOrNegativeMaxReturnsEmpty() {
    QJsonArray h;
    h.append(commit(QStringLiteral("c1")));
    QCOMPARE(HistoryReconciliation::tailHashes(h, 0).size(), 0);
    QCOMPARE(HistoryReconciliation::tailHashes(h, -5).size(), 0);
}

// ---------------------------------------------------------------------
// commitsSinceFork
// ---------------------------------------------------------------------

void TestHistoryReconciliation::commitsSinceFork_emptyHistoryReturnsEmpty() {
    QCOMPARE(HistoryReconciliation::commitsSinceFork(
                     QJsonArray(), QStringLiteral("anything")).size(), 0);
}

void TestHistoryReconciliation::commitsSinceFork_emptyAncestorReturnsWhole() {
    QJsonArray h;
    h.append(commit(QStringLiteral("c1")));
    h.append(commit(QStringLiteral("c2")));
    QJsonArray slice = HistoryReconciliation::commitsSinceFork(h, QString());
    QCOMPARE(slice.size(), 2);
}

void TestHistoryReconciliation::commitsSinceFork_ancestorAtTipReturnsEmptySlice() {
    QJsonArray h;
    h.append(commit(QStringLiteral("c1")));
    h.append(commit(QStringLiteral("c2")));
    QJsonArray slice = HistoryReconciliation::commitsSinceFork(h,
                                                                QStringLiteral("c2"));
    QCOMPARE(slice.size(), 0);
}

void TestHistoryReconciliation::commitsSinceFork_ancestorInMiddleReturnsTailOnly() {
    QJsonArray h;
    h.append(commit(QStringLiteral("c1")));
    h.append(commit(QStringLiteral("c2")));
    h.append(commit(QStringLiteral("c3")));
    h.append(commit(QStringLiteral("c4")));
    QJsonArray slice = HistoryReconciliation::commitsSinceFork(h,
                                                                QStringLiteral("c2"));
    QCOMPARE(slice.size(), 2);
    QCOMPARE(slice.at(0).toObject().value(QStringLiteral("hash")).toString(),
             QStringLiteral("c3"));
    QCOMPARE(slice.at(1).toObject().value(QStringLiteral("hash")).toString(),
             QStringLiteral("c4"));
}

// Documented fall-through: if ancestor not in history, return the entire
// history so the caller can treat it as "unrelated, full reload required".
void TestHistoryReconciliation::commitsSinceFork_ancestorNotFoundReturnsWhole() {
    QJsonArray h;
    h.append(commit(QStringLiteral("c1")));
    h.append(commit(QStringLiteral("c2")));
    QJsonArray slice = HistoryReconciliation::commitsSinceFork(h,
                                                                QStringLiteral("UNKNOWN"));
    QCOMPARE(slice.size(), 2);
}

// ---------------------------------------------------------------------
// synthesizeBundle
// ---------------------------------------------------------------------

void TestHistoryReconciliation::synthesizeBundle_emptySliceIsInvalid() {
    PrBundle b = HistoryReconciliation::synthesizeBundle(
            QJsonArray(),
            QStringLiteral("session-id"),
            QStringLiteral("ancestor"));
    QVERIFY(!b.isValid());
}

void TestHistoryReconciliation::synthesizeBundle_singleCommitUsesTipMessage() {
    QJsonArray slice;
    slice.append(commit(QStringLiteral("tip"),
                        QStringLiteral("added bridge"),
                        QStringLiteral("Alice"),
                        12345));
    PrBundle b = HistoryReconciliation::synthesizeBundle(
            slice,
            QStringLiteral("sess"),
            QStringLiteral("anc"));
    QCOMPARE(b.message,    QStringLiteral("added bridge"));
    QCOMPARE(b.author,     QStringLiteral("Alice"));
    QCOMPARE(b.parentHash, QStringLiteral("anc"));
    QCOMPARE(b.sessionId,  QStringLiteral("sess"));
    QCOMPARE(b.timestamp,  qint64{12345});
}

void TestHistoryReconciliation::synthesizeBundle_multiCommitSummarisesCount() {
    QJsonArray slice;
    slice.append(commit(QStringLiteral("c1"), QStringLiteral("first")));
    slice.append(commit(QStringLiteral("c2"), QStringLiteral("second")));
    slice.append(commit(QStringLiteral("c3"), QStringLiteral("third")));
    PrBundle b = HistoryReconciliation::synthesizeBundle(
            slice,
            QStringLiteral("sess"),
            QStringLiteral("abcdef0123456789"));
    QVERIFY(b.message.contains(QStringLiteral("3 commits")));
    QVERIFY(b.message.contains(QStringLiteral("third"))); // tip message reflected
    QVERIFY(b.message.contains(QStringLiteral("abcdef01"))); // ancestor short hash
}

void TestHistoryReconciliation::synthesizeBundle_concatenatesHunksChronologically() {
    QJsonArray slice;
    slice.append(commitWithHunks(QStringLiteral("c1"),
                                 QJsonArray({hunk(QStringLiteral("A")),
                                             hunk(QStringLiteral("B"))})));
    slice.append(commitWithHunks(QStringLiteral("c2"),
                                 QJsonArray({hunk(QStringLiteral("C"))})));
    PrBundle b = HistoryReconciliation::synthesizeBundle(
            slice,
            QStringLiteral("sess"),
            QStringLiteral("anc"));
    QCOMPARE(b.hunks.size(), 3);
    auto tagAt = [&b](int i) {
        return b.hunks.at(i).toObject()
                .value(QStringLiteral("scope")).toObject()
                .value(QStringLiteral("tag")).toString();
    };
    QCOMPARE(tagAt(0), QStringLiteral("A"));
    QCOMPARE(tagAt(1), QStringLiteral("B"));
    QCOMPARE(tagAt(2), QStringLiteral("C"));
}

// ---------------------------------------------------------------------
// classify
// ---------------------------------------------------------------------

void TestHistoryReconciliation::classify_sameHeadShortCircuits() {
    QStringList tail = {QStringLiteral("X")};
    HistoryReconciliation::Relation r = HistoryReconciliation::classify(
            QStringLiteral("X"), QStringLiteral("X"), tail, tail);
    QCOMPARE(r, HistoryReconciliation::Relation::SameHead);
}

// Local head appears strictly inside remote's tail -> remote is ahead.
void TestHistoryReconciliation::classify_localBehindRemote() {
    QStringList localTail  = {QStringLiteral("L"), QStringLiteral("ROOT")};
    QStringList remoteTail = {QStringLiteral("R2"), QStringLiteral("R1"),
                              QStringLiteral("L"), QStringLiteral("ROOT")};
    HistoryReconciliation::Relation r = HistoryReconciliation::classify(
            QStringLiteral("L"), QStringLiteral("R2"), localTail, remoteTail);
    QCOMPARE(r, HistoryReconciliation::Relation::LocalBehindRemote);
}

void TestHistoryReconciliation::classify_remoteBehindLocal() {
    QStringList localTail  = {QStringLiteral("L2"), QStringLiteral("L1"),
                              QStringLiteral("R"), QStringLiteral("ROOT")};
    QStringList remoteTail = {QStringLiteral("R"), QStringLiteral("ROOT")};
    HistoryReconciliation::Relation r = HistoryReconciliation::classify(
            QStringLiteral("L2"), QStringLiteral("R"), localTail, remoteTail);
    QCOMPARE(r, HistoryReconciliation::Relation::RemoteBehindLocal);
}

void TestHistoryReconciliation::classify_diverged() {
    QStringList localTail  = {QStringLiteral("L"), QStringLiteral("ROOT")};
    QStringList remoteTail = {QStringLiteral("R"), QStringLiteral("ROOT")};
    HistoryReconciliation::Relation r = HistoryReconciliation::classify(
            QStringLiteral("L"), QStringLiteral("R"), localTail, remoteTail);
    QCOMPARE(r, HistoryReconciliation::Relation::Diverged);
}

void TestHistoryReconciliation::classify_unrelated() {
    QStringList localTail  = {QStringLiteral("L1"), QStringLiteral("L0")};
    QStringList remoteTail = {QStringLiteral("R1"), QStringLiteral("R0")};
    HistoryReconciliation::Relation r = HistoryReconciliation::classify(
            QStringLiteral("L1"), QStringLiteral("R1"), localTail, remoteTail);
    QCOMPARE(r, HistoryReconciliation::Relation::Unrelated);
}

QTEST_APPLESS_MAIN(TestHistoryReconciliation)
#include "test_history_reconciliation.moc"
