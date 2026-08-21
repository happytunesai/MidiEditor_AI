/*
 * test_ffxiv_playability
 *
 * Hard-gate tests for FfxivPlayabilityValidator (Phase 46) - the headless
 * FFXIV playability check shared by the `validate_ffxiv` AI tool and the
 * GUI's "Check FFXIV Playability" dialog. The octet experiment showed the
 * monophony check existed only behind the AI tool; now that it has a GUI
 * surface and reports ALL findings with event pointers, its rules are
 * contracts:
 *
 *   1. A clean octet-style file validates with zero issues.
 *   2. HOLD-overlaps are NOT flagged (user-verified in-game rule: a held
 *      note overlapped by later staccato notes is inaudible at speed) -
 *      only notes STARTING on the same tick collide.
 *   3. Distinct pitches starting on one tick are a simultaneous-notes
 *      issue - ONE issue per tick group (a triad counts once), with all
 *      NoteOn events attached (the GUI selects them).
 *   4. Same pitch + same start tick is its own class (stacked duplicate).
 *   5. Guitar tracks: same-tick starts on DIFFERENT channels are variant
 *      switches and legal; on the SAME channel they are flagged.
 *   6. Notes outside C3-C6 are flagged per note.
 *   7. A non-instrument name on a track WITH notes is flagged; the app's
 *      default "Tempo Track" (no notes) is NOT - a silent track occupies
 *      no performer. Program changes are NEVER validated: in game the
 *      track name selects the instrument, programs only drive the
 *      editor's SF2 playback.
 *   8. offendingNotes() deduplicates notes shared by several issues.
 *   9. ALL groups are reported, not just the first per track (the old
 *      tool behaviour the GUI cannot live with).
 *  10. ffxivCollisionSurplus() - the survivor rule behind the GUI's
 *      "Delete colliding notes" - keeps the highest pitch per collision
 *      (louder note among equal pitches), unions the victims across
 *      issues, and can be SCOPED to a subset of findings so a user can
 *      delete the stacked duplicates and keep the chords. Non-collision
 *      findings contribute nothing.
 *  11. MidiTrack's focus overlay (the workbench's focus mode): hidden() is
 *      the effective/rendered state and ORs it in, hiddenByUser() stays the
 *      document flag, a protocol snapshot never carries the overlay, and an
 *      explicit setHidden() ends it - so the Tracks-panel eye and the Track
 *      menu can always bring a focus-hidden track back.
 *  12. Neither reloadState nor a remove/undo round trip clears the overlay,
 *      and a removed track comes back as the very same object - which is why
 *      the workbench clears the overlay through the list of tracks it dimmed
 *      instead of walking the file's current tracks at close time.
 *  13. The tempo-map check (Phase 49) stays quiet on a hand-written tempo
 *      curve and fires only on maps whose density is a property of the
 *      EXPORT; the finding is file-level, names count and shape, and is
 *      switchable like every other check.
 *
 * Harness: same ODR-shim approach as test_ffxiv_fixer_resync - real
 * MidiFile/MidiChannel/MidiTrack/Protocol/MidiEvent stack, GUI periphery
 * shimmed.
 */

#include <QtTest/QtTest>
#include <QObject>
#include <QColor>

#include "../src/ai/FfxivPlayabilityValidator.h"
#include "../src/midi/MidiFile.h"
#include "../src/midi/MidiChannel.h"
#include "../src/midi/MidiTrack.h"
#include "../src/protocol/Protocol.h"
#include "../src/MidiEvent/MidiEvent.h"
#include "../src/MidiEvent/NoteOnEvent.h"
#include "../src/MidiEvent/ProgChangeEvent.h"
#include "../src/MidiEvent/TempoChangeEvent.h"

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

using Type = FfxivPlayabilityIssue::Type;

class TestFfxivPlayability : public QObject {
    Q_OBJECT

private:
    // New empty file; renames track 1 and pins it to a channel.
    static MidiFile *makeFile(const QString &track1Name, int track1Channel) {
        MidiFile *f = new MidiFile();
        f->track(1)->setName(track1Name);
        f->track(1)->assignChannel(track1Channel);
        return f;
    }

    static NoteOnEvent *addNote(MidiFile *f, int ch, MidiTrack *track,
                                int note, int startTick, int endTick) {
        f->protocol()->startNewAction("setup-note");
        NoteOnEvent *on =
            f->channel(ch)->insertNote(note, startTick, endTick, 100, track);
        f->protocol()->endAction();
        return on;
    }

    /** A tempo event on channel 17. insertEvent() (not a raw eventMap()
     *  insert) is what gives the event its own midiTime. */
    static void addTempo(MidiFile *f, int tick, int bpm) {
        auto *ev = new TempoChangeEvent(17, 60000000 / bpm, f->track(0));
        f->channel(17)->insertEvent(ev, tick, false);
    }

    static void addProgChange(MidiFile *f, int ch, MidiTrack *track,
                              int program, int tick) {
        f->protocol()->startNewAction("setup-pc");
        ProgChangeEvent *pc = new ProgChangeEvent(ch, program, track);
        f->channel(ch)->insertEvent(pc, tick);
        f->protocol()->endAction();
    }

private slots:

    // --- 1. clean file ----------------------------------------------------
    void cleanFileIsValid() {
        MidiFile *f = makeFile("Trumpet", 0);
        addNote(f, 0, f->track(1), 60, 0, 100);
        addNote(f, 0, f->track(1), 62, 100, 200);
        addNote(f, 0, f->track(1), 64, 200, 300);

        const FfxivPlayabilityReport r = FfxivPlayabilityValidator::validate(f);
        QVERIFY(r.ok);
        QVERIFY2(r.valid(), qPrintable(r.issues.isEmpty()
                                           ? QString()
                                           : r.issues.first().details));
        QCOMPARE(r.checkedNotes, 3);
        delete f;
    }

    void nullFileReportsError() {
        const FfxivPlayabilityReport r =
            FfxivPlayabilityValidator::validate(nullptr);
        QVERIFY(!r.ok);
        QVERIFY(!r.error.isEmpty());
    }

    // --- 2. hold-overlaps are legal ----------------------------------------
    void holdOverlapIsNotFlagged() {
        MidiFile *f = makeFile("Trumpet", 0);
        // A held note with two staccato notes on top of it - the pattern
        // every real arrangement carries. Different start ticks: legal.
        addNote(f, 0, f->track(1), 60, 0, 400);
        addNote(f, 0, f->track(1), 64, 100, 150);
        addNote(f, 0, f->track(1), 64, 200, 250);

        const FfxivPlayabilityReport r = FfxivPlayabilityValidator::validate(f);
        QVERIFY2(r.valid(), qPrintable(r.issues.isEmpty()
                                           ? QString()
                                           : r.issues.first().details));
        delete f;
    }

    // --- 3. simultaneous starts: one issue per tick group -------------------
    void simultaneousStartsFlaggedOncePerGroup() {
        MidiFile *f = makeFile("Trumpet", 0);
        // A triad starting on one tick = ONE issue carrying all three notes.
        NoteOnEvent *a = addNote(f, 0, f->track(1), 60, 0, 200);
        NoteOnEvent *b = addNote(f, 0, f->track(1), 64, 0, 300);
        NoteOnEvent *c = addNote(f, 0, f->track(1), 67, 0, 250);

        const FfxivPlayabilityReport r = FfxivPlayabilityValidator::validate(f);
        QCOMPARE(r.countOf(Type::Overlap), 1);
        const FfxivPlayabilityIssue &issue = r.issues.first();
        QCOMPARE(issue.events.size(), 3);
        QVERIFY(issue.events.contains(a));
        QVERIFY(issue.events.contains(b));
        QVERIFY(issue.events.contains(c));
        delete f;
    }

    // --- 4. stacked duplicate is its own class -----------------------------
    void stackedDuplicateIsOwnClass() {
        MidiFile *f = makeFile("Trumpet", 0);
        addNote(f, 0, f->track(1), 60, 0, 100);
        addNote(f, 0, f->track(1), 60, 0, 100);

        const FfxivPlayabilityReport r = FfxivPlayabilityValidator::validate(f);
        QCOMPARE(r.countOf(Type::DuplicateNote), 1);
        QCOMPARE(r.countOf(Type::Overlap), 0);
        delete f;
    }

    // --- 5. guitar switch channels ----------------------------------------
    void guitarCrossChannelSameTickIsLegal() {
        MidiFile *f = makeFile("ElectricGuitarOverdriven", 0);
        // Same track, same START tick, DIFFERENT channels = a variant
        // switch, not polyphony.
        addNote(f, 0, f->track(1), 60, 0, 200);
        addNote(f, 1, f->track(1), 64, 0, 300);

        const FfxivPlayabilityReport r = FfxivPlayabilityValidator::validate(f);
        QCOMPARE(r.countOf(Type::Overlap), 0);
        QCOMPARE(r.countOf(Type::DuplicateNote), 0);

        // ... but on the SAME channel it is real.
        addNote(f, 0, f->track(1), 67, 0, 150);
        const FfxivPlayabilityReport r2 = FfxivPlayabilityValidator::validate(f);
        QCOMPARE(r2.countOf(Type::Overlap), 1);
        delete f;
    }

    // --- 6. range ----------------------------------------------------------
    void outOfRangeFlaggedPerNote() {
        MidiFile *f = makeFile("Trumpet", 0);
        NoteOnEvent *low = addNote(f, 0, f->track(1), 40, 0, 100);   // E1
        addNote(f, 0, f->track(1), 60, 200, 300);                     // fine
        NoteOnEvent *high = addNote(f, 0, f->track(1), 96, 400, 500); // C7

        const FfxivPlayabilityReport r = FfxivPlayabilityValidator::validate(f);
        QCOMPARE(r.countOf(Type::OutOfRange), 2);
        const QList<MidiEvent *> offending = r.offendingNotes();
        QVERIFY(offending.contains(low));
        QVERIFY(offending.contains(high));
        delete f;
    }

    // --- 7. track names; programs are NEVER validated -----------------------
    void badNameFlaggedOnlyWithNotes() {
        MidiFile *f = makeFile("Lead Guitar", 0);
        // No notes anywhere yet: neither "Tempo Track" (track 0), nor
        // "Lead Guitar" - silent tracks occupy no performer.
        FfxivPlayabilityReport r = FfxivPlayabilityValidator::validate(f);
        QCOMPARE(r.countOf(Type::TrackName), 0);

        addNote(f, 0, f->track(1), 60, 0, 100);
        r = FfxivPlayabilityValidator::validate(f);
        QCOMPARE(r.countOf(Type::TrackName), 1);
        QVERIFY(r.issues.first().details.contains("Lead Guitar"));
        delete f;
    }

    void suffixedInstrumentNameIsLegal() {
        MidiFile *f = makeFile("Trumpet+12", 0);
        addNote(f, 0, f->track(1), 60, 0, 100);
        const FfxivPlayabilityReport r = FfxivPlayabilityValidator::validate(f);
        QCOMPARE(r.countOf(Type::TrackName), 0);
        delete f;
    }

    // In-game the TRACK NAME selects the instrument; program changes only
    // drive the editor's SF2 playback. A "wrong" program must NOT be a
    // playability finding - this pins the removal of the ProgramMismatch
    // check (its premise was disproven the day it shipped).
    void wrongProgramChangeIsNotAPlayabilityIssue() {
        MidiFile *f = makeFile("Trumpet", 0);
        addNote(f, 0, f->track(1), 60, 100, 200);
        addProgChange(f, 0, f->track(1), 40, 0); // says Violin - irrelevant
        const FfxivPlayabilityReport r = FfxivPlayabilityValidator::validate(f);
        QVERIFY2(r.valid(), qPrintable(r.issues.isEmpty()
                                           ? QString()
                                           : r.issues.first().details));
        delete f;
    }

    // --- 8. offendingNotes dedup + duplicates inside a chord group ----------
    void offendingNotesAreDeduplicated() {
        MidiFile *f = makeFile("Trumpet", 0);
        // C + C + E all starting on one tick: the two Cs are a duplicate
        // issue, the mixed pitches a simultaneous-notes issue - the shared
        // C notes appear in both but must be selected once each.
        NoteOnEvent *c1 = addNote(f, 0, f->track(1), 60, 0, 100);
        addNote(f, 0, f->track(1), 60, 0, 120);
        addNote(f, 0, f->track(1), 64, 0, 100);

        const FfxivPlayabilityReport r = FfxivPlayabilityValidator::validate(f);
        QCOMPARE(r.countOf(Type::DuplicateNote), 1);
        QCOMPARE(r.countOf(Type::Overlap), 1);
        const QList<MidiEvent *> offending = r.offendingNotes();
        QCOMPARE(offending.count(c1), 1);
        QCOMPARE(offending.size(), 3);
        delete f;
    }

    // --- 10. check selection: categories run only when asked ----------------
    void checkSelectionIsHonoured() {
        MidiFile *f = makeFile("Lead Guitar", 0); // bad name, WITH notes
        addNote(f, 0, f->track(1), 40, 0, 100);   // also out of range
        addNote(f, 0, f->track(1), 44, 0, 100);   // and a same-tick collision

        FfxivPlayabilityChecks none;
        none.simultaneousNotes = false;
        none.stackedDuplicates = false;
        none.range = false;
        none.trackNames = false;
        none.channelSpread = false;
        none.emptyTracks = false;
        FfxivPlayabilityReport r = FfxivPlayabilityValidator::validate(f, none);
        QVERIFY(r.valid()); // everything off -> nothing found

        FfxivPlayabilityChecks onlyNames = none;
        onlyNames.trackNames = true;
        r = FfxivPlayabilityValidator::validate(f, onlyNames);
        QCOMPARE(r.issues.size(), 1);
        QCOMPARE(r.countOf(Type::TrackName), 1);
        delete f;
    }

    // --- 10b. the two same-tick findings are INDEPENDENTLY switchable -------
    // Chords are often an intentional decision (the game rolls them as a
    // fast arpeggio), a stacked duplicate is almost always a defect - so
    // hunting duplicates must not mean wading through thousands of chords.
    void chordsAndStackedNotesAreSeparatelySelectable() {
        MidiFile *f = makeFile("Trumpet", 0); // clean name, in range, 1 channel
        // A chord: two DISTINCT pitches on one tick.
        addNote(f, 0, f->track(1), 60, 0, 200);
        addNote(f, 0, f->track(1), 64, 0, 200);
        // A stacked duplicate: the SAME pitch twice on one tick.
        addNote(f, 0, f->track(1), 67, 480, 680);
        addNote(f, 0, f->track(1), 67, 480, 700);

        // Chords only -> the Overlap, and nothing else.
        FfxivPlayabilityChecks chordsOnly;
        chordsOnly.stackedDuplicates = false;
        FfxivPlayabilityReport r =
            FfxivPlayabilityValidator::validate(f, chordsOnly);
        QCOMPARE(r.countOf(Type::Overlap), 1);
        QCOMPARE(r.countOf(Type::DuplicateNote), 0);
        QCOMPARE(r.issues.size(), 1);

        // Stacked notes only -> the DuplicateNote, and nothing else.
        FfxivPlayabilityChecks duplicatesOnly;
        duplicatesOnly.simultaneousNotes = false;
        r = FfxivPlayabilityValidator::validate(f, duplicatesOnly);
        QCOMPARE(r.countOf(Type::DuplicateNote), 1);
        QCOMPARE(r.countOf(Type::Overlap), 0);
        QCOMPARE(r.issues.size(), 1);

        // Both on (the default) -> both findings.
        r = FfxivPlayabilityValidator::validate(f);
        QCOMPARE(r.countOf(Type::Overlap), 1);
        QCOMPARE(r.countOf(Type::DuplicateNote), 1);
        QCOMPARE(r.issues.size(), 2);

        delete f;
    }

    // --- 11. channel spread (editor playback) -------------------------------
    void channelSpreadFlaggedForNonGuitarOnly() {
        // Non-guitar instrument with notes on two channels -> flagged.
        MidiFile *f = makeFile("Trumpet", 0);
        addNote(f, 0, f->track(1), 60, 0, 100);
        addNote(f, 1, f->track(1), 62, 200, 300);
        FfxivPlayabilityReport r = FfxivPlayabilityValidator::validate(f);
        QCOMPARE(r.countOf(Type::ChannelSpread), 1);
        delete f;

        // Guitars spread channels BY DESIGN (variant switches) -> never.
        f = makeFile("ElectricGuitarOverdriven", 0);
        addNote(f, 0, f->track(1), 60, 0, 100);
        addNote(f, 1, f->track(1), 62, 200, 300);
        r = FfxivPlayabilityValidator::validate(f);
        QCOMPARE(r.countOf(Type::ChannelSpread), 0);
        delete f;
    }

    // --- 12. empty instrument tracks ---------------------------------------
    void emptyInstrumentTrackIsInformational() {
        MidiFile *f = makeFile("Trumpet", 0); // named, no notes anywhere
        FfxivPlayabilityReport r = FfxivPlayabilityValidator::validate(f);
        QCOMPARE(r.countOf(Type::EmptyTrack), 1);
        // ... but a non-instrument name without notes stays silent
        // ("Tempo Track" must not flag every file).
        QCOMPARE(r.countOf(Type::TrackName), 0);
        delete f;
    }

    // --- 9. ALL groups reported, not just the first -------------------------
    void allGroupsReportedNotJustFirst() {
        MidiFile *f = makeFile("Trumpet", 0);
        // Three separate same-tick pairs, far apart.
        addNote(f, 0, f->track(1), 60, 0, 200);
        addNote(f, 0, f->track(1), 62, 0, 300);
        addNote(f, 0, f->track(1), 64, 1000, 1200);
        addNote(f, 0, f->track(1), 65, 1000, 1300);
        addNote(f, 0, f->track(1), 67, 2000, 2200);
        addNote(f, 0, f->track(1), 69, 2000, 2300);

        const FfxivPlayabilityReport r = FfxivPlayabilityValidator::validate(f);
        QCOMPARE(r.countOf(Type::Overlap), 3);
        delete f;
    }

    // --- 10. scoped collision surplus (the GUI's delete rule) ---------------
    void collisionSurplusIsScopedAndUnions() {
        MidiFile *f = makeFile("Trumpet", 0);
        // An intentional chord, a stacked duplicate pair, and one note that
        // is merely out of range - the three kinds a real report mixes.
        NoteOnEvent *c4 = addNote(f, 0, f->track(1), 60, 0, 200);
        NoteOnEvent *e4 = addNote(f, 0, f->track(1), 64, 0, 200);
        NoteOnEvent *g4Loud = addNote(f, 0, f->track(1), 67, 480, 600);
        NoteOnEvent *g4Quiet = addNote(f, 0, f->track(1), 67, 480, 600);
        g4Quiet->setVelocity(40, false);
        NoteOnEvent *low = addNote(f, 0, f->track(1), 30, 960, 1000);

        // Hand-built report: the rule must not depend on the detection.
        QList<FfxivPlayabilityIssue> issues;
        FfxivPlayabilityIssue chord;
        chord.type = Type::Overlap;
        chord.events = {c4, e4};
        issues.append(chord);
        FfxivPlayabilityIssue dup;
        dup.type = Type::DuplicateNote;
        dup.events = {g4Loud, g4Quiet};
        issues.append(dup);
        FfxivPlayabilityIssue range;
        range.type = Type::OutOfRange;
        range.events = {low};
        issues.append(range);

        // All three: the chord loses its lower note, the pair loses the
        // quieter copy, the range finding contributes nothing.
        const QList<MidiEvent *> all =
            ffxivCollisionSurplus(issues, {0, 1, 2});
        QCOMPARE(all.size(), 2);
        QVERIFY(all.contains(c4));
        QVERIFY(all.contains(g4Quiet));
        QVERIFY(!all.contains(e4));
        QVERIFY(!all.contains(g4Loud));
        QVERIFY(!all.contains(low));

        // Scoped to the duplicates only - the whole point of the feature:
        // fix the stacked notes, keep the chords.
        const QList<MidiEvent *> dupsOnly = ffxivCollisionSurplus(issues, {1});
        QCOMPARE(dupsOnly.size(), 1);
        QCOMPARE(dupsOnly.first(), static_cast<MidiEvent *>(g4Quiet));

        // A selection without collisions has nothing to delete.
        QVERIFY(ffxivCollisionSurplus(issues, {2}).isEmpty());
        QVERIFY(ffxivCollisionSurplus(issues, {}).isEmpty());
        delete f;
    }

    // --- 11. focus overlay vs. document visibility ---------------------------
    // The workbench's focus mode (double-click a finding -> show only that
    // track) hides through MidiTrack's VIEW-only _focusHidden overlay. The
    // three contracts every caller relies on:
    //   a) hidden() is the EFFECTIVE (rendered) state and ORs the overlay in,
    //      while hiddenByUser() stays the document's own flag - that is what
    //      keeps the Tracks-panel eye, the save-time "not audible" warning,
    //      the collab broadcast and the AI's track list honest while focus is
    //      on (FOCUS-DEADEYE-001).
    //   b) a deliberate user visibility action ENDS the overlay, so the eye
    //      and the Track menu can always bring a focus-hidden track back
    //      instead of being dead controls that still push undo steps.
    //   c) the overlay is never part of an undo snapshot.
    void focusOverlayIsViewOnly() {
        MidiFile *f = makeFile("Trumpet", 0);
        MidiTrack *t = f->track(1);

        QVERIFY(!t->hidden());
        QVERIFY(!t->hiddenByUser());
        QVERIFY(!t->focusHidden());

        // (a) overlay hides for rendering, document flag untouched.
        t->setFocusHidden(true);
        QVERIFY(t->hidden());
        QVERIFY(!t->hiddenByUser());
        QVERIFY(t->focusHidden());

        // (c) a protocol snapshot must not carry the overlay - otherwise an
        // undo taken while focus is active could resurface it later.
        ProtocolEntry *snapshot = t->copy();
        MidiTrack *snapshotTrack = dynamic_cast<MidiTrack *>(snapshot);
        QVERIFY(snapshotTrack != nullptr);
        QVERIFY(!snapshotTrack->focusHidden());
        delete snapshot;

        // (b1) the one-click path the Tracks-panel eye takes for a track that
        // is ONLY focus-hidden: releasing the overlay is view state, so it must
        // cost NO undo step (an empty protocol action would still wipe the redo
        // stack and mark the document modified).
        const int stepsBefore = f->protocol()->stepsBack();
        t->setFocusHidden(false);
        QVERIFY(!t->hidden());
        QCOMPARE(f->protocol()->stepsBack(), stepsBefore);

        // (b2) the undoable path: setHidden(false) on a focus-hidden track also
        // really shows it again (before the fix hidden() stayed true and the
        // click was a no-op that still appended an empty undo step).
        t->setFocusHidden(true);
        QVERIFY(t->hidden());
        f->protocol()->startNewAction("show track");
        t->setHidden(false);
        f->protocol()->endAction();
        QVERIFY(!t->focusHidden());
        QVERIFY(!t->hidden());
        QVERIFY(!t->hiddenByUser());

        // Hiding is a deliberate visibility action too: the overlay ends and
        // the document flag alone decides.
        t->setFocusHidden(true);
        f->protocol()->startNewAction("hide track");
        t->setHidden(true);
        f->protocol()->endAction();
        QVERIFY(!t->focusHidden());
        QVERIFY(t->hidden());
        QVERIFY(t->hiddenByUser());

        delete f;
    }

    // --- 12. undo does not clear the overlay --------------------------------
    // Pins WHY the workbench has to remember which track objects it dimmed:
    // neither reloadState nor a removal/undo round trip touches _focusHidden,
    // and a removed track keeps its identity (the very same pointer comes
    // back). Clearing "track(0..numTracks-1) at close time" therefore misses a
    // track removed while focus was active, and it returns invisible for good
    // (FOCUS-REMOVED-001).
    void focusOverlaySurvivesRemovalAndUndo() {
        MidiFile *f = makeFile("Trumpet", 0);
        f->protocol()->startNewAction("add track");
        f->addTrack();
        f->protocol()->endAction();
        QCOMPARE(f->numTracks(), 3);

        MidiTrack *victim = f->track(2);
        victim->setFocusHidden(true); // as focus mode would

        f->protocol()->startNewAction("remove track");
        QVERIFY(f->removeTrack(victim));
        f->protocol()->endAction();
        QCOMPARE(f->numTracks(), 2);

        f->protocol()->undo(false);
        QCOMPARE(f->numTracks(), 3);
        // Same object, overlay intact - undo restores the document state only.
        QCOMPARE(f->track(2), victim);
        QVERIFY(victim->focusHidden());
        QVERIFY(victim->hidden());
        QVERIFY(!victim->hiddenByUser());

        // What the workbench does at close time, via its remembered list.
        victim->setFocusHidden(false);
        QVERIFY(!victim->hidden());

        delete f;
    }

    // --- 13. tempo map (Phase 49) ------------------------------------------
    // The check has to stay QUIET on normal files - a report that flags every
    // file teaches the user to ignore it. It fires on maps whose density is a
    // property of the EXPORT: a DAW ramp expanded to one event every few
    // ticks, which the editor and the game both pay for event by event.
    void tempoMapCheckIsQuietOnANormalFile() {
        MidiFile *f = makeFile("Trumpet", 0);
        addNote(f, 0, f->track(1), 60, 0, 100);
        // A hand-written accelerando: a handful of tempo events.
        addTempo(f, 480, 130);
        addTempo(f, 960, 140);
        addTempo(f, 1440, 150);

        const FfxivPlayabilityReport r = FfxivPlayabilityValidator::validate(f);
        QCOMPARE(r.countOf(Type::TempoMap), 0);
        delete f;
    }

    void tempoMapCheckFiresOnADenseRamp() {
        MidiFile *f = makeFile("Trumpet", 0);
        addNote(f, 0, f->track(1), 60, 0, 100);
        // 1,200 events over 50 bars = 24 per bar: past the per-bar rule and
        // past the absolute limit.
        f->setEndTick(f->ticksPerQuarter() * 4 * 50);
        for (int i = 0; i < 1200; ++i) {
            addTempo(f, (i + 1) * 32, 100 + i / 40);
        }

        const FfxivPlayabilityReport r = FfxivPlayabilityValidator::validate(f);
        QCOMPARE(r.countOf(Type::TempoMap), 1);
        FfxivPlayabilityIssue issue;
        for (const FfxivPlayabilityIssue &i : r.issues) {
            if (i.type == Type::TempoMap) issue = i;
        }
        // File-level finding: no track, no note pointers to select.
        QCOMPARE(issue.track, -1);
        QVERIFY(issue.events.isEmpty());
        // The text names the count and the shape - that is what tells the user
        // this came out of a DAW rather than out of their arrangement.
        QVERIFY2(issue.details.contains(QStringLiteral("1,201")),
                 qPrintable(issue.details));
        QVERIFY2(issue.details.contains(QStringLiteral("ramp up")),
                 qPrintable(issue.details));
        delete f;
    }

    // Density alone is not enough: a short file with a few dozen tempo events
    // is somebody's tempo curve, not an export artefact. The floor keeps it
    // quiet, and the absolute limit catches long files whose per-bar average
    // hides the mass.
    void tempoMapThresholdRespectsTheDenseFloor() {
        MidiFile *f = makeFile("Trumpet", 0);
        addNote(f, 0, f->track(1), 60, 0, 100);
        f->setEndTick(80 * 32); // ~3.3 bars, covering every event below
        for (int i = 0; i < 20; ++i) { // 6 per bar, but only 20 events
            addTempo(f, (i + 1) * 32, 120 + i);
        }
        QCOMPARE(FfxivPlayabilityValidator::validate(f).countOf(Type::TempoMap), 0);

        // Same density, now past the floor.
        for (int i = 20; i < 80; ++i) {
            addTempo(f, (i + 1) * 32, 120 + i);
        }
        QCOMPARE(FfxivPlayabilityValidator::validate(f).countOf(Type::TempoMap), 1);
        delete f;
    }

    void tempoMapCheckCanBeSwitchedOff() {
        MidiFile *f = makeFile("Trumpet", 0);
        addNote(f, 0, f->track(1), 60, 0, 100);
        f->setEndTick(600 * 16);
        for (int i = 0; i < 600; ++i) {
            addTempo(f, (i + 1) * 16, 120);
        }
        FfxivPlayabilityChecks checks;
        QCOMPARE(FfxivPlayabilityValidator::validate(f, checks).countOf(Type::TempoMap), 1);
        checks.tempoMap = false;
        QCOMPARE(FfxivPlayabilityValidator::validate(f, checks).countOf(Type::TempoMap), 0);
        delete f;
    }
};

QTEST_MAIN(TestFfxivPlayability)
#include "test_ffxiv_playability.moc"
