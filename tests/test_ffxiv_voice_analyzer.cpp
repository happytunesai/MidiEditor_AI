/*
 * test_ffxiv_voice_analyzer
 *
 * Validates the pure-data voice-load + rate detection logic in
 * src/ai/FfxivVoiceLoadCore (Phase 32.4). The core has zero MidiFile /
 * Qt GUI dependencies, so we link only its single .cpp.
 */

#include <QtTest/QtTest>
#include <QObject>

#include "../src/ai/FfxivVoiceLoadCore.h"

using namespace FfxivVoiceLoad;

// Fixed tempo: 480 ticks per quarter note at 120 BPM.
// 1 beat = 500 ms => 1 tick = 500/480 ms ≈ 1.0417 ms.
static const auto kMsAtTickStandard = [](int tick) {
    return int(double(tick) * 500.0 / 480.0);
};

class TestFfxivVoiceAnalyzer : public QObject
{
    Q_OBJECT
private slots:
    void emptyNoteListProducesZeroPeak();
    void monophonicLineStaysAtOne();
    void seventeenNoteChordTriggersOverflow();
    void noteRateHotspotDetected();
    void simultaneousChordOnDifferentChannels();
};

void TestFfxivVoiceAnalyzer::emptyNoteListProducesZeroPeak()
{
    auto r = FfxivVoiceLoad::computeFromNotes({}, kMsAtTickStandard);
    QCOMPARE(r.globalPeak, 0);
    QCOMPARE(r.overflowEvents, 0);
    QVERIFY(r.rateHotspots.isEmpty());
    QVERIFY(r.valid);
}

void TestFfxivVoiceAnalyzer::monophonicLineStaysAtOne()
{
    QVector<NoteSpan> notes;
    for (int i = 0; i < 8; ++i)
        notes.push_back({0, i * 480, (i + 1) * 480});

    auto r = FfxivVoiceLoad::computeFromNotes(notes, kMsAtTickStandard);
    QCOMPARE(r.globalPeak, 1);
    QCOMPARE(r.overflowEvents, 0);
}

void TestFfxivVoiceAnalyzer::seventeenNoteChordTriggersOverflow()
{
    // 17 simultaneous notes spread across multiple channels (>16 ceiling).
    QVector<NoteSpan> notes;
    for (int i = 0; i < 17; ++i)
        notes.push_back({i % 16, 0, 480});

    auto r = FfxivVoiceLoad::computeFromNotes(notes, kMsAtTickStandard);
    QCOMPARE(r.globalPeak, 17);
    QCOMPARE(r.overflowEvents, 1); // only the 17th NoteOn overflows
}

void TestFfxivVoiceAnalyzer::noteRateHotspotDetected()
{
    // 32 NoteOns on channel 3 inside 250 ms (well above the 14-note limit).
    // 250 ms => 240 ticks at the standard fixture rate, so each note 8 ticks.
    QVector<NoteSpan> notes;
    for (int i = 0; i < 32; ++i)
        notes.push_back({3, i * 8, i * 8 + 4});

    auto r = FfxivVoiceLoad::computeFromNotes(notes, kMsAtTickStandard);
    QVERIFY(!r.rateHotspots.isEmpty());
    QCOMPARE(r.rateHotspots.first().channel, 3);
    QVERIFY(r.rateHotspots.first().notesInWindow > kNoteRateCeilingPerChannel);
}

void TestFfxivVoiceAnalyzer::simultaneousChordOnDifferentChannels()
{
    // 16 notes across 16 channels at the same tick: peak == 16, no overflow.
    QVector<NoteSpan> notes;
    for (int ch = 0; ch < 16; ++ch)
        notes.push_back({ch, 100, 200});

    auto r = FfxivVoiceLoad::computeFromNotes(notes, kMsAtTickStandard);
    QCOMPARE(r.globalPeak, 16);
    QCOMPARE(r.overflowEvents, 0);
}

QTEST_APPLESS_MAIN(TestFfxivVoiceAnalyzer)
#include "test_ffxiv_voice_analyzer.moc"
