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
 * v2.3.1 (review F066) - eligibility gate, FFXIVChannelFixer::checkEligibility
 * as embedded in analyzeFile() and run first by fixChannels():
 *  10. A 12-track General MIDI file with ONE track renamed "Flute" is refused
 *      and the reason lists the other note-carrying tracks.
 *  11. All note tracks FFXIV-named (10 tracks incl. drum-split percussion
 *      names) -> eligible, Rebuild runs.
 *  12. 17 note-carrying tracks, all named -> Rebuild refused, Preserve allowed.
 *  13. A GM drum track parked on channel 9 is tolerated (Tier 2 keeps it
 *      there by design); the same track on another channel is refused with
 *      the single-track wording.
 *  14. analyzeFile()'s pre-existing fields are pinned for an eligible file.
 *
 * v2.3.1 (review F065) - Preserve and a guitar track whose channel carries no
 * guitar program at tick 0:
 *  15. A Viola track renamed to ElectricGuitarOverdriven keeps its channel and
 *      the channel takes program 29 from the track name (reported, no rename,
 *      the second run changes nothing).
 *  16. An idle guitar track parked on the Viola's channel leaves that
 *      channel's program alone.
 *
 * Harness: compiles the REAL FFXIVChannelFixer + MidiFile/MidiChannel/
 * MidiTrack/Protocol/MidiEvent stack; only the GUI periphery is ODR-shimmed
 * (Appearance colors, EventWidget), same approach as test_midi_event.
 */

#include <QtTest/QtTest>
#include <QObject>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QJsonArray>
#include <QTemporaryFile>

#include "../src/ai/FFXIVChannelFixer.h"
#include "../src/midi/MidiFile.h"
#include "../src/midi/MidiChannel.h"
#include "../src/midi/MidiTrack.h"
#include "../src/protocol/Protocol.h"
#include "../src/MidiEvent/MidiEvent.h"
#include "../src/MidiEvent/NoteOnEvent.h"
#include "../src/MidiEvent/OffEvent.h"
#include "../src/MidiEvent/ProgChangeEvent.h"
#include "../src/MidiEvent/SysExEvent.h"

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

    // What the real loader made of a hand-written SMF (see loadShape below).
    // Declared here: moc rejects type declarations inside "private slots".
    struct LoadedShape { int notes = 0; bool notesPaired = true; QList<QByteArray> sysex; };

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

    // ---- v2.3.1 eligibility gate (review F066) --------------------------

    // Appends a track named `name`, pins it to `ch` and gives it one note
    // there. Returns the new track's index.
    static int addNoteTrack(MidiFile *f, const QString &name, int ch) {
        f->protocol()->startNewAction("setup-track");
        f->addTrack();
        f->protocol()->endAction();
        const int idx = f->numTracks() - 1;
        MidiTrack *track = f->track(idx);
        track->setName(name);
        track->assignChannel(ch);
        addNote(f, ch, track, 60, idx * 10, idx * 10 + 5);
        return idx;
    }

    static QJsonObject runTier(MidiFile *f, int tier) {
        f->protocol()->startNewAction("fix");
        QJsonObject r = FFXIVChannelFixer::fixChannels(f, tier);
        f->protocol()->endAction();
        return r;
    }

    static QStringList trackNames(MidiFile *f) {
        QStringList out;
        for (int t = 0; t < f->numTracks(); ++t) out << f->track(t)->name();
        return out;
    }

    // Raw SMF (format 1, one track, 480 tpq) around a hand-written track body.
    static QByteArray smfWithOneTrack(const QByteArray &trackBody) {
        QByteArray out;
        out.append("MThd");
        out.append(QByteArray::fromHex("00000006" "0001" "0001" "01E0"));
        out.append("MTrk");
        const QByteArray body = trackBody + QByteArray::fromHex("00FF2F00");
        const quint32 len = static_cast<quint32>(body.size());
        out.append(char((len >> 24) & 0xFF)).append(char((len >> 16) & 0xFF))
           .append(char((len >> 8) & 0xFF)).append(char(len & 0xFF));
        out.append(body);
        return out;
    }

    // Writes the bytes to a temp file, loads them through the REAL loader and
    // reports what channel 0 (notes) and channel 16 (sysex) ended up holding.
    static LoadedShape loadShape(const QByteArray &smf) {
        LoadedShape shape;
        QTemporaryFile tmp(QDir::tempPath() + QStringLiteral("/sysex_XXXXXX.mid"));
        if (!tmp.open()) return shape;
        tmp.write(smf);
        const QString path = tmp.fileName();
        tmp.close();
        bool ok = false;
        MidiFile *loaded = new MidiFile(path, &ok);
        if (ok) {
            QMultiMap<int, MidiEvent *> *notesMap = loaded->channel(0)->eventMap();
            for (auto it = notesMap->begin(); it != notesMap->end(); ++it) {
                if (auto *n = dynamic_cast<NoteOnEvent *>(it.value())) {
                    ++shape.notes;
                    if (!n->offEvent()) shape.notesPaired = false;
                }
            }
            QMultiMap<int, MidiEvent *> *sysMap = loaded->channel(16)->eventMap();
            for (auto it = sysMap->begin(); it != sysMap->end(); ++it) {
                if (auto *sx = dynamic_cast<SysExEvent *>(it.value()))
                    shape.sysex << sx->data();
            }
        }
        delete loaded;
        QFile::remove(path);
        return shape;
    }

    void gate_gmFileWithOneFlute_refusedListingOthers() {
        // Track 0 = idle tempo track, track 1 = the one renamed "Flute",
        // tracks 2..11 keep their General MIDI names: 12 tracks, 11 with notes.
        MidiFile *f = makeFile("Flute", 1);
        addNote(f, 1, f->track(1), 60, 0, 100);
        const QStringList gmNames = {
            "SHEHNAI", "FRENCH HORN", "Acoustic Grand Piano", "Strings",
            "Choir Aahs", "Synth Lead", "Brass Section", "Fretless Bass",
            "Church Organ", "Vibraphone"};
        for (int i = 0; i < gmNames.size(); ++i) {
            const int t = 2 + i;
            addNoteTrack(f, gmNames.at(i), t == 9 ? 13 : t); // never on CH9
        }
        QCOMPARE(f->numTracks(), 12);

        const QJsonObject analysis = FFXIVChannelFixer::analyzeFile(f);
        QVERIFY(analysis["valid"].toBool());          // one name matched...
        QCOMPARE(analysis["ffxivTrackCount"].toInt(), 1);
        QCOMPARE(analysis["noteTrackCount"].toInt(), 11);
        QCOMPARE(analysis["ffxivNamedNoteTrackCount"].toInt(), 1);
        QCOMPARE(analysis["nonFfxivNoteTracks"].toArray().size(), 10);

        const QJsonObject gate = analysis["eligibility"].toObject();
        QVERIFY(!gate["eligible"].toBool());          // ...but the gate refuses
        QVERIFY(gate["tier2Eligible"].toBool());      // (c) is not the problem
        const QString reason = gate["reason"].toString();
        QVERIFY2(reason.startsWith("Tracks 2 SHEHNAI, 3 FRENCH HORN, 4 Acoustic Grand Piano, "),
                 qPrintable(reason));
        QVERIFY2(reason.endsWith("11 Vibraphone are not FFXIV instruments - rename them first."),
                 qPrintable(reason));
        QVERIFY(!reason.contains("1 Flute"));
        const QJsonObject first = analysis["nonFfxivNoteTracks"].toArray().first().toObject();
        QCOMPARE(first["index"].toInt(), 2);
        QCOMPARE(first["name"].toString(), QString("SHEHNAI"));

        // Every tier is refused with the SAME reason text, before any edit.
        const auto before = pcFingerprint(f);
        const QStringList namesBefore = trackNames(f);
        for (int tier : {0, 2, 3}) {
            QJsonObject r = runTier(f, tier);
            QVERIFY(!r["success"].toBool());
            QCOMPARE(r["error"].toString(), reason);
            QCOMPARE(r["tier"].toInt(), 1);
        }
        QCOMPARE(pcFingerprint(f), before);
        QCOMPARE(trackNames(f), namesBefore);
        QCOMPARE(f->track(11)->assignedChannel(), 11); // no clamping happened
        delete f;
    }

    void gate_allNoteTracksNamed_withDrumSplitNames_eligible() {
        // 10 tracks: idle tempo track + 9 note tracks, four of them carrying
        // the FFXIV drum-split names on channel 9.
        MidiFile *f = makeFile("Piano", 1);
        addNote(f, 1, f->track(1), 60, 0, 100);
        addNoteTrack(f, "Flute", 2);
        addNoteTrack(f, "Trumpet+1", 3);   // octave suffix is stripped
        addNoteTrack(f, "Violin", 4);
        addNoteTrack(f, "Harp", 5);
        addNoteTrack(f, "Bass Drum", 9);
        addNoteTrack(f, "Snare Drum", 9);
        addNoteTrack(f, "Cymbal", 9);
        addNoteTrack(f, "Bongo", 9);
        QCOMPARE(f->numTracks(), 10);

        const QJsonObject analysis = FFXIVChannelFixer::analyzeFile(f);
        const QJsonObject gate = analysis["eligibility"].toObject();
        QVERIFY2(gate["eligible"].toBool(), qPrintable(gate["reason"].toString()));
        QVERIFY(gate["tier2Eligible"].toBool());
        QVERIFY(gate["reason"].toString().isEmpty());
        QCOMPARE(gate["noteTrackCount"].toInt(), 9);
        QCOMPARE(gate["ffxivNamedNoteTrackCount"].toInt(), 9);
        QCOMPARE(gate["nonFfxivNoteTracks"].toArray().size(), 0);

        QJsonObject r = runTier(f, 2);
        QVERIFY2(r["success"].toBool(), qPrintable(r["error"].toString()));
        QCOMPARE(r["trackCount"].toInt(), 10);
        delete f;
    }

    void gate_seventeenNoteTracks_tier2Refused() {
        const QStringList names = {
            "Piano", "Harp", "Fiddle", "Lute", "Fife", "Flute", "Oboe",
            "Panpipes", "Clarinet", "Trumpet", "Saxophone", "Trombone",
            "Horn", "Tuba", "Violin", "Viola", "Cello"};
        MidiFile *f = makeFile(names.first(), 1);
        addNote(f, 1, f->track(1), 60, 0, 100);
        for (int i = 1; i < names.size(); ++i)
            addNoteTrack(f, names.at(i), (i + 1) % 16);
        QCOMPARE(f->numTracks(), 18); // idle tempo track + 17 note tracks

        const QJsonObject analysis = FFXIVChannelFixer::analyzeFile(f);
        const QJsonObject gate = analysis["eligibility"].toObject();
        QVERIFY(gate["eligible"].toBool());       // it IS an FFXIV file...
        QVERIFY(!gate["tier2Eligible"].toBool()); // ...but Rebuild is refused
        QCOMPARE(gate["noteTrackCount"].toInt(), 17);
        const QString tier2Reason = gate["tier2Reason"].toString();
        QVERIFY2(tier2Reason.contains("17 tracks with notes"), qPrintable(tier2Reason));
        QVERIFY2(tier2Reason.contains("only 16 channels"), qPrintable(tier2Reason));

        const auto before = pcFingerprint(f);
        QJsonObject r2 = runTier(f, 2);
        QVERIFY(!r2["success"].toBool());
        QCOMPARE(r2["error"].toString(), tier2Reason);
        QCOMPARE(r2["tier"].toInt(), 2);
        QCOMPARE(pcFingerprint(f), before);       // nothing was touched

        QJsonObject r3 = runTier(f, 3);           // Preserve is still allowed
        QVERIFY2(r3["success"].toBool(), qPrintable(r3["error"].toString()));
        delete f;
    }

    void gate_gmDrumTrackOnChannel9_tolerated_elsewhereRefused() {
        MidiFile *f = makeFile("Flute", 1);
        addNote(f, 1, f->track(1), 60, 0, 100);
        const int drums = addNoteTrack(f, "Drums", 9); // drum-split leftover
        QCOMPARE(drums, 2);

        QJsonObject gate = FFXIVChannelFixer::checkEligibility(f);
        QVERIFY2(gate["eligible"].toBool(), qPrintable(gate["reason"].toString()));
        QCOMPARE(gate["noteTrackCount"].toInt(), 2);
        QCOMPARE(gate["nonFfxivNoteTracks"].toArray().size(), 0);

        // The same unmatched name off channel 9 is a real offender.
        MidiFile *g = makeFile("Flute", 1);
        addNote(g, 1, g->track(1), 60, 0, 100);
        addNoteTrack(g, "Drums", 5);
        gate = FFXIVChannelFixer::checkEligibility(g);
        QVERIFY(!gate["eligible"].toBool());
        QCOMPARE(gate["reason"].toString(),
                 QString("Track 2 Drums is not an FFXIV instrument - rename it first."));

        // Zero FFXIV names anywhere keeps the long-standing message.
        MidiFile *h = makeFile("Lead", 1);
        addNote(h, 1, h->track(1), 60, 0, 100);
        gate = FFXIVChannelFixer::checkEligibility(h);
        QVERIFY(!gate["eligible"].toBool());
        QVERIFY(gate["reason"].toString().startsWith("No FFXIV instrument names detected."));
        delete f;
        delete g;
        delete h;
    }

    void analyzeFile_existingFieldsPinnedForEligibleFile() {
        // Pre-gate contract of analyzeFile(): every field the dialog read
        // before v2.3.1 keeps its value and semantics (ffxivTrackCount counts
        // by NAME, idle tracks included; autoDetectedTier 3 = guitar + PC).
        MidiFile *f = makeFile("ElectricGuitarOverdriven", 1);
        addNote(f, 1, f->track(1), 60, 0, 100);
        addPc(f, 1, 29, f->track(1), 0);
        addNoteTrack(f, "Trumpet", 2);
        addNoteTrack(f, "Snare Drum", 9);
        f->protocol()->startNewAction("setup-track");
        f->addTrack();
        f->protocol()->endAction();
        f->track(4)->setName("Piano"); // FFXIV name, no notes

        const QJsonObject a = FFXIVChannelFixer::analyzeFile(f);
        QVERIFY(a["valid"].toBool());
        QCOMPARE(a["trackCount"].toInt(), 5);
        QCOMPARE(a["ffxivTrackCount"].toInt(), 4);
        QCOMPARE(a["hasGuitar"].toBool(), true);
        QCOMPARE(a["totalProgramChanges"].toInt(), 1);
        QCOMPARE(a["autoDetectedTier"].toInt(), 3);
        QCOMPARE(a["guitarVariants"].toArray(), QJsonArray{"ElectricGuitarOverdriven"});
        QCOMPARE(a["percussionTracks"].toArray(), QJsonArray{"Snare Drum"});
        QCOMPARE(a["melodicTracks"].toArray(), (QJsonArray{"Trumpet", "Piano"}));

        // ...and the new fields sit beside them.
        QCOMPARE(a["noteTrackCount"].toInt(), 3);
        QCOMPARE(a["ffxivNamedNoteTrackCount"].toInt(), 3);
        QVERIFY(a["eligibility"].toObject()["eligible"].toBool());
        delete f;
    }

    // ---- v2.3.1 review F065: guitar track on a channel without a guitar PC ----

    void tier3_renamedGuitarTrackGetsProgramFromTrackName() {
        // Track 1 was a Viola on CH1 (PC 41) and got renamed to
        // ElectricGuitarOverdriven; track 2 is a configured Overdriven on CH4
        // (PC 29), so auto-detection lands on Preserve. Before the fix CLEAN
        // stripped the Viola PC from CH1 and nothing came back: program 0.
        MidiFile *f = makeFile("ElectricGuitarOverdriven", 1);
        addNote(f, 1, f->track(1), 60, 0, 100);
        addPc(f, 1, 41, f->track(1), 0);
        const int g = addNoteTrack(f, "ElectricGuitarOverdriven", 4);
        addPc(f, 4, 29, f->track(g), 0);

        const QJsonObject a = FFXIVChannelFixer::analyzeFile(f);
        QCOMPARE(a["autoDetectedTier"].toInt(), 3);
        const QJsonArray noProg = a["guitarTracksWithoutProgram"].toArray();
        QCOMPARE(noProg.size(), 1);
        QCOMPARE(noProg.first().toObject()["index"].toInt(), 1);
        QCOMPARE(noProg.first().toObject()["channel"].toInt(), 1);

        QJsonObject r = runTier(f, 0); // auto -> Preserve
        QVERIFY2(r["success"].toBool(), qPrintable(r["error"].toString()));
        QCOMPARE(r["tier"].toInt(), 3);
        QCOMPARE(r["guitarProgramFallbacks"].toInt(), 1);
        QCOMPARE(f->channel(1)->progAtTick(0), 29);
        QVERIFY(!tickZeroPrograms(f, 1).contains(41));
        QCOMPARE(f->channel(4)->progAtTick(0), 29);
        QCOMPARE(f->track(1)->name(), QString("ElectricGuitarOverdriven")); // no rename
        QCOMPARE(f->track(1)->assignedChannel(), 1);                       // no migration

        // Second run: CH1 now carries a guitar program, the fallback is not
        // needed any more and the file does not change any further.
        const auto after1 = pcFingerprint(f);
        QJsonObject r2 = runTier(f, 3);
        QCOMPARE(r2["guitarProgramFallbacks"].toInt(), 0);
        QCOMPARE(pcFingerprint(f), after1);
        QCOMPARE(FFXIVChannelFixer::analyzeFile(f)["guitarTracksWithoutProgram"].toArray().size(), 0);
        delete f;
    }

    void tier3_idleGuitarTrackLeavesForeignChannelAlone() {
        // An empty ElectricGuitarClean track parked on the Viola's channel must
        // neither strip the Viola's program nor turn CH1 into a guitar channel.
        MidiFile *f = makeFile("Viola", 1);
        addNote(f, 1, f->track(1), 60, 0, 100);
        addPc(f, 1, 41, f->track(1), 0);
        const int g = addNoteTrack(f, "ElectricGuitarOverdriven", 4);
        addPc(f, 4, 29, f->track(g), 0);
        f->protocol()->startNewAction("setup-track");
        f->addTrack();
        f->protocol()->endAction();
        const int idle = f->numTracks() - 1;
        f->track(idle)->setName("ElectricGuitarClean");
        f->track(idle)->assignChannel(1);

        QJsonObject r = runTier(f, 3);
        QVERIFY2(r["success"].toBool(), qPrintable(r["error"].toString()));
        QCOMPARE(r["guitarProgramFallbacks"].toInt(), 0);
        QCOMPARE(tickZeroPrograms(f, 1), QList<int>{41}); // Viola untouched
        QCOMPARE(f->channel(4)->progAtTick(0), 29);
        QCOMPARE(f->track(idle)->assignedChannel(), 1);
        delete f;
    }

    // ---- review R231-21: gate rule (c) mirrors Tier-2 routing --------------

    void gate_ruleC_followsTier2Routing() {
        // Track 0 idle, tracks 1..15 melodic FFXIV names with notes; index 16 is
        // the track Tier 2 would clamp onto channel 15 - unless its NAME routes
        // it to channel 9 the way Tier 2 routes percussion names.
        const QStringList names = {
            "Piano", "Harp", "Fiddle", "Lute", "Fife", "Flute", "Oboe",
            "Panpipes", "Clarinet", "Trumpet", "Saxophone", "Trombone",
            "Horn", "Tuba", "Violin"};
        auto build = [&](const QString &name16, int ch16) {
            MidiFile *f = makeFile(names.first(), 1);
            addNote(f, 1, f->track(1), 60, 0, 100);
            for (int i = 1; i < names.size(); ++i)
                addNoteTrack(f, names.at(i), i + 1);   // indices 2..15
            addNoteTrack(f, name16, ch16);             // index 16
            return f;
        };
        // Timpani is tonal and renumbered by Tier 2: notes on channel 9 do not
        // save it from the clamp -> Rebuild refused.
        MidiFile *timpani = build("Timpani", 9);
        QCOMPARE(timpani->numTracks(), 17);
        QVERIFY(!FFXIVChannelFixer::checkEligibility(timpani)["tier2Eligible"].toBool());
        // A percussion NAME goes to channel 9 whatever channel its notes are on
        // -> no clamp, Rebuild allowed (the old rule refused this file).
        MidiFile *snare = build("Snare Drum", 5);
        QVERIFY(FFXIVChannelFixer::checkEligibility(snare)["tier2Eligible"].toBool());
        // The unmatched drum-split leftover parked on channel 9 stays there.
        MidiFile *drums = build("Drums", 9);
        QVERIFY(FFXIVChannelFixer::checkEligibility(drums)["tier2Eligible"].toBool());
        // A guitar variant at index 16 is clamped - unless the same variant
        // already sits within the first 16 tracks (Tier 2 reuses its channel).
        MidiFile *dup = build("ElectricGuitarClean", 6);
        QVERIFY(!FFXIVChannelFixer::checkEligibility(dup)["tier2Eligible"].toBool());
        dup->track(3)->setName("ElectricGuitarClean");
        QVERIFY(FFXIVChannelFixer::checkEligibility(dup)["tier2Eligible"].toBool());
        delete timpani;
        delete snare;
        delete drums;
        delete dup;
    }

    // ---- review R231-17: read-only tier detection for the AI tool -----------

    void autoTier_matchesFixChannelsDetection() {
        // Rule (A): a guitar program at tick 0 decides Preserve.
        MidiFile *f = makeFile("ElectricGuitarOverdriven", 1);
        addNote(f, 1, f->track(1), 60, 0, 100);
        QCOMPARE(FFXIVChannelFixer::autoTier(f), 2);   // no guitar program yet -> Rebuild
        QCOMPARE(runTier(f, 0)["tier"].toInt(), 2);    // ...and that is what fixChannels does
        // The run inserted PC 29 on channel 1: now configured -> Preserve.
        QCOMPARE(f->channel(1)->progAtTick(0), 29);
        QCOMPARE(FFXIVChannelFixer::autoTier(f), 3);
        QCOMPARE(runTier(f, 0)["tier"].toInt(), 3);

        // No guitar at all -> Rebuild.
        MidiFile *g = makeFile("Viola", 1);
        addNote(g, 1, g->track(1), 60, 0, 100);
        QCOMPARE(FFXIVChannelFixer::autoTier(g), 2);
        QCOMPARE(runTier(g, 0)["tier"].toInt(), 2);

        // Rule (B): no guitar program anywhere, but a guitar track plays on two
        // guitar channels (its own and the other guitar track's) -> Preserve.
        MidiFile *h = makeFile("ElectricGuitarClean", 2);
        addNote(h, 2, h->track(1), 60, 0, 100);
        const int other = addNoteTrack(h, "ElectricGuitarOverdriven", 3);
        addNote(h, 3, h->track(1), 64, 200, 300);      // track 1 also plays on channel 3
        Q_UNUSED(other);
        QCOMPARE(FFXIVChannelFixer::autoTier(h), 3);
        QCOMPARE(runTier(h, 0)["tier"].toInt(), 3);
        delete f;
        delete g;
        delete h;
    }

    // ---- review R231-10 (cross-track case): Note-Off before its Note-On ------

    void loader_pairsNoteOffFromEarlierTrackWithNoteOnFromLaterTrack() {
        // Files written by 2.3.0's move-to-track: the Note-Off stayed in the
        // source track, the Note-On went into the appended target track, which
        // is parsed LATER. The pairing must happen after the last track.
        MidiFile *f = makeFile("Piano", 1);
        f->protocol()->startNewAction("setup-track");
        f->addTrack();
        f->protocol()->endAction();
        MidiTrack *src = f->track(1);
        MidiTrack *dst = f->track(2);
        NoteOnEvent *on = new NoteOnEvent(60, 100, 1, dst);
        OffEvent *off = new OffEvent(1, 127 - 60, src);
        QCOMPARE(off->onEvent(), static_cast<OnEvent *>(on)); // in-memory pairing (pass 2)
        on->setFile(f);
        off->setFile(f);
        on->setMidiTime(480, false);
        off->setMidiTime(960, false);

        QTemporaryFile tmp(QDir::tempPath() + QStringLiteral("/crosstrack_XXXXXX.mid"));
        QVERIFY(tmp.open());
        const QString path = tmp.fileName();
        tmp.close();
        QVERIFY(f->save(path));

        bool ok = false;
        MidiFile *loaded = new MidiFile(path, &ok);
        QVERIFY(ok);
        int notes = 0;
        QMultiMap<int, MidiEvent *> *map = loaded->channel(1)->eventMap();
        for (auto it = map->begin(); it != map->end(); ++it) {
            if (auto *n = dynamic_cast<NoteOnEvent *>(it.value())) {
                ++notes;
                QVERIFY(n->offEvent());
                QCOMPARE(n->midiTime(), 480);
                QCOMPARE(n->offEvent()->midiTime(), 960);
            }
        }
        QCOMPARE(notes, 1);
        delete loaded;
        delete f;
        QFile::remove(path);
    }

    // ---- review R231-13: sysex framing compatibility in the real loader --------
    // (helpers smfWithOneTrack / loadShape live in the private section above -
    // moc rejects non-slot declarations inside "private slots")

    void loader_oldFramedSysexKeepsFollowingNotes() {
        // MidiEditor <= 2.3.0 wrote "F0 <data> F7" without a length. Manufacturer
        // id 01 with a longer payload: read as a length it would swallow one byte
        // and leave the parser inside the message, losing the note behind it.
        const QByteArray note = QByteArray::fromHex("60 903C64 60 803C00");
        LoadedShape s = loadShape(smfWithOneTrack(QByteArray::fromHex("00 F0 01 02 03 F7") + note));
        QCOMPARE(s.notes, 1);
        QVERIFY(s.notesPaired);
        QCOMPARE(s.sysex, QList<QByteArray>{QByteArray::fromHex("010203")});

        // The GM reset most files carry (id 7E) and an extended id (00 20 33).
        s = loadShape(smfWithOneTrack(QByteArray::fromHex("00 F0 7E 7F 09 01 F7") + note));
        QCOMPARE(s.notes, 1);
        QCOMPARE(s.sysex, QList<QByteArray>{QByteArray::fromHex("7E7F0901")});
        s = loadShape(smfWithOneTrack(QByteArray::fromHex("00 F0 00 20 33 01 02 F7") + note));
        QCOMPARE(s.notes, 1);
        QCOMPARE(s.sysex, QList<QByteArray>{QByteArray::fromHex("0020330102")});

        // 2.3.0 saved an empty sysex event as the bare "F0 F7".
        s = loadShape(smfWithOneTrack(QByteArray::fromHex("00 F0 F7") + note));
        QCOMPARE(s.notes, 1);
        QCOMPARE(s.sysex, QList<QByteArray>{QByteArray()});

        // Old framing directly followed by a text meta event: the look-ahead
        // reads the real terminator as a continuation status, but the length
        // that follows is the meta event's delta 0 - rejected, old framing.
        s = loadShape(smfWithOneTrack(QByteArray::fromHex("00 F0 01 02 03 F7  00 FF 01 03 616263") + note));
        QCOMPARE(s.notes, 1);
        QVERIFY(s.notesPaired);
        QCOMPARE(s.sysex, QList<QByteArray>{QByteArray::fromHex("010203")});

        // Old framing whose misread length ends one byte before the terminator:
        // the varlen swallows "F7 00", the meta event is skipped by the new loop
        // and the note's status byte then settles it - old framing.
        s = loadShape(smfWithOneTrack(QByteArray::fromHex("00 F0 03 01 02 03 F7  00 FF 01 03 616263") + note));
        QCOMPARE(s.notes, 1);
        QVERIFY(s.notesPaired);
        QCOMPARE(s.sysex, QList<QByteArray>{QByteArray::fromHex("03010203")});

        // Old framing whose first byte counts the payload bytes after it but not
        // the terminator: the misread chunk stops right before F7, the varlen of
        // the look-ahead swallows F7 plus the delta, the note's status settles
        // it - old framing, full payload.
        s = loadShape(smfWithOneTrack(QByteArray::fromHex("00 F0 02 0A 0B F7") + note));
        QCOMPARE(s.notes, 1);
        QVERIFY(s.notesPaired);
        QCOMPARE(s.sysex, QList<QByteArray>{QByteArray::fromHex("020A0B")});

        // Documented ambiguity: an old-framed message whose first byte equals
        // the count of the bytes that follow it INCLUDING the terminator is
        // byte-identical to a standard terminated packet and is read as one
        // (payload without that first byte).
        s = loadShape(smfWithOneTrack(QByteArray::fromHex("00 F0 03 0A 0B F7") + note));
        QCOMPARE(s.notes, 1);
        QCOMPARE(s.sysex, QList<QByteArray>{QByteArray::fromHex("0A0B")});
    }

    void loader_standardSysexFramingStillLoads() {
        const QByteArray note = QByteArray::fromHex("60 903C64 60 803C00");
        // Length-framed, terminated (what 2.3.1 writes).
        LoadedShape s = loadShape(smfWithOneTrack(QByteArray::fromHex("00 F0 05 7E 7F 09 01 F7") + note));
        QCOMPARE(s.notes, 1);
        QCOMPARE(s.sysex, QList<QByteArray>{QByteArray::fromHex("7E7F0901")});
        // Multi-packet dump: first packet without F7, continuation as F7 escape.
        s = loadShape(smfWithOneTrack(QByteArray::fromHex("00 F0 03 41 10 42  0A F7 04 12 40 00 F7") + note));
        QCOMPARE(s.notes, 1);
        QVERIFY(s.notesPaired);
        QCOMPARE(s.sysex, QList<QByteArray>{QByteArray::fromHex("411042")});
        // The same dump with a text meta event between the packets (allowed by
        // the SMF spec - meta events are not MIDI data) must read identically.
        s = loadShape(smfWithOneTrack(QByteArray::fromHex("00 F0 03 41 10 42  00 FF 01 01 58  0A F7 04 12 40 00 F7") + note));
        QCOMPARE(s.notes, 1);
        QVERIFY(s.notesPaired);
        QCOMPARE(s.sysex, QList<QByteArray>{QByteArray::fromHex("411042")});
    }

    // ---- review R231-11 core rule: snapshot + later per-event item ----------

    void undo_channelSnapshotThenPerEventShift_redoLeavesNoDuplicates() {
        // A protocolled removeEvent snapshots the channel; a later protocolled
        // setMidiTime on another note of the same channel must survive
        // undo -> redo with that note exactly once at its new position.
        MidiFile *f = makeFile("Piano", 1);
        MidiTrack *t = f->track(1);
        auto makeNote = [&](int pitch, int start, int end) {
            NoteOnEvent *n = new NoteOnEvent(pitch, 100, 1, t);
            OffEvent *o = new OffEvent(1, 127 - pitch, t);
            n->setFile(f);
            o->setFile(f);
            n->setMidiTime(start, false);
            o->setMidiTime(end, false);
            return n;
        };
        NoteOnEvent *a = makeNote(60, 0, 100);
        NoteOnEvent *n = makeNote(62, 200, 400);
        auto occurrences = [&](MidiEvent *ev) {
            int c = 0;
            QMultiMap<int, MidiEvent *> *map = f->channel(1)->eventMap();
            for (auto it = map->begin(); it != map->end(); ++it)
                if (it.value() == ev) ++c;
            return c;
        };
        auto keyOf = [&](MidiEvent *ev) {
            QMultiMap<int, MidiEvent *> *map = f->channel(1)->eventMap();
            for (auto it = map->begin(); it != map->end(); ++it)
                if (it.value() == ev) return it.key();
            return -1;
        };

        f->protocol()->startNewAction("mixed");
        QVERIFY(f->channel(1)->removeEvent(a, true));  // channel snapshot
        n->setMidiTime(300, true);                       // per-event item after it
        f->protocol()->endAction();
        QCOMPARE(occurrences(a), 0);
        QCOMPARE(keyOf(n), 300);

        f->protocol()->undo();
        QCOMPARE(occurrences(a), 1);
        QCOMPARE(occurrences(n), 1);
        QCOMPARE(keyOf(n), 200);
        QCOMPARE(n->midiTime(), 200);

        f->protocol()->redo();
        QCOMPARE(occurrences(a), 0);
        QCOMPARE(occurrences(n), 1);
        QCOMPARE(keyOf(n), 300);
        QCOMPARE(n->midiTime(), 300);
        delete f;
    }
};

QTEST_GUILESS_MAIN(TestFfxivFixerResync)
#include "test_ffxiv_fixer_resync.moc"
