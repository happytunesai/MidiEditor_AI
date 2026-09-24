/*
 * test_document_timing (Phase 51 / 51.2, v2.5.0)
 *
 * The MidiFile side of the document tools:
 *   1. new_document's resolution - initTicksPerQuarter() sets the ticks per
 *      quarter note of a NEW, empty document and survives a save/reopen; it
 *      is refused once anything sits after tick 0 (that would retime music)
 *      and outside 1-32767.
 *   2. 51.2 exact tempo - a tempo whose BPM is fractional keeps its exact
 *      microseconds-per-quarter value through setMicrosPerQuarter(), save and
 *      reopen (what set_tempo writes and get_timing_map reads).
 *   3. A save that cannot be written returns false and leaves the document
 *      dirty - never "saved" after a failed write.
 */

#include <QtTest/QtTest>
#include <QObject>
#include <QTemporaryDir>

#include "../src/midi/MidiFile.h"
#include "../src/midi/MidiChannel.h"
#include "../src/midi/MidiTrack.h"
#include "../src/protocol/Protocol.h"
#include "../src/MidiEvent/MidiEvent.h"
#include "../src/MidiEvent/TempoChangeEvent.h"

// ---- ODR shims (same set as test_tempo_map_thinner) ----------------------
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

// ==========================================================================

class TestDocumentTiming : public QObject {
    Q_OBJECT

private:
    QTemporaryDir _dir;

    QString path(const QString &name) const { return _dir.filePath(name); }

    static TempoChangeEvent *tempoAt(MidiFile *f, int tick) {
        const auto *map = f->tempoEvents();
        auto it = map->constFind(tick);
        return it == map->constEnd() ? nullptr : dynamic_cast<TempoChangeEvent *>(it.value());
    }

private slots:
    void initTestCase() { QVERIFY(_dir.isValid()); }

    // --- 1. new_document resolution ---------------------------------------

    void initTicksPerQuarter_setsResolutionOfANewDocument() {
        MidiFile f;
        QVERIFY(f.initTicksPerQuarter(960));
        QCOMPARE(f.ticksPerQuarter(), 960);

        QVERIFY(f.save(path(QStringLiteral("ppq960.mid"))));
        bool ok = false;
        MidiFile reopened(path(QStringLiteral("ppq960.mid")), &ok);
        QVERIFY(ok);
        QCOMPARE(reopened.ticksPerQuarter(), 960);
    }

    void initTicksPerQuarter_refusesOutOfRange() {
        MidiFile f;
        const int before = f.ticksPerQuarter();
        QVERIFY(!f.initTicksPerQuarter(0));
        QVERIFY(!f.initTicksPerQuarter(-5));
        QVERIFY(!f.initTicksPerQuarter(32768)); // bit 15 = SMPTE timing
        QCOMPARE(f.ticksPerQuarter(), before);
        QVERIFY(f.initTicksPerQuarter(32767));
    }

    // Anything after tick 0 means the resolution would retime music.
    void initTicksPerQuarter_refusedOnceEventsFollowTickZero() {
        MidiFile f;
        const int before = f.ticksPerQuarter();
        // Any event after tick 0 counts - a tempo change is the simplest one
        // that needs no note-off partner.
        auto *later = new TempoChangeEvent(17, 400000, f.track(0));
        f.channel(17)->insertEvent(later, 480, false);
        QVERIFY(!f.initTicksPerQuarter(960));
        QCOMPARE(f.ticksPerQuarter(), before);
    }

    // --- 2. exact tempo -----------------------------------------------------

    void fractionalTempo_keepsItsMicrosecondsThroughSaveAndReopen() {
        MidiFile f;
        // 117.5 BPM and 93.75 BPM - neither is a whole BPM; the second one
        // is written through the undoable setter set_tempo uses.
        const int us1175 = qRound(60000000.0 / 117.5); // 510638
        auto *extra = new TempoChangeEvent(17, us1175, f.track(0));
        f.channel(17)->insertEvent(extra, 1920, false);

        TempoChangeEvent *anchor = tempoAt(&f, 0);
        QVERIFY(anchor);
        f.protocol()->startNewAction(QStringLiteral("exact tempo"));
        anchor->setMicrosPerQuarter(640000); // 93.75 BPM
        f.protocol()->endAction();
        QCOMPARE(anchor->microsPerQuarter(), 640000);

        QVERIFY(f.save(path(QStringLiteral("tempo.mid"))));
        bool ok = false;
        MidiFile reopened(path(QStringLiteral("tempo.mid")), &ok);
        QVERIFY(ok);
        QVERIFY(tempoAt(&reopened, 0));
        QVERIFY(tempoAt(&reopened, 1920));
        QCOMPARE(tempoAt(&reopened, 0)->microsPerQuarter(), 640000);
        QCOMPARE(tempoAt(&reopened, 1920)->microsPerQuarter(), us1175);
    }

    // setMicrosPerQuarter() is one undo step like setBeats().
    void exactTempo_isUndoable() {
        MidiFile f;
        TempoChangeEvent *anchor = tempoAt(&f, 0);
        QVERIFY(anchor);
        const int before = anchor->microsPerQuarter();
        f.protocol()->startNewAction(QStringLiteral("exact tempo"));
        anchor->setMicrosPerQuarter(510638);
        f.protocol()->endAction();
        f.protocol()->undo(false);
        QCOMPARE(tempoAt(&f, 0)->microsPerQuarter(), before);
    }

    // --- 3. a failed write is not a save ------------------------------------

    void failedSave_leavesTheDocumentDirty() {
        MidiFile f;
        f.setSaved(false);
        QVERIFY(!f.save(path(QStringLiteral("missing-folder/x.mid"))));
        QVERIFY(!f.saved());
    }
};

QTEST_APPLESS_MAIN(TestDocumentTiming)
#include "test_document_timing.moc"
