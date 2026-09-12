/*
 * MidiEditor AI - SharedClipboard cross-process lock tests.
 *
 * Covers the bounded lock that guards the shared-memory clipboard:
 *   - a lock held by another holder makes the clipboard operation return
 *     false within lockTimeoutMs() instead of blocking the caller (the
 *     "clipboard busy" path; callers then use the in-process clipboard)
 *   - the lock is handed back afterwards, so the next operation runs
 *     without waiting
 *   - a lock left behind by a process that died while holding it is
 *     recovered: the next clipboard operation takes the lock right away
 *     instead of waiting out the timeout or hanging
 *
 * The test binary doubles as its own lock-holding helper: started with
 * "--hold-lock <path>" it takes the lock, prints "held" and sleeps until
 * it is killed. That is the closest reproduction of an editor crashing
 * mid-copy that a unit test can stage.
 *
 * Compiles the REAL SharedClipboard.cpp on top of the midi core stack with
 * the GUI periphery ODR-shimmed - same harness as test_midi_measure.
 */

#include <QtTest/QtTest>
#include <QColor>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QList>
#include <QLockFile>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QThread>

#include <cstdio>
#include <cstring>

#include "../src/tool/SharedClipboard.h"

// ---- ODR shims: Appearance colors (statics used by midi core / events) ---
#include "../src/gui/Appearance.h"
QColor Appearance::borderColor() { return QColor(); }
QColor *Appearance::channelColor(int) {
    static QColor c(128, 128, 128);
    return &c;
}
QColor *Appearance::trackColor(int) {
    static QColor c(128, 128, 128);
    return &c;
}

// ---- ODR shims: EventWidget ----------------------------------------------
#include "../src/gui/EventWidget.h"
void EventWidget::setEvents(QList<MidiEvent *>) {}
void EventWidget::reload() {}
QList<MidiEvent *> EventWidget::events() { return {}; }

namespace {

// Slack for a loaded CI box: the bounded wait may overshoot its deadline by
// the scheduler's granularity, never by seconds.
const int kOvershootMs = 3000;

// Longest we give the helper process to report that it holds the lock.
const int kHelperStartMs = 15000;

} // namespace

class TestSharedClipboardLock : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void lockPolicyIsBounded();
    void busyLockFailsSoftlyWithinTimeout();
    void staleLockFromDeadHolderIsRecovered();
};

void TestSharedClipboardLock::initTestCase() {
    QVERIFY(SharedClipboard::instance()->initialize());
}

void TestSharedClipboardLock::cleanupTestCase() {
    SharedClipboard::instance()->cleanup();
}

void TestSharedClipboardLock::lockPolicyIsBounded() {
    QVERIFY(!SharedClipboard::lockFilePath().isEmpty());
    QVERIFY(SharedClipboard::lockFilePath().endsWith(QStringLiteral(".lock")));
    // A blocked GUI thread must come back in "a moment", not "eventually".
    QVERIFY(SharedClipboard::lockTimeoutMs() > 0);
    QVERIFY(SharedClipboard::lockTimeoutMs() <= 5000);
}

void TestSharedClipboardLock::busyLockFailsSoftlyWithinTimeout() {
    SharedClipboard *clipboard = SharedClipboard::instance();
    const QString path = SharedClipboard::lockFilePath();
    const int timeout = SharedClipboard::lockTimeoutMs();

    // Another holder (a second editor mid-copy) owns the lock.
    QLockFile holder(path);
    QVERIFY2(holder.tryLock(0), "test holder could not take the clipboard lock");

    QElapsedTimer timer;
    timer.start();
    const bool hasData = clipboard->hasData();
    const qint64 elapsed = timer.elapsed();

    QVERIFY2(!hasData, "a busy lock must make the clipboard report no data, not block");
    QVERIFY2(elapsed >= timeout - 100,
             qPrintable(QStringLiteral("returned after %1 ms, before the %2 ms wait was up")
                            .arg(elapsed).arg(timeout)));
    QVERIFY2(elapsed < timeout + kOvershootMs,
             qPrintable(QStringLiteral("returned after %1 ms, well past the %2 ms deadline")
                            .arg(elapsed).arg(timeout)));

    // The same bound holds for the cross-process probe used by paste.
    timer.restart();
    QVERIFY(!clipboard->hasDataFromDifferentProcess());
    QVERIFY(timer.elapsed() < timeout + kOvershootMs);

    holder.unlock();

    // Once the holder is gone the operation completes without waiting and
    // hands the lock back (a fresh holder can take it immediately).
    timer.restart();
    clipboard->hasData();
    QVERIFY2(timer.elapsed() < timeout,
             "clipboard still waited for the timeout after the lock was released");
    QLockFile probe(path);
    QVERIFY2(probe.tryLock(0), "clipboard left its lock behind after a successful operation");
    probe.unlock();
}

void TestSharedClipboardLock::staleLockFromDeadHolderIsRecovered() {
    SharedClipboard *clipboard = SharedClipboard::instance();
    const QString path = SharedClipboard::lockFilePath();
    const int timeout = SharedClipboard::lockTimeoutMs();

    // Stage the crash: a separate process takes the lock and is killed
    // while holding it, leaving its lock file behind.
    QProcess holder;
    holder.setProgram(QCoreApplication::applicationFilePath());
    holder.setArguments({QStringLiteral("--hold-lock"), path});
    holder.start();
    QVERIFY2(holder.waitForStarted(kHelperStartMs), "lock-holding helper did not start");

    QByteArray output;
    QElapsedTimer wait;
    wait.start();
    while (!output.contains("held") && wait.elapsed() < kHelperStartMs
           && holder.state() == QProcess::Running) {
        holder.waitForReadyRead(200);
        output += holder.readAllStandardOutput();
    }
    QVERIFY2(output.contains("held"), "lock-holding helper never reported the lock");
    QVERIFY(QFile::exists(path));

    holder.kill();
    QVERIFY(holder.waitForFinished(kHelperStartMs));
    QVERIFY2(QFile::exists(path), "killed helper did not leave a stale lock behind");

    // The next clipboard operation must recover the stale lock right away:
    // neither hang (the old semaphore behaviour) nor wait out the timeout.
    QElapsedTimer timer;
    timer.start();
    clipboard->hasData(); // result depends on the segment content, not asserted
    const qint64 elapsed = timer.elapsed();
    QVERIFY2(elapsed < timeout,
             qPrintable(QStringLiteral("stale lock was waited out (%1 ms) instead of recovered")
                            .arg(elapsed)));

    // Recovery means the lock was taken and released by this process: the
    // dead holder's file is gone and a fresh holder can take it immediately.
    QVERIFY2(!QFile::exists(path), "stale lock file survived a clipboard operation");
    QLockFile probe(path);
    QVERIFY(probe.tryLock(0));
    probe.unlock();
}

// Helper mode: hold the lock at argv[2] until killed. Used by
// staleLockFromDeadHolderIsRecovered() to stage a crashed holder.
static int holdLockUntilKilled(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QLockFile lock(QString::fromLocal8Bit(argv[2]));
    if (!lock.tryLock(0)) {
        return 2;
    }
    std::printf("held\n");
    std::fflush(stdout);
    // Bounded so a helper orphaned by a crashed test run goes away on its own.
    for (int i = 0; i < 60; ++i) {
        QThread::sleep(1);
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc >= 3 && std::strcmp(argv[1], "--hold-lock") == 0) {
        return holdLockUntilKilled(argc, argv);
    }
    QCoreApplication app(argc, argv);
    TestSharedClipboardLock tc;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&tc, argc, argv);
}

#include "test_shared_clipboard_lock.moc"
