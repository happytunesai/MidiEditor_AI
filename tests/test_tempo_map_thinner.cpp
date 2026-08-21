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
        delete f;
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
        int worst = 0;
        for (int i = 0; i < after.size(); ++i) {
            worst = qMax(worst, qAbs(after.at(i) - before.at(i)));
        }
        QVERIFY2(worst <= int(tol) + 1,
                 qPrintable(QStringLiteral("worst note drift %1 ms").arg(worst)));
        delete f;
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
        delete f;
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
        delete f;
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
        delete f;
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

        // The dry run predicts what the real run does.
        TempoMapThinner::Result applied = TempoMapThinner::thin(f, 2.0);
        QCOMPARE(applied.removed, r.removed);
        QCOMPARE(applied.kept, r.kept);
        delete f;
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
        delete f;
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
        delete f;
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
        delete tight;
        delete loose;
    }
};

QTEST_APPLESS_MAIN(TestTempoMapThinner)
#include "test_tempo_map_thinner.moc"
