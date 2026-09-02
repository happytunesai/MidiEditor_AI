/*
 * test_gp_to_native
 *
 * Regression tests for the GuitarPro -> native-MIDI conversion math in
 * src/converter/GuitarPro/GpToNative.cpp, driven through the public
 * NativeTrack::getMidi() entry point (the volume-fade interpolation itself
 * is private).
 *
 * GP-01 (full-stack review 2026-07-02): createVolumeChanges stepped its
 * interpolation loop by `duration / 20` - integer division, so ANY faded
 * note shorter than 20 ticks (e.g. a staccato 128th: 30/2 = 15) made the
 * increment 0 and the loop never terminated, hanging the import and growing
 * the change list until OOM. The fix clamps the step to >= 1 tick. This
 * test would HANG without the fix, which is exactly the regression signal
 * we want (QtTest's watchdog / CI timeout turns that into a failure).
 *
 * F033 (review 2026-09): Guitar Pro addresses MIDI channels as a flat 0..63
 * table (4 ports x 16 channels). Folding that slot to 0..15 let a port-2
 * track land on the same GM channel as a port-1 track (patch and pitch-bend
 * collisions). NativeFormat now routes every melodic track onto its own
 * channel while the 15 melodic channels last, keeps percussion on 9, and
 * shares only when the file has more than 15 melodic tracks - reporting the
 * sharing through NativeFormat::importWarnings(). The routing tests below
 * drive the full NativeFormat -> GpMidiExport path on a programmatic GpFile.
 */

#include <QtTest/QtTest>
#include <QObject>

#include "../src/converter/GuitarPro/GpToNative.h"

#include <set>
#include <string>
#include <vector>

// Pre-router output of singlePort_distinctChannels_byteIdentical's fixture,
// captured from the same test before the channel router existed.
static const char GOLDEN_SINGLE_PORT_HEX[] =
    "4d546864000000060001000503c04d54726b0000008800ff0308756e7469746c656400ff01044630333300ff010000ff"
    "010000ff010000ff010000ff010000ff0218436f7079726967687420323031372062792047697461726f00ff06224630"
    "3333202f20202d20436f7079726967687420323031372062792047697461726f00ff21010000ff5902000000ff580404"
    "02180800ff510307a12000ff2f004d54726b0000001f00ff21010000ff030647756974617200c01b0090455f87408045"
    "0000ff2f004d54726b0000001d00ff21010000ff03044261737300c2210092455f874082450000ff2f004d54726b0000"
    "001e00ff21010000ff03054472756d7300c9000099455f874089450000ff2f004d54726b0000001d00ff21010000ff03"
    "044b65797300c4040094455f874084450000ff2f00";

class TestGpToNative : public QObject {
    Q_OBJECT

private:
    // Builds a one-note track with the given duration/fading and runs the
    // full public conversion. Returns the produced MIDI track (never null).
    static std::unique_ptr<GpMidiTrack> convertSingleNote(int duration, Fading fading) {
        NativeTrack track;
        track.channel = 0;
        track.patch = 24;
        track.name = "T";

        NativeNote n;
        n.index = 0;
        n.duration = duration;
        n.fading = fading;
        n.fret = 5;
        n.str = 1;
        n.velocity = 90;
        track.notes.push_back(n);

        bool channels[16];
        for (int i = 0; i < 16; ++i) channels[i] = true;
        return track.getMidi(channels);
    }

    // ---- F033 fixture: a GP5-shaped song built in memory --------------------

    struct TrackSpec {
        std::string name;
        int slot;          // flat 0..63 GP channel-table index
        int effectSlot;    // flat 0..63 effect channel index
        int patch;
    };

    // One 4/4 measure at 120 bpm; every track plays a single quarter note on
    // string 1 fret 5 so the bytes exercise program_change/note_on/note_off.
    static std::unique_ptr<GpFile> makeSong(const std::vector<TrackSpec>& specs) {
        auto song = std::make_unique<GpFile>();
        song->versionTuple[0] = 5;
        song->versionTuple[1] = 0;
        song->tempo = 120;
        song->title = "F033";

        auto header = std::make_unique<MeasureHeader>();
        header->number = 1;
        header->song = song.get();
        song->measureHeaders.push_back(std::move(header));
        MeasureHeader* h = song->measureHeaders.back().get();

        int number = 1;
        for (const auto& spec : specs) {
            auto track = std::make_unique<GpTrack>(song.get(), number++);
            track->name = spec.name;
            track->channel.channel = spec.slot;
            track->channel.effectChannel = spec.effectSlot;
            track->channel.instrument = spec.patch;
            track->isPercussionTrack = (spec.slot % 16) == 9;
            const int tuning[6] = {64, 59, 55, 50, 45, 40};
            for (int s = 0; s < 6; ++s) track->strings.push_back(GuitarString(s + 1, tuning[s]));

            auto measure = std::make_unique<GpMeasure>(track.get(), h);
            auto beat = std::make_unique<GpBeat>();
            beat->voice = measure->voices[0].get();
            beat->duration.value = Duration::quarter;
            auto note = std::make_unique<GpNote>(beat.get());
            note->type = NoteType::normal;
            note->str = 1;
            note->value = 5;
            note->velocity = 95;
            beat->notes.push_back(std::move(note));
            measure->voices[0]->beats.push_back(std::move(beat));
            track->addMeasure(std::move(measure));

            song->tracks.push_back(std::move(track));
        }
        song->trackCount = static_cast<int>(song->tracks.size());
        song->measureCount = 1;
        return song;
    }

    // Channel of the first program_change in a converted track (-1 if none).
    static int trackChannel(const GpMidiTrack& track) {
        for (const auto& msg : track.messages) {
            if (msg && msg->type == "program_change") return msg->channel;
        }
        return -1;
    }

    static QByteArray toHex(const std::vector<uint8_t>& bytes) {
        return QByteArray(reinterpret_cast<const char*>(bytes.data()),
                          static_cast<int>(bytes.size())).toHex();
    }

private slots:
    void fadedShortNotes_terminate() {
        // The GP-01 hang window was duration 1..19 (duration/20 == 0). A
        // representative sweep incl. both fade types and the swell must
        // TERMINATE and produce a bounded number of messages.
        const Fading fades[] = {Fading::FadeIn, Fading::FadeOut, Fading::VolumeSwell};
        const int durations[] = {1, 5, 15, 19};
        for (Fading f : fades) {
            for (int d : durations) {
                auto midi = convertSingleNote(d, f);
                QVERIFY(midi != nullptr);
                // A d-tick fade can never legitimately emit more than d
                // interpolation points (plus note on/off + bookkeeping).
                QVERIFY2(midi->messages.size() <= static_cast<size_t>(d) + 16,
                         qPrintable(QString("fade dur=%1 produced %2 messages")
                                        .arg(d).arg(midi->messages.size())));
            }
        }
    }

    void fadedNormalNotes_stillInterpolate() {
        // Regression guard for the fix itself: a normal-length fade must
        // still produce the ~20-segment interpolation it always did.
        auto midi = convertSingleNote(960, Fading::FadeIn);
        QVERIFY(midi != nullptr);
        int volumeChanges = 0;
        for (const auto &msg : midi->messages) {
            if (msg && msg->type == "control_change") volumeChanges++;
        }
        QVERIFY2(volumeChanges >= 15,
                 qPrintable(QString("expected ~20 fade steps, got %1").arg(volumeChanges)));
    }

    void unfadedNote_unaffected() {
        auto midi = convertSingleNote(15, Fading::None);
        QVERIFY(midi != nullptr);
    }

    // ---- F033: GM channel router --------------------------------------------

    void singlePort_distinctChannels_byteIdentical() {
        // A port-1 file whose tracks already sit on distinct channels must
        // come out exactly as before the router existed: every track keeps
        // its own channel and the byte stream is pinned to the pre-router
        // output captured from the same fixture.
        auto song = makeSong({
            {"Guitar", 0, 1, 27},
            {"Bass", 2, 3, 33},
            {"Drums", 9, 9, 0},
            {"Keys", 4, 5, 4},
        });
        NativeFormat format(song.get());
        GpMidiExport mid = format.toMidi();
        QVERIFY(format.importWarnings().empty());
        QCOMPARE(mid.midiTracks.size(), static_cast<size_t>(5));
        QCOMPARE(trackChannel(*mid.midiTracks[1]), 0);
        QCOMPARE(trackChannel(*mid.midiTracks[2]), 2);
        QCOMPARE(trackChannel(*mid.midiTracks[3]), 9);
        QCOMPARE(trackChannel(*mid.midiTracks[4]), 4);

        QCOMPARE(toHex(mid.createBytes()), QByteArray(GOLDEN_SINGLE_PORT_HEX));
    }

    void secondPort_tracksGetDistinctChannels() {
        // Port-2 slots 16/18/20/22 fold onto 0/2/4/6 - the port-1 channels.
        // With 8 melodic tracks (<= 15) no two may share a channel, both drum
        // tracks stay on 9, and the re-routed tracks avoid the port-1 effect
        // channels (1/3/5/7) so bends keep their pairing.
        auto song = makeSong({
            {"Gtr 1", 0, 1, 27},
            {"Gtr 2", 2, 3, 27},
            {"Bass", 4, 5, 33},
            {"Keys", 6, 7, 4},
            {"Drums", 9, 9, 0},
            {"Gtr 3 (port 2)", 16, 17, 29},
            {"Gtr 4 (port 2)", 18, 19, 29},
            {"Strings (port 2)", 20, 21, 48},
            {"Choir (port 2)", 22, 23, 52},
            {"Perc (port 2)", 25, 25, 0},
        });
        NativeFormat format(song.get());
        GpMidiExport mid = format.toMidi();
        QVERIFY2(format.importWarnings().empty(),
                 format.importWarnings().empty() ? "" : format.importWarnings().front().c_str());
        QCOMPARE(mid.midiTracks.size(), static_cast<size_t>(11));

        std::set<int> melodic;
        for (size_t i = 1; i < mid.midiTracks.size(); ++i) {
            const int ch = trackChannel(*mid.midiTracks[i]);
            QVERIFY(ch >= 0 && ch < 16);
            const bool drums = (i == 5 || i == 10);
            if (drums) {
                QCOMPARE(ch, 9);
                continue;
            }
            QVERIFY(ch != 9);
            QVERIFY2(melodic.insert(ch).second,
                     qPrintable(QString("track %1 shares channel %2").arg(i).arg(ch)));
        }
        // Port-1 tracks keep their own channels.
        QCOMPARE(trackChannel(*mid.midiTracks[1]), 0);
        QCOMPARE(trackChannel(*mid.midiTracks[2]), 2);
        QCOMPARE(trackChannel(*mid.midiTracks[3]), 4);
        QCOMPARE(trackChannel(*mid.midiTracks[4]), 6);
        // Port-2 tracks are placed on channels nobody declared as an effect
        // channel first (8, 10, 11, 12), leaving 1/3/5/7 for the bends.
        QCOMPARE(trackChannel(*mid.midiTracks[6]), 8);
        QCOMPARE(trackChannel(*mid.midiTracks[7]), 10);
        QCOMPARE(trackChannel(*mid.midiTracks[8]), 11);
        QCOMPARE(trackChannel(*mid.midiTracks[9]), 12);
    }

    void moreThanFifteenMelodic_sharesLeastLoadedAndWarns() {
        // 15 melodic tracks fill 0-8/10-15; the 16th and 17th (port 2) must
        // share deterministically (least-loaded, own channel preferred on a
        // tie) and each sharing is reported once.
        std::vector<TrackSpec> specs;
        for (int ch = 0; ch < 16; ++ch) {
            if (ch == 9) continue;
            specs.push_back({"T" + std::to_string(ch), ch, ch, 27});
        }
        specs.push_back({"Extra A", 16, 17, 30});   // folds to 0
        specs.push_back({"Extra B", 18, 19, 30});   // folds to 2
        auto song = makeSong(specs);
        NativeFormat format(song.get());
        GpMidiExport mid = format.toMidi();
        QCOMPARE(mid.midiTracks.size(), static_cast<size_t>(18));

        // The first 15 keep their own channels.
        int idx = 1;
        for (int ch = 0; ch < 16; ++ch) {
            if (ch == 9) continue;
            QCOMPARE(trackChannel(*mid.midiTracks[idx++]), ch);
        }
        QCOMPARE(trackChannel(*mid.midiTracks[16]), 0);
        QCOMPARE(trackChannel(*mid.midiTracks[17]), 2);

        const auto& warnings = format.importWarnings();
        QCOMPARE(warnings.size(), static_cast<size_t>(2));
        QVERIFY2(warnings[0].find("Extra A") != std::string::npos &&
                 warnings[0].find("T0") != std::string::npos,
                 warnings[0].c_str());
        QVERIFY2(warnings[1].find("Extra B") != std::string::npos &&
                 warnings[1].find("T2") != std::string::npos,
                 warnings[1].c_str());

        // Same input, same routing: the fallback must be deterministic.
        NativeFormat again(song.get());
        GpMidiExport mid2 = again.toMidi();
        QCOMPARE(toHex(mid2.createBytes()), toHex(mid.createBytes()));
    }
};

QTEST_APPLESS_MAIN(TestGpToNative)
#include "test_gp_to_native.moc"
