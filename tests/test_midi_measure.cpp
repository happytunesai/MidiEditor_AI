/*
 * test_midi_measure
 *
 * Pins the bar-numbering contract of MidiFile::measure() and
 * MidiFile::startTickOfMeasure(). Both are 1-BASED - tick 0 lies in measure 1 -
 * and that is the number the status bar, the time display and the measure tool
 * put in front of the user.
 *
 * This suite exists because the header used to document the return value as
 * "0-based". Every caller that believed the comment added 1 and reported bars
 * one too high; the AI context (EditorContext) shipped that way and told the
 * model the song had one more bar than it has. The convention is cheap to
 * assert and impossible to notice by reading, so it gets nailed down here.
 *
 *   1. Tick 0 is measure 1 (NOT 0) and every tick maps to a positive number.
 *   2. The out-params bracket the queried tick and span exactly one measure.
 *   3. Measure boundaries advance the number by exactly one.
 *   4. startTickOfMeasure() is 1-based too: measure 1 starts at tick 0.
 *   5. measure()/startTickOfMeasure() round-trip for every measure.
 *   6. The totalMeasures contract: measure(endTick()) is the number of bars,
 *      with no +1 - the exact expression EditorContext reports to the AI.
 *   7. meterAt() reports the denominator as the SMF power-of-two EXPONENT (2
 *      means /4) - the same class of trap, and the reason the toolbar's bar
 *      display showed "4/2" and counted half-note beats.
 *   8. A mid-song meter change keeps the numbering continuous and switches to
 *      the new measure length (guards the accumulate loop).
 *   9. measureCount() is the bar count of the SONG, so it does not gain a
 *      phantom bar when the file ends exactly on a bar line (a fresh file is
 *      7680 ticks = exactly 10 bars, and measure(endTick()) says 11).
 *  10. deleteMeasures() leaves the file with a meter: exactly one
 *      TimeSignatureEvent survives at tick 0 with the meter of the first
 *      undeleted bar, and the readers still work afterwards. The re-anchor may
 *      not be decided by comparing num/denom against meterAt()'s no-event
 *      fallback, because that fallback IS a valid 4/4.
 *  11. Nothing in this family dereferences a null TimeSignatureEvent: with
 *      channel 18 emptied, measure() (both overloads), startTickOfMeasure(),
 *      measureCount(), insertMeasures() and deleteMeasures() fall back to 4/4
 *      instead of crashing. The measure tool asks measure() on every paint, so
 *      a null here is an access violation on the next mouse move.
 *  12. deleteMeasures() removes the HALF-OPEN range [tickFrom, tickTo): events
 *      starting exactly on the downbeat of the first KEPT bar survive and shift
 *      left, notes starting inside the range go away with their off event, and
 *      notes spanning into the range are shortened to the splice point.
 *  13. The tempo cache survives being used from two threads at once.
 *      PlayerThread calls msOfTick()/tick(ms) while the GUI thread edits, and
 *      the cache turned those into calls that may REBUILD a std::vector. Three
 *      angles: a worker hammering the conversions while the GUI thread forces
 *      rebuild after rebuild, a worker reading the answers a GUI-thread
 *      channel-17 edit produced, and calcMaxTime() emitting recalcWidgetSize()
 *      with the cache lock released (a repaint asks the file for timings, and
 *      the lock is not recursive).
 *
 * NOT testable here: measure() dereferences BOTH out-params unconditionally,
 * so passing nullptr is an access violation, not a soft failure. That is now
 * documented on the declaration - a death test would only re-crash the runner.
 *
 * Harness: compiles the REAL MidiFile/MidiChannel/MidiTrack/Protocol/MidiEvent
 * stack with the GUI periphery ODR-shimmed, same approach as
 * test_ffxiv_fixer_resync.
 */

#include <QtTest/QtTest>
#include <QObject>
#include <QAtomicInt>
#include <QColor>
#include <QElapsedTimer>
#include <QSemaphore>
#include <QSet>
#include <QThread>
#include <QVector>

#include "../src/midi/MidiFile.h"
#include "../src/midi/MidiChannel.h"
#include "../src/midi/MidiTrack.h"
#include "../src/protocol/Protocol.h"
#include "../src/MidiEvent/MidiEvent.h"
#include "../src/MidiEvent/ControlChangeEvent.h"
#include "../src/MidiEvent/NoteOnEvent.h"
#include "../src/MidiEvent/OffEvent.h"
#include "../src/MidiEvent/TempoChangeEvent.h"
#include "../src/MidiEvent/TimeSignatureEvent.h"

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

// ==========================================================================
// Phase 48 / v2.3 thread-safety workers.
//
// The timing conversions are the one part of MidiFile that runs outside the
// GUI thread: PlayerThread::run() asks msOfTick() for its start position and
// PlayerThread::timeout() asks tick(ms) every 15 ms, while the user keeps
// editing.
//
// v2.3 changed WHAT those off-thread calls are allowed to do. They used to
// rebuild the tempo cache on whatever thread asked first, which meant the
// player thread walked channel 17's live QMultiMap while the GUI thread was
// free to delete it (undo swaps the whole map out) - a use-after-free that no
// mutex around the cache could fix. The rule now is:
//
//   only the DOCUMENT thread rebuilds; every other thread reads an immutable
//   published snapshot and is allowed to be one edit stale.
//
// The tests below assert exactly that pair of invariants:
//   - an off-thread reader never disagrees with the published snapshot, and
//   - a document-thread rebuild swaps in a new snapshot that off-thread
//     readers then see.
// They do NOT try to prove the absence of a race by hammering (that can only
// ever fail to reproduce one); the hammer test that exists checks that
// overlapping access stays self-consistent and terminates.
//
// What is deliberately NOT overlapped: the WRITE into channel 17's QMultiMap.
// That map has never been guarded by anything, so a test that let a GUI-thread
// insertEvent() overlap a worker's query would be testing a separate,
// pre-existing hazard - and would be flaky by construction. The mutation tests
// below therefore hand the two threads a semaphore.
// ==========================================================================

/** One pre-computed question and its single-threaded answer. Anything a torn
 *  read of the anchor vector produces differs from these numbers. */
struct TimingProbe {
    int tick;
    int expectedMs;
    int ms;
    int expectedTick;
    int rangeStartMs;
    int rangeEndMs;
    int expectedRangeStartTick;
    int expectedRangeEndTick;
};

/** Hammers the two conversions MidiFile documents as callable from any thread
 *  and counts every answer that differs from the pre-computed one. Fields are
 *  written by the worker only and read after wait(), which is a happens-before
 *  edge.
 *
 *  The tick(startms, endms, ...) overload is deliberately NOT hammered from
 *  here: it hands out pointers to live tempo events and is document-thread
 *  only, so the GUI side of the test drives it instead. */
class TimingHammerThread : public QThread {
public:
    TimingHammerThread(MidiFile *f, const QVector<TimingProbe> *probes, int rounds)
        : _file(f), _probes(probes), _rounds(rounds) {}

    int mismatches = 0;
    int queries = 0;
    QAtomicInt running{1};

protected:
    void run() override {
        for (int r = 0; r < _rounds; ++r) {
            for (const TimingProbe &p : *_probes) {
                if (_file->msOfTick(p.tick) != p.expectedMs) {
                    ++mismatches;
                }
                if (_file->tick(p.ms) != p.expectedTick) {
                    ++mismatches;
                }
                queries += 2;
            }
        }
        running.storeRelease(0);
    }

private:
    MidiFile *_file;
    const QVector<TimingProbe> *_probes;
    int _rounds;
};

/** Reads one tick's time once per "epoch". The main thread performs a
 *  channel-17 edit, re-queries the file once (which is what republishes the
 *  snapshot) and releases `go`; the worker then reads that new snapshot and
 *  releases `done`. */
class EpochReaderThread : public QThread {
public:
    EpochReaderThread(MidiFile *f, int probeTick, int epochs,
                      QSemaphore *go, QSemaphore *done)
        : _file(f), _probeTick(probeTick), _epochs(epochs), _go(go), _done(done) {}

    QVector<int> observed;
    int unstable = 0;

protected:
    void run() override {
        for (int e = 0; e < _epochs; ++e) {
            _go->acquire();
            // Reads the snapshot the document thread published after its edit.
            // This thread rebuilds nothing - that is the whole point - so the
            // repeats below must all agree with the first read.
            const int first = _file->msOfTick(_probeTick);
            for (int k = 0; k < 64; ++k) {
                if (_file->msOfTick(_probeTick) != first) {
                    ++unstable;
                }
            }
            observed.append(first);
            _done->release();
        }
    }

private:
    MidiFile *_file;
    int _probeTick;
    int _epochs;
    QSemaphore *_go;
    QSemaphore *_done;
};

/** A single cached query on another thread - the stand-in for the repaint that
 *  a recalcWidgetSize() slot really triggers. */
class SingleQueryThread : public QThread {
public:
    SingleQueryThread(MidiFile *f, int tick) : _file(f), _tick(tick) {}
    int result = -1;

protected:
    void run() override { result = _file->msOfTick(_tick); }

private:
    MidiFile *_file;
    int _tick;
};

// ==========================================================================

class TestMidiMeasure : public QObject {
    Q_OBJECT

private:
    // Expected ticks per measure derived INDEPENDENTLY of measure() itself,
    // so a wrong bar length cannot make the test agree with the bug.
    static int expectedTicksPerMeasure(MidiFile *f, int tick) {
        // NOTE: meterAt()'s denominator out-param is the SMF power-of-two
        // EXPONENT (2 means /4), not the printed denominator.
        int num = 4, denPow = 2;
        f->meterAt(tick, &num, &denPow);
        return f->ticksPerQuarter() * 4 * num / (1 << denPow);
    }

    // Inserts a meter change, same construction the file itself uses
    // (denominator as a power of two: 2 => 4, 3 => 8).
    static void insertMeter(MidiFile *f, int tick, int num, int denPow) {
        f->protocol()->startNewAction("meter");
        TimeSignatureEvent *ev =
            new TimeSignatureEvent(18, num, denPow, 24, 8, f->track(0));
        f->channel(18)->insertEvent(ev, tick);
        f->protocol()->endAction();
    }

    // Replaces the tick-0 4/4 the ctor plants. Insert first, then drop the old
    // one: MidiChannel::removeEvent() refuses to remove the LAST event at tick 0
    // on channels 17/18, so the order matters.
    static void setInitialMeter(MidiFile *f, int num, int denPow) {
        f->protocol()->startNewAction("initial meter");
        const QList<MidiEvent *> before = f->channel(18)->eventMap()->values(0);
        TimeSignatureEvent *ev =
            new TimeSignatureEvent(18, num, denPow, 24, 8, f->track(0));
        f->channel(18)->insertEvent(ev, 0);
        foreach (MidiEvent *old, before) {
            f->channel(18)->removeEvent(old);
        }
        f->protocol()->endAction();
    }

    // Empties channel 18 completely - the state every null-deref guard exists
    // for. Same insert-then-remove dance is not possible here (the guard keeps
    // the last tick-0 event), so this goes through the map directly, which is
    // exactly what a caller that bypasses MidiChannel would do.
    static void stripAllTimeSignatures(MidiFile *f) {
        f->channel(18)->eventMap()->clear();
    }

    // ====================================================================
    // Phase 48 helpers: a REFERENCE implementation of the timing lookups.
    //
    // These are literal transcriptions of the linear walks MidiFile used
    // before the tempo cache existed. They are kept here on purpose: the
    // cache is only allowed to be faster, never different, and a reference
    // that lives in the test cannot silently follow a change in production.
    // ====================================================================

    static QList<MidiEvent *> tempoMap(MidiFile *f) {
        return f->channel(17)->eventMap()->values();
    }

    // Old MidiFile::msOfTick(tick) - anchor on the last tempo event at or
    // before `tick`, or on the FIRST one when `tick` precedes it.
    static double refMsOfTick(MidiFile *f, int tick) {
        double timeMs = 0;
        TempoChangeEvent *event = nullptr;
        const QList<MidiEvent *> events = tempoMap(f);
        for (MidiEvent *raw : events) {
            TempoChangeEvent *ev = dynamic_cast<TempoChangeEvent *>(raw);
            if (!ev) {
                continue;
            }
            if (!event || ev->midiTime() <= tick) {
                if (event) {
                    timeMs += event->msPerTick() * (ev->midiTime() - event->midiTime());
                }
                event = ev;
            } else {
                break;
            }
        }
        if (!event) {
            return 0;
        }
        return timeMs + event->msPerTick() * (tick - event->midiTime());
    }

    // Old MidiFile::tick(ms).
    static int refTickOfMs(MidiFile *f, int ms) {
        double time = 0;
        double timeMsNextEvent = 0;
        TempoChangeEvent *event = nullptr;
        const QList<MidiEvent *> events = tempoMap(f);
        for (int i = 0; i < events.length(); ++i) {
            TempoChangeEvent *ev = dynamic_cast<TempoChangeEvent *>(events.at(i));
            if (!ev) {
                continue;
            }
            event = ev;
            time = timeMsNextEvent;
            if (i >= events.length() - 1) {
                break;
            }
            timeMsNextEvent += (events.at(i + 1)->midiTime() - ev->midiTime()) * ev->msPerTick();
            if (timeMsNextEvent > ms) {
                break;
            }
        }
        if (!event) {
            return 0;
        }
        return (int) ((ms - time) / event->msPerTick() + event->midiTime());
    }

    // Old MidiFile::tick(startms, endms, ...) - the overload MatrixWidget
    // calls once per paint to get the visible window plus the tempo events
    // inside it.
    static int refTickRange(MidiFile *f, int startms, int endms,
                            QList<MidiEvent *> *out, int *endTick, int *msOfFirstEvent) {
        double time = 0;
        double timeMsNextEvent = 0;
        TempoChangeEvent *event = nullptr;
        const QList<MidiEvent *> events = tempoMap(f);

        int i = 0;
        for (; i < events.length(); ++i) {
            TempoChangeEvent *ev = dynamic_cast<TempoChangeEvent *>(events.at(i));
            if (!ev) {
                continue;
            }
            event = ev;
            time = timeMsNextEvent;
            if (i >= events.length() - 1) {
                break;
            }
            timeMsNextEvent += (events.at(i + 1)->midiTime() - ev->midiTime()) * ev->msPerTick();
            if (timeMsNextEvent > startms) {
                break;
            }
        }
        if (!event) {
            return 0;
        }
        const int startTick =
            (int) ((startms - time) / event->msPerTick() + event->midiTime());
        *msOfFirstEvent = (int) time;
        out->append(event);
        ++i;

        for (; i < events.length() && timeMsNextEvent < endms; ++i) {
            TempoChangeEvent *ev = dynamic_cast<TempoChangeEvent *>(events.at(i));
            if (!ev) {
                continue;
            }
            event = ev;
            if (!out->contains(event)) {
                out->append(event);
            }
            time = timeMsNextEvent;
            if (i >= events.length() - 1) {
                break;
            }
            timeMsNextEvent += (events.at(i + 1)->midiTime() - ev->midiTime()) * ev->msPerTick();
            if (timeMsNextEvent > endms) {
                break;
            }
        }
        *endTick = (int) ((endms - time) / event->msPerTick() + event->midiTime());
        return startTick;
    }

    // Deterministic pseudo-random probes (no dependency on a seeded RNG's
    // implementation, so the same ticks are checked on every platform).
    static int nextProbe(quint32 &state, int modulo) {
        state = state * 1103515245u + 12345u;
        return int((state >> 8) % quint32(modulo));
    }

    // Makes room so inserting tempo events far out does not drag the
    // file-length recompute into the middle of the build.
    static void growFile(MidiFile *f, int endTick) {
        f->protocol()->startNewAction("length");
        f->setEndTick(endTick);
        f->protocol()->endAction();
    }

    // A dense DAW-style tempo ramp: `count` tempo events every `step` ticks,
    // each with a different BPM. Written with the documented bulk idiom - ONE
    // channel snapshot, then toProtocol=false inserts - because a protocolled
    // insert per event would clone the whole event map `count` times.
    static QList<TempoChangeEvent *> buildTempoRamp(MidiFile *f, int count, int step) {
        QList<TempoChangeEvent *> made;
        MidiChannel *ch = f->channel(17);
        f->protocol()->startNewAction("tempo ramp");
        ProtocolEntry *snapshot = ch->copy();
        for (int i = 1; i <= count; ++i) {
            const int bpm = 60 + (i * 7) % 120;          // 60..179, never 0
            TempoChangeEvent *ev =
                new TempoChangeEvent(17, 60000000 / bpm, f->track(0));
            ch->insertEvent(ev, i * step, false);
            made.append(ev);
        }
        ch->protocol(snapshot, ch);
        f->protocol()->endAction();
        return made;
    }

    static QList<TimeSignatureEvent *> timeSigs(MidiFile *f) {
        QList<TimeSignatureEvent *> out;
        foreach (MidiEvent *ev, f->channel(18)->eventMap()->values()) {
            TimeSignatureEvent *ts = dynamic_cast<TimeSignatureEvent *>(ev);
            if (ts) {
                out.append(ts);
            }
        }
        return out;
    }

private slots:

    // --- 1. tick 0 is measure 1 -------------------------------------------
    void tickZeroIsMeasureOne() {
        MidiFile f;
        int start = -1, end = -1;
        QCOMPARE(f.measure(0, &start, &end), 1);
        QCOMPARE(start, 0);
    }

    void everyMeasureNumberIsPositive() {
        MidiFile f;
        const int bar = expectedTicksPerMeasure(&f, 0);
        for (int tick = 0; tick < bar * 20; tick += bar / 4) {
            int s = 0, e = 0;
            QVERIFY2(f.measure(tick, &s, &e) >= 1,
                     qPrintable(QString("measure(%1) < 1").arg(tick)));
        }
    }

    // --- 2. out-params bracket the tick -----------------------------------
    void outParamsBracketTheTick() {
        MidiFile f;
        const int bar = expectedTicksPerMeasure(&f, 0);
        const int probes[] = {0, 1, bar - 1, bar, bar + 7, bar * 5, bar * 9 + 3};
        for (int tick : probes) {
            int s = -1, e = -1;
            f.measure(tick, &s, &e);
            QVERIFY2(s <= tick && tick < e,
                     qPrintable(QString("tick %1 not inside [%2,%3)")
                                    .arg(tick).arg(s).arg(e)));
            QCOMPARE(e - s, bar);
        }
    }

    // --- 3. boundaries advance by exactly one -----------------------------
    void barBoundariesAdvanceByOne() {
        MidiFile f;
        const int bar = expectedTicksPerMeasure(&f, 0);
        int s = 0, e = 0;
        QCOMPARE(f.measure(bar - 1, &s, &e), 1);
        QCOMPARE(f.measure(bar, &s, &e), 2);
        QCOMPARE(f.measure(bar * 2, &s, &e), 3);
        for (int k = 0; k < 16; ++k) {
            QCOMPARE(f.measure(k * bar, &s, &e), k + 1);
        }
    }

    // --- 4. startTickOfMeasure() is 1-based too ---------------------------
    void startTickOfMeasureIsOneBased() {
        MidiFile f;
        const int bar = expectedTicksPerMeasure(&f, 0);
        QCOMPARE(f.startTickOfMeasure(1), 0);
        QCOMPARE(f.startTickOfMeasure(2), bar);
        QCOMPARE(f.startTickOfMeasure(9), bar * 8);
    }

    // --- 5. round-trip ----------------------------------------------------
    void measureAndStartTickRoundTrip() {
        MidiFile f;
        for (int m = 1; m <= 16; ++m) {
            int s = 0, e = 0;
            const int tick = f.startTickOfMeasure(m);
            QCOMPARE(f.measure(tick, &s, &e), m);
            QCOMPARE(s, tick);
        }
    }

    // --- 6. the totalMeasures contract EditorContext reports --------------
    void totalMeasuresNeedsNoPlusOne() {
        MidiFile f;
        const int bar = expectedTicksPerMeasure(&f, 0);
        // A song ending mid-way through bar 8 has EIGHT bars.
        f.protocol()->startNewAction("end");
        f.setEndTick(bar * 7 + bar / 2);
        f.protocol()->endAction();
        int s = 0, e = 0;
        QCOMPARE(f.measure(f.endTick(), &s, &e), 8);

        // Ending exactly ON a bar line puts the end tick in the NEXT bar -
        // still no +1 anywhere.
        f.protocol()->startNewAction("end2");
        f.setEndTick(bar * 8);
        f.protocol()->endAction();
        QCOMPARE(f.measure(f.endTick(), &s, &e), 9);
    }

    // --- 7. meterAt() reports the denominator as an EXPONENT --------------
    // The other half of the family: a caller that treats it as the printed
    // denominator gets half-note beats and displays "4/2" for a 4/4 song.
    void meterAtReportsDenominatorAsExponent() {
        MidiFile f;
        int num = 0, denPow = 0;
        f.meterAt(0, &num, &denPow);
        QCOMPARE(num, 4);
        QCOMPARE(denPow, 2);          // 2 means /4, NOT "/2"
        QCOMPARE(1 << denPow, 4);

        // A 6/8 change reports 3, not 8.
        const int tick = f.startTickOfMeasure(3);
        insertMeter(&f, tick, 6, 3);
        f.meterAt(tick, &num, &denPow);
        QCOMPARE(num, 6);
        QCOMPARE(denPow, 3);
        QCOMPARE(1 << denPow, 8);
        // ... and the measure length follows the actual denominator.
        int s = 0, e = 0;
        f.measure(tick, &s, &e);
        QCOMPARE(e - s, f.ticksPerQuarter() * 4 * 6 / 8);
    }

    // --- 8. meter change keeps the numbering continuous -------------------
    void meterChangeKeepsNumbering() {
        MidiFile f;
        const int fourFour = expectedTicksPerMeasure(&f, 0);
        // Switch to 3/4 at the start of bar 5.
        const int switchTick = f.startTickOfMeasure(5);
        insertMeter(&f, switchTick, 3, 2);

        const int threeFour = expectedTicksPerMeasure(&f, switchTick);
        QCOMPARE(threeFour, fourFour * 3 / 4);

        int s = 0, e = 0;
        // Bar 4 is still the last 4/4 bar.
        QCOMPARE(f.measure(switchTick - 1, &s, &e), 4);
        QCOMPARE(e - s, fourFour);
        // Numbering continues at 5 and now advances every 3/4 bar.
        QCOMPARE(f.measure(switchTick, &s, &e), 5);
        QCOMPARE(e - s, threeFour);
        QCOMPARE(f.measure(switchTick + threeFour, &s, &e), 6);
        QCOMPARE(f.measure(switchTick + threeFour * 2, &s, &e), 7);
    }

    // --- 9. measureCount(): the song's bar count, no phantom bar ------------
    // measure(endTick()) is one too high for every file that ends exactly on a
    // bar line, because endTick() is the EXCLUSIVE end and therefore already
    // lies in the next bar. A fresh file is exactly 10 bars.
    void measureCountHasNoPhantomBar() {
        MidiFile f;
        const int bar = expectedTicksPerMeasure(&f, 0);
        QCOMPARE(f.endTick(), bar * 10);
        QCOMPARE(f.measureCount(), 10);

        // The trap this method exists to avoid - still true of measure() itself.
        int s = 0, e = 0;
        QCOMPARE(f.measure(f.endTick(), &s, &e), 11);

        // One tick INTO bar 11 really is 11 bars.
        f.protocol()->startNewAction("end");
        f.setEndTick(bar * 10 + 1);
        f.protocol()->endAction();
        QCOMPARE(f.measureCount(), 11);

        // Ends in the middle of a bar are unaffected.
        f.protocol()->startNewAction("end2");
        f.setEndTick(bar * 7 + bar / 2);
        f.protocol()->endAction();
        QCOMPARE(f.measureCount(), 8);
    }

    void measureCountOfZeroLengthFileIsOne() {
        MidiFile f;
        f.protocol()->startNewAction("empty");
        f.setEndTick(0);
        f.protocol()->endAction();
        QCOMPARE(f.endTick(), 0);
        QCOMPARE(f.measureCount(), 1);
    }

    // --- 10. deleteMeasures() must leave the file with a meter --------------
    void deletingFirstMeasureKeepsTimeSignature() {
        MidiFile f;
        const int bar = expectedTicksPerMeasure(&f, 0);

        // The safety net that makes this survivable at all: channel 18 refuses
        // to give up its last event at tick 0. Assert it, because the
        // re-anchoring rule below must not silently depend on it.
        QCOMPARE(timeSigs(&f).size(), 1);
        QCOMPARE(f.channel(18)->removeEvent(timeSigs(&f).first()), false);

        f.protocol()->startNewAction("Remove measures");
        f.deleteMeasures(1, 1);
        f.protocol()->endAction();

        const QList<TimeSignatureEvent *> after = timeSigs(&f);
        QCOMPARE(after.size(), 1);
        QCOMPARE(after.first()->midiTime(), 0);
        QCOMPARE(after.first()->num(), 4);
        QCOMPARE(after.first()->denom(), 2);

        // ... and the readers, which dereference that event, still work.
        int s = -1, e = -1;
        QCOMPARE(f.measure(0, &s, &e), 1);
        QCOMPARE(s, 0);
        QCOMPARE(e, bar);
        QCOMPARE(f.startTickOfMeasure(1), 0);
        QCOMPARE(f.startTickOfMeasure(3), bar * 2);
        QCOMPARE(f.measureCount(), 9);   // 10 bars minus the deleted one
    }

    void deletingFirstMeasureKeepsThreeFourMeter() {
        MidiFile f;
        setInitialMeter(&f, 3, 2);
        const int bar = expectedTicksPerMeasure(&f, 0);
        QCOMPARE(bar, f.ticksPerQuarter() * 3);
        QCOMPARE(timeSigs(&f).size(), 1);

        f.protocol()->startNewAction("Remove measures");
        f.deleteMeasures(1, 1);
        f.protocol()->endAction();

        int num = 0, denPow = 0;
        TimeSignatureEvent *anchor = nullptr;
        f.meterAt(0, &num, &denPow, &anchor);
        QVERIFY2(anchor, "no TimeSignatureEvent left to anchor the meter");
        QCOMPARE(anchor->midiTime(), 0);
        QCOMPARE(num, 3);
        QCOMPARE(denPow, 2);
        QCOMPARE(timeSigs(&f).size(), 1);

        int s = -1, e = -1;
        QCOMPARE(f.measure(0, &s, &e), 1);
        QCOMPARE(e - s, bar);
    }

    // Deleting a range that ENDS on a meter change moves that meter to the
    // start. The old tick-0 event survives (channel 18 keeps its last tick-0
    // event), so the new meter must be written INTO it - a second event on tick
    // 0 would shadow it, since the map iterates the newest first and meterAt()
    // keeps the last one it sees.
    void deletingUpToAMeterChangeRetunesTheAnchor() {
        MidiFile f;
        const int fourFour = expectedTicksPerMeasure(&f, 0);
        const int switchTick = f.startTickOfMeasure(3);
        insertMeter(&f, switchTick, 3, 2);
        QCOMPARE(timeSigs(&f).size(), 2);

        f.protocol()->startNewAction("Remove measures");
        f.deleteMeasures(1, 2);
        f.protocol()->endAction();

        QCOMPARE(timeSigs(&f).size(), 1);
        int num = 0, denPow = 0;
        f.meterAt(0, &num, &denPow);
        QCOMPARE(num, 3);
        QCOMPARE(denPow, 2);
        int s = -1, e = -1;
        QCOMPARE(f.measure(0, &s, &e), 1);
        QCOMPARE(e - s, fourFour * 3 / 4);
    }

    // --- 11. no null TimeSignatureEvent dereference anywhere ----------------
    void emptyTimeSignatureChannelDoesNotCrash() {
        MidiFile f;
        const int bar = f.ticksPerQuarter() * 4;   // the 4/4 fallback
        // Kept as a non-null sentinel for the out-param check below; the strip
        // only drops it out of the map, the object stays alive.
        TimeSignatureEvent *sentinel = timeSigs(&f).value(0);
        QVERIFY(sentinel);
        stripAllTimeSignatures(&f);
        QVERIFY(f.channel(18)->eventMap()->isEmpty());

        // meterAt() reports 4/4 AND a null event - the null is the only way a
        // caller can tell "no meter at all" from "a real 4/4".
        int num = 0, denPow = 0;
        TimeSignatureEvent *anchor = sentinel;
        f.meterAt(0, &num, &denPow, &anchor);
        QCOMPARE(num, 4);
        QCOMPARE(denPow, 2);
        QVERIFY2(anchor == nullptr, "meterAt() left the event out-param untouched");

        int s = -1, e = -1;
        QCOMPARE(f.measure(0, &s, &e), 1);
        QCOMPARE(s, 0);
        QCOMPARE(e, bar);
        QCOMPARE(f.measure(bar * 3 + 5, &s, &e), 4);
        QCOMPARE(s, bar * 3);
        QCOMPARE(e, bar * 4);

        QCOMPARE(f.startTickOfMeasure(1), 0);
        QCOMPARE(f.startTickOfMeasure(5), bar * 4);
        QCOMPARE(f.measureCount(), 10);

        // The list-returning overload too.
        QList<TimeSignatureEvent *> *list = nullptr;
        int tickInMeasure = -1;
        QCOMPARE(f.measure(bar * 2, bar * 4, &list, &tickInMeasure), 3);
        QCOMPARE(tickInMeasure, 0);
        QVERIFY(list && list->isEmpty());
        delete list;
    }

    void insertAndDeleteMeasuresSurviveAnEmptyMeterChannel() {
        MidiFile f;
        const int bar = f.ticksPerQuarter() * 4;
        stripAllTimeSignatures(&f);

        f.protocol()->startNewAction("Insert measures");
        f.insertMeasures(1, 2);            // used to read an uninitialised ptr
        f.protocol()->endAction();
        QCOMPARE(f.endTick(), bar * 12);

        MidiFile g;
        stripAllTimeSignatures(&g);
        g.protocol()->startNewAction("Remove measures");
        g.deleteMeasures(1, 1);
        g.protocol()->endAction();
        QCOMPARE(g.endTick(), bar * 9);
        // The delete re-anchors a meter, so the file is no longer meterless.
        QCOMPARE(timeSigs(&g).size(), 1);
        QCOMPARE(timeSigs(&g).first()->midiTime(), 0);
        QCOMPARE(timeSigs(&g).first()->num(), 4);
        QCOMPARE(timeSigs(&g).first()->denom(), 2);
    }

    // --- 12. deleteMeasures() deletes a HALF-OPEN range ---------------------
    // tickTo is the start of the first bar that SURVIVES, so everything sitting
    // exactly on that downbeat has to survive and shift left with it. The
    // delete loop used to test `tick <= tickTo`, so deleting bar 1 also wiped
    // every note, controller and program change starting on the downbeat of
    // bar 2.
    void deletingBarsKeepsEventsOnTheNextDownbeat() {
        MidiFile f;
        const int bar = expectedTicksPerMeasure(&f, 0);
        MidiChannel *ch = f.channel(0);

        f.protocol()->startNewAction("content");
        NoteOnEvent *inside = ch->insertNote(60, 0, 480, 100, f.track(0));
        NoteOnEvent *downbeat = ch->insertNote(62, bar, bar + 480, 100, f.track(0));
        NoteOnEvent *later = ch->insertNote(64, bar * 2, bar * 2 + 480, 100, f.track(0));
        ControlChangeEvent *cc = new ControlChangeEvent(0, 7, 90, f.track(0));
        ch->insertEvent(cc, bar);
        f.protocol()->endAction();
        OffEvent *insideOff = inside->offEvent();
        QVERIFY(insideOff);
        QCOMPARE(int(ch->eventMap()->size()), 7); // 3 notes (on+off) + controller

        f.protocol()->startNewAction("Remove measures");
        f.deleteMeasures(1, 1);          // tickFrom = 0, tickTo = bar
        f.protocol()->endAction();

        // (a) the note starting exactly on the next downbeat survives and lands
        //     on tickFrom, off event and all.
        QVERIFY2(ch->eventMap()->contains(0, downbeat),
                 "note on the first kept downbeat was deleted or not shifted");
        QVERIFY(ch->eventMap()->contains(480, downbeat->offEvent()));
        QCOMPARE(downbeat->note(), 62);

        // (b) the note fully inside the deleted bar is gone - with its off.
        QVERIFY(!ch->eventMap()->values().contains(inside));
        QVERIFY(!ch->eventMap()->values().contains(insideOff));

        // (c) a controller event on that downbeat survives too.
        QVERIFY2(ch->eventMap()->contains(0, cc),
                 "controller on the first kept downbeat was deleted");

        // Everything behind it keeps its distance.
        QVERIFY(ch->eventMap()->contains(bar, later));
        QVERIFY(ch->eventMap()->contains(bar + 480, later->offEvent()));
        QCOMPARE(int(ch->eventMap()->size()), 5); // 2 notes + controller
    }

    // A note that STARTS before the deleted range and ends inside it is
    // shortened to the splice point, never deleted; one that spans the whole
    // range loses exactly the deleted length.
    void deletingBarsShortensNotesSpanningTheSplice() {
        MidiFile f;
        const int bar = expectedTicksPerMeasure(&f, 0);
        MidiChannel *ch = f.channel(0);

        f.protocol()->startNewAction("content");
        NoteOnEvent *intoRange = ch->insertNote(60, bar / 2, bar + 240, 100, f.track(0));
        NoteOnEvent *acrossRange = ch->insertNote(62, bar / 2, bar * 2 + 240, 100, f.track(0));
        f.protocol()->endAction();

        f.protocol()->startNewAction("Remove measures");
        f.deleteMeasures(2, 2);          // tickFrom = bar, tickTo = bar * 2
        f.protocol()->endAction();

        QVERIFY(ch->eventMap()->contains(bar / 2, intoRange));
        QCOMPARE(intoRange->offEvent()->midiTime(), bar);   // clamped, not shifted
        QVERIFY(ch->eventMap()->contains(bar, intoRange->offEvent()));
        QVERIFY(intoRange->offEvent()->midiTime() > intoRange->midiTime());

        QVERIFY(ch->eventMap()->contains(bar / 2, acrossRange));
        QCOMPARE(acrossRange->offEvent()->midiTime(), bar + 240); // shifted by -bar
        QCOMPARE(int(ch->eventMap()->size()), 4);           // nothing was deleted
    }

    // ======================================================================
    // Phase 48: the tempo-map cache
    //
    // msOfTick()/tick() used to walk the whole channel-17 map (with a
    // dynamic_cast per entry) on every call - per note, per grid line, per
    // cursor position on every paint, and once per event of the file on every
    // Play press. On a file carrying a DAW-exported tempo ramp that is
    // quadratic, and it showed: Play started after about ten seconds and
    // scrolling crawled.
    //
    // The replacement is a sorted anchor vector with a binary search. Speed is
    // the easy half; the dangerous half is invalidation, because a stale cache
    // means notes drawn in the wrong place and playback at the wrong time -
    // worse than slow. So every mutation FAMILY gets a case here, and every
    // result is checked against the linear reference above rather than against
    // a hard-coded number.
    // ======================================================================

    void cachedMsOfTickMatchesTheLinearReference() {
        MidiFile f;
        growFile(&f, 40000);
        buildTempoRamp(&f, 400, 24);

        quint32 state = 0x51ED0048u;
        for (int i = 0; i < 500; ++i) {
            const int tick = nextProbe(state, 12000);
            const int cached = f.msOfTick(tick);
            const int reference = (int) refMsOfTick(&f, tick);
            QVERIFY2(qAbs(cached - reference) <= 1,
                     qPrintable(QString("msOfTick(%1): cached %2 ms, linear %3 ms")
                                    .arg(tick).arg(cached).arg(reference)));
        }

        // The anchors themselves, and the region before / after the ramp.
        const int edges[] = {0, 1, 23, 24, 25, 9599, 9600, 9601, 40000};
        for (int tick : edges) {
            QVERIFY2(qAbs(f.msOfTick(tick) - (int) refMsOfTick(&f, tick)) <= 1,
                     qPrintable(QString("msOfTick(%1) disagrees at an edge").arg(tick)));
        }
    }

    void cachedTickOfMsMatchesTheLinearReference() {
        MidiFile f;
        growFile(&f, 40000);
        buildTempoRamp(&f, 400, 24);

        const int songMs = f.msOfTick(9600);
        QVERIFY(songMs > 0);

        quint32 state = 0x0048BEEFu;
        for (int i = 0; i < 500; ++i) {
            const int ms = nextProbe(state, songMs + 5000);
            const int cached = f.tick(ms);
            const int reference = refTickOfMs(&f, ms);
            QVERIFY2(qAbs(cached - reference) <= 2,
                     qPrintable(QString("tick(%1 ms): cached %2, linear %3")
                                    .arg(ms).arg(cached).arg(reference)));
        }
    }

    // Round-trip: a tick converted to ms and back lands on itself (within the
    // integer truncation both directions do).
    void msOfTickAndTickOfMsRoundTrip() {
        MidiFile f;
        growFile(&f, 40000);
        buildTempoRamp(&f, 400, 24);

        quint32 state = 0xC0FFEE48u;
        for (int i = 0; i < 300; ++i) {
            const int tick = nextProbe(state, 9600);
            const int back = f.tick(f.msOfTick(tick));
            QVERIFY2(qAbs(back - tick) <= 2,
                     qPrintable(QString("round-trip of tick %1 came back as %2")
                                    .arg(tick).arg(back)));
        }
    }

    // The overload MatrixWidget calls per paint: same start tick, same end
    // tick, same tempo events in the same order as the linear original.
    void cachedRangeLookupMatchesTheLinearReference() {
        MidiFile f;
        growFile(&f, 40000);
        buildTempoRamp(&f, 400, 24);

        const int songMs = f.msOfTick(9600);
        const int windows[][2] = {{0, 500}, {0, songMs}, {200, 1200},
                                  {songMs / 2, songMs / 2 + 800},
                                  {songMs - 100, songMs + 4000},
                                  {songMs + 1000, songMs + 2000}};
        for (const auto &w : windows) {
            QList<MidiEvent *> *cachedList = nullptr;
            int cachedEnd = -1, cachedFirstMs = -1;
            const int cachedStart = f.tick(w[0], w[1], &cachedList, &cachedEnd, &cachedFirstMs);

            QList<MidiEvent *> refList;
            int refEnd = -1, refFirstMs = -1;
            const int refStart = refTickRange(&f, w[0], w[1], &refList, &refEnd, &refFirstMs);

            QVERIFY(cachedList);
            const QString where = QString("window [%1,%2] ms").arg(w[0]).arg(w[1]);
            QVERIFY2(qAbs(cachedStart - refStart) <= 2, qPrintable("startTick: " + where));
            QVERIFY2(qAbs(cachedEnd - refEnd) <= 2, qPrintable("endTick: " + where));
            QVERIFY2(qAbs(cachedFirstMs - refFirstMs) <= 1, qPrintable("msOfFirstEvent: " + where));
            QVERIFY2(*cachedList == refList, qPrintable("tempo event list: " + where));

            // v2.3: the production walk dropped its per-append
            // QList::contains() (an O(k^2) scan per repaint). It could only
            // ever have fired if two anchors carried the same event pointer,
            // which cannot happen - each anchor comes from a distinct node of
            // channel 17's map and an event lives at exactly one position in
            // it. Asserted here rather than argued: a duplicate would show up
            // as a list longer than its set of distinct pointers.
            QSet<MidiEvent *> distinct(cachedList->begin(), cachedList->end());
            QVERIFY2(distinct.size() == cachedList->size(),
                     qPrintable("duplicate tempo event in the range list: " + where));
            delete cachedList;
        }
    }

    // --- invalidation: a BPM edit changes no map entry, only a value --------
    void setBeatsInvalidatesTheCache() {
        MidiFile f;
        growFile(&f, 40000);
        QList<TempoChangeEvent *> ramp = buildTempoRamp(&f, 200, 24);

        const int probe = 4800;
        const int before = f.msOfTick(probe);
        QCOMPARE(before, (int) refMsOfTick(&f, probe));

        f.protocol()->startNewAction("tempo");
        ramp.at(0)->setBeats(30);          // drastically slower from tick 24 on
        f.protocol()->endAction();

        const int after = f.msOfTick(probe);
        QVERIFY2(after != before, "setBeats() left a stale cached time behind");
        QCOMPARE(after, (int) refMsOfTick(&f, probe));
    }

    // --- invalidation: removing and re-inserting tempo events ---------------
    void removeAndInsertEventInvalidateTheCache() {
        MidiFile f;
        growFile(&f, 40000);
        QList<TempoChangeEvent *> ramp = buildTempoRamp(&f, 200, 24);

        const int probe = 4800;
        const int withRamp = f.msOfTick(probe);
        QCOMPARE(withRamp, (int) refMsOfTick(&f, probe));

        // Strip the ramp down to its first event. Bulk idiom: ONE snapshot.
        f.protocol()->startNewAction("thin");
        MidiChannel *ch = f.channel(17);
        ProtocolEntry *snapshot = ch->copy();
        for (int i = 1; i < ramp.size(); ++i) {
            QVERIFY(ch->removeEvent(ramp.at(i), false));
        }
        ch->protocol(snapshot, ch);
        f.protocol()->endAction();

        const int thinned = f.msOfTick(probe);
        QVERIFY2(thinned != withRamp, "removeEvent() left a stale cached time behind");
        QCOMPARE(thinned, (int) refMsOfTick(&f, probe));

        // ... and an insert is seen just as immediately.
        f.protocol()->startNewAction("insert tempo");
        TempoChangeEvent *slow = new TempoChangeEvent(17, 60000000 / 30, f.track(0));
        ch->insertEvent(slow, 480);
        f.protocol()->endAction();

        const int reinserted = f.msOfTick(probe);
        QVERIFY2(reinserted != thinned, "insertEvent() left a stale cached time behind");
        QCOMPARE(reinserted, (int) refMsOfTick(&f, probe));
    }

    // --- invalidation: moving a tempo event in time -------------------------
    void movingATempoEventInvalidatesTheCache() {
        MidiFile f;
        growFile(&f, 40000);
        QList<TempoChangeEvent *> ramp = buildTempoRamp(&f, 20, 240);

        const int probe = 4800;
        const int before = f.msOfTick(probe);
        QCOMPARE(before, (int) refMsOfTick(&f, probe));

        f.protocol()->startNewAction("move tempo");
        ramp.at(0)->setMidiTime(3600, false);
        f.protocol()->endAction();

        // Same number of events, different timing - the size safety net alone
        // could never have caught this.
        QCOMPARE(f.msOfTick(probe), (int) refMsOfTick(&f, probe));
        QVERIFY2(f.msOfTick(probe) != before,
                 "setMidiTime() on a tempo event left a stale cached time behind");
    }

    // --- invalidation: undo/redo -------------------------------------------
    void undoAndRedoOfATempoEditRevalidate() {
        MidiFile f;
        growFile(&f, 40000);
        QList<TempoChangeEvent *> ramp = buildTempoRamp(&f, 200, 24);

        const int probe = 4800;
        const int original = f.msOfTick(probe);

        f.protocol()->startNewAction("tempo");
        ramp.at(0)->setBeats(30);
        f.protocol()->endAction();
        const int edited = f.msOfTick(probe);
        QVERIFY(edited != original);

        f.protocol()->undo(false);
        QCOMPARE(f.msOfTick(probe), original);
        QCOMPARE(f.msOfTick(probe), (int) refMsOfTick(&f, probe));

        f.protocol()->redo(false);
        QCOMPARE(f.msOfTick(probe), edited);
        QCOMPARE(f.msOfTick(probe), (int) refMsOfTick(&f, probe));

        // Undoing the whole ramp restores the file's single tick-0 tempo.
        f.protocol()->undo(false);   // the edit
        f.protocol()->undo(false);   // the ramp
        QCOMPARE(int(f.channel(17)->eventMap()->size()), 1);
        QCOMPARE(f.msOfTick(probe), (int) refMsOfTick(&f, probe));
    }

    // --- the safety net: a writer that bypasses MidiChannel entirely --------
    // MainWindow has paths that erase straight out of eventMap(). Those never
    // bump the revision counter, so the cache also compares the size of the
    // tempo map with the size it was built from.
    void aDirectMapWriteIsCaughtByTheSizeCheck() {
        MidiFile f;
        growFile(&f, 40000);
        QList<TempoChangeEvent *> ramp = buildTempoRamp(&f, 200, 24);

        const int probe = 4800;
        const int before = f.msOfTick(probe);
        QCOMPARE(before, (int) refMsOfTick(&f, probe));

        QMultiMap<int, MidiEvent *> *map = f.channel(17)->eventMap();
        for (int i = 1; i < ramp.size(); ++i) {
            map->remove(ramp.at(i)->midiTime(), ramp.at(i));
        }

        const int after = f.msOfTick(probe);
        QVERIFY2(after != before, "a direct eventMap() write was not noticed");
        QCOMPARE(after, (int) refMsOfTick(&f, probe));
    }

    // ======================================================================
    // Phase 48 thread safety. The cache made msOfTick()/tick()/calcMaxTime()
    // calls that may REBUILD a std::vector, and PlayerThread runs two of them
    // off the GUI thread while the user edits. See the worker classes above
    // for what these tests deliberately do and do not overlap.
    // ======================================================================

    // --- 13a. two threads inside the cache at the same time -----------------
    // The worker asks the two conversions that are safe from any thread; the
    // GUI thread invalidates and re-reads as fast as it can, so the worker is
    // reading published snapshots while the document thread keeps replacing
    // them. Every answer is compared against a value computed single-threaded
    // before either thread started - the tempo map itself never changes here,
    // so a wrong number can only come from a reader seeing an anchor array in
    // some state it was never published in.
    //
    // This is a smoke test, not a proof: a passing run does not demonstrate
    // the absence of a race (the invariant tests below do that structurally),
    // it demonstrates that overlapping access stays self-consistent and does
    // not hang or crash.
    void concurrentTimingQueriesSurviveRebuildsFromTheOtherThread() {
        MidiFile f;
        growFile(&f, 320000);
        buildTempoRamp(&f, 256, 240);

        QVector<TimingProbe> probes;
        quint32 state = 987654321u;
        for (int i = 0; i < 128; ++i) {
            TimingProbe p;
            p.tick = nextProbe(state, 61440);
            p.expectedMs = f.msOfTick(p.tick);
            p.ms = nextProbe(state, 120000);
            p.expectedTick = f.tick(p.ms);
            p.rangeStartMs = nextProbe(state, 60000);
            p.rangeEndMs = p.rangeStartMs + 2000;
            QList<MidiEvent *> *list = nullptr;
            int endTick = -1, msOfFirst = -1;
            p.expectedRangeStartTick =
                f.tick(p.rangeStartMs, p.rangeEndMs, &list, &endTick, &msOfFirst);
            p.expectedRangeEndTick = endTick;
            delete list;
            probes.append(p);
        }

        TimingHammerThread worker(&f, &probes, 20);
        worker.start();

        int guiMismatches = 0;
        int guiIterations = 0;
        const TimingProbe &guiProbe = probes.first();
        QList<MidiEvent *> *guiList = nullptr;
        while (worker.running.loadAcquire()) {
            // Every iteration forces a real rebuild, so the worker is reading
            // snapshots that are constantly being replaced underneath it.
            f.invalidateTempoCache();
            if (f.msOfTick(guiProbe.tick) != guiProbe.expectedMs) {
                ++guiMismatches;
            }
            // The document-thread-only overload runs here, where it belongs.
            int endTick = -1, msOfFirst = -1;
            const int startTick = f.tick(guiProbe.rangeStartMs, guiProbe.rangeEndMs,
                                         &guiList, &endTick, &msOfFirst);
            if (startTick != guiProbe.expectedRangeStartTick
                || endTick != guiProbe.expectedRangeEndTick) {
                ++guiMismatches;
            }
            f.calcMaxTime();
            ++guiIterations;
            if (guiIterations > 2000000) {
                break;   // safety valve; the worker is bounded, so never hit
            }
        }
        delete guiList;
        QVERIFY2(worker.wait(60000), "the timing worker did not finish");

        QVERIFY2(worker.queries > 0, "the worker never ran a query");
        QVERIFY2(guiIterations > 0, "the two threads never overlapped");
        QCOMPARE(worker.mismatches, 0);
        QCOMPARE(guiMismatches, 0);
    }

    // --- 13b. a GUI-thread tempo edit, read back on the other thread --------
    // This is the PlayerThread situation: the user edits the tempo map while
    // playback keeps asking for timings. The semaphores keep the unguarded
    // QMultiMap write off the worker's back (see the note above the workers).
    //
    // The edit alone is NOT what the worker sees - a rebuild only ever happens
    // on the document thread, so the msOfTick() below is what republishes the
    // snapshot. That single document-thread query stands in for the repaint
    // that follows every real edit.
    void aChannel17EditIsSeenByAQueryOnAnotherThread() {
        MidiFile f;
        growFile(&f, 320000);
        buildTempoRamp(&f, 64, 240);

        const int probeTick = 30000;
        const int epochs = 24;

        QSemaphore go, done;
        EpochReaderThread reader(&f, probeTick, epochs, &go, &done);
        reader.start();

        QVector<int> expected;
        for (int e = 0; e < epochs; ++e) {
            // One user action = one protocol action.
            f.protocol()->startNewAction("tempo edit");
            const int bpm = 70 + (e * 13) % 100;
            TempoChangeEvent *ev =
                new TempoChangeEvent(17, 60000000 / bpm, f.track(0));
            f.channel(17)->insertEvent(ev, 16000 + e * 7);
            f.protocol()->endAction();

            // Document-thread query: rebuilds and republishes the snapshot,
            // and must itself agree with the linear reference.
            const int published = f.msOfTick(probeTick);
            QCOMPARE(published, (int) refMsOfTick(&f, probeTick));
            expected.append(published);
            go.release();
            done.acquire();
        }
        QVERIFY2(reader.wait(60000), "the epoch reader did not finish");

        QCOMPARE(reader.observed.size(), expected.size());
        for (int e = 0; e < epochs; ++e) {
            QCOMPARE(reader.observed.at(e), expected.at(e));
        }
        QVERIFY2(reader.unstable == 0,
                 "repeated msOfTick() calls on the worker thread disagreed");
    }

    // --- 13b-2. a rebuild publishes a NEW snapshot --------------------------
    // The document-thread half of the v2.3 contract: a revision bump makes the
    // next document-thread query rebuild, and that rebuild's values are what
    // everything (this thread and any other) reads from then on. Single
    // threaded on purpose - it pins the mechanism, not a race.
    void aTempoEditRebuildsAndRepublishesTheSnapshot() {
        MidiFile f;
        growFile(&f, 320000);
        QList<TempoChangeEvent *> ramp = buildTempoRamp(&f, 32, 240);

        const int probeTick = 30000;
        const int before = f.msOfTick(probeTick);
        QCOMPARE(before, (int) refMsOfTick(&f, probeTick));

        const quint64 revBefore = MidiChannel::tempoRevision();
        f.protocol()->startNewAction("tempo");
        ramp.at(0)->setBeats(30);          // same map size, different timing
        f.protocol()->endAction();
        QVERIFY2(MidiChannel::tempoRevision() != revBefore,
                 "the tempo edit did not bump the revision counter");

        const int after = f.msOfTick(probeTick);
        QVERIFY2(after != before, "the rebuild did not replace the old anchors");
        QCOMPARE(after, (int) refMsOfTick(&f, probeTick));

        // Stable afterwards: the new snapshot is published, not rebuilt per
        // query, so repeated reads cannot drift.
        for (int i = 0; i < 32; ++i) {
            QCOMPARE(f.msOfTick(probeTick), after);
        }

        // And another thread now reads exactly the republished value.
        SingleQueryThread reader(&f, probeTick);
        reader.start();
        QVERIFY2(reader.wait(5000), "the reader thread did not finish");
        QCOMPARE(reader.result, after);
    }

    // --- 13b-3. the invariant: no other thread ever rebuilds ----------------
    // This is the fix for the use-after-free, stated as a testable fact rather
    // than as a comment: after a channel-17 edit that the document thread has
    // NOT yet queried, a worker thread must still answer from the previous
    // snapshot. If it answered with the new timing it would have had to walk
    // the live QMultiMap itself - which is exactly what must never happen,
    // because that map can be deleted out from under it (undo, Thin Tempo
    // Map). The one-edit staleness this pins down is the documented,
    // self-correcting trade: the very next document-thread query fixes it.
    void anotherThreadNeverRebuildsTheTempoSnapshot() {
        MidiFile f;
        growFile(&f, 320000);
        buildTempoRamp(&f, 32, 240);

        const int probeTick = 30000;
        const int published = f.msOfTick(probeTick);     // publishes

        SingleQueryThread first(&f, probeTick);
        first.start();
        QVERIFY2(first.wait(5000), "the first reader thread did not finish");
        QCOMPARE(first.result, published);

        // Edit channel 17 and deliberately ask the file NOTHING afterwards.
        // (Insert well inside the existing length so the insert itself cannot
        // trigger a length recompute - that would query the file for us.)
        f.protocol()->startNewAction("tempo edit");
        f.channel(17)->insertEvent(new TempoChangeEvent(17, 60000000 / 30, f.track(0)), 12000);
        f.protocol()->endAction();

        const int edited = (int) refMsOfTick(&f, probeTick);
        QVERIFY2(edited != published,
                 "the edit changed nothing - this test would prove nothing");

        SingleQueryThread stale(&f, probeTick);
        stale.start();
        QVERIFY2(stale.wait(5000), "the stale reader thread did not finish");
        QCOMPARE(stale.result, published);   // the OLD snapshot, by design

        // One document-thread query later, the worker sees the edit.
        QCOMPARE(f.msOfTick(probeTick), edited);
        SingleQueryThread fresh(&f, probeTick);
        fresh.start();
        QVERIFY2(fresh.wait(5000), "the fresh reader thread did not finish");
        QCOMPARE(fresh.result, edited);
    }

    // --- 13c. the lock is released before the signal goes out ---------------
    // calcMaxTime() reads the cache and then emits recalcWidgetSize(). The
    // widgets on that signal repaint, and a repaint asks the file for timings -
    // so emitting while still holding the (non-recursive) cache mutex would
    // deadlock the editor on every file-length change. The query runs on
    // another thread with a deadline, which turns that regression into a failed
    // test rather than a hung one.
    void calcMaxTimeEmitsWithTheCacheLockReleased() {
        MidiFile f;
        growFile(&f, 320000);
        buildTempoRamp(&f, 64, 240);

        const int probeTick = 12000;
        const int expected = f.msOfTick(probeTick);

        bool finished = false;
        int seen = -1;
        // Connected only now: growFile()/buildTempoRamp() call calcMaxTime()
        // themselves.
        QObject::connect(&f, &MidiFile::recalcWidgetSize, &f, [&]() {
            SingleQueryThread *q = new SingleQueryThread(&f, probeTick);
            q->start();
            finished = q->wait(5000);
            if (finished) {
                seen = q->result;
                delete q;
            }
            // Otherwise it is still blocked on the mutex: leak it rather than
            // destroy a running QThread. The test has already failed.
        }, Qt::DirectConnection);

        f.calcMaxTime();

        QVERIFY2(finished,
                 "calcMaxTime() emitted recalcWidgetSize() while still holding "
                 "the tempo cache lock - a repainting slot would deadlock");
        QCOMPARE(seen, expected);
    }

    // --- the reason all of the above exists ---------------------------------
    // A synthetic stand-in for the file that triggered this phase: ~12k tempo
    // events (a ramp every 24 ticks) next to ~5k notes. preparePlayerData()
    // asks msOfTick() once per event of the file, so with a linear msOfTick()
    // this is ~200 million dynamic_casts - about ten seconds before playback
    // starts. The budget below is deliberately generous (CI machines are slow
    // and shared); the point is that the old path missed it by two orders of
    // magnitude, so it can neither flake nor quietly stop testing anything.
    void densTempoMapKeepsPlaybackPrepAndTimingQueriesFast() {
        MidiFile f;
        growFile(&f, 320000);

        QElapsedTimer build;
        build.start();
        buildTempoRamp(&f, 12000, 24);
        QCOMPARE(int(f.channel(17)->eventMap()->size()), 12001);

        // No channel snapshot needed: this case never undoes anything, and
        // 5000 protocolled inserts would clone the event map 5000 times.
        MidiChannel *ch = f.channel(0);
        for (int i = 0; i < 5000; ++i) {
            ch->insertNote(36 + (i % 60), i * 57, i * 57 + 40, 100, f.track(1), false);
        }
        const qint64 buildMs = build.elapsed();

        QElapsedTimer t;
        t.start();
        f.preparePlayerData(0);
        const qint64 prepMs = t.elapsed();
        QVERIFY(f.playerData()->size() > 0);

        const int kSweep = 50000;
        t.restart();
        qint64 checksum = 0;
        for (int i = 0; i < kSweep; ++i) {
            checksum += f.msOfTick((i * 1439) % 288000);
        }
        const qint64 sweepMs = t.elapsed();

        t.restart();
        for (int i = 0; i < kSweep; ++i) {
            checksum += f.tick((i * 1439) % 600000);
        }
        const qint64 inverseMs = t.elapsed();

        qInfo().noquote()
            << QString("[tempo] 12k tempo events + 5k notes: build %1 ms, "
                       "preparePlayerData %2 ms, %3 x msOfTick %4 ms, "
                       "%3 x tickOfMs %5 ms (checksum %6)")
                   .arg(buildMs).arg(prepMs).arg(kSweep)
                   .arg(sweepMs).arg(inverseMs).arg(checksum);

        // Measured on the reference machine: one OLD linear msOfTick() on this
        // map costs ~135 us, so the old code needed ~3 s for the playback prep
        // alone and ~7 s per sweep - about 17 s in total against this budget.
        // The cached path comes in around 10 ms, i.e. two orders of magnitude
        // of headroom in both directions: it cannot flake on a slow shared CI
        // box, and it cannot silently stop testing anything either.
        QVERIFY2(prepMs + sweepMs + inverseMs < 2000,
                 qPrintable(QString("dense tempo map is slow again: "
                                    "preparePlayerData %1 ms + %2 x msOfTick %3 ms "
                                    "+ %2 x tickOfMs %4 ms")
                                .arg(prepMs).arg(kSweep).arg(sweepMs).arg(inverseMs)));
    }
};

QTEST_MAIN(TestMidiMeasure)
#include "test_midi_measure.moc"
