/*
 * test_tempo_map_thinner (Phase 49, v2.3)
 *
 * Pins TempoMapThinner::thin(): a dense DAW-exported tempo ramp is reduced to
 * the events that matter for timing. Contract to cover (see roadmap Phase 49):
 *   1. A ramp file thins under the ms-drift tolerance; end time and note ms
 *      positions stay inside the corridor (the tolerance is the knob: at
 *      0.5 ms the end of a long ramp lands under a millisecond).
 *   2. Idempotent - a second run removes nothing.
 *   3. The tick-0 anchor always survives.
 *   4. One protocol action; undo restores every event.
 *   5. dryRun analyses without touching the file.
 *
 * v2.3 review additions:
 *   6. THIN-RERUN-001: the tolerance is a BUDGET for the document, not a fresh
 *      allowance per run. Running the tool again - at the same tolerance or a
 *      looser one - must not let the file walk away by a multiple of it, and
 *      the reported drift must be the total shift from the map as loaded.
 *      Editing the tempo map elsewhere resets that reference, honestly.
 *   7. THIN-ZEROTOL-001: tolerance 0 ("do not move my music at all") must
 *      still collapse a run of IDENTICAL tempo events, whatever the BPM.
 */

#include <QtTest/QtTest>
#include <QObject>

#include "../src/midi/TempoMapThinner.h"
#include "../src/midi/MidiFile.h"
#include "../src/midi/MidiChannel.h"
#include "../src/midi/MidiTrack.h"
#include "../src/protocol/Protocol.h"
#include "../src/MidiEvent/MidiEvent.h"
#include "../src/MidiEvent/TempoChangeEvent.h"

#include <cmath>

// ---- ODR shims (same set as test_midi_measure): the midi core references
// Appearance colors and the EventWidget singleton, which live in GUI TUs this
// target deliberately does not compile. ----------------------------------
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

#include "../src/gui/EventWidget.h"
void EventWidget::setEvents(QList<MidiEvent *>) {}
void EventWidget::reload() {}
QList<MidiEvent *> EventWidget::events() { return {}; }

// ==========================================================================

class TestTempoMapThinner : public QObject {
    Q_OBJECT

private:
    /** Microseconds per quarter for a whole-BPM tempo - the unit
     *  TempoChangeEvent's constructor expects. */
    static int usPerQuarter(int bpm) { return 60000000 / bpm; }

    /** Appends a tempo event WITHOUT going through the protocol - fixture
     *  building must not show up as undo steps the tests then have to skip.
     *  insertEvent() (not a raw eventMap() insert) is what gives the event its
     *  own midiTime; a raw insert only sets the map KEY and leaves every event
     *  claiming tick 0. */
    static void addTempo(MidiFile *f, int tick, int bpm) {
        auto *ev = new TempoChangeEvent(17, usPerQuarter(bpm), f->track(0));
        f->channel(17)->insertEvent(ev, tick, false);
    }

    /** The trigger shape: a DAW tempo ramp expanded to one event every few
     *  ticks. `count` events at `spacing` ticks, BPM ramping linearly from
     *  `fromBpm` to `toBpm`. Whole BPM is all TempoChangeEvent can store, so
     *  the ramp arrives as a staircase of long identical runs - exactly what
     *  the real file looks like once loaded. */
    static MidiFile *makeRamp(int count, int spacing, int fromBpm, int toBpm) {
        MidiFile *f = new MidiFile();
        // The default file already carries one 120 BPM event on tick 0; the
        // ramp overwrites it so the map starts at fromBpm. The end tick is set
        // FIRST: setMidiTime() recomputes the file length for every event
        // landing beyond it, which would make fixture building quadratic.
        f->channel(17)->eventMap()->clear();
        f->setEndTick(count * spacing);
        for (int i = 0; i < count; ++i) {
            const double p = (count > 1) ? double(i) / (count - 1) : 0.0;
            const int bpm = qRound(fromBpm + p * (toBpm - fromBpm));
            addTempo(f, i * spacing, bpm);
        }
        f->calcMaxTime();
        return f;
    }

    /** Tear a fixture down. forgetFile() drops the document's remembered
     *  original tempo map BEFORE the MidiFile address can be handed out again
     *  by the allocator - otherwise the next fixture could inherit a stale
     *  reference and the tests would depend on allocation luck. */
    static void destroy(MidiFile *f) {
        TempoMapThinner::forgetFile(f);
        delete f;
    }

    static int tempoCount(MidiFile *f) {
        return TempoMapThinner::tempoEventCount(f);
    }

    /** msOfTick() over a spread of ticks - the timing the user actually
     *  hears, sampled the way a note position would be. */
    static QList<int> sampleTimes(MidiFile *f, int endTick, int samples) {
        QList<int> out;
        for (int i = 0; i <= samples; ++i) {
            out.append(f->msOfTick(int(qint64(endTick) * i / samples)));
        }
        return out;
    }

    /** Worst |ms| distance between two msOfTick() samplings. */
    static int worstShift(const QList<int> &a, const QList<int> &b) {
        int worst = 0;
        const int n = qMin(a.size(), b.size());
        for (int i = 0; i < n; ++i) {
            worst = qMax(worst, qAbs(a.at(i) - b.at(i)));
        }
        return worst;
    }

private slots:

    // --- guards -----------------------------------------------------------
    void refusesNullFile() {
        TempoMapThinner::Result r = TempoMapThinner::thin(nullptr);
        QVERIFY(!r.ok);
        QVERIFY(!r.error.isEmpty());
    }

    void singleAnchorIsAlreadyMinimal() {
        MidiFile *f = new MidiFile(); // one tempo event on tick 0
        TempoMapThinner::Result r = TempoMapThinner::thin(f);
        QVERIFY(r.ok);
        QCOMPARE(r.before, 1);
        QCOMPARE(r.removed, 0);
        QCOMPARE(r.kept, 1);
        QCOMPARE(tempoCount(f), 1);
        destroy(f);
    }

    // --- 1. the trigger shape ---------------------------------------------
    void denseRampThinsAndKeepsTiming() {
        const double tol = 2.0;
        MidiFile *f = makeRamp(12000, 8, 100, 160);
        const int endTick = f->endTick();
        QCOMPARE(tempoCount(f), 12000);

        const QList<int> before = sampleTimes(f, endTick, 400);
        const int endMsBefore = f->msOfTick(endTick);

        TempoMapThinner::Result r = TempoMapThinner::thin(f, tol);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.before, 12000);
        QCOMPARE(r.kept, tempoCount(f));
        QCOMPARE(r.removed, 12000 - r.kept);
        // The roadmap's acceptance number: "12,871 -> well under 100".
        QVERIFY2(r.kept < 100,
                 qPrintable(QStringLiteral("kept %1 events").arg(r.kept)));
        QVERIFY(r.kept >= 2);
        // Nothing had been spent before this run.
        QCOMPARE(r.alreadyDriftedMs, 0.0);

        // Drift stays inside the corridor EVERYWHERE, not just at the anchors.
        QVERIFY2(r.maxDriftMs <= tol + 1e-6,
                 qPrintable(QStringLiteral("maxDrift %1").arg(r.maxDriftMs)));
        // The file's end lands inside the same corridor - a few minutes of
        // music do NOT accumulate their thousands of dropped events into a
        // creeping offset, which is the whole point of the drift criterion.
        QVERIFY2(std::fabs(r.endDriftMs) <= tol + 1e-6,
                 qPrintable(QStringLiteral("endDrift %1").arg(r.endDriftMs)));
        QVERIFY(qAbs(f->msOfTick(endTick) - endMsBefore) <= int(tol) + 1);

        // Note positions: msOfTick() is integer-truncated, so allow the
        // tolerance plus one truncation step.
        const QList<int> after = sampleTimes(f, endTick, 400);
        QCOMPARE(after.size(), before.size());
        const int worst = worstShift(after, before);
        QVERIFY2(worst <= int(tol) + 1,
                 qPrintable(QStringLiteral("worst note drift %1 ms").arg(worst)));
        destroy(f);
    }

    // --- 2. idempotent -----------------------------------------------------
    void secondRunRemovesNothing() {
        MidiFile *f = makeRamp(4000, 12, 90, 150);
        TempoMapThinner::Result first = TempoMapThinner::thin(f, 2.0);
        QVERIFY(first.ok);
        QVERIFY(first.removed > 0);

        TempoMapThinner::Result second = TempoMapThinner::thin(f, 2.0);
        QVERIFY(second.ok);
        QCOMPARE(second.removed, 0);
        QCOMPARE(second.kept, first.kept);
        QCOMPARE(tempoCount(f), first.kept);
        destroy(f);
    }

    // --- 3. tick 0 always survives ----------------------------------------
    void tickZeroAnchorIsKept() {
        MidiFile *f = makeRamp(2000, 16, 110, 170);
        const int firstBpmBefore =
            dynamic_cast<TempoChangeEvent *>(f->channel(17)->eventMap()->value(0))
                ->beatsPerQuarter();

        TempoMapThinner::Result r = TempoMapThinner::thin(f, 2.0);
        QVERIFY(r.ok);
        QVERIFY(r.removed > 0);
        QCOMPARE(f->channel(17)->eventMap()->count(0), 1);
        auto *anchor = dynamic_cast<TempoChangeEvent *>(
            f->channel(17)->eventMap()->value(0));
        QVERIFY(anchor != nullptr);
        // Surviving events keep their own BPM - the thinner never rewrites a
        // tempo value, it only drops events.
        QCOMPARE(anchor->beatsPerQuarter(), firstBpmBefore);
        destroy(f);
    }

    // --- 4. ONE undo step restores every event -----------------------------
    void oneUndoStepRestoresTheWholeMap() {
        MidiFile *f = makeRamp(3000, 10, 100, 140);
        const int endTick = f->endTick();
        const int endMsBefore = f->msOfTick(endTick);
        const int stepsBefore = f->protocol()->stepsBack();

        TempoMapThinner::Result r = TempoMapThinner::thin(f, 2.0);
        QVERIFY(r.ok);
        QVERIFY(r.removed > 0);
        // Exactly ONE protocol action for the whole bulk edit.
        QCOMPARE(f->protocol()->stepsBack(), stepsBefore + 1);

        f->protocol()->undo(false);
        QCOMPARE(tempoCount(f), 3000);
        QCOMPARE(f->msOfTick(endTick), endMsBefore);
        QCOMPARE(f->protocol()->stepsBack(), stepsBefore);

        // ... and redo takes the thinned map back.
        f->protocol()->redo(false);
        QCOMPARE(tempoCount(f), r.kept);
        destroy(f);
    }

    // --- 5. dryRun touches nothing ----------------------------------------
    void dryRunLeavesTheFileAlone() {
        MidiFile *f = makeRamp(2000, 16, 100, 150);
        const int stepsBefore = f->protocol()->stepsBack();
        const int endMsBefore = f->msOfTick(f->endTick());

        TempoMapThinner::Result r = TempoMapThinner::thin(f, 2.0, true);
        QVERIFY(r.ok);
        QVERIFY(r.dryRun);
        QVERIFY(r.removed > 0);
        QCOMPARE(tempoCount(f), 2000);
        QCOMPARE(f->protocol()->stepsBack(), stepsBefore);
        QCOMPARE(f->msOfTick(f->endTick()), endMsBefore);

        // The dry run predicts what the real run does. It also established the
        // remembered original, so the run that follows measures against the
        // same map the preview did.
        TempoMapThinner::Result applied = TempoMapThinner::thin(f, 2.0);
        QCOMPARE(applied.removed, r.removed);
        QCOMPARE(applied.kept, r.kept);
        QCOMPARE(applied.maxDriftMs, r.maxDriftMs);
        destroy(f);
    }

    // --- redundant runs collapse for free ----------------------------------
    void identicalTemposCollapseWithZeroDrift() {
        MidiFile *f = new MidiFile();
        f->channel(17)->eventMap()->clear();
        f->setEndTick(500 * 12);
        for (int i = 0; i < 500; ++i) {
            addTempo(f, i * 12, 128); // the very same BPM, 500 times
        }
        f->calcMaxTime();
        const int endMsBefore = f->msOfTick(f->endTick());

        // Even with a ZERO tolerance the redundant copies have to go: they
        // change nothing about the timing at all.
        TempoMapThinner::Result r = TempoMapThinner::thin(f, 0.0);
        QVERIFY(r.ok);
        QCOMPARE(r.kept, 1);
        QCOMPARE(r.removed, 499);
        QCOMPARE(r.maxDriftMs, 0.0);
        QCOMPARE(f->msOfTick(f->endTick()), endMsBefore);
        destroy(f);
    }

    // --- THIN-ZEROTOL-001: the same, for BPMs that are not binary fractions -
    void zeroToleranceCollapsesIdenticalTemposAtAnyBpm() {
        // 128 BPM above is the easy case: 60000/(192*128) happens to be a
        // binary fraction, so the reference sum is arithmetically exact and a
        // strict `drift > 0.0` test survives it. Most tempos are not - 120 BPM
        // at 192 ticks per quarter is 60000/23040 - and the accumulated
        // rounding then breaks a zero-width corridor at the SECOND event,
        // leaving a map of identical tempo events essentially untouched.
        // Relaxing the corridor by 1e-9 ms (inaudible by nine orders of
        // magnitude) is what makes "no drift at all" still do the lossless
        // work it can do.
        const QList<int> bpms = { 100, 120, 137, 143, 165 };
        for (int bpm : bpms) {
            MidiFile *f = new MidiFile();
            f->channel(17)->eventMap()->clear();
            f->setEndTick(800 * 7);
            for (int i = 0; i < 800; ++i) {
                addTempo(f, i * 7, bpm);
            }
            f->calcMaxTime();
            const int endMsBefore = f->msOfTick(f->endTick());

            TempoMapThinner::Result r = TempoMapThinner::thin(f, 0.0);
            QVERIFY2(r.ok, qPrintable(r.error));
            QVERIFY2(r.kept == 1,
                     qPrintable(QStringLiteral("%1 BPM: kept %2 of 800 events")
                                    .arg(bpm).arg(r.kept)));
            QCOMPARE(r.removed, 799);
            QVERIFY2(r.maxDriftMs < 1e-6,
                     qPrintable(QStringLiteral("%1 BPM: maxDrift %2")
                                    .arg(bpm).arg(r.maxDriftMs)));
            QVERIFY(qAbs(f->msOfTick(f->endTick()) - endMsBefore) <= 1);
            destroy(f);
        }
    }

    // --- a genuine tempo change is never dropped at tolerance 0 -------------
    void zeroToleranceKeepsRealTempoChanges() {
        MidiFile *f = new MidiFile();
        f->channel(17)->eventMap()->clear();
        f->setEndTick(5760);
        addTempo(f, 0, 120);
        addTempo(f, 1920, 60);
        addTempo(f, 3840, 180);
        f->calcMaxTime();

        TempoMapThinner::Result r = TempoMapThinner::thin(f, 0.0);
        QVERIFY(r.ok);
        QCOMPARE(r.removed, 0);
        QCOMPARE(r.kept, 3);
        destroy(f);
    }

    // --- a tighter tolerance keeps more events -----------------------------
    void toleranceControlsHowMuchSurvives() {
        MidiFile *tight = makeRamp(4000, 12, 100, 160);
        MidiFile *loose = makeRamp(4000, 12, 100, 160);
        const TempoMapThinner::Result rTight = TempoMapThinner::thin(tight, 0.5);
        const TempoMapThinner::Result rLoose = TempoMapThinner::thin(loose, 8.0);
        QVERIFY(rTight.ok);
        QVERIFY(rLoose.ok);
        QVERIFY2(rTight.kept >= rLoose.kept,
                 qPrintable(QStringLiteral("tight %1 vs loose %2")
                                .arg(rTight.kept).arg(rLoose.kept)));
        QVERIFY(rTight.maxDriftMs <= 0.5 + 1e-6);
        QVERIFY(rLoose.maxDriftMs <= 8.0 + 1e-6);
        // The corridor is the knob for end-time fidelity too: a tight setting
        // buys sub-millisecond end time at the price of more surviving events.
        QVERIFY2(std::fabs(rTight.endDriftMs) < 1.0,
                 qPrintable(QStringLiteral("endDrift %1").arg(rTight.endDriftMs)));
        destroy(tight);
        destroy(loose);
    }

    // --- THIN-RERUN-001: the corridor is a budget for the document ---------
    void repeatedRunsAtTheSameToleranceAreAFixedPoint() {
        const double tol = 3.0;
        MidiFile *f = makeRamp(6000, 10, 96, 168);
        const int endTick = f->endTick();
        const QList<int> asLoaded = sampleTimes(f, endTick, 300);

        const TempoMapThinner::Result first = TempoMapThinner::thin(f, tol);
        QVERIFY2(first.ok, qPrintable(first.error));
        QVERIFY(first.removed > 0);

        for (int run = 0; run < 3; ++run) {
            const TempoMapThinner::Result again = TempoMapThinner::thin(f, tol);
            QVERIFY2(again.ok, qPrintable(again.error));
            QCOMPARE(again.removed, 0);
            QCOMPARE(again.kept, first.kept);
            // Nothing moved, so what the map already carries IS the report -
            // and neither of them resets to "no drift" just because the map on
            // disk now looks like the reference the old pass measured against.
            QVERIFY(std::fabs(again.alreadyDriftedMs - again.maxDriftMs) < 1e-9);
            QVERIFY2(again.maxDriftMs <= tol + 1e-6,
                     qPrintable(QStringLiteral("run %1 maxDrift %2")
                                    .arg(run).arg(again.maxDriftMs)));
        }

        const int worst = worstShift(sampleTimes(f, endTick, 300), asLoaded);
        QVERIFY2(worst <= int(tol) + 1,
                 qPrintable(QStringLiteral("worst %1 ms after four runs").arg(worst)));
        destroy(f);
    }

    void aLooserSecondRunDoesNotSpendTheCorridorTwice() {
        const double firstTol = 2.0;
        const double secondTol = 8.0;
        MidiFile *f = makeRamp(8000, 8, 100, 170);
        const int endTick = f->endTick();
        const QList<int> asLoaded = sampleTimes(f, endTick, 500);
        const int endMsAsLoaded = f->msOfTick(endTick);

        const TempoMapThinner::Result first = TempoMapThinner::thin(f, firstTol);
        QVERIFY2(first.ok, qPrintable(first.error));
        QVERIFY(first.removed > 0);
        QCOMPARE(first.alreadyDriftedMs, 0.0);
        QVERIFY(first.maxDriftMs <= firstTol + 1e-6);

        // Same session, same document: the user opens the tool again and tries
        // a looser setting. "8 ms" is what the music may move from the file as
        // it was LOADED - not a fresh 8 ms on top of the 2 ms already spent.
        const TempoMapThinner::Result second = TempoMapThinner::thin(f, secondTol);
        QVERIFY2(second.ok, qPrintable(second.error));
        QVERIFY(second.removed > 0);
        QVERIFY2(second.alreadyDriftedMs > 0.0,
                 "the second run must know what the first one spent");
        QVERIFY(second.alreadyDriftedMs <= firstTol + 1e-6);
        QVERIFY2(second.maxDriftMs <= secondTol + 1e-6,
                 qPrintable(QStringLiteral("maxDrift %1").arg(second.maxDriftMs)));

        // ... and the file agrees with the report, both in the worst case and
        // at the end. A report measured against the already-thinned map would
        // under-state the real shift by whatever the first run used up.
        const int worst = worstShift(sampleTimes(f, endTick, 500), asLoaded);
        QVERIFY2(worst <= int(secondTol) + 1,
                 qPrintable(QStringLiteral("worst %1 ms").arg(worst)));
        const int realEndShift = qAbs(f->msOfTick(endTick) - endMsAsLoaded);
        QVERIFY2(qAbs(double(realEndShift) - std::fabs(second.endDriftMs)) <= 1.5,
                 qPrintable(QStringLiteral("reported end drift %1, real %2")
                                .arg(second.endDriftMs).arg(realEndShift)));
        destroy(f);
    }

    void aTighterSecondRunReportsWhatIsAlreadySpent() {
        MidiFile *f = makeRamp(6000, 10, 100, 170);
        const TempoMapThinner::Result loose = TempoMapThinner::thin(f, 10.0);
        QVERIFY2(loose.ok, qPrintable(loose.error));
        QVERIFY(loose.removed > 0);
        QVERIFY(loose.maxDriftMs > 1.0); // the loose run really did spend some

        // Asking for 1 ms afterwards cannot buy the spent milliseconds back.
        // The honest answer is to remove nothing more AND to keep reporting the
        // shift the document actually carries, so the dialog can say that the
        // request cannot be met instead of printing a reassuring "1.00 ms".
        const TempoMapThinner::Result tight = TempoMapThinner::thin(f, 1.0);
        QVERIFY2(tight.ok, qPrintable(tight.error));
        QCOMPARE(tight.removed, 0);
        QVERIFY2(tight.maxDriftMs > 1.0,
                 qPrintable(QStringLiteral("maxDrift %1").arg(tight.maxDriftMs)));
        QVERIFY(std::fabs(tight.maxDriftMs - loose.maxDriftMs) < 1e-9);
        destroy(f);
    }

    void editingTheTempoMapResetsTheReference() {
        MidiFile *f = makeRamp(3000, 12, 100, 150);
        const TempoMapThinner::Result first = TempoMapThinner::thin(f, 2.0);
        QVERIFY2(first.ok, qPrintable(first.error));
        QVERIFY(first.removed > 0);

        // The user adds a tempo event by hand. The remembered map no longer
        // describes this document (the current map is not a thinned version of
        // it any more), so the corridor starts over from the map as it now is -
        // the only reference that is still honest.
        addTempo(f, 5, 96);
        f->calcMaxTime();

        const TempoMapThinner::Result after = TempoMapThinner::thin(f, 2.0);
        QVERIFY2(after.ok, qPrintable(after.error));
        QCOMPARE(after.alreadyDriftedMs, 0.0);
        QVERIFY(after.maxDriftMs <= 2.0 + 1e-6);
        destroy(f);
    }

    void undoRestoresTheMapAndTheCorridorWithIt() {
        MidiFile *f = makeRamp(4000, 10, 100, 160);
        const TempoMapThinner::Result first = TempoMapThinner::thin(f, 2.0);
        QVERIFY(first.ok);
        QVERIFY(first.removed > 0);

        f->protocol()->undo(false);
        QCOMPARE(tempoCount(f), 4000);

        // The restored map IS the remembered original again, so a run after an
        // undo is a first run in every respect - same result, nothing spent.
        const TempoMapThinner::Result again = TempoMapThinner::thin(f, 2.0);
        QVERIFY2(again.ok, qPrintable(again.error));
        QCOMPARE(again.alreadyDriftedMs, 0.0);
        QCOMPARE(again.kept, first.kept);
        QCOMPARE(again.removed, first.removed);
        destroy(f);
    }
};

QTEST_APPLESS_MAIN(TestTempoMapThinner)
#include "test_tempo_map_thinner.moc"
