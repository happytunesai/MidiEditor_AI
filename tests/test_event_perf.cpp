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
private:
    Protocol *_protocol;
};
MidiFile::MidiFile() : _protocol(nullptr) {}
Protocol *MidiFile::protocol() { return _protocol; }
void MidiFile::setSaved(bool /*v*/) {}

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
};

QTEST_APPLESS_MAIN(TestEventPerf)
#include "test_event_perf.moc"
