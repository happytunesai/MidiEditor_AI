// Tests for DrumKitPreset — the static factory that surfaces the three
// hard-coded percussion-grouping presets (General MIDI, Rock, Jazz) used
// by the drum-roll view to bucket channel-9 notes into named lanes.
//
// DrumKitPreset has no Qt parent / signal / GUI dependency — it's a plain
// data factory. The test compiles only that one .cpp.
//
// What we cover:
//   * presets() exposes exactly the three documented kits in the documented
//     order (any reorder would break the lane mapping users have configured).
//   * The General-MIDI preset matches the canonical GM percussion key map
//     well-known anchors: kick (35/36), acoustic snare (38), open hi-hat (46),
//     crash 1 (49), ride 1 (51).
//   * Every note number across every preset stays inside the GM percussion
//     range (35..81) — out-of-range numbers would either silently disappear
//     (no MIDI note plays at that pitch on a drum kit) or trigger an
//     unintended chromatic note.
//   * A note number cannot appear in two groups inside the same preset
//     (the lane bucket mapping is deterministic — duplicates would silently
//     drop notes from one of the two groups).

#include <QtTest/QtTest>

#include "DrumKitPreset.h"

#include <QSet>

class TestDrumKitPreset : public QObject {
    Q_OBJECT
private slots:
    void presets_returnsGmRockAndJazzInThatOrder();
    void gmPreset_carriesCanonicalGeneralMidiAnchors();
    void allPresetNotes_stayWithinGmPercussionRange();
    void notesAreUniqueWithinAPreset();
    void everyGroup_hasNonEmptyNameAndNoteList();
    void ffxivPreset_mapsGmDrumsToFfxivInstrumentTracks();
    void ffxivMapPresets_kitsAreSaneForBardRange();
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static bool groupContains(const DrumKitPreset& p, const QString& name, int note)
{
    for (const DrumGroup& g : p.groups) {
        if (g.name == name && g.noteNumbers.contains(note))
            return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

void TestDrumKitPreset::presets_returnsGmRockAndJazzInThatOrder()
{
    QList<DrumKitPreset> all = DrumKitPreset::presets();
    QCOMPARE(all.size(), 3);
    QCOMPARE(all[0].name, QString("General MIDI"));
    QCOMPARE(all[1].name, QString("Rock"));
    QCOMPARE(all[2].name, QString("Jazz"));
}

void TestDrumKitPreset::gmPreset_carriesCanonicalGeneralMidiAnchors()
{
    DrumKitPreset gm = DrumKitPreset::gmPreset();

    // Anchor notes per the General MIDI Level 1 percussion key map.
    QVERIFY2(groupContains(gm, "Bass Drum",    35), "GM acoustic bass drum");
    QVERIFY2(groupContains(gm, "Bass Drum",    36), "GM bass drum 1");
    QVERIFY2(groupContains(gm, "Snare",        38), "GM acoustic snare");
    QVERIFY2(groupContains(gm, "Hi-Hat",       42), "GM closed hi-hat");
    QVERIFY2(groupContains(gm, "Hi-Hat",       46), "GM open hi-hat");
    QVERIFY2(groupContains(gm, "Crash Cymbal", 49), "GM crash cymbal 1");
    QVERIFY2(groupContains(gm, "Ride Cymbal",  51), "GM ride cymbal 1");
}

void TestDrumKitPreset::allPresetNotes_stayWithinGmPercussionRange()
{
    // GM Level 1 percussion key map covers MIDI notes 35..81 inclusive. Any
    // entry outside that band is almost certainly a typo — at best it emits a
    // chromatic pitch on a drum-kit channel, at worst it's silent.
    for (const DrumKitPreset& p : DrumKitPreset::presets()) {
        for (const DrumGroup& g : p.groups) {
            for (int n : g.noteNumbers) {
                QVERIFY2(n >= 35 && n <= 81,
                         qPrintable(QString("Preset '%1' group '%2' has out-of-range note %3")
                                        .arg(p.name).arg(g.name).arg(n)));
            }
        }
    }
}

void TestDrumKitPreset::notesAreUniqueWithinAPreset()
{
    // A note that falls into two groups is a bug — the drum-roll view picks
    // one bucket and silently hides the note from the other.
    for (const DrumKitPreset& p : DrumKitPreset::presets()) {
        QSet<int> seen;
        for (const DrumGroup& g : p.groups) {
            for (int n : g.noteNumbers) {
                QVERIFY2(!seen.contains(n),
                         qPrintable(QString("Preset '%1' duplicates note %2 across groups")
                                        .arg(p.name).arg(n)));
                seen.insert(n);
            }
        }
    }
}

void TestDrumKitPreset::everyGroup_hasNonEmptyNameAndNoteList()
{
    for (const DrumKitPreset& p : DrumKitPreset::presets()) {
        QVERIFY2(!p.name.isEmpty(),
                 "preset name must not be empty");
        QVERIFY2(!p.groups.isEmpty(),
                 qPrintable(QString("preset '%1' has no groups").arg(p.name)));
        for (const DrumGroup& g : p.groups) {
            QVERIFY2(!g.name.isEmpty(),
                     qPrintable(QString("preset '%1' has unnamed group").arg(p.name)));
            QVERIFY2(!g.noteNumbers.isEmpty(),
                     qPrintable(QString("preset '%1' group '%2' is empty")
                                    .arg(p.name).arg(g.name)));
        }
    }
}

void TestDrumKitPreset::ffxivPreset_mapsGmDrumsToFfxivInstrumentTracks()
{
    DrumKitPreset fx = DrumKitPreset::ffxivPreset();

    // The four playable FFXIV percussion track names MUST match the keys in
    // FluidSynthEngine::drumProgramForTrackName() exactly, or the name-keyed
    // program-change injection won't fire and a cosmetic CH9 split would play
    // as raw GM drums. (Names hard-asserted here as the contract.)
    QVERIFY2(groupContains(fx, "Bass Drum",  35), "kick -> Bass Drum");
    QVERIFY2(groupContains(fx, "Bass Drum",  36), "kick -> Bass Drum");
    QVERIFY2(groupContains(fx, "Bass Drum",  41), "tom -> Bass Drum (FFXIV has no tom)");
    QVERIFY2(groupContains(fx, "Bass Drum",  50), "high tom -> Bass Drum");
    QVERIFY2(groupContains(fx, "Snare Drum", 38), "acoustic snare -> Snare Drum");
    QVERIFY2(groupContains(fx, "Snare Drum", 40), "electric snare -> Snare Drum");
    QVERIFY2(groupContains(fx, "Cymbal",     42), "closed hi-hat -> Cymbal");
    QVERIFY2(groupContains(fx, "Cymbal",     46), "open hi-hat -> Cymbal");
    QVERIFY2(groupContains(fx, "Cymbal",     49), "crash -> Cymbal");
    QVERIFY2(groupContains(fx, "Cymbal",     51), "ride -> Cymbal");
    QVERIFY2(groupContains(fx, "Bongo",      60), "high bongo -> Bongo");
    QVERIFY2(groupContains(fx, "Bongo",      64), "low conga -> Bongo");
    QVERIFY2(groupContains(fx, "Other Percussion", 76), "woodblock -> Other Percussion");
    QVERIFY2(groupContains(fx, "Other Percussion", 80), "triangle -> Other Percussion");

    // Toms must NOT be routed to a "Timpani" track - Timpani is tonal and is
    // handled separately by the channel fixer, not by the percussion split.
    for (const DrumGroup& g : fx.groups)
        QVERIFY2(g.name != QString("Timpani"),
                 "ffxivPreset must not create a Timpani drum group");

    // No GM drum note in two groups, and every note 35..81 is covered so the
    // split never silently drops a percussion hit.
    QSet<int> seen;
    for (const DrumGroup& g : fx.groups) {
        QVERIFY2(!g.name.isEmpty(), "ffxiv group name must not be empty");
        for (int n : g.noteNumbers) {
            QVERIFY2(n >= 35 && n <= 81,
                     qPrintable(QString("ffxiv group '%1' note %2 out of GM range")
                                    .arg(g.name).arg(n)));
            QVERIFY2(!seen.contains(n),
                     qPrintable(QString("ffxiv preset duplicates note %1").arg(n)));
            seen.insert(n);
        }
    }
    for (int n = 35; n <= 81; ++n)
        QVERIFY2(seen.contains(n),
                 qPrintable(QString("ffxiv preset does not cover GM note %1").arg(n)));
}

void TestDrumKitPreset::ffxivMapPresets_kitsAreSaneForBardRange()
{
    const QList<FfxivDrumMapPreset> kits = FfxivDrumMapPreset::presets();

    // The house kit first, then the four community kits, in this order.
    QCOMPARE(kits.size(), 5);
    QCOMPARE(kits[0].name, QString("MidiEditor AI (Happy Tunes)"));
    QCOMPARE(kits[1].name, QString("Mog Amp"));
    QCOMPARE(kits[2].name, QString("Bard Forge 1"));
    QCOMPARE(kits[3].name, QString("Bard Forge 2"));
    QCOMPARE(kits[4].name, QString("Bard Metal"));

    // House-kit anchors (calibrated against real before/after edits): BOTH
    // kicks anchor on C4, BOTH snares on C5, toms flat +24, crash convention
    // low-crash->C5 / china->F#5 / high-crash->C6. Hi-hats/rides stay
    // unmapped on purpose (they fall through to "Other Percussion" untouched
    // - which cymbal goes to which zone is a per-song editor decision).
    {
        const FfxivDrumMapPreset &house = kits[0];
        QSet<int> houseSources;
        for (const FfxivDrumMapGroup &g : house.groups)
            for (const FfxivDrumNoteMap &m : g.mappings)
                houseSources.insert(m.sourceNote);
        auto target = [&house](int src) {
            for (const FfxivDrumMapGroup &g : house.groups)
                for (const FfxivDrumNoteMap &m : g.mappings)
                    if (m.sourceNote == src) return m.targetNote;
            return -1;
        };
        QCOMPARE(target(35), 60); // kick anchor: C4
        QCOMPARE(target(36), 60); // kick anchor: C4 (both kicks)
        QCOMPARE(target(45), 69); // tom +24
        QCOMPARE(target(38), 72); // snare anchor: C5
        QCOMPARE(target(40), 72); // snare anchor: C5 (both snares)
        QCOMPARE(target(49), 72); // low crash -> C5
        QCOMPARE(target(52), 78); // china -> F#5
        QCOMPARE(target(57), 84); // high crash -> C6
        QCOMPARE(target(60), 70); // hi bongo (Bard Metal hand-perc block)
        QCOMPARE(target(64), 74); // low conga
        QVERIFY2(!houseSources.contains(42) && !houseSources.contains(46)
                     && !houseSources.contains(51),
                 "hi-hats and rides must stay unmapped (editor's call per song)");
    }

    // Track names must match FluidSynthEngine::drumProgramForTrackName()'s
    // keys, and the programs must be OUR FF14 SoundFont presets - meow's
    // originals used 96-99 for ITS SoundFont; a blind port would be silent.
    const QHash<QString, int> expectedProgram = {
        {"Bass Drum", 117}, {"Snare Drum", 118}, {"Cymbal", 119}, {"Bongo", 116},
    };

    for (const FfxivDrumMapPreset& kit : kits) {
        QSet<int> seenSources;
        for (const FfxivDrumMapGroup& g : kit.groups) {
            QVERIFY2(expectedProgram.contains(g.trackName),
                     qPrintable(QString("kit '%1' has unknown group '%2'")
                                    .arg(kit.name).arg(g.trackName)));
            QCOMPARE(g.programNumber, expectedProgram.value(g.trackName));
            QVERIFY2(!g.mappings.isEmpty(),
                     qPrintable(QString("kit '%1' group '%2' has no mappings")
                                    .arg(kit.name).arg(g.trackName)));
            for (const FfxivDrumNoteMap& m : g.mappings) {
                QVERIFY2(m.sourceNote >= 35 && m.sourceNote <= 81,
                         qPrintable(QString("kit '%1' source %2 outside GM drum range")
                                        .arg(kit.name).arg(m.sourceNote)));
                // FFXIV bard instruments play C3-C6 (MIDI 48-84); a target
                // outside that range would be silent in game.
                QVERIFY2(m.targetNote >= 48 && m.targetNote <= 84,
                         qPrintable(QString("kit '%1' target %2 outside bard range C3-C6")
                                        .arg(kit.name).arg(m.targetNote)));
                QVERIFY2(!seenSources.contains(m.sourceNote),
                         qPrintable(QString("kit '%1' maps GM note %2 twice")
                                        .arg(kit.name).arg(m.sourceNote)));
                seenSources.insert(m.sourceNote);
            }
        }
    }
}

QTEST_APPLESS_MAIN(TestDrumKitPreset)
#include "test_drum_kit_preset.moc"
