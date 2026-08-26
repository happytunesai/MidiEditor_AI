/*
 * MidiEditor
 * Copyright (C) 2010  Markus Schwenk
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "MidiFile.h"

#include <QDataStream>
#include <QFile>
#include <QMutexLocker>
#include <QThread>

#include "../MidiEvent/ControlChangeEvent.h"
#include "../MidiEvent/KeySignatureEvent.h"
#include "../MidiEvent/MidiEvent.h"
#include "../MidiEvent/NoteOnEvent.h"
#include "../MidiEvent/OffEvent.h"
#include "../MidiEvent/OnEvent.h"
#include "../MidiEvent/ProgChangeEvent.h"
#include "../MidiEvent/TempoChangeEvent.h"
#include "../MidiEvent/TextEvent.h"
#include "../MidiEvent/TimeSignatureEvent.h"
#include "../protocol/Protocol.h"
#include "LyricManager.h"
#include "MidiChannel.h"
#include "MidiTrack.h"
#include "InstrumentDefinitions.h"
#include "math.h"

#include <algorithm>
#include <memory>

int MidiFile::defaultTimePerQuarter = 192;

MidiFile::MidiFile() {
    _saved = true;
    midiTicks = 0;
    _cursorTick = 0;
    prot = new Protocol(this);
    prot->addEmptyAction("New file");
    _path = "";
    _pauseTick = -1;
    for (int i = 0; i < 19; i++) {
        channels[i] = new MidiChannel(this, i);
    }

    timePerQuarter = MidiFile::defaultTimePerQuarter;
    _midiFormat = 1;

    _tracks = new QList<MidiTrack *>();
    MidiTrack *tempoTrack = new MidiTrack(this);
    tempoTrack->setName(tr("Tempo Track"));
    tempoTrack->setNumber(0);
    _tracks->append(tempoTrack);

    MidiTrack *instrumentTrack = new MidiTrack(this);
    instrumentTrack->setName(tr("New Instrument"));
    instrumentTrack->setNumber(1);
    _tracks->append(instrumentTrack);

    connect(tempoTrack, SIGNAL(trackChanged()), this, SIGNAL(trackChanged()));
    connect(instrumentTrack, SIGNAL(trackChanged()), this, SIGNAL(trackChanged()));

    // add timesig
    TimeSignatureEvent *timeSig = new TimeSignatureEvent(18, 4, 2, 24, 8, tempoTrack);
    timeSig->setFile(this);
    channel(18)->eventMap()->insert(0, timeSig);

    // create tempo change
    TempoChangeEvent *tempoEv = new TempoChangeEvent(17, 500000, tempoTrack);
    tempoEv->setFile(this);
    channel(17)->eventMap()->insert(0, tempoEv);
    invalidateTempoCache();

    playerMap = new QMultiMap<int, MidiEvent *>;

    midiTicks = 7680;
    calcMaxTime();

    _lyricManager = new LyricManager(this, this);
}

MidiFile::MidiFile(QString path, bool *ok, QStringList *log) {
    bool deleteLog = false;
    if (!log) {
        log = new QStringList();
        deleteLog = true;
    }

    _pauseTick = -1;
    _saved = true;
    midiTicks = 0;
    _cursorTick = 0;
    prot = new Protocol(this);
    prot->addEmptyAction(tr("File opened"));
    _path = path;
    _tracks = new QList<MidiTrack *>();
    QFile *f = new QFile(path);

    if (!f->open(QIODevice::ReadOnly)) {
        *ok = false;
        log->append(tr("Error: File could not be opened."));
        printLog(log);
        if (deleteLog) {
            delete log;
        }
        delete f;
        return;
    }

    for (int i = 0; i < 19; i++) {
        channels[i] = new MidiChannel(this, i);
    }

    QDataStream *stream = new QDataStream(f);
    stream->setByteOrder(QDataStream::BigEndian);
    if (!readMidiFile(stream, log)) {
        *ok = false;
        printLog(log);
        if (deleteLog) {
            delete log;
        }
        delete stream; // Clean up stream on error
        delete f;
        return;
    }

    delete stream; // Clean up stream
    delete f;

    *ok = true;
    playerMap = new QMultiMap<int, MidiEvent *>;
    // Phase 48: the loader writes into channel 17 directly - make sure the
    // very first timing query builds the cache from the finished map.
    invalidateTempoCache();
    calcMaxTime();

    _lyricManager = new LyricManager(this, this);
    _lyricManager->importFromTextEvents();

    printLog(log);

    // Clean up log if we created it
    if (deleteLog) {
        delete log;
    }
}

MidiFile::MidiFile(int ticks, Protocol *p) {
    midiTicks = ticks;
    prot = p;
    _tracks = nullptr;
    playerMap = nullptr;
    _lyricManager = nullptr;
    _saved = false;
    _cursorTick = 0;
    _pauseTick = -1;
    _midiFormat = 1;
    timePerQuarter = defaultTimePerQuarter;
    maxTimeMS = 0;
    for (int i = 0; i < 19; i++) {
        channels[i] = nullptr;
    }
}

MidiFile::~MidiFile() {
    // Conservative cleanup - only delete containers, not the complex
    // protocol-managed objects to avoid crashes

    // Note: We intentionally do NOT delete the protocol or events here
    // The original system was designed to leak these rather than crash
    // This is a compromise between memory usage and stability

    // Clean up tracks (these should be safe)
    if (_tracks) {
        qDeleteAll(*_tracks);
        delete _tracks;
    }

    // Clean up player map (this should be safe)
    if (playerMap) {
        delete playerMap;
    }

    // Clean up channels (only the containers, not the protocol or events)
    for (int i = 0; i < 19; i++) {
        if (channels[i]) {
            delete channels[i];
        }
    }
}

bool MidiFile::readMidiFile(QDataStream *content, QStringList *log) {
    OffEvent::clearOnEvents();

    quint8 tempByte;

    QString badHeader = tr("Error: Bad format in file header (Expected MThd).");
    (*content) >> tempByte;
    if (tempByte != 'M') {
        log->append(badHeader);
        return false;
    }
    (*content) >> tempByte;
    if (tempByte != 'T') {
        log->append(badHeader);
        return false;
    }
    (*content) >> tempByte;
    if (tempByte != 'h') {
        log->append(badHeader);
        return false;
    }
    (*content) >> tempByte;
    if (tempByte != 'd') {
        log->append(badHeader);
        return false;
    }

    quint32 MThdTrackLength;
    (*content) >> MThdTrackLength;
    if (MThdTrackLength != 6) {
        log->append(tr("Error: MThdTrackLength wrong (expected 6)."));
        return false;
    }

    quint16 midiFormat;
    (*content) >> midiFormat;
    if (midiFormat > 1) {
        log->append(tr("Error: MidiFormat v. 2 cannot be loaded with this Editor."));
        return false;
    }

    _midiFormat = midiFormat;

    quint16 numTracks;
    (*content) >> numTracks;

    quint16 basisVelocity;
    (*content) >> basisVelocity;
    timePerQuarter = (int) basisVelocity;

    bool ok;
    for (int num = 0; num < numTracks; num++) {
        ok = readTrack(content, num, log);
        if (!ok) {
            log->append(tr("Error in Track ") + QString::number(num));
            // Essential: don't fail completely on one bad track
            // Try to skip to next track
            while (!content->atEnd()) {
                quint8 searchByte;
                (*content) >> searchByte;
                if (searchByte == 'M') {
                    // Check if this is start of "MTrk"
                    qint64 pos = content->device()->pos();
                    quint8 t, r, k;
                    if (!content->atEnd()) (*content) >> t;
                    if (!content->atEnd()) (*content) >> r;
                    if (!content->atEnd()) (*content) >> k;

                    if (t == 'T' && r == 'r' && k == 'k') {
                        // Found next track, rewind to start of "MTrk"
                        content->device()->seek(pos - 1);
                        break;
                    } else {
                        // Not "MTrk", continue searching
                        content->device()->seek(pos);
                    }
                }
            }
        }
    }

    // find corrupted OnEvents (without OffEvent)
    QList<OnEvent*> corruptedEvents = OffEvent::corruptedOnEvents();
    if (!corruptedEvents.isEmpty()) {
        foreach(OnEvent* onevent, corruptedEvents) {
            if (onevent) {
                int eventChannel = onevent->channel();
                if (eventChannel >= 0 && eventChannel < 19) {
                    try {
                        // Safely remove the event
                        MidiChannel* ch = channel(eventChannel);
                        if (ch) {
                            log->append(tr("Warning: found OnEvent without OffEvent (line ") + QString::number(onevent->line()) + tr(") - removing..."));
                            ch->removeEvent(onevent);
                        }
                    } catch (...) {
                        // Silent error handling
                    }
                }
            }
        }
    }

    OffEvent::clearOnEvents();

    return true;
}

bool MidiFile::readTrack(QDataStream *content, int num, QStringList *log) {
    quint8 tempByte;

    QString badHeader = tr("Error: Bad format in track header (Track ") + QString::number(num) + tr(", Expected MTrk).");
    (*content) >> tempByte;
    if (tempByte != 'M') {
        while (!content->atEnd()) {
            (*content) >> tempByte;
            if (tempByte == 'M')
                break;
        }
        if (content->atEnd()) {
            log->append(badHeader);
            return false;
        }
    }
    (*content) >> tempByte;
    if (tempByte != 'T') {
        log->append(badHeader);
        return false;
    }
    (*content) >> tempByte;
    if (tempByte != 'r') {
        log->append(badHeader);
        return false;
    }
    (*content) >> tempByte;
    if (tempByte != 'k') {
        log->append(badHeader);
        return false;
    }

    quint32 numBytes;
    (*content) >> numBytes;

    bool ok = true;
    bool endEvent = false;
    int position = 0;

    MidiTrack *track = new MidiTrack(this);
    track->setNumber(num);

    _tracks->append(track);
    connect(track, SIGNAL(trackChanged()), this, SIGNAL(trackChanged()));

    int channelFrequency[16];
    for (int i = 0; i < 16; i++) {
        channelFrequency[i] = 0;
    }

    while (!endEvent) {
        // Essential safety: prevent infinite loops from corrupted data
        if (content->atEnd()) {
            break;
        }

        position += deltaTime(content);

        MidiEvent *event = MidiEvent::loadMidiEvent(content, &ok, &endEvent, track);
        if (!ok) {
            return false;
        }

        OffEvent *offEvent = dynamic_cast<OffEvent *>(event);
        if (offEvent && !offEvent->onEvent()) {
            log->append(tr("Warning: detected offEvent without prior onEvent. Skipping!"));
            // Clean up the orphaned OffEvent to prevent memory leaks
            delete offEvent;
            continue;
        }

        // check whether its the tracks name
        if (event && event->line() == MidiEvent::TEXT_EVENT_LINE) {
            TextEvent *textEvent = dynamic_cast<TextEvent *>(event);
            if (textEvent) {
                if (textEvent->type() == TextEvent::TRACKNAME) {
                    track->setNameEvent(textEvent);
                }
            }
        }

        if (endEvent) {
            if (midiTicks < position) {
                midiTicks = position;
            }
            break;
        }
        if (!event) {
            return false;
        }

        event->setFile(this);

        // also inserts to the Map of the channel
        event->setMidiTime(position, false);

        if (event->channel() < 16) {
            channelFrequency[event->channel()]++;
        }
    }

    // end of track
    (*content) >> tempByte;
    QString errorText = tr("Error: track ") + QString::number(num) + tr("not ended as expected. ");
    if (tempByte != 0x00) {
        log->append(errorText);
        return false;
    }

    // check whether TimeSignature at tick 0 is given. If not, create one.
    // this will be done after reading the first track
    if (!channel(18)->eventMap()->contains(0)) {
        log->append(tr("Warning: no TimeSignatureEvent detected at tick 0. Adding default value."));
        TimeSignatureEvent *timeSig = new TimeSignatureEvent(18, 4, 2, 24, 8, track);
        timeSig->setFile(this);
        timeSig->setTrack(track, false);
        channel(18)->eventMap()->insert(0, timeSig);
    }

    // check whether TempoChangeEvent at tick 0 is given. If not, create one.
    if (!channel(17)->eventMap()->contains(0)) {
        log->append("Warning: no TempoChangeEvent detected at tick 0. Adding default value.");
        TempoChangeEvent *tempoEv = new TempoChangeEvent(17, 500000, track);
        tempoEv->setFile(this);
        tempoEv->setTrack(track, false);
        channel(17)->eventMap()->insert(0, tempoEv);
        invalidateTempoCache();
    }

    // assign channel
    int assignedChannel = 0;
    for (int i = 1; i < 16; i++) {
        if (channelFrequency[i] > channelFrequency[assignedChannel]) {
            assignedChannel = i;
        }
    }
    track->assignChannel(assignedChannel);

    return true;
}

int MidiFile::deltaTime(QDataStream *content) {
    return variableLengthvalue(content);
}

int MidiFile::variableLengthvalue(QDataStream *content) {
    quint32 v = 0;
    quint8 byte = 0;
    int bytesRead = 0;
    const int MAX_VARIABLE_LENGTH_BYTES = 4; // MIDI standard allows max 4 bytes for variable length values

    do {
        // Safety check: prevent infinite loops from malformed MIDI data
        if (bytesRead >= MAX_VARIABLE_LENGTH_BYTES) {
            return 0; // Return 0 as safe fallback
        }

        // Check if stream has data available
        if (content->atEnd()) {
            return 0;
        }

        (*content) >> byte;
        bytesRead++;

        // Check for potential overflow before shifting
        if (v > (UINT32_MAX >> 7)) {
            return 0;
        }

        v <<= 7;
        v |= (byte & 0x7F);
    } while (byte & (1 << 7));

    // Additional safety check for unreasonably large values
    if (v > 0x0FFFFFFF) { // MIDI standard maximum
        return 0;
    }
    return (int) v;
}

QMultiMap<int, MidiEvent *> *MidiFile::timeSignatureEvents() {
    return channels[18]->eventMap();
}

QMultiMap<int, MidiEvent *> *MidiFile::tempoEvents() {
    return channels[17]->eventMap();
}

void MidiFile::calcMaxTime() {
    // The file's length in ms is the last tempo anchor's time plus the tail
    // that runs from it to endTick() at that anchor's tempo - which is exactly
    // what the accumulating walk this replaces computed. Reading it off the
    // cached anchors matters because the loader calls calcMaxTime() once per
    // event that extends the file, so on a dense tempo ramp the old walk was
    // quadratic in the number of tempo events all by itself.
    //
    // DOCUMENT THREAD ONLY (see the header): maxTimeMS is a plain int that
    // maxTime() hands out unsynchronised, and recalcWidgetSize() below runs
    // repainting slots.
    double time = 0;
    {
        // The snapshot pointer keeps the anchors alive for as long as this
        // scope needs them; the cache lock is already released inside
        // tempoSnapshot().
        const TempoSnapshotPtr anchors = tempoSnapshot();
        if (!anchors->empty()) {
            const TempoAnchor &last = anchors->back();
            time = last.msAtTick + last.msPerTick * (midiTicks - last.tick);
        }
    }
    maxTimeMS = (int) time;
    // Emitted with no lock held on purpose: recalcWidgetSize() is connected to
    // widgets that repaint, and a repaint asks msOfTick() again - which would
    // deadlock on the non-recursive mutex.
    emit recalcWidgetSize();
}

int MidiFile::maxTime() {
    return maxTimeMS;
}

int MidiFile::endTick() {
    return midiTicks;
}

void MidiFile::setEndTick(int tick) {
    if (tick == midiTicks) return;
    if (tick < 0) tick = 0;
    ProtocolEntry *toCopy = copy();
    midiTicks = tick;
    ProtocolEntry::protocol(toCopy, this);
    calcMaxTime();
}

int MidiFile::timeMS(int midiTime) {
    return msOfTick(midiTime);
}

void MidiFile::invalidateTempoCache() {
    QMutexLocker locker(&_tempoCacheMutex);
    // Only marks it stale. The currently published snapshot stays published on
    // purpose - see the header: an off-thread query must keep getting real
    // numbers, and the document thread rebuilds on its very next query anyway.
    _tempoCacheValid = false;
}

bool MidiFile::onOwnerThread() const {
    // QObject affinity is the document's thread: the thread the file was
    // constructed on (the GUI thread for every document the editor opens), or
    // whatever moveToThread() last handed it to. Nothing ever moves a MidiFile
    // today, so this is simply "am I the GUI thread?" - written against the
    // affinity rather than against qApp so it stays correct if that changes.
    QThread *owner = thread();
    return owner == nullptr || owner == QThread::currentThread();
}

MidiFile::TempoSnapshotPtr MidiFile::tempoSnapshot() {
    QMutexLocker locker(&_tempoCacheMutex);
    if (onOwnerThread()) {
        // The ONLY place channel 17 is iterated for the cache, and it runs on
        // the only thread that is allowed to look at that map at all.
        rebuildTempoCacheLocked();
    }
    // Off the document thread we deliberately do NOT rebuild: walking the map
    // here is the use-after-free this design exists to remove (undo replaces
    // the whole QMultiMap, Thin Tempo Map deletes half of it). Answering from
    // the last published anchors is at most one edit stale and self-corrects.
    if (!_tempoCache) {
        // Nothing published yet. Shared so this costs one allocation per
        // process, not one per query.
        static const TempoSnapshotPtr empty = std::make_shared<const TempoSnapshot>();
        return empty;
    }
    return _tempoCache;   // counted copy taken under the lock: the array it
                          // points at can no longer be swapped away from us
}

void MidiFile::rebuildTempoCacheLocked() {
    // PRECONDITIONS: _tempoCacheMutex held AND we are on the document thread.
    // Must not call a public method of this class - the mutex is not recursive.
    const quint64 revision = MidiChannel::tempoRevision();
    // Protocol snapshots (MidiFile(int, Protocol*)) have no channels at all.
    MidiChannel *tempoChannel = channels[17];
    QMultiMap<int, MidiEvent *> *map = tempoChannel ? tempoChannel->eventMap() : nullptr;
    const int eventCount = map ? (int) map->size() : 0;

    if (_tempoCache && _tempoCacheValid && _tempoCacheRevision == revision
        && _tempoCacheEventCount == eventCount) {
        return;
    }

    // ONE linear walk into a FRESH array - roughly the work the old msOfTick()
    // did per query. The array in _tempoCache is never touched: a reader on
    // another thread may be binary-searching it right now, and it keeps its own
    // reference, so it stays valid until it lets go.
    std::shared_ptr<TempoSnapshot> next = std::make_shared<TempoSnapshot>();
    if (map) {
        next->reserve((size_t) (eventCount > 0 ? eventCount : 0));
        for (auto it = map->constBegin(); it != map->constEnd(); ++it) {
            TempoChangeEvent *ev = dynamic_cast<TempoChangeEvent *>(it.value());
            if (!ev) {
                continue;
            }
            TempoAnchor anchor;
            anchor.tick = ev->midiTime();
            // The FIRST anchor always starts at 0 ms even when it does not sit
            // on tick 0 - that is what the linear walk this replaces did, and
            // the whole timeline is anchored on it.
            anchor.msAtTick = 0.0;
            if (!next->empty()) {
                const TempoAnchor &last = next->back();
                anchor.msAtTick = last.msAtTick + last.msPerTick * (anchor.tick - last.tick);
            }
            double msPerTick = ev->msPerTick();
            if (!(msPerTick > 0.0)) {
                msPerTick = 1.0; // never divide by zero in tickOfMs
            }
            anchor.msPerTick = msPerTick;
            anchor.event = ev;
            next->push_back(anchor);
        }
    }

    _tempoCache = next;          // publish: one pointer swap under the lock
    _tempoCacheValid = true;
    _tempoCacheRevision = revision;
    _tempoCacheEventCount = eventCount;
}

int MidiFile::tempoAnchorIndexForTick(const TempoSnapshot &anchors, int tick) {
    if (anchors.empty()) {
        return -1;
    }
    auto it = std::upper_bound(anchors.begin(), anchors.end(), tick,
                               [](int t, const TempoAnchor &a) { return t < a.tick; });
    if (it == anchors.begin()) {
        return 0;
    }
    return (int) (it - anchors.begin()) - 1;
}

int MidiFile::tempoAnchorIndexForMs(const TempoSnapshot &anchors, double ms) {
    if (anchors.empty()) {
        return -1;
    }
    auto it = std::upper_bound(anchors.begin(), anchors.end(), ms,
                               [](double m, const TempoAnchor &a) { return m < a.msAtTick; });
    if (it == anchors.begin()) {
        return 0;
    }
    return (int) (it - anchors.begin()) - 1;
}

double MidiFile::msOfTickCached(int tick) {
    // Search and read happen with NO lock held - they do not need one. The
    // snapshot is const and the reference below keeps it alive, so no other
    // thread can change or free what is being read.
    const TempoSnapshotPtr anchors = tempoSnapshot();
    const int i = tempoAnchorIndexForTick(*anchors, tick);
    if (i < 0) {
        return 0.0;
    }
    const TempoAnchor &a = (*anchors)[(size_t) i];
    return a.msAtTick + a.msPerTick * (tick - a.tick);
}

int MidiFile::tick(int ms) {
    // O(log n) inverse of msOfTick(): msAtTick grows monotonically with the
    // anchor index (msPerTick is always positive), so the same sorted array is
    // binary-searchable from either side.
    //
    // Called from PlayerThread::timeout() every 15 ms - on the player thread,
    // which therefore only ever reads the published snapshot (see the header).
    const TempoSnapshotPtr anchors = tempoSnapshot();
    const int i = tempoAnchorIndexForMs(*anchors, (double) ms);
    if (i < 0) {
        return 0;
    }
    const TempoAnchor &a = (*anchors)[(size_t) i];
    return (int) ((ms - a.msAtTick) / a.msPerTick + a.tick);
}

int MidiFile::msOfTick(int tick, QList<MidiEvent *> *events, int
                       msOfFirstEventInList) {
    if (!events) {
        // Fast path: O(log n) lookup in the cached tempo map. This runs per
        // grid line, note and cursor on every paint and once per event of the
        // file on every Play press, so it must not walk the map - a file with
        // a DAW-exported tempo ramp (>12k tempo events) froze the editor and
        // needed ~10 s to start playback when this was a linear scan.
        //
        // Thread-safe: msOfTickCached() reads an immutable published snapshot.
        // PlayerThread asks this for its start position while the GUI thread
        // edits; off the document thread the answer may be one edit stale.
        return (int) msOfTickCached(tick);
    }

    // The list form below is the caller's own window into the tempo map and
    // touches no shared state of this object - no lock, no thread-safety
    // promise beyond the caller's own list.

    // timeMs holds the time of the current tick
    double timeMs = 0;

    // event is the previous TempoChangeEvent in the list, ev the current
    TempoChangeEvent *event = 0;
    for (int i = 0; i < events->length(); i++) {
        TempoChangeEvent *ev = dynamic_cast<TempoChangeEvent *>(events->at(i));
        if (!ev) {
            continue;
        }
        if (!event || ev->midiTime() <= tick) {
            if (!event) {
                // first event in the list at time msOfFirstEventInList
                timeMs = msOfFirstEventInList;
            } else {
                // any event before the endTick
                timeMs += event->msPerTick() * (ev->midiTime() - event->midiTime());
            }
            event = ev;
        } else {
            // end: ev is later than the endTick
            break;
        }
    }

    if (!event) {
        return 0;
    }

    timeMs += event->msPerTick() * (tick - event->midiTime());
    return (int) timeMs;
}

int MidiFile::tick(int startms, int endms, QList<MidiEvent *> **eventList,
                   int *endTick, int *msOfFirstEvent) {
    // delete old eventList, create a new
    if ((*eventList)) {
        delete (*eventList);
    }
    *eventList = new QList<MidiEvent *>;

    // MatrixWidget asks this once per paint for the visible time window. It
    // used to copy the whole tempo map into a QList and walk it from the top,
    // which is why scrolling a dense tempo ramp crawled. The cached anchors
    // give the start in O(log n); the walk that follows only covers the tempo
    // events INSIDE the window, and the arithmetic is the same as before.
    //
    // DOCUMENT THREAD ONLY (see the header): the list it fills holds pointers
    // to live tempo events.
    //
    // No lock is held across the walk below. It walks a snapshot this scope
    // holds a counted reference to, so the array cannot change or be freed
    // while it is being read - which is also why the previous version's
    // whole-walk critical section is gone: it serialised every repaint against
    // every other timing query for no added safety.
    const TempoSnapshotPtr anchorsPtr = tempoSnapshot();
    const TempoSnapshot &anchors = *anchorsPtr;
    const int n = (int) anchors.size();
    // Guard before the deref: if the tempo track (channel 17) carried no usable
    // TempoChangeEvent there is nothing to anchor on. Normal files always have
    // a tick-0 tempo so this isn't hit in practice, but the guard before
    // *endTick (below) used to be too late to stop the deref (BUG-CORE-012).
    if (n == 0) {
        return 0;
    }

    int i = tempoAnchorIndexForMs(anchors, (double) startms);
    if (i < 0) {
        return 0;
    }

    // holds the time of the current event, and the time of the one after it
    double time = anchors[(size_t) i].msAtTick;
    double timeMsNextEvent = (i + 1 < n) ? anchors[(size_t) (i + 1)].msAtTick : time;

    const int startTick = (int) ((startms - time) / anchors[(size_t) i].msPerTick
                                 + anchors[(size_t) i].tick);
    *msOfFirstEvent = (int) time;
    (*eventList)->append(anchors[(size_t) i].event);

    // index of the anchor the end tick is measured from
    int last = i;
    i++;

    // Collect the tempo events between start and end and get the endTick.
    //
    // No duplicate check here, on purpose. Every anchor of a snapshot comes
    // from a DISTINCT node of channel 17's QMultiMap, and an event lives at
    // exactly one (tick, event) position in that map - MidiEvent::setMidiTime()
    // removes before it re-inserts - so two anchors can never carry the same
    // event pointer. The loop also visits each index at most once, and starts
    // one past the index appended above. The QList::contains() that used to
    // stand here could therefore never fire, while costing an O(k) scan per
    // appended event: O(k^2) per repaint on a dense tempo ramp, which is the
    // exact case the cache was built for.
    for (; i < n && timeMsNextEvent < endms; i++) {
        last = i;
        (*eventList)->append(anchors[(size_t) i].event);

        time = timeMsNextEvent;

        if (i >= n - 1) {
            break;
        }
        timeMsNextEvent = anchors[(size_t) (i + 1)].msAtTick;
        if (timeMsNextEvent > endms) {
            break;
        }
    }

    *endTick = (int) ((endms - time) / anchors[(size_t) last].msPerTick
                      + anchors[(size_t) last].tick);
    return startTick;
}

int MidiFile::numTracks() {
    return _tracks->size();
}

int MidiFile::measure(int startTick, int endTick,
                      QList<TimeSignatureEvent *> **eventList, int *ticksInmeasure) {
    if ((*eventList)) {
        delete (*eventList);
    }
    *eventList = new QList<TimeSignatureEvent *>;
    QList<MidiEvent *> events = channels[18]->eventMap()->values();
    TimeSignatureEvent *event = 0;
    int i = 0;
    int measure = 1;

    // find the startEvent and the firstTick
    for (; i < events.length(); i++) {
        TimeSignatureEvent *ev = dynamic_cast<TimeSignatureEvent *>(events.at(i));
        if (!ev) {
            qWarning("unknown eventtype in the List [2]");
            continue;
        }
        if (!event) {
            event = ev;
            measure = 1;
            continue;
        } else {
            if (ev->midiTime() <= startTick) {
                int ticks = ev->midiTime() - event->midiTime();
                measure += event->measures(ticks);
                event = ev;
            } else {
                break;
            }
        }
    }
    if (!event) {
        // Channel 18 holds no TimeSignatureEvent at all. Normally impossible
        // (the ctor and the reader both plant one at tick 0), but an edit that
        // removes the tick-0 event - or a file whose time signature failed to
        // parse - used to make this dereference null. Fall back to 4/4; the
        // returned event list stays empty, callers iterate it rather than
        // index into it.
        const int ticksPerMeasure = ticksPerMeasureOfMeter(4, 2);
        const int barIndex = (startTick > 0) ? (startTick / ticksPerMeasure) : 0;
        if (ticksInmeasure) {
            *ticksInmeasure = startTick - barIndex * ticksPerMeasure;
        }
        return barIndex + 1;
    }
    int ticks = startTick - event->midiTime();
    measure += event->measures(ticks, ticksInmeasure);
    (*eventList)->append(event);

    // find the endEvent, save all events between start and end in the list and
    // get the endTick
    for (; i < events.length(); i++) {
        TimeSignatureEvent *ev = dynamic_cast<TimeSignatureEvent *>(events.at(i));

        if (!ev) {
            qWarning("unknown eventtype in the List [2]");
            continue;
        }
        if (ev->midiTime() <= endTick) {
            (*eventList)->append(ev);
        } else {
            break;
        }
    }
    return measure;
}

int MidiFile::measure(int startTick, int *startTickOfMeasure, int *endTickOfMeasure) {
    QList<MidiEvent *> events = channels[18]->eventMap()->values();
    TimeSignatureEvent *event = 0;
    int i = 0;
    int measure = 1;

    // find the startEvent and the firstTick
    for (; i < events.length(); i++) {
        TimeSignatureEvent *ev = dynamic_cast<TimeSignatureEvent *>(events.at(i));
        if (!ev) {
            qWarning("unknown eventtype in the List [2]");
            continue;
        }
        if (!event) {
            event = ev;
            measure = 1;
            continue;
        } else {
            if (ev->midiTime() <= startTick) {
                int ticks = ev->midiTime() - event->midiTime();
                measure += event->measures(ticks);
                event = ev;
            } else {
                break;
            }
        }
    }
    if (!event) {
        // No TimeSignatureEvent on channel 18 - see the sibling overload. This
        // one is the hot path: the measure tool asks it on every paint, so a
        // null dereference here is an access violation on the next mouse move.
        const int ticksPerMeasure = ticksPerMeasureOfMeter(4, 2);
        const int barIndex = (startTick > 0) ? (startTick / ticksPerMeasure) : 0;
        *(startTickOfMeasure) = barIndex * ticksPerMeasure;
        *(endTickOfMeasure) = *startTickOfMeasure + ticksPerMeasure;
        return barIndex + 1;
    }
    int ticks = startTick - event->midiTime();
    int ticksInmeasure;
    measure += event->measures(ticks, &ticksInmeasure);
    *(startTickOfMeasure) = startTick - ticksInmeasure;
    *(endTickOfMeasure) = *startTickOfMeasure + event->ticksPerMeasure();
    return measure;
}

int MidiFile::measureCount() {
    // The bar of the last SOUNDING tick. endTick() is the exclusive end of the
    // song, so measure(endTick()) reports one bar too many for every file that
    // ends exactly on a bar line - which is every fresh file (7680 ticks = 10
    // bars of 4/4).
    const int lastTick = endTick() - 1;
    if (lastTick < 0) {
        return 1;
    }
    int startOfMeasure = 0, endOfMeasure = 0;
    return qMax(1, measure(lastTick, &startOfMeasure, &endOfMeasure));
}

int MidiFile::ticksPerQuarter() {
    return timePerQuarter;
}

QMultiMap<int, MidiEvent *> *MidiFile::channelEvents(int channel) {
    // Add bounds checking to prevent crashes
    if (channel < 0 || channel >= 19) {
        return channels[0]->eventMap(); // Return a safe default
    }

    // Additional safety check for null channels
    if (!channels[channel]) {
        return channels[0]->eventMap(); // Return a safe default
    }

    return channels[channel]->eventMap();
}

Protocol *MidiFile::protocol() {
    return prot;
}

LyricManager *MidiFile::lyricManager() {
    return _lyricManager;
}

MidiChannel *MidiFile::channel(int i) {
    // Add bounds checking to prevent crashes
    if (i < 0 || i >= 19) {
        return channels[0]; // Return a safe default
    }

    // Additional safety check for null channels
    if (!channels[i]) {
        return channels[0]; // Return a safe default
    }

    return channels[i];
}

QString MidiFile::instrumentName(int prog) {
    QString customName = InstrumentDefinitions::instance()->instrumentName(prog);
    if (!customName.isEmpty()) {
        return customName;
    }

    return gmInstrumentName(prog);
}

QString MidiFile::gmInstrumentName(int prog) {
    switch (prog + 1) {
        case 1: {
            return tr("Acoustic Grand Piano");
        }
        case 2: {
            return tr("Bright Acoustic Piano");
        }
        case 3: {
            return tr("Electric Grand Piano");
        }
        case 4: {
            return tr("Honky-tonk Piano");
        }
        case 5: {
            return tr("Electric Piano 1");
        }
        case 6: {
            return tr("Electric Piano 2");
        }
        case 7: {
            return tr("Harpsichord");
        }
        case 8: {
            return tr("Clavinet (Clavi)");
        }
        case 9: {
            return tr("Celesta");
        }
        case 10: {
            return tr("Glockenspiel");
        }
        case 11: {
            return tr("Music Box");
        }
        case 12: {
            return tr("Vibraphone");
        }
        case 13: {
            return tr("Marimba");
        }
        case 14: {
            return tr("Xylophone");
        }
        case 15: {
            return tr("Tubular Bells");
        }
        case 16: {
            return tr("Dulcimer");
        }
        case 17: {
            return tr("Drawbar Organ");
        }
        case 18: {
            return tr("Percussive Organ");
        }
        case 19: {
            return tr("Rock Organ");
        }
        case 20: {
            return tr("Church Organ");
        }
        case 21: {
            return tr("Reed Organ");
        }
        case 22: {
            return tr("Accordion");
        }
        case 23: {
            return tr("Harmonica");
        }
        case 24: {
            return tr("Tango Accordion");
        }
        case 25: {
            return tr("Acoustic Guitar (nylon)");
        }
        case 26: {
            return tr("Acoustic Guitar (steel)");
        }
        case 27: {
            return tr("Electric Guitar (jazz)");
        }
        case 28: {
            return tr("Electric Guitar (clean)");
        }
        case 29: {
            return tr("Electric Guitar (muted)");
        }
        case 30: {
            return tr("Overdriven Guitar");
        }
        case 31: {
            return tr("Distortion Guitar");
        }
        case 32: {
            return tr("Guitar harmonics");
        }
        case 33: {
            return tr("Acoustic Bass");
        }
        case 34: {
            return tr("Electric Bass (finger)");
        }
        case 35: {
            return tr("Electric Bass (pick)");
        }
        case 36: {
            return tr("Fretless Bass");
        }
        case 37: {
            return tr("Slap Bass 1");
        }
        case 38: {
            return tr("Slap Bass 2");
        }
        case 39: {
            return tr("Synth Bass 1");
        }
        case 40: {
            return tr("Synth Bass 2");
        }
        case 41: {
            return tr("Violin");
        }
        case 42: {
            return tr("Viola");
        }
        case 43: {
            return tr("Cello");
        }
        case 44: {
            return tr("Contrabass");
        }
        case 45: {
            return tr("Tremolo Strings");
        }
        case 46: {
            return tr("Pizzicato Strings");
        }
        case 47: {
            return tr("Orchestral Harp");
        }
        case 48: {
            return tr("Timpani");
        }
        case 49: {
            return tr("String Ensemble 1");
        }
        case 50: {
            return tr("String Ensemble 2");
        }
        case 51: {
            return tr("Synth Strings 1");
        }
        case 52: {
            return tr("Synth Strings 2");
        }
        case 53: {
            return tr("Choir Aahs");
        }
        case 54: {
            return tr("Voice Oohs");
        }
        case 55: {
            return tr("Synth Choir");
        }
        case 56: {
            return tr("Orchestra Hit");
        }
        case 57: {
            return tr("Trumpet");
        }
        case 58: {
            return tr("Trombone");
        }
        case 59: {
            return tr("Tuba");
        }
        case 60: {
            return tr("Muted Trumpet");
        }
        case 61: {
            return tr("French Horn");
        }
        case 62: {
            return tr("Brass Section");
        }
        case 63: {
            return tr("Synth Brass 1");
        }
        case 64: {
            return tr("Synth Brass 2");
        }
        case 65: {
            return tr("Soprano Sax");
        }
        case 66: {
            return tr("Alto Sax");
        }
        case 67: {
            return tr("Tenor Sax");
        }
        case 68: {
            return tr("Baritone Sax");
        }
        case 69: {
            return tr("Oboe");
        }
        case 70: {
            return tr("English Horn");
        }
        case 71: {
            return tr("Bassoon");
        }
        case 72: {
            return tr("Clarinet");
        }
        case 73: {
            return tr("Piccolo");
        }
        case 74: {
            return tr("Flute");
        }
        case 75: {
            return tr("Recorder");
        }
        case 76: {
            return tr("Pan Flute");
        }
        case 77: {
            return tr("Blown Bottle");
        }
        case 78: {
            return tr("Shakuhachi");
        }
        case 79: {
            return tr("Whistle");
        }
        case 80: {
            return tr("Ocarina");
        }
        case 81: {
            return tr("Lead 1 (square)");
        }
        case 82: {
            return tr("Lead 2 (sawtooth)");
        }
        case 83: {
            return tr("Lead 3 (calliope)");
        }
        case 84: {
            return tr("Lead 4 (chiff)");
        }
        case 85: {
            return tr("Lead 5 (charang)");
        }
        case 86: {
            return tr("Lead 6 (voice)");
        }
        case 87: {
            return tr("Lead 7 (fifths)");
        }
        case 88: {
            return tr("Lead 8 (bass + lead)");
        }
        case 89: {
            return tr("Pad 1 (new age)");
        }
        case 90: {
            return tr("Pad 2 (warm)");
        }
        case 91: {
            return tr("Pad 3 (polysynth)");
        }
        case 92: {
            return tr("Pad 4 (choir)");
        }
        case 93: {
            return tr("Pad 5 (bowed)");
        }
        case 94: {
            return tr("Pad 6 (metallic)");
        }
        case 95: {
            return tr("Pad 7 (halo)");
        }
        case 96: {
            return tr("Pad 8 (sweep)");
        }
        case 97: {
            return tr("FX 1 (rain)");
        }
        case 98: {
            return tr("FX 2 (soundtrack)");
        }
        case 99: {
            return tr("FX 3 (crystal)");
        }
        case 100: {
            return tr("FX 4 (atmosphere)");
        }
        case 101: {
            return tr("FX 5 (brightness)");
        }
        case 102: {
            return tr("FX 6 (goblins)");
        }
        case 103: {
            return tr("FX 7 (echoes)");
        }
        case 104: {
            return tr("FX 8 (sci-fi)");
        }
        case 105: {
            return tr("Sitar");
        }
        case 106: {
            return tr("Banjo");
        }
        case 107: {
            return tr("Shamisen");
        }
        case 108: {
            return tr("Koto");
        }
        case 109: {
            return tr("Kalimba");
        }
        case 110: {
            return tr("Bag pipe");
        }
        case 111: {
            return tr("Fiddle");
        }
        case 112: {
            return tr("Shanai");
        }
        case 113: {
            return tr("Tinkle Bell");
        }
        case 114: {
            return tr("Agogo");
        }
        case 115: {
            return tr("Steel Drums");
        }
        case 116: {
            return tr("Woodblock");
        }
        case 117: {
            return tr("Taiko Drum");
        }
        case 118: {
            return tr("Melodic Tom");
        }
        case 119: {
            return tr("Synth Drum");
        }
        case 120: {
            return tr("Reverse Cymbal");
        }
        case 121: {
            return tr("Guitar Fret Noise");
        }
        case 122: {
            return tr("Breath Noise");
        }
        case 123: {
            return tr("Seashore");
        }
        case 124: {
            return tr("Bird Tweet");
        }
        case 125: {
            return tr("Telephone Ring");
        }
        case 126: {
            return tr("Helicopter");
        }
        case 127: {
            return tr("Applause");
        }
        case 128: {
            return tr("Gunshot");
        }
    }
    return tr("out of range");
}

QString MidiFile::controlChangeName(int control) {
    QString customName = InstrumentDefinitions::instance()->controlChangeName(control);
    if (!customName.isEmpty()) {
        return customName;
    }

    switch (control) {
        case 0: {
            return tr("Bank Select (MSB)");
        }
        case 1: {
            return tr("Modulation Wheel (MSB)");
        }
        case 2: {
            return tr("Breath Controller (MSB)");
        }

        case 4: {
            return tr("Foot Controller (MSB)");
        }
        case 5: {
            return tr("Portamento Time (MSB)");
        }
        case 6: {
            return tr("Data Entry (MSB)");
        }
        case 7: {
            return tr("Channel Volume (MSB)");
        }
        case 8: {
            return tr("Balance (MSB)");
        }

        case 10: {
            return tr("Pan (MSB)");
        }
        case 11: {
            return tr("Expression (MSB)");
        }
        case 12: {
            return tr("Effect Control 1 (MSB)");
        }
        case 13: {
            return tr("Effect Control 2 (MSB)");
        }

        case 16: {
            return tr("General Purpose Controller 1 (MSB)");
        }
        case 17: {
            return tr("General Purpose Controller 2 (MSB)");
        }
        case 18: {
            return tr("General Purpose Controller 3 (MSB)");
        }
        case 19: {
            return tr("General Purpose Controller 4 (MSB)");
        }

        case 32: {
            return tr("Bank Select (LSB)");
        }
        case 33: {
            return tr("Modulation Wheel (LSB)");
        }
        case 34: {
            return tr("Breath Controller (LSB)");
        }

        case 36: {
            return tr("Foot Controller (LSB)");
        }
        case 37: {
            return tr("Portamento Time (LSB)");
        }
        case 38: {
            return tr("Data Entry (LSB) Channel");
        }
        case 39: {
            return tr("Channel Volume (LSB)");
        }
        case 40: {
            return tr("Balance (LSB)");
        }

        case 42: {
            return tr("Pan (LSB)");
        }
        case 43: {
            return tr("Expression (LSB)");
        }
        case 44: {
            return tr("Effect Control 1 (LSB)");
        }
        case 45: {
            return tr("Effect Control 2 (LSB)");
        }

        case 48: {
            return tr("General Purpose Controller 1 (LSB)");
        }
        case 49: {
            return tr("General Purpose Controller 2 (LSB)");
        }
        case 50: {
            return tr("General Purpose Controller 3 (LSB)");
        }
        case 51: {
            return tr("General Purpose Controller 4 (LSB)");
        }

        case 64: {
            return tr("Sustain Pedal");
        }
        case 65: {
            return tr("Portamento On/Off");
        }
        case 66: {
            return tr("Sostenuto");
        }
        case 67: {
            return tr("Soft Pedal");
        }
        case 68: {
            return tr("Legato Footswitch");
        }
        case 69: {
            return tr("Hold 2");
        }
        case 70: {
            return tr("Sound Controller 1");
        }
        case 71: {
            return tr("Sound Controller 2");
        }
        case 72: {
            return tr("Sound Controller 3");
        }
        case 73: {
            return tr("Sound Controller 4");
        }
        case 74: {
            return tr("Sound Controller 5");
        }
        case 75: {
            return tr("Sound Controller 6");
        }
        case 76: {
            return tr("Sound Controller 7");
        }
        case 77: {
            return tr("Sound Controller 8");
        }
        case 78: {
            return tr("Sound Controller 9");
        }
        case 79: {
            return tr("Sound Controller 10 (GM2 default: Undefined)");
        }
        case 80: {
            return tr("General Purpose Controller 5");
        }
        case 81: {
            return tr("General Purpose Controller 6");
        }
        case 82: {
            return tr("General Purpose Controller 7");
        }
        case 83: {
            return tr("General Purpose Controller 8");
        }
        case 84: {
            return tr("Portamento Control");
        }

        case 91: {
            return tr("Effects 1 Depth (default: Reverb Send)");
        }
        case 92: {
            return tr("Effects 2 Depth (default: Tremolo Depth)");
        }
        case 93: {
            return tr("Effects 3 Depth (default: Chorus Send)");
        }
        case 94: {
            return tr("Effects 4 Depth (default: Celeste [Detune] Depth)");
        }
        case 95: {
            return tr("Effects 5 Depth (default: Phaser Depth)");
        }
        case 96: {
            return tr("Data Increment");
        }
        case 97: {
            return tr("Data Decrement");
        }
        case 98: {
            return tr("Non-Registered Parameter Number (LSB)");
        }
        case 99: {
            return tr("Non-Registered Parameter Number(MSB)");
        }
        case 100: {
            return tr("Registered Parameter Number (LSB)");
        }
        case 101: {
            return tr("Registered Parameter Number(MSB)");
        }

        case 120: {
            return tr("All Sound Off");
        }
        case 121: {
            return tr("Reset All Controllers");
        }
        case 122: {
            return tr("Local Control On/Off");
        }
        case 123: {
            return tr("All Notes Off");
        }
        case 124: {
            return tr("Omni Mode Off");
        }
        case 125: {
            return tr("Omni Mode On");
        }
        case 126: {
            return tr("Poly Mode Off");
        }
        case 127: {
            return tr("Poly Mode On");
        }
    }

    return tr("undefined");
}

QList<MidiEvent *> *MidiFile::eventsBetween(int start, int end) {
    QList<MidiEvent *> *eventList = new QList<MidiEvent *>;
    for (int i = 0; i < 19; i++) {
        QMultiMap<int, MidiEvent *> *events = channels[i]->eventMap();
        QMultiMap<int, MidiEvent *>::iterator current = events->lowerBound(start);
        QMultiMap<int, MidiEvent *>::iterator upperBound = events->upperBound(end);
        while (current != upperBound) {
            if (!eventList->contains(current.value())) {
                eventList->append(current.value());
            }
            current++;
        }
    }
    return eventList;
}

bool MidiFile::channelMuted(int ch) {
    // all general channels
    if (ch > 15) {
        return false;
    }

    // check solochannel
    for (int i = 0; i < 17; i++) {
        if (channel(i)->solo()) {
            return i != ch;
        }
    }

    return channel(ch)->mute();
}

void MidiFile::preparePlayerData(int tickFrom) {
    playerMap->clear();
    QList<MidiEvent *> *prgList;

    for (int i = 0; i < 19; i++) {
        if (channelMuted(i)) {
            continue;
        }

        // prgList saves all ProgramChangeEvents before cursorPosition. The last
        // will be sent when playing
        prgList = new QList<MidiEvent *>;

        QMultiMap<int, MidiEvent *> *channelEvents = channels[i]->eventMap();
        QMultiMap<int, MidiEvent *>::iterator it = channelEvents->begin();

        while (it != channelEvents->end()) {
            int tick = it.key();
            MidiEvent *event = it.value();
            if (tick >= tickFrom) {
                // all Events after cursorTick are added
                int ms = msOfTick(tick);
                if (!event->track()->muted()) {
                    playerMap->insert(ms, event);
                }
            } else {
                ProgChangeEvent *prg = dynamic_cast<ProgChangeEvent *>(event);
                if (prg) {
                    // save ProgramChenges in the list, the last will be added
                    // to the playerMap later
                    prgList->append(prg);
                }
                ControlChangeEvent *ctrl = dynamic_cast<ControlChangeEvent *>(event);
                if (ctrl) {
                    // insert all ControlChanges on first position
                    // playerMap->insert(msOfTick(cursorTick())-1, ctrl);
                }
            }
            it++;
        }

        if (prgList->count() > 0) {
            // set the program of the channel
            playerMap->insert(msOfTick(tickFrom) - 1, prgList->last());
        }

        delete prgList;
        prgList = 0;
    }
}

QMultiMap<int, MidiEvent *> *MidiFile::playerData() {
    return playerMap;
}

int MidiFile::cursorTick() {
    return _cursorTick;
}

void MidiFile::setCursorTick(int tick) {
    _cursorTick = tick;
    emit cursorPositionChanged();
}

int MidiFile::pauseTick() {
    return _pauseTick;
}

void MidiFile::setPauseTick(int tick) {
    _pauseTick = tick;
}

bool MidiFile::save(QString path, bool skipMutedTrackEvents,
                    const QHash<QString, int> &drumProgramByTrackName) {
    QFile f(path);

    if (!f.open(QIODevice::WriteOnly)) {
        return false;
    }

    QDataStream stream(&f);
    stream.setByteOrder(QDataStream::BigEndian);

    // All Events are stored in allEvents. This is because the data has to be
    // saved by tracks and not by channels
    QMultiMap<int, MidiEvent *> allEvents = QMultiMap<int, MidiEvent *>();
    for (int i = 0; i < 19; i++) {
        QMultiMap<int, MidiEvent *>::iterator it = channels[i]->eventMap()->begin();
        while (it != channels[i]->eventMap()->end()) {
            allEvents.insert(it.key(), it.value());
            it++;
        }
    }

    QByteArray data = QByteArray();
    data.append('M');
    data.append('T');
    data.append('h');
    data.append('d');

    int trackL = 6;
    for (int i = 3; i >= 0; i--) {
        data.append((qint8) ((trackL & (0xFF << 8 * i)) >> 8 * i));
    }

    for (int i = 1; i >= 0; i--) {
        data.append((qint8) ((_midiFormat & (0xFF << 8 * i)) >> 8 * i));
    }

    for (int i = 1; i >= 0; i--) {
        data.append((qint8) ((numTracks() & (0xFF << 8 * i)) >> 8 * i));
    }

    for (int i = 1; i >= 0; i--) {
        data.append((qint8) ((timePerQuarter & (0xFF << 8 * i)) >> 8 * i));
    }

    for (int num = 0; num < numTracks(); num++) {
        data.append('M');
        data.append('T');
        data.append('r');
        data.append('k');

        int trackLengthPos = data.size();

        data.append('\0');
        data.append('\0');
        data.append('\0');
        data.append('\0');

        int numBytes = 0;
        int currentTick = 0;
        QMultiMap<int, MidiEvent *>::iterator it = allEvents.begin();
        while (it != allEvents.end()) {
            MidiEvent *event = it.value();
            int tick = it.key();

            if (_tracks->at(num) == event->track() &&
                !(skipMutedTrackEvents && _tracks->at(num)->muted())) {
                // Inject a CH9 Program Change before the very first
                // CH9 NoteOn from a known-percussion track so offline
                // FluidSynth resolves the right FFXIV drum preset
                // even when the MIDI key isn't on the GM drum kit
                // map (e.g. Snare Drum hits living on key 60 would
                // otherwise fall through to the Bongo preset).
                if (!drumProgramByTrackName.isEmpty()) {
                    NoteOnEvent *noteOn = dynamic_cast<NoteOnEvent *>(event);
                    if (noteOn && noteOn->channel() == 9 && noteOn->velocity() > 0) {
                        const QString tname = _tracks->at(num)->name();
                        auto it = drumProgramByTrackName.constFind(tname);
                        if (it != drumProgramByTrackName.constEnd() && it.value() >= 0) {
                            // delta-time 0 PC right before the NoteOn
                            QByteArray dt = writeDeltaTime(tick - currentTick);
                            numBytes += dt.size();
                            data.append(dt);
                            data.append(static_cast<char>(0xC9));
                            data.append(static_cast<char>(it.value() & 0x7F));
                            numBytes += 2;
                            currentTick = tick;
                        }
                    }
                }
                // write the deltaTime before the event
                int time = tick - currentTick;
                QByteArray deltaTime = writeDeltaTime(time);
                numBytes += deltaTime.size();
                data.append(deltaTime);

                // write the events data
                QByteArray eventData = event->save();
                numBytes += eventData.size();
                data.append(eventData);

                // save this tick as last time
                currentTick = tick;
            }

            it++;
        }

        // write the endEvent
        int time = endTick() - currentTick;
        QByteArray deltaTime = writeDeltaTime(time);
        numBytes += deltaTime.size();
        data.append(deltaTime);
        data.append(char(0xFF));
        data.append(char(0x2F));
        data.append('\0');
        numBytes += 3;

        // write numBytes
        for (int i = 3; i >= 0; i--) {
            data[trackLengthPos + 3 - i] = ((qint8) ((numBytes & (0xFF << 8 * i)) >> 8 * i));
        }
    }

    // write data to the filestream
    for (int i = 0; i < data.size(); i++) {
        stream << (qint8) (data.at(i));
    }

    // close the file
    f.close();

    _saved = true;

    return true;
}

QByteArray MidiFile::writeDeltaTime(int time) {
    return writeVariableLengthValue(time);
}

QByteArray MidiFile::writeVariableLengthValue(int value) {
    QByteArray array = QByteArray();

    bool isFirst = true;
    for (int i = 3; i >= 0; i--) {
        int b = value >> (7 * i);
        qint8 byte = (qint8) b & 127;
        if (!isFirst || byte > 0 || i == 0) {
            isFirst = false;
            if (i > 0) {
                // set 8th bit
                byte |= 128;
            }
            array.append(byte);
        }
    }

    return array;
}

QString MidiFile::path() {
    return _path;
}

void MidiFile::setPath(QString path) {
    _path = path;
}

bool MidiFile::saved() {
    return _saved;
}

void MidiFile::setSaved(bool b) {
    _saved = b;
}

void MidiFile::setMaxLengthMs(int ms) {
    ProtocolEntry *toCopy = copy();
    int oldTicks = midiTicks;
    midiTicks = tick(ms);
    ProtocolEntry::protocol(toCopy, this);
    if (midiTicks < oldTicks) {
        // remove events after maxTick
        QList<MidiEvent *> *ev = eventsBetween(midiTicks, oldTicks);
        // BULK-OP UNDO: snapshot every touched channel ONCE, then call
        // removeEvent with toProtocol=false. Otherwise each per-event call
        // deep-copies the channel's full QMultiMap and pushes a ProtocolItem
        // (O(events) channel clones -> multi-GB RAM on heavy files).
        // See FFXIVChannelFixer fix 2026-04-21 for the matching pattern.
        QSet<int> touchedChannels;
        for (MidiEvent *event : *ev) touchedChannels.insert(event->channel());
        QHash<int, ProtocolEntry *> channelSnapshots;
        for (int ch : touchedChannels) {
            MidiChannel *c = channel(ch);
            if (c) channelSnapshots.insert(ch, c->copy());
        }
        foreach(MidiEvent* event, *ev) {
            channel(event->channel())->removeEvent(event, false);
        }
        for (auto it = channelSnapshots.constBegin(); it != channelSnapshots.constEnd(); ++it) {
            channel(it.key())->protocol(it.value(), channel(it.key()));
        }
        delete ev;
    }
    calcMaxTime();
}

ProtocolEntry *MidiFile::copy() {
    MidiFile *file = new MidiFile(midiTicks, protocol());
    file->_tracks = new QList<MidiTrack *>(*(_tracks));
    file->pasteTracks = pasteTracks;
    return file;
}

void MidiFile::reloadState(ProtocolEntry *entry) {
    // Phase 48: undo/redo restores channels wholesale, so nothing about the
    // cached tempo map can be trusted afterwards. The cache is deliberately
    // not part of copy() - snapshots stay cheap and we simply rebuild here.
    invalidateTempoCache();
    MidiFile *file = dynamic_cast<MidiFile *>(entry);
    if (file) {
        midiTicks = file->midiTicks;
        delete _tracks;
        _tracks = new QList<MidiTrack *>(*(file->_tracks));
        pasteTracks = file->pasteTracks;
    }
    calcMaxTime();
}

MidiFile *MidiFile::file() {
    return this;
}

QList<MidiTrack *> *MidiFile::tracks() {
    return _tracks;
}

void MidiFile::addTrack() {
    ProtocolEntry *toCopy = copy();
    MidiTrack *track = new MidiTrack(this);
    track->setNumber(_tracks->size());
    if (track->number() > 0) {
        track->assignChannel(track->number() - 1);
    }
    _tracks->append(track);
    track->setName("New Track");
    int n = 0;
    foreach(MidiTrack* track, *_tracks) {
        track->setNumber(n++);
    }
    ProtocolEntry::protocol(toCopy, this);
    connect(track, SIGNAL(trackChanged()), this, SIGNAL(trackChanged()));
}

bool MidiFile::moveTrack(MidiTrack *track, int delta) {
    if (!track || !_tracks) {
        return false;
    }
    int idx = _tracks->indexOf(track);
    int to = idx + delta;
    if (idx < 0 || to < 0 || to >= _tracks->size()) {
        return false;
    }
    // Same protocol pattern as addTrack/removeTrack: the FILE snapshot covers
    // the _tracks list ORDER (MidiTrack items only restore numbers). Without
    // it, undo reverts the numbers but keeps the swapped order - positional
    // track(int) lookups then disagree with the displayed list.
    ProtocolEntry *toCopy = copy();
    _tracks->swapItemsAt(idx, to);
    int n = 0;
    foreach(MidiTrack* t, *_tracks) {
        t->setNumber(n++);
    }
    ProtocolEntry::protocol(toCopy, this);
    return true;
}

bool MidiFile::removeTrack(MidiTrack *track) {
    if (numTracks() < 2) {
        return false;
    }

    ProtocolEntry *toCopy = copy();

    // BULK-OP UNDO: take a single snapshot of each channel BEFORE any event
    // removal. The default removeEvent(ev, true) path deep-clones the
    // channel's full QMultiMap and pushes a ProtocolItem per call -- on a
    // brass track with thousands of Breath Controller (MSB) CCs that
    // materialises tens of thousands of full-channel clones in the open
    // undo action (40-50 GB RAM, multi-minute stalls). With toProtocol=false
    // on the inner loop and one channel ProtocolItem per touched channel
    // committed at the end, peak RAM collapses to ~19 channel snapshots
    // regardless of event count. Undo semantics are identical: replaying
    // the action restores _tracks (file snapshot) + every touched channel.
    // See FFXIVChannelFixer fix 2026-04-21 for the matching pattern.
    QVector<ProtocolEntry *> channelSnapshots(19, nullptr);
    for (int i = 0; i < 19; i++) {
        if (channels[i]) channelSnapshots[i] = channels[i]->copy();
    }

    QMultiMap<int, MidiEvent *> allEvents = QMultiMap<int, MidiEvent *>();
    for (int i = 0; i < 19; i++) {
        QMultiMap<int, MidiEvent *>::iterator it = channels[i]->eventMap()->begin();
        while (it != channels[i]->eventMap()->end()) {
            allEvents.insert(it.key(), it.value());
            it++;
        }
    }

    _tracks->removeAll(track);

    QMultiMap<int, MidiEvent *>::iterator it = allEvents.begin();
    while (it != allEvents.end()) {
        MidiEvent *event = it.value();
        if (event->track() == track) {
            if (!channels[event->channel()]->removeEvent(event, false)) {
                event->setTrack(_tracks->first());
            }
        }
        it++;
    }

    // Commit one ProtocolItem per channel -- single coarse-grained undo step.
    for (int i = 0; i < 19; i++) {
        if (channelSnapshots[i])
            channels[i]->protocol(channelSnapshots[i], channels[i]);
    }

    // remove links from pasted tracks
    foreach(MidiFile* fileFrom, pasteTracks.keys()) {
        QList<MidiTrack *> sourcesToRemove;
        foreach(MidiTrack* source, pasteTracks.value(fileFrom).keys()) {
            if (pasteTracks.value(fileFrom).value(source) == track) {
                sourcesToRemove.append(source);
            }
        }
        QMap<MidiTrack *, MidiTrack *> tracks = pasteTracks.value(fileFrom);
        foreach(MidiTrack* source, sourcesToRemove) {
            tracks.remove(source);
        }
        pasteTracks.insert(fileFrom, tracks);
    }
    int n = 0;
    foreach(MidiTrack* track, *_tracks) {
        track->setNumber(n++);
    }

    ProtocolEntry::protocol(toCopy, this);

    return true;
}

MidiTrack *MidiFile::track(int number) {
    if (_tracks->size() > number) {
        return _tracks->at(number);
    } else {
        return 0;
    }
}

int MidiFile::tonalityAt(int tick) {
    QMultiMap<int, MidiEvent *> *events = channels[16]->eventMap();

    QMultiMap<int, MidiEvent *>::iterator it = events->begin();
    KeySignatureEvent *event = 0;
    while (it != events->end()) {
        KeySignatureEvent *keySig = dynamic_cast<KeySignatureEvent *>(it.value());
        if (keySig && keySig->midiTime() <= tick) {
            event = keySig;
        } else if (keySig) {
            break;
        }
        it++;
    }

    if (!event) {
        return 0;
    } else {
        return event->tonality();
    }
}

void MidiFile::meterAt(int tick, int *num, int *denum, TimeSignatureEvent **lastTimeSigEvent) {
    QMultiMap<int, MidiEvent *> *meterEvents = timeSignatureEvents();
    QMultiMap<int, MidiEvent *>::iterator it = meterEvents->begin();
    TimeSignatureEvent *event = 0;
    while (it != meterEvents->end()) {
        TimeSignatureEvent *timeSig = dynamic_cast<TimeSignatureEvent *>(it.value());
        if (timeSig && timeSig->midiTime() <= tick) {
            event = timeSig;
        } else if (timeSig) {
            break;
        }
        it++;
    }

    // The out-param is written in BOTH branches, null included. A caller
    // CANNOT tell "channel 18 has no event at or before tick" from "a real 4/4
    // event" by looking at num/denum - the fallback below is a valid 4/4, bit
    // for bit - so a null event pointer is the only reliable signal that there
    // is nothing anchoring the meter. deleteMeasures() depends on it, and
    // leaving the out-param untouched in the fallback branch used to hand
    // callers whatever they had initialised it to (uninitialised memory, in
    // deleteMeasures()/insertMeasures()).
    if (lastTimeSigEvent) {
        *lastTimeSigEvent = event;
    }

    if (!event) {
        // 4/4 default. denum is the power-of-two EXPONENT, so /4 is 2 - the
        // former 4 here meant /16 to every caller that converted correctly.
        // Reached whenever channel 18 has no event at or before tick - e.g.
        // while deleteMeasures() has the tick-0 time signature removed - so it
        // must not contradict the documented convention.
        *num = 4;
        *denum = 2;
    } else {
        *num = event->num();
        *denum = event->denom();
    }
}

int MidiFile::ticksPerMeasureOfMeter(int num, int denumPow) {
    // denumPow is the SMF power-of-two EXPONENT (2 means /4). Clamp it before
    // shifting: a corrupt file can carry any byte here and 1 << 200 is UB.
    const int denum = 1 << qBound(0, denumPow, 16);
    const int tpq = (timePerQuarter > 0) ? timePerQuarter : defaultTimePerQuarter;
    if (num <= 0 || denum <= 0) {
        return qMax(1, 4 * tpq);
    }
    return qMax(1, 4 * num * tpq / denum);
}

void MidiFile::printLog(QStringList *log) {
    foreach(QString str, *log) {
        qWarning(str.toUtf8().constData());
    }
}

void MidiFile::registerCopiedTrack(MidiTrack *source, MidiTrack *destination, MidiFile *fileFrom) {
    //  if(fileFrom == this){
    //      return;
    //  }

    ProtocolEntry *toCopy = copy();

    QMap<MidiTrack *, MidiTrack *> list;
    if (pasteTracks.contains(fileFrom)) {
        list = pasteTracks.value(fileFrom);
    }

    list.insert(source, destination);
    pasteTracks.insert(fileFrom, list);

    ProtocolEntry::protocol(toCopy, this);
}

MidiTrack *MidiFile::getPasteTrack(MidiTrack *source, MidiFile *fileFrom) {
    //  if(fileFrom == this){
    //      return source;
    //  }

    if (!pasteTracks.contains(fileFrom) || !pasteTracks.value(fileFrom).contains(source)) {
        return 0;
    }

    return pasteTracks.value(fileFrom).value(source);
}

QList<int> MidiFile::quantization(int fractionSize) {
    int fractionTicks;

    if (fractionSize >= 0) {
        // Regular divisions: same as before
        fractionTicks = (4 * ticksPerQuarter()) / qPow(2, fractionSize);
    } else if (fractionSize <= -100) {
        // Extended subdivision system (same logic as MatrixWidget)
        int subdivisionType = (-fractionSize) / 100; // 1=triplets, 2=quintuplets, etc.
        int baseDivision = (-fractionSize) % 100; // Extract base division

        double baseDiv = 4 / (double) qPow(2, baseDivision);

        if (subdivisionType == 1) {
            // Triplets: divide by 3
            fractionTicks = (baseDiv * ticksPerQuarter()) / 3;
        } else if (subdivisionType == 2) {
            // Quintuplets: divide by 5
            fractionTicks = (baseDiv * ticksPerQuarter()) / 5;
        } else if (subdivisionType == 3) {
            // Sextuplets: divide by 6
            fractionTicks = (baseDiv * ticksPerQuarter()) / 6;
        } else if (subdivisionType == 4) {
            // Septuplets: divide by 7
            fractionTicks = (baseDiv * ticksPerQuarter()) / 7;
        } else if (subdivisionType == 5) {
            // Dotted notes: multiply by 1.5
            fractionTicks = (baseDiv * ticksPerQuarter()) * 1.5;
        } else if (subdivisionType == 6) {
            // Double dotted notes: multiply by 1.75
            fractionTicks = (baseDiv * ticksPerQuarter()) * 1.75;
        } else {
            // Fallback to triplets for unknown types
            fractionTicks = (baseDiv * ticksPerQuarter()) / 3;
        }
    } else {
        // Fallback for unexpected values
        fractionTicks = ticksPerQuarter(); // Default to quarter notes
    }

    QList<int> list;

    QList<MidiEvent *> timeSigs = timeSignatureEvents()->values();
    TimeSignatureEvent *last = 0;
    foreach(MidiEvent* event, timeSigs) {
        TimeSignatureEvent *t = dynamic_cast<TimeSignatureEvent *>(event);

        if (last) {
            int current = last->midiTime();
            while (current < t->midiTime()) {
                list.append(current);
                current += fractionTicks;
            }
        }

        last = t;
    }

    if (last) {
        int current = last->midiTime();
        while (current <= midiTicks) {
            list.append(current);
            current += fractionTicks;
        }
    }
    return list;
}


int MidiFile::startTickOfMeasure(int measure) {
    QMultiMap<int, MidiEvent *> *timeSigs = timeSignatureEvents();
    QMultiMap<int, MidiEvent *>::iterator it = timeSigs->begin();

    // Find the time signature event the measure is in and its start measure.
    // NOTE: value(0) is a lookup by KEY, so it yields null both for an empty
    // channel 18 and for a file whose first time signature does not sit at tick
    // 0 - and this used to be dereferenced straight away.
    int currentMeasure = 1;
    TimeSignatureEvent *currentEvent = dynamic_cast<TimeSignatureEvent *>(timeSigs->value(0));
    if (!currentEvent) {
        // Nothing to anchor on: assume 4/4 from tick 0, matching the fallback
        // in measure() so the two stay round-trippable.
        return qMax(0, measure - 1) * ticksPerMeasureOfMeter(4, 2);
    }
    if (it != timeSigs->end()) {
        it++;
    }
    while (it != timeSigs->end()) {
        TimeSignatureEvent *nextEvent = dynamic_cast<TimeSignatureEvent *>(it.value());
        if (!nextEvent) {
            // A foreign event type on channel 18 - skip it instead of letting
            // the failed cast null out currentEvent for the next round.
            it++;
            continue;
        }
        int endMeasureOfCurrentEvent = currentMeasure + ceil((it.key() - currentEvent->midiTime()) / currentEvent->ticksPerMeasure());
        if (endMeasureOfCurrentEvent > measure) {
            break;
        }
        currentEvent = nextEvent;
        currentMeasure = endMeasureOfCurrentEvent;
        it++;
    }

    return currentEvent->midiTime() + (measure - currentMeasure) * currentEvent->ticksPerMeasure();
}

void MidiFile::deleteMeasures(int from, int to) {
    int tickFrom = startTickOfMeasure(from);
    int tickTo = startTickOfMeasure(to + 1);

    // Find and remember meter (time signature event) at the first undeleted measure
    int num;
    int denom;
    meterAt(tickTo, &num, &denom);
    ProtocolEntry *toCopy = copy();

    // BULK-OP UNDO: same pattern as removeTrack -- snapshot per channel
    // once, mutate fast (toProtocol=false), commit one ProtocolItem per
    // touched channel at the end. Avoids O(events) channel-map deep clones.
    QVector<ProtocolEntry *> deleteMeasuresSnapshots(19, nullptr);

    // Delete every event STARTING inside the HALF-OPEN range [tickFrom, tickTo).
    // tickTo is the start tick of the first bar that SURVIVES, so everything
    // sitting exactly ON it - notes, controllers, program changes, meter and
    // tempo changes - belongs to the surviving material and gets shifted left
    // below instead of deleted. The bound used to be inclusive, which silently
    // ate the complete downbeat of the first kept bar on every delete.
    // Notes are keyed by their ON event: a note starting inside the range takes
    // its OFF event with it, wherever that sits.
    QList<QPair<int, MidiEvent *>> refusedRemoval;
    QList<OffEvent *> spanningOffs;
    for (int ch = 0; ch < 19; ch++) {
        QMultiMap<int, MidiEvent *>::Iterator it = channel(ch)->eventMap()->begin();
        QList<MidiEvent *> toRemove;
        while (it != channel(ch)->eventMap()->end()) {
            if (it.key() >= tickFrom && it.key() < tickTo) {
                OffEvent *offEvent = dynamic_cast<OffEvent *>(it.value());
                if (offEvent) {
                    // An OFF event alone inside the range belongs to a note
                    // that STARTED before tickFrom (an ON inside the range
                    // removes its own OFF in the else branch). Such a spanning
                    // note is SHORTENED to the splice point, never deleted -
                    // clamped, not shifted, because shifting an off event out
                    // of the deleted interval would give the note a negative
                    // duration.
                    if (offEvent->onEvent()
                        && offEvent->onEvent()->midiTime() < tickFrom) {
                        spanningOffs.append(offEvent);
                    }
                } else {
                    toRemove.append(it.value());

                    OnEvent *onEvent = dynamic_cast<OnEvent *>(it.value());
                    if (onEvent && onEvent->offEvent()) {
                        toRemove.append(onEvent->offEvent());
                    }
                }
            }
            it++;
        }

        if (!toRemove.isEmpty() && channel(ch))
            deleteMeasuresSnapshots[ch] = channel(ch)->copy();
        foreach(MidiEvent* event, toRemove) {
            // removeEvent() refuses to drop the LAST event on tick 0 of the
            // tempo (17) / time-signature (18) channel; remember those, they
            // get a second chance once the shift below has put the meter/tempo
            // of the first surviving bar on the same tick.
            if (!channel(ch)->removeEvent(event, false))
                refusedRemoval.append(qMakePair(ch, event));
        }
    }
    for (int ch = 0; ch < 19; ch++) {
        if (deleteMeasuresSnapshots[ch])
            channel(ch)->protocol(deleteMeasuresSnapshots[ch], channel(ch));
    }

    // Notes spanning into the deleted range end exactly at the splice point.
    foreach (OffEvent *off, spanningOffs) {
        if (off->midiTime() != tickFrom)
            off->setMidiTime(tickFrom);
    }

    // All remaining events from the end tick ON have to be shifted - tickTo
    // INCLUDED, so the downbeat of the first kept bar lands on tickFrom. Off
    // events left inside the deleted interval are not shifted (that would
    // cause a negative duration); they were clamped to tickFrom above.
    for (int ch = 0; ch < 19; ch++) {
        QList<MidiEvent *> toUpdate;
        QMultiMap<int, MidiEvent *>::Iterator it = channel(ch)->eventMap()->begin();
        while (it != channel(ch)->eventMap()->end()) {
            if (it.key() >= tickTo) {
                toUpdate.append(it.value());
            }
            it++;
        }

        foreach(MidiEvent *event, toUpdate) {
            event->setMidiTime(event->midiTime() - (tickTo - tickFrom));
        }
    }

    // Second chance for the events the tick-0 guard kept alive: a tempo or
    // meter change from INSIDE the deleted range that survived at tickFrom
    // would now shadow the one the shift just moved there (the map iterates the
    // newest first, so meterAt() can settle on the stale one) and the file
    // would save both. With more than one event on the tick the guard no longer
    // applies.
    for (const QPair<int, MidiEvent *> &stale : refusedRemoval) {
        MidiChannel *c = channel(stale.first);
        if (!c || !c->eventMap()->contains(stale.second->midiTime(), stale.second))
            continue;
        if (c->eventMap()->count(stale.second->midiTime()) > 1)
            c->removeEvent(stale.second, true);
    }

    // Re-anchor the meter. The event that carried it may be GONE: the delete
    // loop above strips every non-off event in [tickFrom, tickTo) from all 19
    // channels, channel 18 included. So the decision has to be driven by "is
    // there still an event anchoring tickFrom?" and NEVER by comparing
    // num/denom against what meterAt() reports: its no-event fallback is itself
    // a valid 4/4 (num 4, denum 2), so a plain value comparison would accept a
    // file with ZERO time signature events - and measure() /
    // startTickOfMeasure() are then one null dereference away from an access
    // violation on the next paint of the measure tool.
    int numAfter;
    int denomAfter;
    TimeSignatureEvent *timeSigAfter = nullptr;
    meterAt(tickFrom, &numAfter, &denomAfter, &timeSigAfter);
    int ticksPerMeasure;
    if (!timeSigAfter || denom != denomAfter || num != numAfter) {
        // Retune an event that already sits exactly ON tickFrom instead of
        // adding a second one there. Two TimeSignatureEvents on the same tick
        // shadow each other - the map iterates the newest first, so meterAt()
        // and measure() both settle on the STALE one - and the file would save
        // both. Reachable whenever the deleted range starts at bar 1 and ends
        // on a meter change: MidiChannel::removeEvent() refuses to drop the
        // last tick-0 event on channel 18, so the old meter is still there.
        QList<TimeSignatureEvent *> onTickFrom;
        foreach (MidiEvent *ev, channel(18)->eventMap()->values(tickFrom)) {
            TimeSignatureEvent *ts = dynamic_cast<TimeSignatureEvent *>(ev);
            if (ts) {
                onTickFrom.append(ts);
            }
        }
        if (!onTickFrom.isEmpty()) {
            foreach (TimeSignatureEvent *ts, onTickFrom) {
                ts->setNumerator(num);
                ts->setDenominator(denom);
            }
            ticksPerMeasure = onTickFrom.first()->ticksPerMeasure();
        } else {
            TimeSignatureEvent *newEvent = new TimeSignatureEvent(18, num, denom, 24, 8, track(0));
            channel(18)->insertEvent(newEvent, tickFrom);
            ticksPerMeasure = newEvent->ticksPerMeasure();
        }
    } else {
        ticksPerMeasure = timeSigAfter->ticksPerMeasure();
    }

    midiTicks = midiTicks - (tickTo - tickFrom);
    // make sure, we have at east one measure
    if (ticksPerMeasure > midiTicks) {
        midiTicks = ticksPerMeasure;
    }
    calcMaxTime();
    ProtocolEntry::protocol(toCopy, this);
}

void MidiFile::insertMeasures(int after, int numMeasures) {
    if (after == 0) {
        // Cannot insert before first measure.
        return;
    }
    ProtocolEntry *toCopy = copy();
    int tick = startTickOfMeasure(after + 1);

    // Find meter at measure and compute number of inserted ticks.
    int num;
    int denom;
    TimeSignatureEvent *lastTimeSig = nullptr;
    meterAt(tick - 1, &num, &denom, &lastTimeSig);
    // meterAt() reports a 4/4 default with a NULL event when channel 18 holds
    // no time signature at or before the tick, so derive the measure length
    // from the reported meter rather than dereferencing the event (which used
    // to be an uninitialised pointer in that case).
    int numTicks = (lastTimeSig ? lastTimeSig->ticksPerMeasure()
                                : ticksPerMeasureOfMeter(num, denom)) * numMeasures;
    midiTicks = midiTicks + numTicks;

    // Shift all ticks.
    for (int ch = 0; ch < 19; ch++) {
        QList<MidiEvent *> toUpdate;
        QMultiMap<int, MidiEvent *>::Iterator it = channel(ch)->eventMap()->begin();
        while (it != channel(ch)->eventMap()->end()) {
            if (it.key() >= tick) {
                toUpdate.append(it.value());
            }
            it++;
        }

        foreach(MidiEvent *event, toUpdate) {
            event->setMidiTime(event->midiTime() + numTicks);
        }
    }

    calcMaxTime();
    ProtocolEntry::protocol(toCopy, this);
}
