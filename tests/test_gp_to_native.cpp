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
 */

#include <QtTest/QtTest>
#include <QObject>

#include "../src/converter/GuitarPro/GpToNative.h"

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
};

QTEST_APPLESS_MAIN(TestGpToNative)
#include "test_gp_to_native.moc"
