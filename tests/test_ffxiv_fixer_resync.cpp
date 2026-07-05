/*
 * test_ffxiv_fixer_resync
 *
 * Hard-gate tests for the v2.0 Tier-3 opt-in "re-sync non-guitar channel
 * instruments from track names" (FFXIVChannelFixer::fixChannels, param
 * resyncNonGuitar). These encode the pre-build verification findings:
 *
 *   1. OFF by default: plain Tier 3 leaves non-guitar channel PCs
 *      byte-identical (the daily "Tier 2 once, Tier 3 repeatedly" contract).
 *   2. Stale program is rewritten (Trumpet->Trombone => channel PC 56->57).
 *   3. IDEMPOTENT: a second consecutive resync run changes nothing and
 *      reports zero resynced channels.
 *   4. "No PC at all" is detected as needs-fix even for Piano (program 0) -
 *      the progAtTick(0)==0 ambiguity from the verification.
 *   5. Stacked tick-0 PCs (old Tier-2 fan-out) collapse to exactly ONE.
 *   6. Mid-song program changes survive (tick-0-only removal).
 *   7. Percussion names are skipped (CH9 untouched); Timpani (tonal) IS
 *      resynced.
 *   8. Shared channel: the track with the earliest first NoteOn on that
 *      channel owns the program.
 *   9. Non-FFXIV track names leave their channel untouched.
 *
 * Harness: compiles the REAL FFXIVChannelFixer + MidiFile/MidiChannel/
 * MidiTrack/Protocol/MidiEvent stack; only the GUI periphery is ODR-shimmed
 * (Appearance colors, EventWidget), same approach as test_midi_event.
 */

#include <QtTest/QtTest>
#include <QObject>
#include <QColor>
#include <QJsonObject>

#include "../src/ai/FFXIVChannelFixer.h"
#include "../src/midi/MidiFile.h"
#include "../src/midi/MidiChannel.h"
#include "../src/midi/MidiTrack.h"
#include "../src/protocol/Protocol.h"
#include "../src/MidiEvent/MidiEvent.h"
#include "../src/MidiEvent/NoteOnEvent.h"
#include "../src/MidiEvent/ProgChangeEvent.h"

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

class TestFfxivFixerResync : public QObject {
    Q_OBJECT

private:
    // --- helpers ----------------------------------------------------------

    // New empty file (2 tracks: "Tempo Track", "New Instrument").
    // Renames track 1 and pins it to a channel.
    static MidiFile *makeFile(const QString &track1Name, int track1Channel) {
        MidiFile *f = new MidiFile();
        f->track(1)->setName(track1Name);
        f->track(1)->assignChannel(track1Channel);
        return f;
    }

    static void addNote(MidiFile *f, int ch, MidiTrack *track, int note,
                        int startTick, int endTick) {
        f->protocol()->startNewAction("setup-note");
        f->channel(ch)->insertNote(note, startTick, endTick, 127, track);
        f->protocol()->endAction();
    }

    static void addPc(MidiFile *f, int ch, int program, MidiTrack *track,
                      int tick) {
        auto *pc = new ProgChangeEvent(ch, program, track);
        pc->setFile(f);
        f->channel(ch)->insertEvent(pc, tick, false);
    }

    static QJsonObject runTier3(MidiFile *f, bool resync) {
        f->protocol()->startNewAction("fix");
        QJsonObject r = FFXIVChannelFixer::fixChannels(f, 3, nullptr, resync);
        f->protocol()->endAction();
        return r;
    }

    static QList<int> tickZeroPrograms(MidiFile *f, int ch) {
        QList<int> out;
        const QList<MidiEvent *> atZero = f->channel(ch)->eventMap()->values(0);
        for (MidiEvent *ev : atZero) {
            if (auto *pc = dynamic_cast<ProgChangeEvent *>(ev))
                out.append(pc->program());
        }
        return out;
    }

    // Full PC snapshot of a channel: list of (tick, program), map order.
    static QList<QPair<int, int>> allPcs(MidiFile *f, int ch) {
        QList<QPair<int, int>> out;
        QMultiMap<int, MidiEvent *> *map = f->channel(ch)->eventMap();
        for (auto it = map->begin(); it != map->end(); ++it) {
            if (auto *pc = dynamic_cast<ProgChangeEvent *>(it.value()))
                out.append({it.key(), pc->program()});
        }
        return out;
    }

    // Whole-file state fingerprint used by the idempotency gate: per channel
    // the event count + every PC (tick, program).
    static QList<QList<QPair<int, int>>> pcFingerprint(MidiFile *f) {
        QList<QList<QPair<int, int>>> out;
        for (int ch = 0; ch < 16; ++ch) {
            QList<QPair<int, int>> chState = allPcs(f, ch);
            chState.prepend({-1, f->channel(ch)->eventMap()->size()});
            out.append(chState);
        }
        return out;
    }

private slots:

    void resyncOff_leavesNonGuitarPcsUntouched() {
        MidiFile *f = makeFile("Trombone", 2);
        addNote(f, 2, f->track(1), 60, 0, 100);
        addPc(f, 2, 56, f->track(1), 0); // stale Trumpet PC

        QJsonObject r = runTier3(f, false);
        QVERIFY(r["success"].toBool());
        QCOMPARE(tickZeroPrograms(f, 2), QList<int>{56}); // untouched
        QCOMPARE(r["resyncedNonGuitarChannels"].toInt(), 0);
        delete f;
    }

    void resync_rewritesStaleProgram() {
        MidiFile *f = makeFile("Trombone", 2);
        addNote(f, 2, f->track(1), 60, 0, 100);
        addPc(f, 2, 56, f->track(1), 0); // stale Trumpet PC (56)

        QJsonObject r = runTier3(f, true);
        QVERIFY(r["success"].toBool());
        QCOMPARE(tickZeroPrograms(f, 2), QList<int>{57}); // Trombone = 57
        QCOMPARE(r["resyncedNonGuitarChannels"].toInt(), 1);
        delete f;
    }

    void resync_idempotent_secondRunNoOp() {
        MidiFile *f = makeFile("Trombone", 2);
        addNote(f, 2, f->track(1), 60, 0, 100);
        addPc(f, 2, 56, f->track(1), 0);

        runTier3(f, true);
        const auto after1 = pcFingerprint(f);

        QJsonObject r2 = runTier3(f, true);
        const auto after2 = pcFingerprint(f);

        QCOMPARE(after2, after1);                          // zero net change
        QCOMPARE(r2["resyncedNonGuitarChannels"].toInt(), 0);
        QCOMPARE(tickZeroPrograms(f, 2), QList<int>{57});  // still exactly one
        delete f;
    }

    void resync_insertsWhenNoPcAtAll_evenPiano() {
        // Piano = program 0: progAtTick(0) can't tell "no PC" from "Piano".
        // The explicit tick-0 scan must treat the bare channel as needs-fix.
        MidiFile *f = makeFile("Piano", 3);
        addNote(f, 3, f->track(1), 60, 0, 100);
        // no PC at all

        QJsonObject r = runTier3(f, true);
        QVERIFY(r["success"].toBool());
        QCOMPARE(tickZeroPrograms(f, 3), QList<int>{0}); // exactly one Piano PC
        QCOMPARE(r["resyncedNonGuitarChannels"].toInt(), 1);

        // And the second run is a no-op even though program == 0.
        QJsonObject r2 = runTier3(f, true);
        QCOMPARE(r2["resyncedNonGuitarChannels"].toInt(), 0);
        QCOMPARE(tickZeroPrograms(f, 3), QList<int>{0});
        delete f;
    }

    void resync_collapsesStackedPcsToOne() {
        // Old Tier-2 runs stacked one PC per track at tick 0. Even when the
        // program already matches, the duplicates collapse to exactly one.
        MidiFile *f = makeFile("Trombone", 2);
        addNote(f, 2, f->track(1), 60, 0, 100);
        addPc(f, 2, 57, f->track(1), 0);
        addPc(f, 2, 57, f->track(0), 0); // duplicate from another track

        QCOMPARE(tickZeroPrograms(f, 2).size(), 2);
        runTier3(f, true);
        QCOMPARE(tickZeroPrograms(f, 2), QList<int>{57});
        delete f;
    }

    void resync_preservesMidSongPc() {
        MidiFile *f = makeFile("Trombone", 2);
        addNote(f, 2, f->track(1), 60, 0, 100);
        addPc(f, 2, 56, f->track(1), 0);    // stale tick-0 PC
        addPc(f, 2, 58, f->track(1), 1000); // deliberate mid-song switch

        runTier3(f, true);
        QCOMPARE(tickZeroPrograms(f, 2), QList<int>{57});
        const auto pcs = allPcs(f, 2);
        QCOMPARE(pcs.size(), 2);
        QCOMPARE(pcs.last(), qMakePair(1000, 58)); // mid-song PC survives
        delete f;
    }

    void resync_skipsPercussionNames_butResyncsTimpani() {
        MidiFile *f = makeFile("Snare Drum", 9);
        addNote(f, 9, f->track(1), 38, 0, 100);
        addPc(f, 9, 56, f->track(1), 0); // odd PC on the drum channel

        f->protocol()->startNewAction("setup-track");
        f->addTrack();
        f->protocol()->endAction();
        MidiTrack *timpani = f->track(2);
        timpani->setName("Timpani");
        timpani->assignChannel(4);
        addNote(f, 4, timpani, 50, 0, 100);
        // Timpani channel has no PC -> needs one (program 47)

        QJsonObject r = runTier3(f, true);
        QVERIFY(r["success"].toBool());
        QCOMPARE(tickZeroPrograms(f, 9), QList<int>{56}); // percussion untouched
        QCOMPARE(tickZeroPrograms(f, 4), QList<int>{47}); // Timpani resynced
        delete f;
    }

    void resync_sharedChannel_earliestNoteOwns() {
        MidiFile *f = makeFile("Trumpet", 5);
        addNote(f, 5, f->track(1), 60, 100, 200); // Trumpet starts at tick 100

        f->protocol()->startNewAction("setup-track");
        f->addTrack();
        f->protocol()->endAction();
        MidiTrack *trombone = f->track(2);
        trombone->setName("Trombone");
        trombone->assignChannel(5);
        addNote(f, 5, trombone, 48, 0, 80); // Trombone starts at tick 0

        runTier3(f, true);
        // Trombone (57) owns the channel - its first note is earliest.
        QCOMPARE(tickZeroPrograms(f, 5), QList<int>{57});

        // Deterministic across a repeat run.
        QJsonObject r2 = runTier3(f, true);
        QCOMPARE(r2["resyncedNonGuitarChannels"].toInt(), 0);
        QCOMPARE(tickZeroPrograms(f, 5), QList<int>{57});
        delete f;
    }

    void resync_neverTargetsDrumChannel9() {
        // A melodic FFXIV-named track ASSIGNED to CH9 (Tier 2 puts track
        // index 9 there on 10+-track files) must not strip the percussion
        // PCs stacked on the drum channel nor stamp a melodic program there.
        MidiFile *f = makeFile("Violin", 9);
        addNote(f, 9, f->track(1), 60, 0, 100);
        addPc(f, 9, 117, f->track(1), 0); // Bass Drum PC from a Tier-2 run
        addPc(f, 9, 118, f->track(0), 0); // Snare PC (stacked, count > 1)

        QJsonObject r = runTier3(f, true);
        QVERIFY(r["success"].toBool());
        QCOMPARE(r["resyncedNonGuitarChannels"].toInt(), 0);
        QCOMPARE(tickZeroPrograms(f, 9).size(), 2); // both drum PCs survive
        delete f;
    }

    void resync_idleTrackDoesNotClobberForeignChannel() {
        // An FFXIV-named track with NO notes on its assigned channel must not
        // claim it: the channel may belong to a non-FFXIV track whose
        // deliberate tick-0 program change would otherwise be replaced.
        MidiFile *f = makeFile("Trombone", 2); // needed so Tier 1 doesn't abort
        addNote(f, 2, f->track(1), 60, 0, 100);

        f->protocol()->startNewAction("setup-track");
        f->addTrack();
        f->addTrack();
        f->protocol()->endAction();
        MidiTrack *synth = f->track(2);
        synth->setName("MySynth"); // not an FFXIV instrument
        synth->assignChannel(6);
        addNote(f, 6, synth, 60, 0, 100);
        addPc(f, 6, 20, synth, 0); // the user's deliberate program

        MidiTrack *idle = f->track(3);
        idle->setName("Trumpet");  // FFXIV name, but NO notes anywhere
        idle->assignChannel(6);    // stale/foreign assignment

        runTier3(f, true);
        QCOMPARE(tickZeroPrograms(f, 6), QList<int>{20}); // untouched
        delete f;
    }

    void resync_skipsNonFfxivNames() {
        MidiFile *f = makeFile("Trombone", 2); // needed so Tier 1 doesn't abort
        addNote(f, 2, f->track(1), 60, 0, 100);

        f->protocol()->startNewAction("setup-track");
        f->addTrack();
        f->protocol()->endAction();
        MidiTrack *synth = f->track(2);
        synth->setName("MySynth"); // not an FFXIV instrument
        synth->assignChannel(6);
        addNote(f, 6, synth, 60, 0, 100);
        addPc(f, 6, 20, synth, 0); // whatever the user configured

        runTier3(f, true);
        QCOMPARE(tickZeroPrograms(f, 6), QList<int>{20}); // untouched
        delete f;
    }
};

QTEST_GUILESS_MAIN(TestFfxivFixerResync)
#include "test_ffxiv_fixer_resync.moc"
