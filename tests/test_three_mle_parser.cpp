// Tests for ThreeMleParser — converts a 3MLE-format INI text (used by the
// FFXIV bard community for multi-channel MML scores) into an MmlSong.
//
// 3MLE files look like:
//
//    [Settings]
//    Title=Some Song
//    Tempo=140
//
//    [Channel1]
//    cdefgab
//
//    [Channel2]
//    o3 cccc
//
// ThreeMleParser walks the text section by section, extracts settings, then
// hands each channel block to MmlLexer + MmlParser (already covered by
// test_mml_converter). What's left here is the INI plumbing:
//   * section header detection
//   * Settings key/value extraction (case-insensitive keys)
//   * channel number parsing from "[Channel<N>]"
//   * tempo bounds
//   * track naming with title prefix
//   * channel mapping: 1-based → (n-1) % 16
//   * comment / blank-line skipping

#include <QtTest/QtTest>

#include "ThreeMleParser.h"
#include "MmlModels.h"

class TestThreeMleParser : public QObject {
    Q_OBJECT
private slots:
    void emptyInput_returnsSongWithDefaultsAndNoTracks();
    void settingsSection_overridesTempoAndTitle();
    void tempoOutOfRange_fallsBackToDefault120();
    void singleChannelBlock_producesOneTrackOnChannelZero();
    void channelNumber_isMappedOneBasedToZeroBased();
    void titleSetting_appearsInEachTrackNameAsTitleDashChN();
    void noTitle_setsTrackNameToChannelLabel();
    void commentsAndBlankLines_areIgnored();
    void multipleChannelsAcrossSections_areMergedAndSortedByNumber();
    void emptyChannelBlock_doesNotProduceTrack();
};

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

void TestThreeMleParser::emptyInput_returnsSongWithDefaultsAndNoTracks()
{
    MmlSong song = ThreeMleParser::parse(QString(), 480);
    QCOMPARE(song.tempo, 120);
    QCOMPARE(song.ticksPerQuarter, 480);
    QCOMPARE(song.tracks.size(), 0);
}

void TestThreeMleParser::settingsSection_overridesTempoAndTitle()
{
    QString src =
        "[Settings]\n"
        "Tempo=140\n"
        "Title=Hymn\n"
        "[Channel1]\n"
        "c\n";

    MmlSong song = ThreeMleParser::parse(src, 480);
    QCOMPARE(song.tempo, 140);
    QCOMPARE(song.tracks.size(), 1);
    QCOMPARE(song.tracks[0].name, QString("Hymn - Ch1"));
}

void TestThreeMleParser::tempoOutOfRange_fallsBackToDefault120()
{
    // Tempo guard: only 1..999 is accepted.
    MmlSong tooLow = ThreeMleParser::parse(
        "[Settings]\nTempo=0\n[Channel1]\nc\n", 480);
    QCOMPARE(tooLow.tempo, 120);

    MmlSong tooHigh = ThreeMleParser::parse(
        "[Settings]\nTempo=1000\n[Channel1]\nc\n", 480);
    QCOMPARE(tooHigh.tempo, 120);

    MmlSong negative = ThreeMleParser::parse(
        "[Settings]\nTempo=-5\n[Channel1]\nc\n", 480);
    QCOMPARE(negative.tempo, 120);
}

void TestThreeMleParser::singleChannelBlock_producesOneTrackOnChannelZero()
{
    MmlSong song = ThreeMleParser::parse("[Channel1]\ncde\n", 480);
    QCOMPARE(song.tracks.size(), 1);
    QCOMPARE(song.tracks[0].channel, 0); // 1-based 1 → 0-based 0
    QVERIFY(!song.tracks[0].notes.isEmpty());
}

void TestThreeMleParser::channelNumber_isMappedOneBasedToZeroBased()
{
    QString src =
        "[Channel3]\n"
        "c\n"
        "[Channel17]\n"  // wraps via % 16
        "c\n";

    MmlSong song = ThreeMleParser::parse(src, 480);
    QCOMPARE(song.tracks.size(), 2);

    // Channel3 → (3-1) % 16 = 2; Channel17 → (17-1) % 16 = 0.
    // The parser sorts by 1-based channel ascending, so [Channel3] comes first.
    QCOMPARE(song.tracks[0].channel, 2);
    QCOMPARE(song.tracks[1].channel, 0);
}

void TestThreeMleParser::titleSetting_appearsInEachTrackNameAsTitleDashChN()
{
    QString src =
        "[Settings]\n"
        "Title=Bardsong\n"
        "[Channel2]\n"
        "c\n"
        "[Channel5]\n"
        "g\n";

    MmlSong song = ThreeMleParser::parse(src, 480);
    QCOMPARE(song.tracks.size(), 2);
    QCOMPARE(song.tracks[0].name, QString("Bardsong - Ch2"));
    QCOMPARE(song.tracks[1].name, QString("Bardsong - Ch5"));
}

void TestThreeMleParser::noTitle_setsTrackNameToChannelLabel()
{
    MmlSong song = ThreeMleParser::parse("[Channel4]\nc\n", 480);
    QCOMPARE(song.tracks.size(), 1);
    QCOMPARE(song.tracks[0].name, QString("Channel 4"));
}

void TestThreeMleParser::commentsAndBlankLines_areIgnored()
{
    // // is the 3MLE comment marker.
    QString src =
        "// header line\n"
        "\n"
        "[Settings]\n"
        "// inline comment\n"
        "Tempo=90\n"
        "\n"
        "[Channel1]\n"
        "// channel intro comment\n"
        "c\n";

    MmlSong song = ThreeMleParser::parse(src, 480);
    QCOMPARE(song.tempo, 90);
    QCOMPARE(song.tracks.size(), 1);
}

void TestThreeMleParser::multipleChannelsAcrossSections_areMergedAndSortedByNumber()
{
    // Same channel split across two [ChannelN] re-entries: parser must
    // accumulate the MML, not reset it. Distinct channels should appear
    // in ascending channel-number order in the output.
    QString src =
        "[Channel2]\n"
        "c\n"
        "[Channel1]\n"
        "d\n"
        "[Channel2]\n"
        "e\n";

    MmlSong song = ThreeMleParser::parse(src, 480);
    QCOMPARE(song.tracks.size(), 2);
    QCOMPARE(song.tracks[0].channel, 0); // Channel1 sorts first
    QCOMPARE(song.tracks[1].channel, 1); // Channel2 second
    // The accumulated [Channel2] body is "c e" → 2 notes.
    QCOMPARE(song.tracks[1].notes.size(), 2);
}

void TestThreeMleParser::emptyChannelBlock_doesNotProduceTrack()
{
    // [Channel3] declared but empty; only [Channel1] has content.
    QString src =
        "[Channel3]\n"
        "[Channel1]\n"
        "c\n";

    MmlSong song = ThreeMleParser::parse(src, 480);
    QCOMPARE(song.tracks.size(), 1);
    QCOMPARE(song.tracks[0].channel, 0);
}

QTEST_APPLESS_MAIN(TestThreeMleParser)
#include "test_three_mle_parser.moc"
