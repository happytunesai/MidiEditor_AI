/*
 * test_tempo_map_thinner (Phase 49, v2.3)
 *
 * Pins TempoMapThinner::thin(): a dense DAW-exported tempo ramp is reduced to
 * the events that matter for timing. Contract to cover (see roadmap Phase 49):
 *   1. A ramp file thins under the ms-drift tolerance; end time preserved
 *      to < 1 ms and note ms positions within tolerance.
 *   2. Idempotent - a second run removes nothing.
 *   3. The tick-0 anchor always survives.
 *   4. One protocol action; undo restores every event.
 *   5. dryRun analyses without touching the file.
 */

#include <QtTest/QtTest>
#include <QObject>

#include "../src/midi/TempoMapThinner.h"
#include "../src/midi/MidiFile.h"

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

class TestTempoMapThinner : public QObject {
    Q_OBJECT

private slots:
    // Skeleton case - replaced by the Phase 49 implementation's tests.
    void skeletonRefusesNullFile() {
        TempoMapThinner::Result r = TempoMapThinner::thin(nullptr);
        QVERIFY(!r.ok);
        QVERIFY(!r.error.isEmpty());
    }
};

QTEST_APPLESS_MAIN(TestTempoMapThinner)
#include "test_tempo_map_thinner.moc"
