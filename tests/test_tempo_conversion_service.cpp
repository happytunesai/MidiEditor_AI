/*
 * test_tempo_conversion_service
 *
 * Unit tests for src/converter/TempoConversionService — Phase 33's
 * "Convert Tempo, Preserve Duration" headless engine.
 *
 * Coverage (subset of spec §33.7):
 *   1.  preview() rejects a null file.
 *   2.  preview() rejects non-positive BPM.
 *   3.  preview() reports an "identical BPM" warning and ok=true.
 *   4.  preview() computes the correct scale factor and predicted
 *       new duration for EventsOnly mode.
 *   5.  convert() with WholeProject + ReplaceFixed:
 *         - scales every note tick by (target/source);
 *         - removes all source tempo events and inserts exactly one
 *           target tempo event at tick 0;
 *         - leaves the predicted duration unchanged.
 *   6.  convert() with SelectedChannels + EventsOnly only touches the
 *       chosen channel's events; tempo map is untouched.
 *   7.  convert() with SelectedEvents + a frozen pointer set only moves
 *       the listed events.
 *   8.  A partial scope combined with ReplaceFixed or ScaleTempoMap is
 *       refused (the tempo map is shared): nothing moves inside or
 *       outside the scope, the map stays intact, and the legal
 *       partial-scope mode (EventsOnly) still works.
 *   9.  A SelectedChannels scope leaves the file-global time-signature and
 *       meta events (channels 18 / 16) alone, even with includeTimeSig /
 *       includeMeta at their default true.
 *  10.  Round-trip 90 → 180 → 90 BPM on a NoteOn returns to (or within
 *       ±1 tick of) the original tick.
 *
 * Strategy
 * --------
 * TempoConversionService.cpp drags in MidiFile / MidiChannel / MidiTrack
 * / MidiEvent / TempoChangeEvent / Protocol. We follow the same ODR-shim
 * approach as test_midi_channel:
 *   - real .cpps:  MidiChannel + ChannelVisibilityManager + the four event
 *                  types we directly construct (NoteOnEvent, OffEvent,
 *                  OnEvent, TempoChangeEvent) + ProgChangeEvent for the
 *                  ProgChange dependency on OnEvent + the protocol/* TUs;
 *   - shimmed:    MidiFile, MidiTrack, MidiEvent base, GraphicObject,
 *                 EventWidget, Appearance.
 *
 * Critical difference from test_midi_channel: TempoConversionService
 * relies on MidiEvent::setMidiTime() actually moving an event in its
 * channel's QMultiMap. The shim therefore replicates that behaviour
 * (remove from old key, store new tick, insert under new key) instead
 * of merely overwriting the timePos field.
 */

#include <QtTest/QtTest>
#include <QObject>
#include <QList>
#include <QMultiMap>
#include <QSet>

// ---- Forward decls used by ODR shims ------------------------------------
class MidiTrack;
class MidiFile;
class Protocol;
class MidiChannel;
class MidiEvent;
class TextEvent;

// ---- ODR shims: Appearance ----------------------------------------------
#include <QColor>
class Appearance {
public:
    static QColor *channelColor(int n);
    static QColor borderColor();
};
QColor *Appearance::channelColor(int /*n*/) {
    static QColor c(Qt::black);
    return &c;
}
QColor Appearance::borderColor() { return QColor(Qt::black); }

// (real ChannelVisibilityManager.cpp is linked into the test binary;
//  it has no heavy dependencies — see CMakeLists below.)

// ---- ODR shim: MidiFile -------------------------------------------------
// Just enough to back channel(int), channelEvents(int), msOfTick(int),
// endTick(), calcMaxTime(), ticksPerQuarter(), track(int), protocol(),
// setMaxLengthMs(int).
#include "../src/protocol/Protocol.h"
class MidiFile {
public:
    MidiFile();
    ~MidiFile();

    Protocol *protocol();
    MidiChannel *channel(int i);
    QMultiMap<int, MidiEvent *> *channelEvents(int channel);
    MidiTrack *track(int n);
    int endTick();
    int ticksPerQuarter();
    void calcMaxTime();
    void setMaxLengthMs(int ms);
    int msOfTick(int tick, QList<MidiEvent *> *events = nullptr,
                 int msOfFirstEventInList = 0);
    void setSaved(bool b);
    qint64 maxLengthInMs() const { return _maxMs; }

    // test-only helpers
    void setProtocol(Protocol *p) { _protocol = p; }
    void addTrack(MidiTrack *t) { _tracks.append(t); }
    void setTempoBpm(double bpm) { _bpm = bpm; }
    double tempoBpm() const { return _bpm; }

private:
    Protocol *_protocol;
    MidiChannel *_channels[19];
    QList<MidiTrack *> _tracks;
    qint64 _maxMs;
    double _bpm;
};

// ---- ODR shim: MidiTrack ------------------------------------------------
#include "../src/protocol/ProtocolEntry.h"
class MidiTrack : public ProtocolEntry {
public:
    explicit MidiTrack(int n) : _num(n), _hidden(false) {}
    ~MidiTrack() override = default;

    int number();
    void setNumber(int n) { _num = n; }
    void setNameEvent(TextEvent *nameEvent);
    TextEvent *nameEvent();
    bool hidden() const { return _hidden; }

    // ProtocolEntry interface
    ProtocolEntry *copy() override { return new MidiTrack(_num); }
    void reloadState(ProtocolEntry *) override {}
    QString typeString() { return QStringLiteral("MidiTrack"); }

private:
    int _num;
    bool _hidden;
};
int MidiTrack::number() { return _num; }
void MidiTrack::setNameEvent(TextEvent *) {}
TextEvent *MidiTrack::nameEvent() { return nullptr; }

// ---- ODR shim: GraphicObject / EventWidget ------------------------------
#include "../src/gui/GraphicObject.h"
GraphicObject::GraphicObject() {}
void GraphicObject::draw(QPainter *, QColor) {}
bool GraphicObject::shown() { return false; }

#include "../src/gui/EventWidget.h"
void EventWidget::setEvents(QList<MidiEvent *>) {}
void EventWidget::reload() {}

// ---- ODR shim: MidiEvent base -------------------------------------------
// Critical: setMidiTime must actually move the event in its channel's
// QMultiMap, otherwise TempoConversionService's tick-scaling pass would
// leave the channel map indexed by the old tick and the test assertions
// would be meaningless.
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
ProtocolEntry *MidiEvent::copy() { return new MidiEvent(*this); }
void MidiEvent::reloadState(ProtocolEntry *) {}
QString MidiEvent::typeString() { return QStringLiteral("MidiEvent"); }
bool MidiEvent::isOnEvent() { return false; }
void MidiEvent::moveToChannel(int channel, bool) { numChannel = channel; }
int MidiEvent::channel() {
    if (numChannel < 0 || numChannel > 18) return 0;
    return numChannel;
}
MidiTrack *MidiEvent::track() { return _track; }

void MidiEvent::setMidiTime(int t, bool /*toProtocol*/) {
    if (midiFile) {
        QMultiMap<int, MidiEvent *> *map =
            midiFile->channelEvents(numChannel);
        if (map) {
            map->remove(timePos, this);
            timePos = t;
            map->insert(t, this);
            return;
        }
    }
    timePos = t;
}
int MidiEvent::midiTime() { return timePos; }

// ---- Real headers (after shims so our shim names win) -------------------
#include "../src/midi/MidiChannel.h"
#include "../src/MidiEvent/NoteOnEvent.h"
#include "../src/MidiEvent/OffEvent.h"
#include "../src/MidiEvent/OnEvent.h"
#include "../src/MidiEvent/TempoChangeEvent.h"
#include "../src/MidiEvent/ProgChangeEvent.h"
#include "../src/converter/TempoConversionService.h"

// ---- MidiFile shim implementation (now that MidiChannel is fully known) -
MidiFile::MidiFile()
    : _protocol(nullptr), _maxMs(0), _bpm(120.0) {
    for (int i = 0; i < 19; ++i) {
        _channels[i] = new MidiChannel(this, i);
    }
}
MidiFile::~MidiFile() {
    for (int i = 0; i < 19; ++i) delete _channels[i];
    qDeleteAll(_tracks);
}
Protocol *MidiFile::protocol() { return _protocol; }
void MidiFile::setSaved(bool) {}
void MidiFile::setMaxLengthMs(int) {}
MidiChannel *MidiFile::channel(int i) {
    if (i < 0 || i > 18) return nullptr;
    return _channels[i];
}
QMultiMap<int, MidiEvent *> *MidiFile::channelEvents(int c) {
    MidiChannel *ch = channel(c);
    return ch ? ch->eventMap() : nullptr;
}
MidiTrack *MidiFile::track(int n) {
    for (MidiTrack *t : _tracks) {
        if (t->number() == n) return t;
    }
    return _tracks.isEmpty() ? nullptr : _tracks.first();
}
int MidiFile::endTick() {
    int last = 0;
    for (int i = 0; i < 19; ++i) {
        QMultiMap<int, MidiEvent *> *m = _channels[i]->eventMap();
        if (m && !m->isEmpty()) {
            int k = (m->constEnd() - 1).key();
            if (k > last) last = k;
        }
    }
    return last;
}
int MidiFile::ticksPerQuarter() { return 480; }
void MidiFile::calcMaxTime() {
    // Approximate: assume a single project tempo (_bpm). 480 ticks/quarter.
    // ms = ticks * (60000 / (bpm * 480))
    double msPerTick = 60000.0 / (_bpm * 480.0);
    _maxMs = static_cast<qint64>(endTick() * msPerTick);
}
int MidiFile::msOfTick(int tick, QList<MidiEvent *> * /*events*/,
                       int /*msOfFirstEventInList*/) {
    double msPerTick = 60000.0 / (_bpm * 480.0);
    return static_cast<int>(tick * msPerTick);
}

// =========================================================================
// Test helpers
// =========================================================================
namespace {

struct ScopedFile {
    MidiFile *file;
    Protocol *protocol;
    MidiTrack *track0;
    MidiTrack *track1;

    ScopedFile() {
        file = new MidiFile();
        // Protocol(MidiFile *f) — single-arg ctor (see Protocol.h).
        protocol = new Protocol(file);
        file->setProtocol(protocol);
        track0 = new MidiTrack(0);
        track1 = new MidiTrack(1);
        file->addTrack(track0);
        file->addTrack(track1);
    }
    ~ScopedFile() {
        delete file;
        delete protocol;
    }

    NoteOnEvent *addNote(int channel, int tick, int durationTicks,
                         int pitch = 60, int velocity = 100,
                         MidiTrack *trk = nullptr) {
        if (!trk) trk = track0;
        // Build note manually — MidiChannel::insertNote routes through
        // protocol() which we keep silent by setting file ourselves and
        // pushing into the channel map directly via setMidiTime.
        NoteOnEvent *on = new NoteOnEvent(pitch, velocity, channel, trk);
        OffEvent *off = new OffEvent(channel, 127 - pitch, trk);
        on->setFile(file);
        off->setFile(file);
        on->setMidiTime(tick, false);
        off->setMidiTime(tick + durationTicks, false);
        return on;
    }

    /// Bare event on one of the file-GLOBAL channels (16 = meta/lyrics/key
    /// signature, 18 = time signature). The service only ever looks at the
    /// channel index and the tick, so the base MidiEvent (shimmed in this TU)
    /// is enough — TimeSignatureEvent is not linked into this test.
    MidiEvent *addGlobalEvent(int channel, int tick) {
        MidiEvent *ev = new MidiEvent(channel, track0);
        ev->setFile(file);
        ev->setMidiTime(tick, false);
        return ev;
    }

    TempoChangeEvent *addTempo(int tick, int bpm) {
        // mspq = 60000000 / bpm ; TempoChangeEvent stores microseconds
        // per quarter; setBeats() can rewrite the BPM later.
        TempoChangeEvent *t = new TempoChangeEvent(17,
                                                   60000000 / bpm,
                                                   track0);
        t->setFile(file);
        t->setMidiTime(tick, false);
        return t;
    }
};

} // namespace

// =========================================================================
class TestTempoConversionService : public QObject {
    Q_OBJECT
private slots:
    void preview_nullFile_returnsError();
    void preview_zeroBpm_returnsError();
    void preview_identicalBpm_warnsAndOk();
    void preview_eventsOnly_predictsScaledDuration();

    void convert_replaceFixed_scalesNotesAndCollapsesTempo();
    void convert_eventsOnly_perChannel_isolatesScope();
    void convert_selectedEvents_onlyMovesListed();
    void convert_partialScope_rejectsTempoMapModes();
    void convert_selectedChannels_leavesGlobalMetaAlone();

    void convert_roundTrip_returnsToOrigin();
    void convert_scaleTempoMap_keepsExactTempo();
};

// -------------------------------------------------------------------------
void TestTempoConversionService::preview_nullFile_returnsError() {
    TempoConversionOptions opts;
    opts.sourceBpm = 120.0;
    opts.targetBpm = 240.0;
    auto r = TempoConversionService::preview(nullptr, opts);
    QVERIFY(!r.ok);
    QVERIFY(!r.error.isEmpty());
}

void TestTempoConversionService::preview_zeroBpm_returnsError() {
    ScopedFile f;
    TempoConversionOptions opts;
    opts.sourceBpm = 0.0;
    opts.targetBpm = 120.0;
    auto r = TempoConversionService::preview(f.file, opts);
    QVERIFY(!r.ok);
    QVERIFY(!r.error.isEmpty());
}

void TestTempoConversionService::preview_identicalBpm_warnsAndOk() {
    ScopedFile f;
    f.addNote(0, 0, 480);
    TempoConversionOptions opts;
    opts.sourceBpm = 120.0;
    opts.targetBpm = 120.0;
    auto r = TempoConversionService::preview(f.file, opts);
    QVERIFY(r.ok);
    QVERIFY(!r.warning.isEmpty());
    QCOMPARE(r.scaleFactor, 1.0);
}

void TestTempoConversionService::preview_eventsOnly_predictsScaledDuration() {
    ScopedFile f;
    f.file->setTempoBpm(90.0); // shim duration model uses this
    f.addNote(0, 0, 1920); // 1 bar at any TPQ
    TempoConversionOptions opts;
    opts.sourceBpm = 90.0;
    opts.targetBpm = 180.0;
    opts.tempoMode = TempoConversionTempoMode::EventsOnly;
    auto r = TempoConversionService::preview(f.file, opts);
    QVERIFY(r.ok);
    QCOMPARE(r.scaleFactor, 2.0);
    // EventsOnly: ticks scale ×2, tempo unchanged → predicted duration ×2.
    QCOMPARE(static_cast<qint64>(r.newDurationMs),
             static_cast<qint64>(r.oldDurationMs) * 2);
}

// -------------------------------------------------------------------------
void TestTempoConversionService::convert_replaceFixed_scalesNotesAndCollapsesTempo() {
    ScopedFile f;
    f.file->setTempoBpm(90.0);
    f.addTempo(0, 90);
    NoteOnEvent *on = f.addNote(0, 480, 480);
    OffEvent *off = on->offEvent();
    QVERIFY(off);

    TempoConversionOptions opts;
    opts.sourceBpm = 90.0;
    opts.targetBpm = 180.0;
    opts.scope = TempoConversionScope::WholeProject;
    opts.tempoMode = TempoConversionTempoMode::ReplaceFixed;

    auto r = TempoConversionService::convert(f.file, opts);
    QVERIFY(r.ok);
    QCOMPARE(r.scaleFactor, 2.0);
    QCOMPARE(on->midiTime(), 960);
    QCOMPARE(off->midiTime(), 1920);

    // Tempo channel: exactly one event at tick 0, encoding 180 BPM.
    QMultiMap<int, MidiEvent *> *tmap = f.file->channelEvents(17);
    QCOMPARE(tmap->size(), 1);
    auto it = tmap->begin();
    QCOMPARE(it.key(), 0);
    auto *tc = dynamic_cast<TempoChangeEvent *>(it.value());
    QVERIFY(tc);
    QCOMPARE(tc->beatsPerQuarter(), 180);
    QCOMPARE(r.tempoEventsRemoved, 1);
    QCOMPARE(r.tempoEventsInserted, 1);
}

void TestTempoConversionService::convert_eventsOnly_perChannel_isolatesScope() {
    ScopedFile f;
    f.file->setTempoBpm(120.0);
    NoteOnEvent *vocal = f.addNote(/*ch*/ 0, 480, 240, 60, 100, f.track1);
    NoteOnEvent *drums = f.addNote(/*ch*/ 9, 480, 240, 36, 100, f.track1);

    TempoConversionOptions opts;
    opts.sourceBpm = 90.0;
    opts.targetBpm = 180.0;
    opts.scope = TempoConversionScope::SelectedChannels;
    opts.channelIds = {0};
    opts.tempoMode = TempoConversionTempoMode::EventsOnly;

    auto r = TempoConversionService::convert(f.file, opts);
    QVERIFY(r.ok);
    // Vocal moved (×2), drums untouched.
    QCOMPARE(vocal->midiTime(), 960);
    QCOMPARE(drums->midiTime(), 480);
    // No new tempo event inserted.
    QCOMPARE(r.tempoEventsInserted, 0);
}

void TestTempoConversionService::convert_selectedEvents_onlyMovesListed() {
    ScopedFile f;
    f.file->setTempoBpm(120.0);
    NoteOnEvent *a = f.addNote(0, 240, 240);
    NoteOnEvent *b = f.addNote(0, 720, 240);

    TempoConversionOptions opts;
    opts.sourceBpm = 120.0;
    opts.targetBpm = 240.0;
    opts.scope = TempoConversionScope::SelectedEvents;
    opts.tempoMode = TempoConversionTempoMode::EventsOnly;
    opts.selectedEventPtrs = {reinterpret_cast<quintptr>(a)};

    const int bTickBefore = b->midiTime();
    auto r = TempoConversionService::convert(f.file, opts);
    QVERIFY(r.ok);
    QCOMPARE(a->midiTime(), 480);
    QCOMPARE(b->midiTime(), bTickBefore);
}

// TEMPO-PARTIAL-REPLACE-001: the tempo map is shared by the whole file, so a
// partial scope must not be allowed to replace or rescale it — that would
// retime every event OUTSIDE the scope. Both map-touching modes are refused;
// EventsOnly (the legal partial-scope mode) still works.
void TestTempoConversionService::convert_partialScope_rejectsTempoMapModes() {
    ScopedFile f;
    f.file->setTempoBpm(90.0);
    TempoChangeEvent *tempoAtStart = f.addTempo(0, 90);
    TempoChangeEvent *tempoLater = f.addTempo(1920, 120);

    // In scope: channel 0 on track 1. Outside: channel 9 on track 0.
    NoteOnEvent *inScope = f.addNote(0, 480, 240, 60, 100, f.track1);
    NoteOnEvent *outside = f.addNote(9, 480, 240, 36, 100, f.track0);
    OffEvent *outsideOff = outside->offEvent();
    QVERIFY(outsideOff);

    const int inTickBefore = inScope->midiTime();
    const int outTickBefore = outside->midiTime();
    const int outOffTickBefore = outsideOff->midiTime();

    const TempoConversionTempoMode mapModes[] = {
        TempoConversionTempoMode::ReplaceFixed,
        TempoConversionTempoMode::ScaleTempoMap
    };
    for (TempoConversionTempoMode mode : mapModes) {
        TempoConversionOptions opts;
        opts.sourceBpm = 90.0;
        opts.targetBpm = 180.0;
        opts.scope = TempoConversionScope::SelectedTracks;
        opts.trackIds = {1};
        opts.tempoMode = mode;

        // The shared validator names the reason...
        const QString conflict = TempoConversionService::scopeModeConflict(opts);
        QVERIFY(!conflict.isEmpty());
        QVERIFY(conflict.contains(QStringLiteral("partial scope")));

        // ...and both entry points refuse.
        const auto p = TempoConversionService::preview(f.file, opts);
        QVERIFY(!p.ok);
        QCOMPARE(p.error, conflict);

        const auto r = TempoConversionService::convert(f.file, opts);
        QVERIFY(!r.ok);
        QCOMPARE(r.error, conflict);
        QCOMPARE(r.affectedEvents, 0);
        QCOMPARE(r.tempoEventsRemoved, 0);
        QCOMPARE(r.tempoEventsInserted, 0);

        // Nothing moved — neither inside nor outside the scope.
        QCOMPARE(inScope->midiTime(), inTickBefore);
        QCOMPARE(outside->midiTime(), outTickBefore);
        QCOMPARE(outsideOff->midiTime(), outOffTickBefore);

        // Tempo map intact: same two events, same ticks, same BPMs.
        QMultiMap<int, MidiEvent *> *tmap = f.file->channelEvents(17);
        QCOMPARE(tmap->size(), 2);
        QCOMPARE(tempoAtStart->midiTime(), 0);
        QCOMPARE(tempoLater->midiTime(), 1920);
        QCOMPARE(tempoAtStart->beatsPerQuarter(), 90);
        QCOMPARE(tempoLater->beatsPerQuarter(), 120);
    }

    // The legal combination is untouched: EventsOnly re-ticks track 1 only.
    TempoConversionOptions legal;
    legal.sourceBpm = 90.0;
    legal.targetBpm = 180.0;
    legal.scope = TempoConversionScope::SelectedTracks;
    legal.trackIds = {1};
    legal.tempoMode = TempoConversionTempoMode::EventsOnly;
    QVERIFY(TempoConversionService::scopeModeConflict(legal).isEmpty());

    const auto ok = TempoConversionService::convert(f.file, legal);
    QVERIFY(ok.ok);
    QCOMPARE(inScope->midiTime(), inTickBefore * 2);
    QCOMPARE(outside->midiTime(), outTickBefore);
    QCOMPARE(outsideOff->midiTime(), outOffTickBefore);
    QCOMPARE(f.file->channelEvents(17)->size(), 2);
    QCOMPARE(tempoAtStart->beatsPerQuarter(), 90);
    QCOMPARE(tempoLater->beatsPerQuarter(), 120);
}

// TEMPO-PARTIAL-META-001: channels 16 and 18 carry the file-GLOBAL meta and
// time-signature events. `channelIds` can only name MIDI channels 0..15, so a
// channel scope never asks for them — yet they used to be re-ticked anyway
// (the channel filter only covered 0..15 and includeTimeSig/includeMeta
// default to true), moving the bar grid for the material OUTSIDE the scope.
void TestTempoConversionService::convert_selectedChannels_leavesGlobalMetaAlone() {
    ScopedFile f;
    f.file->setTempoBpm(120.0);
    NoteOnEvent *inScope = f.addNote(/*ch*/ 0, 480, 240);
    MidiEvent *timeSigAtStart = f.addGlobalEvent(/*ch*/ 18, 0);
    MidiEvent *timeSigLater = f.addGlobalEvent(/*ch*/ 18, 960);
    MidiEvent *meta = f.addGlobalEvent(/*ch*/ 16, 480);

    TempoConversionOptions opts;
    opts.sourceBpm = 120.0;
    opts.targetBpm = 240.0;
    opts.scope = TempoConversionScope::SelectedChannels;
    opts.channelIds = {0};
    opts.tempoMode = TempoConversionTempoMode::EventsOnly;
    // Left at their defaults ON PURPOSE — that is what the AI/MCP tool passes,
    // so the scope alone has to protect the global events.
    QVERIFY(opts.includeTimeSig);
    QVERIFY(opts.includeMeta);

    // The note and its off event, and nothing from channels 16/18.
    const auto pre = TempoConversionService::preview(f.file, opts);
    QVERIFY(pre.ok);
    QCOMPARE(pre.affectedEvents, 2);

    const auto r = TempoConversionService::convert(f.file, opts);
    QVERIFY(r.ok);
    QCOMPARE(r.affectedEvents, 2);
    QCOMPARE(inScope->midiTime(), 960);
    QCOMPARE(timeSigAtStart->midiTime(), 0);
    QCOMPARE(timeSigLater->midiTime(), 960);
    QCOMPARE(meta->midiTime(), 480);
}

void TestTempoConversionService::convert_roundTrip_returnsToOrigin() {
    ScopedFile f;
    f.file->setTempoBpm(90.0);
    f.addTempo(0, 90);
    NoteOnEvent *on = f.addNote(0, 480, 240);
    const int origTick = on->midiTime();

    {
        TempoConversionOptions opts;
        opts.sourceBpm = 90.0;
        opts.targetBpm = 180.0;
        opts.scope = TempoConversionScope::WholeProject;
        opts.tempoMode = TempoConversionTempoMode::ReplaceFixed;
        QVERIFY(TempoConversionService::convert(f.file, opts).ok);
    }
    QCOMPARE(on->midiTime(), 960);

    {
        TempoConversionOptions opts;
        opts.sourceBpm = 180.0;
        opts.targetBpm = 90.0;
        opts.scope = TempoConversionScope::WholeProject;
        opts.tempoMode = TempoConversionTempoMode::ReplaceFixed;
        QVERIFY(TempoConversionService::convert(f.file, opts).ok);
    }
    QVERIFY(qAbs(on->midiTime() - origTick) <= 1);
}

void TestTempoConversionService::convert_scaleTempoMap_keepsExactTempo() {
    // Review R231-12: ScaleTempoMap scaled the ticks by the exact ratio but
    // wrote a whole BPM, so every passage drifted by the rounding error. The
    // event stores microseconds per quarter, so 128.5 BPM is representable.
    ScopedFile f;
    f.file->setTempoBpm(120.0);
    TempoChangeEvent *tempo = f.addTempo(0, 120);
    NoteOnEvent *on = f.addNote(0, 480, 480);

    TempoConversionOptions opts;
    opts.sourceBpm = 120.0;
    opts.targetBpm = 128.5;
    opts.scope = TempoConversionScope::WholeProject;
    opts.tempoMode = TempoConversionTempoMode::ScaleTempoMap;

    auto preview = TempoConversionService::preview(f.file, opts);
    QVERIFY(preview.ok);
    QVERIFY2(preview.warning.isEmpty(), qPrintable(preview.warning));

    auto r = TempoConversionService::convert(f.file, opts);
    QVERIFY(r.ok);
    QCOMPARE(tempo->microsPerQuarter(), qRound(60000000.0 / 128.5)); // exact, not 129 BPM
    QCOMPARE(tempo->beatsPerQuarter(), 128);                         // truncated display value
    QCOMPARE(on->midiTime(), qRound(480 * 128.5 / 120.0));
}

QTEST_MAIN(TestTempoConversionService)
#include "test_tempo_conversion_service.moc"
