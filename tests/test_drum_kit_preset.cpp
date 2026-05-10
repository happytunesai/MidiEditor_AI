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

QTEST_APPLESS_MAIN(TestDrumKitPreset)
#include "test_drum_kit_preset.moc"
