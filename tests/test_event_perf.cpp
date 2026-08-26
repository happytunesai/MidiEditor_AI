/*
 * test_event_perf
 *
 * Performance / memory smoke-harness for large event populations. NOT a
 * correctness test - it builds dense NoteOn/Off populations (up to 100k
 * note pairs) stored in per-channel QMultiMaps (exactly the production
 * MidiChannel store) and reports construction time, select-all/scan time,
 * and (on Windows) the working-set delta so we get a measured
 * bytes-per-note figure.
 *
 * Why this exists
 * ---------------
 * Phase 28 (multi-document tabs) needs a grounded answer to "how much RAM
 * does an open document cost, and do we need a tab limit?". This harness
 * turns the back-of-envelope ~250-300 B/note estimate into a measured
 * number. See Planning/02_ROADMAP.md Phase 28.
 *
 * The asserts are deliberately LOOSE (anti-hang guards only). The numbers
 * come out via qInfo(), which QtTest suppresses at default verbosity AND
 * which the GUI-subsystem test exe does not flush to a captured stdout on
 * Windows. To see them, write the QtTest log straight to a file:
 *
 *     test_event_perf.exe -v2 -o results.txt,txt
 *
 * Measured baseline (Win64, MSVC2019, Qt 6.5.3, 2026-06-14): ~336-341 B per
 * note all-in (event objects + per-channel QMultiMap nodes); 100k notes
 * (200k events) ~= 32 MB working-set; build 43 ms; select-all 200k 16 ms.
 *
 * Strategy / linkage
 * ------------------
 * Same ODR-shim approach as test_midi_channel: link the four event .cpp
 * files (NoteOnEvent / OffEvent / OnEvent / ProgChangeEvent) + the protocol
 * TUs, while MidiFile / MidiTrack / Appearance / EventWidget / the MidiEvent
 * base are out-of-line shims defined here. Events are stored in plain
 * QMultiMap<int, MidiEvent*> (one per channel) - what MidiChannel wraps -
 * which keeps the link surface minimal and the lifetime fully under our
 * control (no MidiChannel ownership to reason about).
 */

#include <QtTest/QtTest>
#include <QObject>
#include <QList>
#include <QVector>
#include <QMultiMap>
#include <QElapsedTimer>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
static qint64 workingSetBytes() {
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
        return static_cast<qint64>(pmc.WorkingSetSize);
    return -1;
}
#else
static qint64 workingSetBytes() { return -1; }
#endif

// ---- Forward decls used by ODR shims ------------------------------------
class MidiTrack;
class MidiFile;
class Protocol;
class MidiEvent;

// ---- ODR shims: Appearance ----------------------------------------------
#include <QColor>
class Appearance {
public:
    static QColor *channelColor(int n);
};
QColor *Appearance::channelColor(int /*n*/) {
    static QColor c(Qt::black);
    return &c;
}

// ---- ODR shims: MidiFile (never constructed) ----------------------------
class MidiFile {
public:
    MidiFile();
    Protocol *protocol();
    void setSaved(bool v);
    void calcMaxTime();
private:
    Protocol *_protocol;
};
MidiFile::MidiFile() : _protocol(nullptr) {}
Protocol *MidiFile::protocol() { return _protocol; }
void MidiFile::setSaved(bool /*v*/) {}
// no-op: MidiChannel::reloadState recomputes the file length for the tempo
// channel (ch 17); the perf test never touches channel 17 timing.
void MidiFile::calcMaxTime() {}

// ---- ODR shims: MidiTrack -----------------------------------------------
#include "../src/protocol/ProtocolEntry.h"
class TextEvent;
class MidiTrack : public ProtocolEntry {
public:
    virtual ~MidiTrack();
    void setNameEvent(TextEvent *e);
    TextEvent *nameEvent();
    bool hidden();
};
MidiTrack::~MidiTrack() = default;
void MidiTrack::setNameEvent(TextEvent * /*e*/) {}
TextEvent *MidiTrack::nameEvent() { return nullptr; }
bool MidiTrack::hidden() { return false; }

// ---- ODR shims: GraphicObject / EventWidget -----------------------------
#include "../src/gui/GraphicObject.h"
GraphicObject::GraphicObject() {}
void GraphicObject::draw(QPainter *, QColor) {}
bool GraphicObject::shown() { return false; }

#include "../src/gui/EventWidget.h"
void EventWidget::setEvents(QList<MidiEvent *>) {}
void EventWidget::reload() {}

// ---- ODR shims: MidiEvent base ------------------------------------------
#include "../src/MidiEvent/MidiEvent.h"

quint8 MidiEvent::_startByte = 0;
EventWidget *MidiEvent::_eventWidget = nullptr;

MidiEvent::MidiEvent(int channel, MidiTrack *track) {
    _track = track;
    numChannel = channel;
    timePos = 0;
    midiFile = nullptr;
    _tempID = -1;
}
MidiEvent::MidiEvent(MidiEvent &other)
    : ProtocolEntry(other), GraphicObject() {
    _track = other._track;
    numChannel = other.numChannel;
    timePos = other.timePos;
    midiFile = other.midiFile;
    _tempID = other._tempID;
}
MidiFile *MidiEvent::file() { return midiFile; }
void MidiEvent::setFile(MidiFile *f) { midiFile = f; }
int MidiEvent::line() { return UNKNOWN_LINE; }
QString MidiEvent::toMessage() { return QString(); }
QByteArray MidiEvent::save() { return QByteArray(); }
void MidiEvent::draw(QPainter *, QColor) {}
ProtocolEntry *MidiEvent::copy() { return nullptr; }
void MidiEvent::reloadState(ProtocolEntry *) {}
QString MidiEvent::typeString() { return QString(); }
bool MidiEvent::isOnEvent() { return false; }
void MidiEvent::setMidiTime(int t, bool) { timePos = t; }
int MidiEvent::midiTime() { return timePos; }
void MidiEvent::moveToChannel(int channel, bool) { numChannel = channel; }
int MidiEvent::channel() {
    if (numChannel < 0 || numChannel > 18) return 0;
    return numChannel;
}
MidiTrack *MidiEvent::track() { return _track; }

// ---- Real headers (after shims so our shim names win) -------------------
#include "../src/MidiEvent/NoteOnEvent.h"
#include "../src/MidiEvent/OffEvent.h"
#include "../src/MidiEvent/OnEvent.h"
#include "../src/MidiEvent/ProgChangeEvent.h"
// Real MidiChannel (its .cpp is already in this target); forward-declares
// MidiFile, so it composes with the shim above.
#include "../src/midi/MidiChannel.h"

// =========================================================================

typedef QMultiMap<int, MidiEvent *> ChannelMap;

class TestEventPerf : public QObject {
    Q_OBJECT

private:
    static constexpr int kChannels = 16;

    // Build `noteCount` NoteOn/Off pairs across 16 per-channel maps; every
    // created event pointer is appended to `sink` for cleanup. Returns ms.
    qint64 buildNotes(int noteCount,
                      QVector<ChannelMap> &maps,
                      QVector<MidiEvent *> &sink) {
        QElapsedTimer t;
        t.start();
        for (int i = 0; i < noteCount; ++i) {
            int ch = i % kChannels;
            int note = 36 + (i % 60);
            int tickOn = i * 10;
            int tickOff = tickOn + 8;

            NoteOnEvent *on = new NoteOnEvent(note, 100, ch, nullptr);
            OffEvent *off = new OffEvent(ch, on->line(), nullptr);
            on->setMidiTime(tickOn, false);
            off->setMidiTime(tickOff, false);

            maps[ch].insert(tickOn, on);
            maps[ch].insert(tickOff, off);
            sink.append(on);
            sink.append(off);
        }
        return t.elapsed();
    }

    // Drop the maps' references first, then free each event exactly once.
    void destroyAll(QVector<ChannelMap> &maps, QVector<MidiEvent *> &sink) {
        for (ChannelMap &m : maps) m.clear();
        for (MidiEvent *e : sink) delete e;
        sink.clear();
        OffEvent::clearOnEvents();
    }

    void runBuildCase(int noteCount, const char *label) {
        OffEvent::clearOnEvents();
        QVector<ChannelMap> maps(kChannels);
        QVector<MidiEvent *> sink;
        sink.reserve(noteCount * 2);

        qint64 wsBefore = workingSetBytes();
        qint64 buildMs = buildNotes(noteCount, maps, sink);
        qint64 wsAfter = workingSetBytes();

        int events = noteCount * 2;
        QVERIFY2(sink.size() == events, "expected 2 events per note");

        qInfo().noquote() << QString("[%1] %2 notes (%3 events): build %4 ms")
                                 .arg(label).arg(noteCount).arg(events).arg(buildMs);

        if (wsBefore >= 0 && wsAfter >= 0) {
            qint64 deltaBytes = wsAfter - wsBefore;
            double perNote = noteCount > 0 ? double(deltaBytes) / noteCount : 0.0;
            qInfo().noquote()
                << QString("[%1] working-set delta ~%2 MB  (~%3 B/note, ~%4 B/event) [approx, incl. allocator+page overhead]")
                       .arg(label)
                       .arg(deltaBytes / (1024.0 * 1024.0), 0, 'f', 1)
                       .arg(perNote, 0, 'f', 0)
                       .arg(events > 0 ? double(deltaBytes) / events : 0.0, 0, 'f', 0);
        } else {
            qInfo().noquote() << QString("[%1] working-set measurement unavailable on this platform").arg(label);
        }

        QVERIFY2(buildMs < 60000, "building notes took absurdly long (>60s)");

        destroyAll(maps, sink);
    }

private slots:

    void init() { OffEvent::clearOnEvents(); }
    void cleanup() { OffEvent::clearOnEvents(); }

    // -----------------------------------------------------------------
    void build_50k_notes_reportTimingAndMemory() {
        runBuildCase(50000, "50k");
    }

    // -----------------------------------------------------------------
    void build_100k_notes_reportTimingAndMemory() {
        runBuildCase(100000, "100k");
    }

    // -----------------------------------------------------------------
    // Select-all + a read/scan pass over a 100k-note population - models
    // Selection::setSelection(all) and a redraw/scan walk respectively.
    void selectAllAndScan_100k_reportTiming() {
        OffEvent::clearOnEvents();
        QVector<ChannelMap> maps(kChannels);
        QVector<MidiEvent *> sink;
        sink.reserve(200000);
        buildNotes(100000, maps, sink);

        QElapsedTimer t;
        t.start();
        QList<MidiEvent *> selected;
        for (ChannelMap &m : maps)
            for (auto it = m.begin(); it != m.end(); ++it)
                selected.append(it.value());
        qint64 selectMs = t.elapsed();

        t.restart();
        qint64 tickSum = 0;
        for (MidiEvent *e : selected)
            tickSum += e->midiTime();
        qint64 scanMs = t.elapsed();

        qInfo().noquote() << QString("[100k] select-all %1 events: %2 ms; scan pass: %3 ms (checksum %4)")
                                 .arg(selected.size()).arg(selectMs).arg(scanMs).arg(tickSum);

        QVERIFY2(selected.size() == 200000, "select-all should see all events");
        QVERIFY2(selectMs < 30000 && scanMs < 30000, "select/scan took absurdly long (>30s)");

        destroyAll(maps, sink);
    }

    // -----------------------------------------------------------------
    // v2.2 #3 (undo-memory instrumentation): MidiChannel::copy() is the undo
    // system's single heavy snapshot factory - every protocolled channel
    // mutation clones the whole event map there - and the status-bar sampler
    // prices undo history via the counters copy() maintains. Pin their
    // arithmetic: count/node-sum track copy() exactly and charge the map size
    // at copy time; snapshots themselves report zero (the counters are not
    // inherited); and reloadState() (= undo) leaves them untouched, so the
    // session figure stays monotonic through undo/redo churn.
    void undoSnapshotCountersTrackCopyExactly() {
        MidiFile shimFile;
        MidiChannel ch(&shimFile, 0);
        QCOMPARE(ch.snapshotCount(), qint64(0));
        QCOMPARE(ch.snapshotNodeSum(), qint64(0));

        const int n1 = 500;
        for (int i = 0; i < n1; ++i)
            ch.eventMap()->insert(i, nullptr);

        ProtocolEntry *snap1 = ch.copy();
        ProtocolEntry *snap2 = ch.copy();
        QCOMPARE(ch.snapshotCount(), qint64(2));
        QCOMPARE(ch.snapshotNodeSum(), qint64(2 * n1));

        // Growth is charged with the map size AT COPY TIME.
        const int n2 = 250;
        for (int i = 0; i < n2; ++i)
            ch.eventMap()->insert(n1 + i, nullptr);
        ProtocolEntry *snap3 = ch.copy();
        QCOMPARE(ch.snapshotCount(), qint64(3));
        QCOMPARE(ch.snapshotNodeSum(), qint64(2 * n1 + n1 + n2));

        // The snapshot prices as zero - only LIVE channels are summed, so a
        // copied-counter would double the session figure.
        MidiChannel *snapCh = dynamic_cast<MidiChannel *>(snap3);
        QVERIFY(snapCh);
        QCOMPARE(snapCh->snapshotCount(), qint64(0));
        QCOMPARE(snapCh->snapshotNodeSum(), qint64(0));

        // Undo restores the map but never the counters.
        ch.reloadState(snap1);
        QCOMPARE(static_cast<int>(ch.eventMap()->size()), n1);
        QCOMPARE(ch.snapshotCount(), qint64(3));
        QCOMPARE(ch.snapshotNodeSum(), qint64(2 * n1 + n1 + n2));

        // snap1's map is now owned by ch (reloadState adopted it); the
        // MidiChannel shells have no destructor, so these deletes drop the
        // shells only - fine here, the test process ends right after.
        delete snap2;
        delete snap3;
        delete snap1;
    }

    // -----------------------------------------------------------------
    // Phase 48 (tempo-map cache): MidiFile keeps a binary-searchable cache of
    // channel 17 and can only trust it because every channel-17 mutation bumps
    // MidiChannel::tempoRevision(). Two things have to hold for that to work on
    // the files this phase exists for (a DAW tempo ramp, >12k tempo events):
    //
    //   1. The hook fires on the toProtocol=false BULK path - the path a tempo
    //      edit over the whole map necessarily uses, since the protocolled path
    //      would clone the entire event map per event.
    //   2. Bumping is O(1), so guarding correctness does not re-introduce a
    //      per-mutation cost on exactly the maps that are already large.
    //
    // The full end-to-end perf pin (preparePlayerData + an msOfTick sweep on a
    // 12k-tempo-event file) lives in test_midi_measure, which links the real
    // MidiFile; this harness ODR-shims MidiFile away on purpose.
    void tempoChannelBulkMutationsBumpRevisionAtConstantCost() {
        MidiFile shimFile;
        MidiChannel tempoCh(&shimFile, 17);

        const int n = 12000;
        QVector<MidiEvent *> events;
        events.reserve(n);

        const quint64 revBefore = MidiChannel::tempoRevision();

        QElapsedTimer t;
        t.start();
        for (int i = 0; i < n; ++i) {
            // Tick 1 upwards: channel 17 refuses to give up its last event at
            // tick 0, and this case is about the bulk path, not that guard.
            const int tick = i * 24 + 1;
            ProgChangeEvent *ev = new ProgChangeEvent(17, i % 128, nullptr);
            ev->setMidiTime(tick, false);
            tempoCh.eventMap()->insert(tick, ev);
            events.append(ev);
        }
        const qint64 insertMs = t.elapsed();

        t.restart();
        for (MidiEvent *ev : events) {
            QVERIFY2(tempoCh.removeEvent(ev, false),
                     "bulk removal from the tempo channel was refused");
        }
        const qint64 removeMs = t.elapsed();

        QCOMPARE(int(tempoCh.eventMap()->size()), 0);
        // Exactly one bump per removal - no more (a rebuild per event would be
        // quadratic), no fewer (a missed bump means a stale cache, i.e. wrong
        // note positions and wrong playback timing).
        QCOMPARE(MidiChannel::tempoRevision() - revBefore, quint64(n));

        qInfo().noquote()
            << QString("[tempo] %1 channel-17 events: bulk insert %2 ms, "
                       "bulk removeEvent(toProtocol=false) %3 ms")
                   .arg(n).arg(insertMs).arg(removeMs);

        QVERIFY2(insertMs < 5000 && removeMs < 5000,
                 "bulk tempo-map churn took absurdly long (>5s)");

        for (MidiEvent *ev : events) delete ev;
    }
};

QTEST_APPLESS_MAIN(TestEventPerf)
#include "test_event_perf.moc"
