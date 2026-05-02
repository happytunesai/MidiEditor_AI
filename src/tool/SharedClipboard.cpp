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

#include "SharedClipboard.h"
#include "../MidiEvent/MidiEvent.h"
#include "../MidiEvent/TempoChangeEvent.h"
#include "../MidiEvent/TimeSignatureEvent.h"
#include "../MidiEvent/OffEvent.h"
#include "../MidiEvent/OnEvent.h"
#include "../midi/MidiFile.h"
#include "../midi/MidiTrack.h"

#include <QDataStream>
#include <QDateTime>
#include <QIODevice>

// Static member definitions
SharedClipboard *SharedClipboard::_instance = nullptr;
const QString SharedClipboard::SHARED_MEMORY_KEY = "MidiEditor_Clipboard_v1";
const QString SharedClipboard::SEMAPHORE_KEY = "MidiEditor_Clipboard_Semaphore_v1";
// v2 (Phase 34) widens the per-event header with sourceTrackId and adds
// a track-name table at the start of the data block. v1 readers will
// reject the buffer via the version mismatch in pasteEvents().
const int SharedClipboard::CLIPBOARD_VERSION = 2;
const int SharedClipboard::MAX_CLIPBOARD_SIZE = 1024 * 1024; // 1MB max

// Global storage for timing information during deserialization
static QList<QPair<int, int> > g_originalTimings; // midiTime, channel pairs
// Phase 34: parallel storage — same length as g_originalTimings.
static QList<PasteSourceInfo> g_pasteSourceInfos;
// Phase 34: ordered (trackId, name) list captured by the most recent
// successful deserialise. Empty for v1 buffers.
static QList<QPair<int, QString>> g_sourceTracks;

// Phase 36.x: source ticksPerQuarter from the most recent pasteEvents()
// read, exposed via SharedClipboard::sourceTicksPerQuarter() so the
// shared-clipboard paste path in EventTool can rescale event timings
// the same way the local clipboard already does in pasteAction().
static int g_sourceTicksPerQuarter = 0;

SharedClipboard::SharedClipboard(QObject *parent)
    : QObject(parent)
      , _sharedMemory(nullptr)
      , _semaphore(nullptr)
      , _initialized(false) {
}

SharedClipboard::~SharedClipboard() {
    cleanup();
}

SharedClipboard *SharedClipboard::instance() {
    if (!_instance) {
        _instance = new SharedClipboard();
    }
    return _instance;
}

bool SharedClipboard::initialize() {
    if (_initialized) {
        return true;
    }

    // Create semaphore for synchronization
    _semaphore = new QSystemSemaphore(SEMAPHORE_KEY, 1, QSystemSemaphore::Create);
    if (_semaphore->error() != QSystemSemaphore::NoError) {
        delete _semaphore;
        _semaphore = nullptr;
        return false;
    }

    // Create shared memory
    _sharedMemory = new QSharedMemory(SHARED_MEMORY_KEY);

    // Try to attach to existing shared memory first
    bool attached = _sharedMemory->attach();

    if (!attached) {
        // If attach fails, try to create new shared memory
        if (!_sharedMemory->create(MAX_CLIPBOARD_SIZE)) {
            delete _sharedMemory;
            _sharedMemory = nullptr;
            delete _semaphore;
            _semaphore = nullptr;
            return false;
        }

        // Initialize the shared memory with empty data
        if (lockMemory()) {
            ClipboardHeader *header = static_cast<ClipboardHeader *>(_sharedMemory->data());
            header->version = CLIPBOARD_VERSION;
            header->eventCount = 0;
            header->dataSize = 0;
            header->timestamp = 0;
            header->sourceProcessId = 0; // No data yet
            header->hasTempoEvents = 0;
            unlockMemory();
        }
    } else {
        // Check if existing data is valid
        if (lockMemory()) {
            ClipboardHeader *header = static_cast<ClipboardHeader *>(_sharedMemory->data());
            unlockMemory();
        }
    }

    _initialized = true;
    return true;
}

bool SharedClipboard::copyEvents(const QList<MidiEvent *> &events, MidiFile *sourceFile) {
    if (!_initialized || !_sharedMemory || events.isEmpty() || !sourceFile) {
        return false;
    }

    QByteArray serializedData = serializeEvents(events, sourceFile);
    if (serializedData.isEmpty()) {
        return false;
    }

    if (!lockMemory()) {
        return false;
    }

    // Check if data fits in shared memory
    int headerSize = sizeof(ClipboardHeader);
    int totalSize = headerSize + serializedData.size();
    int availableSize = _sharedMemory->size();

    if (totalSize > availableSize) {
        unlockMemory();
        return false;
    }

    // Check if events contain tempo/time signature events
    bool hasTempoEvents = false;
    for (MidiEvent *event : events) {
        if (dynamic_cast<TempoChangeEvent *>(event) || dynamic_cast<TimeSignatureEvent *>(event)) {
            hasTempoEvents = true;
            break;
        }
    }

    // Write header
    ClipboardHeader *header = static_cast<ClipboardHeader *>(_sharedMemory->data());
    header->version = CLIPBOARD_VERSION;
    header->ticksPerQuarter = sourceFile->ticksPerQuarter();
    header->tempoBeatsPerQuarter = getCurrentTempo(sourceFile);
    header->eventCount = events.size();
    header->dataSize = serializedData.size();
    header->timestamp = QDateTime::currentMSecsSinceEpoch();
    header->sourceProcessId = QCoreApplication::applicationPid();
    header->hasTempoEvents = hasTempoEvents ? 1 : 0;

    // Write serialized event data
    char *dataPtr = static_cast<char *>(_sharedMemory->data()) + sizeof(ClipboardHeader);
    memcpy(dataPtr, serializedData.constData(), serializedData.size());

    unlockMemory();
    return true;
}

bool SharedClipboard::pasteEvents(MidiFile *targetFile, QList<MidiEvent *> &pastedEvents, bool applyTempoConversion, int targetCursorTick) {
    if (!_initialized || !_sharedMemory || !targetFile) {
        return false;
    }

    if (!lockMemory()) {
        return false;
    }

    ClipboardHeader *header = static_cast<ClipboardHeader *>(_sharedMemory->data());

    // Check version compatibility
    if (header->version != CLIPBOARD_VERSION || header->eventCount == 0) {
        unlockMemory();
        return false;
    }

    // Store header info for tempo conversion
    int sourceTicksPerQuarter = header->ticksPerQuarter;
    int sourceTempo = header->tempoBeatsPerQuarter;
    bool hasTempoEvents = (header->hasTempoEvents == 1);
    g_sourceTicksPerQuarter = sourceTicksPerQuarter;

    // Read serialized data - copy it to ensure data integrity
    char *dataPtr = static_cast<char *>(_sharedMemory->data()) + sizeof(ClipboardHeader);
    QByteArray serializedData;
    serializedData.resize(header->dataSize);
    memcpy(serializedData.data(), dataPtr, header->dataSize);

    unlockMemory();

    // Deserialize events
    bool result = deserializeEvents(serializedData, targetFile, pastedEvents);
    
    // Note: Tempo conversion is handled automatically by the MIDI engine
    // No manual conversion needed in SharedClipboard
    
    return result;
}

bool SharedClipboard::hasData() {
    if (!_initialized || !_sharedMemory) {
        return false;
    }

    if (!lockMemory()) {
        return false;
    }

    ClipboardHeader *header = static_cast<ClipboardHeader *>(_sharedMemory->data());
    bool hasValidData = (header->version == CLIPBOARD_VERSION && header->eventCount > 0);

    unlockMemory();
    return hasValidData;
}

bool SharedClipboard::hasDataFromDifferentProcess() {
    if (!_initialized || !_sharedMemory) {
        return false;
    }

    if (!lockMemory()) {
        return false;
    }

    ClipboardHeader *header = static_cast<ClipboardHeader *>(_sharedMemory->data());
    qint64 currentPid = QCoreApplication::applicationPid();

    // Check all conditions for valid cross-process data
    bool validVersion = (header->version == CLIPBOARD_VERSION);
    bool hasEvents = (header->eventCount > 0);
    bool differentProcess = (header->sourceProcessId != 0 && header->sourceProcessId != currentPid);
    bool hasValidData = validVersion && hasEvents && differentProcess;

    unlockMemory();
    return hasValidData;
}

void SharedClipboard::clear() {
    if (!_initialized || !_sharedMemory) {
        return;
    }

    if (lockMemory()) {
        ClipboardHeader *header = static_cast<ClipboardHeader *>(_sharedMemory->data());
        header->eventCount = 0;
        header->dataSize = 0;
        header->timestamp = 0;
        header->hasTempoEvents = 0;
        unlockMemory();
    }
}

void SharedClipboard::cleanup() {
    if (_sharedMemory) {
        _sharedMemory->detach();
        delete _sharedMemory;
        _sharedMemory = nullptr;
    }

    if (_semaphore) {
        delete _semaphore;
        _semaphore = nullptr;
    }

    _initialized = false;
}

QByteArray SharedClipboard::serializeEvents(const QList<MidiEvent *> &events, MidiFile *sourceFile) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);

    // ---- v2 prelude: track-name table ----------------------------------
    // Walk the events once, collect a stable insertion-ordered list of
    // (trackId -> trackName) for every track referenced. Off-events are
    // skipped here because we serialise them inline with their OnEvent
    // below; their track is identical anyway.
    QList<int> trackOrder;
    QHash<int, QString> trackNames;
    for (MidiEvent *event : events) {
        if (!event) continue;
        if (dynamic_cast<OffEvent *>(event)) continue;
        MidiTrack *t = event->track();
        const int tid = t ? t->number() : -1;
        if (!trackNames.contains(tid)) {
            trackNames.insert(tid, t ? t->name() : QString());
            trackOrder.append(tid);
        }
    }
    stream << static_cast<qint32>(trackOrder.size());
    for (int tid : trackOrder) {
        stream << static_cast<qint32>(tid);
        const QByteArray utf8 = trackNames.value(tid).toUtf8();
        stream << static_cast<qint32>(utf8.size());
        if (!utf8.isEmpty()) {
            stream.writeRawData(utf8.constData(), utf8.size());
        }
    }

    // ---- per-event records ---------------------------------------------
    // Write each event with its timing information
    for (MidiEvent *event: events) {
        if (!event) continue;

        // Skip OffEvents as they will be handled by their OnEvents
        OffEvent *offEvent = dynamic_cast<OffEvent *>(event);
        if (offEvent) continue;

        // Write event timing
        int midiTime = event->midiTime();
        int channel = event->channel();
        const int sourceTrackId = event->track() ? event->track()->number() : -1;
        stream << midiTime;
        stream << channel;
        stream << static_cast<qint32>(sourceTrackId); // v2 addition

        // Write event data
        QByteArray eventData = event->save();
        int dataSize = eventData.size();
        stream << dataSize;
        stream.writeRawData(eventData.constData(), eventData.size());

        // If this is an OnEvent, also serialize its corresponding OffEvent
        OnEvent *onEvent = dynamic_cast<OnEvent *>(event);
        if (onEvent && onEvent->offEvent()) {
            OffEvent *off = onEvent->offEvent();
            int offMidiTime = off->midiTime();
            int offChannel = off->channel();
            const int offTrackId = off->track() ? off->track()->number() : sourceTrackId;
            stream << offMidiTime;
            stream << offChannel;
            stream << static_cast<qint32>(offTrackId); // v2 addition
            QByteArray offData = off->save();
            int offDataSize = offData.size();
            stream << offDataSize;
            stream.writeRawData(offData.constData(), offData.size());
        }
    }

    return data;
}

bool SharedClipboard::deserializeEvents(const QByteArray &data, MidiFile *targetFile, QList<MidiEvent *> &events) {
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);

    events.clear();
    g_originalTimings.clear(); // Clear previous timing data
    g_pasteSourceInfos.clear();
    g_sourceTracks.clear();

    // ---- v2 prelude: track-name table ----------------------------------
    qint32 trackCount = 0;
    if (stream.device()->bytesAvailable() < 4) return false;
    stream >> trackCount;
    if (trackCount < 0 || trackCount > 1024) return false;
    QHash<int, QString> trackNameById;
    for (qint32 i = 0; i < trackCount; ++i) {
        if (stream.device()->bytesAvailable() < 8) return false;
        qint32 tid = -1;
        qint32 nameLen = 0;
        stream >> tid;
        stream >> nameLen;
        if (nameLen < 0 || nameLen > 4096) return false;
        QByteArray nameBytes;
        if (nameLen > 0) {
            nameBytes.resize(nameLen);
            int got = stream.readRawData(nameBytes.data(), nameLen);
            if (got != nameLen) return false;
        }
        const QString name = QString::fromUtf8(nameBytes);
        trackNameById.insert(tid, name);
        g_sourceTracks.append(qMakePair(static_cast<int>(tid), name));
    }

    int eventIndex = 0;
    while (!stream.atEnd()) {
        int midiTime, channel, dataSize;
        qint32 sourceTrackId = -1;

        // v2 per-event header: 4 ints = 16 bytes
        int available = stream.device()->bytesAvailable();
        if (available < 16) {
            break;
        }

        stream >> midiTime;
        stream >> channel;
        stream >> sourceTrackId;
        stream >> dataSize;

        if (dataSize <= 0 || dataSize > 1024) { // Sanity check
            return false;
        }

        QByteArray eventData(dataSize, 0);
        int bytesRead = stream.readRawData(eventData.data(), dataSize);
        if (bytesRead != dataSize) {
            return false;
        }

        // Create a temporary data stream to parse the event
        QDataStream eventStream(eventData);
        eventStream.setByteOrder(QDataStream::BigEndian);

        bool ok = false;
        bool endEvent = false;
        // Use track 0 as default placeholder; the paste site will reassign
        // the real target track based on PasteSpecialOptions.
        MidiTrack *defaultTrack = targetFile->track(0);
        if (!defaultTrack) {
            return false;
        }

        MidiEvent *event = MidiEvent::loadMidiEvent(&eventStream, &ok, &endEvent, defaultTrack);

        if (ok && event && !endEvent) {
            // Store the original timing information in global storage
            g_originalTimings.append(QPair<int, int>(midiTime, channel));

            PasteSourceInfo info;
            info.originalTime = midiTime;
            info.originalChannel = channel;
            info.sourceTrackId = sourceTrackId;
            info.sourceTrackName = trackNameById.value(sourceTrackId);
            g_pasteSourceInfos.append(info);

            // Don't try to set properties on deserialized events - they can crash
            // Just add the event as-is
            events.append(event);
        } else if (event) {
            delete event;
        }

        eventIndex++;
    }

    return !events.isEmpty();
}

QPair<int, int> SharedClipboard::getOriginalTiming(int index) {
    if (index >= 0 && index < g_originalTimings.size()) {
        return g_originalTimings[index];
    }
    return QPair<int, int>(-1, -1); // Invalid
}

PasteSourceInfo SharedClipboard::getPasteSourceInfo(int index) {
    if (index >= 0 && index < g_pasteSourceInfos.size()) {
        return g_pasteSourceInfos[index];
    }
    return PasteSourceInfo{};
}

QList<QPair<int, QString>> SharedClipboard::sourceTrackList() {
    return g_sourceTracks;
}

int SharedClipboard::sourceTicksPerQuarter() {
    return g_sourceTicksPerQuarter;
}

int SharedClipboard::getCurrentTempo(MidiFile *file, int atTick) {
    if (!file) return 120; // Default tempo

    QMultiMap<int, MidiEvent *> *tempoEvents = file->tempoEvents();
    if (!tempoEvents || tempoEvents->isEmpty()) {
        return 120; // Default tempo
    }

    // Find the most recent tempo event at or before the given tick
    TempoChangeEvent *currentTempo = nullptr;
    for (auto it = tempoEvents->begin(); it != tempoEvents->end(); ++it) {
        if (it.key() <= atTick) {
            TempoChangeEvent *tempo = dynamic_cast<TempoChangeEvent *>(it.value());
            if (tempo) {
                currentTempo = tempo;
            }
        } else {
            break;
        }
    }

    return currentTempo ? currentTempo->beatsPerQuarter() : 120;
}

int SharedClipboard::convertTiming(int originalTime, int sourceTicksPerQuarter, int sourceTempo, 
                                 int targetTicksPerQuarter, int targetTempo) {
    if (sourceTempo == targetTempo && sourceTicksPerQuarter == targetTicksPerQuarter) {
        return originalTime; // No conversion needed
    }
    
    // Convert to real time (milliseconds) using source timing
    double sourceTickDurationMs = (60000.0 / sourceTempo) / sourceTicksPerQuarter;
    double realTimeMs = originalTime * sourceTickDurationMs;
    
    // Convert back to ticks using target timing
    double targetTickDurationMs = (60000.0 / targetTempo) / targetTicksPerQuarter;
    int targetTime = (int)(realTimeMs / targetTickDurationMs + 0.5); // Round to nearest tick
    
    return targetTime;
}

bool SharedClipboard::lockMemory() {
    if (!_semaphore) {
        return false;
    }

    bool acquired = _semaphore->acquire();
    return acquired;
}

void SharedClipboard::unlockMemory() {
    if (_semaphore) {
        bool released = _semaphore->release();
    }
}
