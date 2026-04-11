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

#include "LyricManager.h"

#include "MidiFile.h"
#include "MidiChannel.h"
#include "MidiTrack.h"
#include "../MidiEvent/TextEvent.h"
#include "../converter/SrtParser.h"
#include "../protocol/Protocol.h"

#include <QMultiMap>
#include <QStringList>
#include <algorithm>

LyricManager::LyricManager(MidiFile *file, QObject *parent)
    : QObject(parent)
    , _file(file)
{
}

// === Access ===

const QList<LyricBlock> &LyricManager::allBlocks() const
{
    return _blocks;
}

LyricBlock LyricManager::blockAt(int index) const
{
    if (index < 0 || index >= _blocks.size()) {
        return LyricBlock();
    }
    return _blocks.at(index);
}

LyricBlock LyricManager::blockAtTick(int tick) const
{
    int idx = indexAtTick(tick);
    if (idx >= 0) {
        return _blocks.at(idx);
    }
    return LyricBlock();
}

int LyricManager::indexAtTick(int tick) const
{
    for (int i = 0; i < _blocks.size(); i++) {
        if (tick >= _blocks[i].startTick && tick < _blocks[i].endTick) {
            return i;
        }
    }
    return -1;
}

int LyricManager::count() const
{
    return _blocks.size();
}

bool LyricManager::hasLyrics() const
{
    return !_blocks.isEmpty();
}

const LyricMetadata &LyricManager::metadata() const
{
    return _metadata;
}

void LyricManager::setMetadata(const LyricMetadata &meta)
{
    _metadata = meta;
    emit lyricsChanged();
}

bool LyricManager::hasMetadata() const
{
    return !_metadata.isEmpty();
}

// === Editing ===

void LyricManager::addBlock(const LyricBlock &block)
{
    if (block.text.trimmed().isEmpty()) return;

    if (_file && _file->protocol()) {
        _file->protocol()->startNewAction("Add Lyric Block");
    }

    // Create a TextEvent in the MIDI file for this block
    LyricBlock newBlock = block;
    if (_file) {
        MidiTrack *track = nullptr;
        if (block.trackIndex >= 0 && block.trackIndex < _file->numTracks()) {
            track = _file->track(block.trackIndex);
        } else if (_file->numTracks() > 0) {
            track = _file->track(0);
        }

        if (track) {
            TextEvent *te = new TextEvent(16, track);
            te->setText(block.text);
            te->setType(TextEvent::LYRIK);
            _file->channel(16)->insertEvent(te, block.startTick);
            newBlock.sourceEvent = te;
        }
    }

    int idx = insertSorted(newBlock);

    if (_file && _file->protocol()) {
        _file->protocol()->endAction();
    }

    emit blockAdded(idx);
    emit lyricsChanged();
}

void LyricManager::removeBlock(int index)
{
    if (index < 0 || index >= _blocks.size()) return;

    if (_file && _file->protocol()) {
        _file->protocol()->startNewAction("Remove Lyric Block");
    }

    LyricBlock &block = _blocks[index];

    // Remove the underlying TextEvent from the MIDI file (use event's own channel)
    if (block.sourceEvent && _file) {
        int ch = block.sourceEvent->channel();
        _file->channel(ch)->removeEvent(block.sourceEvent);
    }

    _blocks.removeAt(index);

    if (_file && _file->protocol()) {
        _file->protocol()->endAction();
    }

    emit blockRemoved(index);
    emit lyricsChanged();
}

void LyricManager::moveBlock(int index, int newStartTick)
{
    if (index < 0 || index >= _blocks.size()) return;
    if (newStartTick < 0) newStartTick = 0;

    if (_file && _file->protocol()) {
        _file->protocol()->startNewAction("Move Lyric Block");
    }

    LyricBlock block = _blocks[index];
    int duration = block.durationTicks();
    block.startTick = newStartTick;
    block.endTick = newStartTick + duration;

    // Move the underlying TextEvent
    if (block.sourceEvent && _file) {
        block.sourceEvent->setMidiTime(newStartTick);
    }

    // Remove and re-insert to maintain sort order
    _blocks.removeAt(index);
    int newIdx = insertSorted(block);

    if (_file && _file->protocol()) {
        _file->protocol()->endAction();
    }

    emit blockModified(newIdx);
    emit lyricsChanged();
}

void LyricManager::resizeBlock(int index, int newEndTick)
{
    if (index < 0 || index >= _blocks.size()) return;

    LyricBlock &block = _blocks[index];
    if (newEndTick <= block.startTick) return;

    if (_file && _file->protocol()) {
        _file->protocol()->startNewAction("Resize Lyric Block");
    }

    block.endTick = newEndTick;

    if (_file && _file->protocol()) {
        _file->protocol()->endAction();
    }

    emit blockModified(index);
    emit lyricsChanged();
}

void LyricManager::editBlockText(int index, const QString &newText)
{
    if (index < 0 || index >= _blocks.size()) return;

    if (_file && _file->protocol()) {
        _file->protocol()->startNewAction("Edit Lyric Text");
    }

    LyricBlock &block = _blocks[index];
    block.text = newText;

    // Update the underlying TextEvent
    if (block.sourceEvent) {
        TextEvent *te = dynamic_cast<TextEvent *>(block.sourceEvent);
        if (te) {
            te->setText(newText);
        }
    }

    if (_file && _file->protocol()) {
        _file->protocol()->endAction();
    }

    emit blockModified(index);
    emit lyricsChanged();
}

// === Direct editing (no Protocol, no re-sort) ===

void LyricManager::moveBlockDirect(int index, int newStartTick)
{
    if (index < 0 || index >= _blocks.size()) return;
    if (newStartTick < 0) newStartTick = 0;

    LyricBlock &block = _blocks[index];
    int duration = block.durationTicks();
    block.startTick = newStartTick;
    block.endTick = newStartTick + duration;

    // Move the underlying TextEvent
    if (block.sourceEvent && _file) {
        block.sourceEvent->setMidiTime(newStartTick);
    }

    // NOTE: Does NOT re-sort. Caller is responsible for sort if needed.
    emit blockModified(index);
    emit lyricsChanged();
}

void LyricManager::resizeBlockDirect(int index, int newEndTick)
{
    if (index < 0 || index >= _blocks.size()) return;

    LyricBlock &block = _blocks[index];
    if (newEndTick <= block.startTick) return;
    block.endTick = newEndTick;

    emit blockModified(index);
    emit lyricsChanged();
}

void LyricManager::editBlockTextDirect(int index, const QString &newText)
{
    if (index < 0 || index >= _blocks.size()) return;

    LyricBlock &block = _blocks[index];
    block.text = newText;

    if (block.sourceEvent) {
        TextEvent *te = dynamic_cast<TextEvent *>(block.sourceEvent);
        if (te) te->setText(newText);
    }

    emit blockModified(index);
    emit lyricsChanged();
}

void LyricManager::removeBlockDirect(int index)
{
    if (index < 0 || index >= _blocks.size()) return;

    LyricBlock &block = _blocks[index];
    if (block.sourceEvent && _file) {
        int ch = block.sourceEvent->channel();
        _file->channel(ch)->removeEvent(block.sourceEvent);
    }

    _blocks.removeAt(index);

    emit blockRemoved(index);
    emit lyricsChanged();
}

// === Import ===

void LyricManager::importFromTextEvents()
{
    _blocks.clear();

    if (!_file) {
        emit lyricsChanged();
        return;
    }

    // Collect all lyric/text events from all channels
    struct EventInfo {
        int tick;
        MidiEvent *event;
        int trackIdx;
    };
    QList<EventInfo> collected;

    for (int ch = 0; ch < 17; ch++) {
        QMultiMap<int, MidiEvent *> *map = _file->channelEvents(ch);
        if (!map) continue;

        for (auto it = map->constBegin(); it != map->constEnd(); ++it) {
            TextEvent *te = dynamic_cast<TextEvent *>(it.value());
            if (!te) continue;

            int t = te->type();
            if (t == TextEvent::LYRIK || t == TextEvent::TEXT) {
                if (te->text().trimmed().isEmpty()) continue;

                EventInfo info;
                info.tick = it.key();
                info.event = te;
                info.trackIdx = te->track() ? te->track()->number() : -1;
                collected.append(info);
            }
        }
    }

    // Sort by tick
    std::sort(collected.begin(), collected.end(),
              [](const EventInfo &a, const EventInfo &b) {
                  return a.tick < b.tick;
              });

    // Convert to LyricBlocks
    for (int i = 0; i < collected.size(); i++) {
        LyricBlock block;
        block.startTick = collected[i].tick;
        block.text = dynamic_cast<TextEvent *>(collected[i].event)->text();
        block.trackIndex = collected[i].trackIdx;
        block.sourceEvent = collected[i].event;

        // endTick = start of next block, or +480 ticks for the last block
        if (i + 1 < collected.size()) {
            block.endTick = collected[i + 1].tick;
            // Enforce minimum duration for same-tick events (P3-003)
            if (block.endTick <= block.startTick)
                block.endTick = block.startTick + 120;
        } else {
            block.endTick = block.startTick + 480;
        }

        _blocks.append(block);
    }

    emit lyricsChanged();
}

void LyricManager::importFromPlainText(const QString &text, int startTick,
                                        int defaultDurationTicks, bool skipEmptyLines)
{
    if (text.trimmed().isEmpty()) return;

    if (_file && _file->protocol()) {
        _file->protocol()->startNewAction("Import Lyrics from Text");
    }

    QStringList lines = text.split('\n');
    int currentTick = startTick;

    for (const QString &line : lines) {
        QString trimmed = line.trimmed();

        if (trimmed.isEmpty()) {
            if (skipEmptyLines) continue;
            // Non-skip mode: advance tick for empty line (gap)
            currentTick += defaultDurationTicks;
            continue;
        }

        LyricBlock block;
        block.startTick = currentTick;
        block.endTick = currentTick + defaultDurationTicks;
        block.text = trimmed;
        block.trackIndex = -1;

        // Create a TextEvent for this block
        if (_file && _file->numTracks() > 0) {
            MidiTrack *track = _file->track(0);
            TextEvent *te = new TextEvent(16, track);
            te->setText(trimmed);
            te->setType(TextEvent::LYRIK);
            _file->channel(16)->insertEvent(te, currentTick);
            block.sourceEvent = te;
        }

        _blocks.append(block);
        currentTick = block.endTick;
    }

    // Re-sort to maintain sort invariant (LYRIC-013)
    std::sort(_blocks.begin(), _blocks.end(),
              [](const LyricBlock &a, const LyricBlock &b) {
                  return a.startTick < b.startTick;
              });

    if (_file && _file->protocol()) {
        _file->protocol()->endAction();
    }

    emit lyricsChanged();
}

void LyricManager::importFromSrt(const QString &srtPath)
{
    if (!_file) return;

    QList<LyricBlock> imported = SrtParser::importSrt(srtPath, _file);
    if (imported.isEmpty()) return;

    if (_file->protocol()) {
        _file->protocol()->startNewAction("Import Lyrics from SRT");
    }

    // Create TextEvents for each imported block
    MidiTrack *defaultTrack = (_file->numTracks() > 0) ? _file->track(0) : nullptr;

    for (LyricBlock &block : imported) {
        if (defaultTrack) {
            TextEvent *te = new TextEvent(16, defaultTrack);
            te->setText(block.text);
            te->setType(TextEvent::LYRIK);
            _file->channel(16)->insertEvent(te, block.startTick);
            block.sourceEvent = te;
        }
        _blocks.append(block);
    }

    // Re-sort since we appended
    std::sort(_blocks.begin(), _blocks.end(),
              [](const LyricBlock &a, const LyricBlock &b) {
                  return a.startTick < b.startTick;
              });

    if (_file->protocol()) {
        _file->protocol()->endAction();
    }

    emit lyricsChanged();
}

// === Export ===

bool LyricManager::exportToSrt(const QString &srtPath)
{
    if (!_file || _blocks.isEmpty()) return false;
    return SrtParser::exportSrt(srtPath, _blocks, _file);
}

void LyricManager::exportToTextEvents()
{
    if (!_file || _blocks.isEmpty()) return;
    if (!_file->protocol()) return;

    _file->protocol()->startNewAction("Export Lyrics to MIDI");

    // Remove existing lyric/text events from all channels
    for (int ch = 0; ch < 17; ch++) {
        QMultiMap<int, MidiEvent *> *map = _file->channelEvents(ch);
        if (!map) continue;
        QList<MidiEvent *> toRemove;
        for (auto it = map->constBegin(); it != map->constEnd(); ++it) {
            TextEvent *te = dynamic_cast<TextEvent *>(it.value());
            if (te && (te->type() == TextEvent::LYRIK || te->type() == TextEvent::TEXT)) {
                toRemove.append(te);
            }
        }
        for (MidiEvent *ev : toRemove) {
            _file->channel(ch)->removeEvent(ev);
        }
    }

    // Create new TextEvents for each block
    MidiTrack *defaultTrack = (_file->numTracks() > 0) ? _file->track(0) : nullptr;
    if (!defaultTrack) {
        _file->protocol()->endAction();
        return;
    }

    for (int i = 0; i < _blocks.size(); i++) {
        LyricBlock &block = _blocks[i];

        MidiTrack *track = defaultTrack;
        if (block.trackIndex >= 0 && block.trackIndex < _file->numTracks()) {
            track = _file->track(block.trackIndex);
        }

        TextEvent *te = new TextEvent(16, track);
        te->setText(block.text);
        te->setType(TextEvent::LYRIK);
        _file->channel(16)->insertEvent(te, block.startTick);

        block.sourceEvent = te;
    }

    _file->protocol()->endAction();
    emit lyricsChanged();
}

// === Bulk Operations ===

void LyricManager::clearAllBlocks()
{
    if (_blocks.isEmpty()) return;

    if (_file && _file->protocol()) {
        _file->protocol()->startNewAction("Clear All Lyrics");
    }

    // Remove all linked TextEvents (use event's own channel)
    for (const LyricBlock &block : _blocks) {
        if (block.sourceEvent && _file) {
            int ch = block.sourceEvent->channel();
            _file->channel(ch)->removeEvent(block.sourceEvent);
        }
    }

    _blocks.clear();

    if (_file && _file->protocol()) {
        _file->protocol()->endAction();
    }

    emit lyricsChanged();
}

void LyricManager::setFile(MidiFile *file)
{
    _file = file;
    _blocks.clear();

    if (_file) {
        importFromTextEvents();
    } else {
        emit lyricsChanged();
    }
}

// === Private ===

int LyricManager::insertSorted(const LyricBlock &block)
{
    // Binary search for insertion point
    int lo = 0, hi = _blocks.size();
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (_blocks[mid].startTick < block.startTick) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    _blocks.insert(lo, block);
    return lo;
}

void LyricManager::sortBlocks()
{
    std::sort(_blocks.begin(), _blocks.end(),
              [](const LyricBlock &a, const LyricBlock &b) {
                  return a.startTick < b.startTick;
              });
    emit lyricsChanged();
}
