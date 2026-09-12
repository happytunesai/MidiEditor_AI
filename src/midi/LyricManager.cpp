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

namespace {

// F187: lyric metadata is persisted as LRC header TextEvents ("[ar:...]" etc.)
// at tick 0 - the same tags LrcExporter writes - so it survives save/reload
// and undo. The order below is the order the events are created in.
const char *const kLyricHeaderTags[] = { "ar", "ti", "al", "by", "offset" };

// Returns the lower-case tag of a "[tag:value]" header, or an empty string
// when the text is not one of the known LRC header tags.
QString lyricHeaderTag(const QString &text, QString *value)
{
    const QString s = text.trimmed();
    if (s.size() < 4 || !s.startsWith(QLatin1Char('[')) || !s.endsWith(QLatin1Char(']')))
        return QString();
    const int colon = s.indexOf(QLatin1Char(':'));
    if (colon < 2)
        return QString();
    const QString tag = s.mid(1, colon - 1).toLower();
    bool known = false;
    for (const char *t : kLyricHeaderTags) {
        if (tag == QLatin1String(t)) { known = true; break; }
    }
    if (!known)
        return QString();
    if (value)
        *value = s.mid(colon + 1, s.size() - colon - 2).trimmed();
    return tag;
}

// Parses one header into meta. Returns false when text is not a header.
bool parseLyricHeader(const QString &text, LyricMetadata &meta)
{
    QString val;
    const QString tag = lyricHeaderTag(text, &val);
    if (tag.isEmpty())
        return false;
    if (tag == QLatin1String("ar")) {
        meta.artist = val;
    } else if (tag == QLatin1String("ti")) {
        meta.title = val;
    } else if (tag == QLatin1String("al")) {
        meta.album = val;
    } else if (tag == QLatin1String("by")) {
        meta.lyricsBy = val;
    } else {
        bool ok = false;
        const int v = val.toInt(&ok);
        if (!ok)
            return false;
        meta.offsetMs = v;
    }
    return true;
}

// A tick-0 lyric/text event whose text is a known LRC header tag.
bool isLyricHeaderEvent(TextEvent *te, int tick)
{
    if (tick != 0 || !te)
        return false;
    const int t = te->type();
    if (t != TextEvent::LYRIK && t != TextEvent::TEXT)
        return false;
    LyricMetadata scratch;
    return parseLyricHeader(te->text(), scratch);
}

// Serialised value for a tag; empty means "no header event for this tag".
QString lyricHeaderValue(const LyricMetadata &meta, const QString &tag)
{
    if (tag == QLatin1String("ar")) return meta.artist.trimmed();
    if (tag == QLatin1String("ti")) return meta.title.trimmed();
    if (tag == QLatin1String("al")) return meta.album.trimmed();
    if (tag == QLatin1String("by")) return meta.lyricsBy.trimmed();
    if (tag == QLatin1String("offset") && meta.offsetMs != 0) return QString::number(meta.offsetMs);
    return QString();
}

} // namespace

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

void LyricManager::setMetadata(const LyricMetadata &meta, bool ownAction)
{
    // F187: this used to assign the in-memory struct only, so the Lyric Settings
    // were lost on reload and never dirtied the file. Persist every non-empty
    // field as a tick-0 "[tag:value]" TextEvent inside one Protocol action;
    // importFromTextEvents() reads them back (also after undo/redo).
    // ownAction=false lets a caller that already holds an open action (the LRC
    // import) write the header events into ITS step instead of a second one.
    MidiTrack *track = (_file && _file->numTracks() > 0) ? _file->track(0) : nullptr;
    if (!track) {
        _metadata = meta;
        emit lyricsChanged();
        return;
    }

    // Unchanged values would record an empty step: startNewAction() clears the
    // redo stack and endAction() dirties the file although nothing changed.
    const bool same = _metadata.artist.trimmed() == meta.artist.trimmed() &&
                      _metadata.title.trimmed() == meta.title.trimmed() &&
                      _metadata.album.trimmed() == meta.album.trimmed() &&
                      _metadata.lyricsBy.trimmed() == meta.lyricsBy.trimmed() &&
                      _metadata.offsetMs == meta.offsetMs;
    if (same) {
        _metadata = meta;
        emit lyricsChanged();
        return;
    }

    // Existing header events per tag (tick 0 on any lyric-bearing channel)
    QMap<QString, QList<TextEvent *>> existing;
    for (int ch = 0; ch < 17; ch++) {
        QMultiMap<int, MidiEvent *> *map = _file->channelEvents(ch);
        if (!map) continue;
        for (auto it = map->constBegin(); it != map->constEnd() && it.key() <= 0; ++it) {
            TextEvent *te = dynamic_cast<TextEvent *>(it.value());
            if (!isLyricHeaderEvent(te, it.key())) continue;
            existing[lyricHeaderTag(te->text(), nullptr)].append(te);
        }
    }

    if (ownAction && _file->protocol()) {
        _file->protocol()->startNewAction("Edit Lyric Metadata");
    }

    for (const char *tagC : kLyricHeaderTags) {
        const QString tag = QLatin1String(tagC);
        const QString value = lyricHeaderValue(meta, tag);
        const QList<TextEvent *> events = existing.value(tag);

        if (value.isEmpty()) {
            for (TextEvent *te : events) {
                _file->channel(te->channel())->removeEvent(te);
            }
            continue;
        }

        const QString text = QStringLiteral("[%1:%2]").arg(tag, value);
        if (events.isEmpty()) {
            TextEvent *te = new TextEvent(16, track);
            te->setText(text);
            te->setType(TextEvent::TEXT);
            _file->channel(16)->insertEvent(te, 0);
        } else {
            if (events.first()->text() != text) {
                events.first()->setText(text);
            }
            // One event per tag: drop duplicates
            for (int i = 1; i < events.size(); i++) {
                _file->channel(events[i]->channel())->removeEvent(events[i]);
            }
        }
    }

    _metadata = meta;

    if (ownAction && _file->protocol()) {
        _file->protocol()->endAction();
    }

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

    // Only a block backed by a TextEvent records a ProtocolItem here. Without one
    // the step stays empty: endAction() drops it again but still clears the redo
    // stack and marks the file modified, leaving a "Move Lyric Block" entry the
    // user can never undo. Mark the file modified directly in that case.
    const bool toProtocol = _blocks[index].sourceEvent && _file && _file->protocol();
    if (toProtocol) {
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

    if (toProtocol) {
        _file->protocol()->endAction();
    } else if (_file) {
        _file->setSaved(false);
    }

    emit blockModified(newIdx);
    emit lyricsChanged();
}

void LyricManager::resizeBlock(int index, int newEndTick)
{
    if (index < 0 || index >= _blocks.size()) return;

    LyricBlock &block = _blocks[index];
    if (newEndTick <= block.startTick) return;

    // A resize only changes the in-memory LyricBlock::endTick - no MidiEvent is
    // touched, so the action recorded nothing while startNewAction() had already
    // dropped the redo stack and endAction() marked the file modified: a phantom
    // "Resize Lyric Block" undo entry. Same decision as LyricTimelineWidget's
    // DragResizeRight (LYRIC-005) - mark the file modified, record no step.
    block.endTick = newEndTick;
    if (_file) {
        _file->setSaved(false);
    }

    emit blockModified(index);
    emit lyricsChanged();
}

void LyricManager::editBlockText(int index, const QString &newText)
{
    if (index < 0 || index >= _blocks.size()) return;

    TextEvent *te = dynamic_cast<TextEvent *>(_blocks[index].sourceEvent);

    // Only TextEvent::setText() records a ProtocolItem. For a block without a
    // source event the step stays empty: endAction() drops it but still clears
    // the redo stack and marks the file modified - a phantom "Edit Lyric Text"
    // entry the user can never undo. Mark the file modified directly instead.
    const bool toProtocol = te && _file && _file->protocol();
    if (toProtocol) {
        _file->protocol()->startNewAction("Edit Lyric Text");
    }

    LyricBlock &block = _blocks[index];
    block.text = newText;

    // Update the underlying TextEvent
    if (te) {
        te->setText(newText);
    }

    if (toProtocol) {
        _file->protocol()->endAction();
    } else if (_file) {
        _file->setSaved(false);
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
    // F187: the file is the source of truth for the metadata too - rebuild it
    // from the tick-0 header events (undo/redo re-enters here as well).
    _metadata = LyricMetadata();

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

                // F187: "[ar:...]"-style headers at tick 0 are metadata, not
                // phrases - parse them and keep them out of the block list.
                if (isLyricHeaderEvent(te, it.key())) {
                    parseLyricHeader(te->text(), _metadata);
                    continue;
                }

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

    // BULK-OP UNDO (same idiom as TempoMapThinner::thin): one snapshot of the
    // text channel, inserts with toProtocol=false, then a single commit. The
    // default toProtocol=true cloned the whole - growing - event map once per
    // imported line, i.e. O(N^2) map nodes pinned in this single undo step.
    MidiChannel *lyricChannel = _file ? _file->channel(16) : nullptr;
    ProtocolEntry *channelSnapshot = nullptr;

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
        if (lyricChannel && _file->numTracks() > 0) {
            MidiTrack *track = _file->track(0);
            TextEvent *te = new TextEvent(16, track);
            te->setText(trimmed);
            te->setType(TextEvent::LYRIK);
            if (!channelSnapshot) {
                channelSnapshot = lyricChannel->copy();
            }
            lyricChannel->insertEvent(te, currentTick, false);
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

    if (channelSnapshot) {
        lyricChannel->protocol(channelSnapshot, lyricChannel);
    }

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

    // BULK-OP UNDO (same idiom as TempoMapThinner::thin): one snapshot of the
    // text channel, inserts with toProtocol=false, then a single commit. The
    // default toProtocol=true cloned the whole - growing - event map once per
    // subtitle entry, i.e. O(N^2) map nodes pinned in this single undo step.
    MidiChannel *lyricChannel = _file->channel(16);
    ProtocolEntry *channelSnapshot = nullptr;

    for (LyricBlock &block : imported) {
        if (defaultTrack) {
            TextEvent *te = new TextEvent(16, defaultTrack);
            te->setText(block.text);
            te->setType(TextEvent::LYRIK);
            if (!channelSnapshot) {
                channelSnapshot = lyricChannel->copy();
            }
            lyricChannel->insertEvent(te, block.startTick, false);
            block.sourceEvent = te;
        }
        _blocks.append(block);
    }

    // Re-sort since we appended
    std::sort(_blocks.begin(), _blocks.end(),
              [](const LyricBlock &a, const LyricBlock &b) {
                  return a.startTick < b.startTick;
              });

    if (channelSnapshot) {
        lyricChannel->protocol(channelSnapshot, lyricChannel);
    }

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

    // BULK-OP UNDO (same idiom as TempoMapThinner::thin): one snapshot per
    // touched channel, every removal/insert with toProtocol=false, then a single
    // commit per channel. The default toProtocol=true cloned the whole event map
    // once per lyric event - O(N^2) map nodes pinned in this single undo step.
    ProtocolEntry *snapshots[19] = { nullptr };
    auto commitSnapshots = [&]() {
        for (int ch = 0; ch < 19; ch++) {
            if (!snapshots[ch]) continue;
            MidiChannel *channel = _file->channel(ch);
            channel->protocol(snapshots[ch], channel);
            snapshots[ch] = nullptr;
        }
    };

    // Remove existing lyric/text events from all channels
    for (int ch = 0; ch < 17; ch++) {
        QMultiMap<int, MidiEvent *> *map = _file->channelEvents(ch);
        if (!map) continue;
        QList<MidiEvent *> toRemove;
        for (auto it = map->constBegin(); it != map->constEnd(); ++it) {
            TextEvent *te = dynamic_cast<TextEvent *>(it.value());
            if (te && (te->type() == TextEvent::LYRIK || te->type() == TextEvent::TEXT)) {
                // F187: tick-0 header events hold the metadata - never a block,
                // so they must survive the rebuild of the lyric events.
                if (isLyricHeaderEvent(te, it.key())) continue;
                toRemove.append(te);
            }
        }
        if (toRemove.isEmpty()) continue;
        MidiChannel *channel = _file->channel(ch);
        if (!snapshots[ch]) {
            snapshots[ch] = channel->copy();
        }
        for (MidiEvent *ev : toRemove) {
            channel->removeEvent(ev, false);
        }
    }

    // Create new TextEvents for each block
    MidiTrack *defaultTrack = (_file->numTracks() > 0) ? _file->track(0) : nullptr;
    if (!defaultTrack) {
        commitSnapshots();
        _file->protocol()->endAction();
        return;
    }

    MidiChannel *lyricChannel = _file->channel(16);
    if (!snapshots[16]) {
        snapshots[16] = lyricChannel->copy();
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
        lyricChannel->insertEvent(te, block.startTick, false);

        block.sourceEvent = te;
    }

    commitSnapshots();
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

    // BULK-OP UNDO (same idiom as TempoMapThinner::thin): one snapshot per
    // touched channel, removals with toProtocol=false, then a single commit per
    // channel. The default toProtocol=true cloned the whole event map once per
    // lyric event - O(N^2) map nodes pinned in this single undo step.
    ProtocolEntry *snapshots[19] = { nullptr };
    for (const LyricBlock &block : _blocks) {
        if (block.sourceEvent && _file) {
            int ch = block.sourceEvent->channel();
            MidiChannel *channel = _file->channel(ch);
            if (!snapshots[ch]) {
                snapshots[ch] = channel->copy();
            }
            channel->removeEvent(block.sourceEvent, false);
        }
    }
    for (int ch = 0; ch < 19 && _file; ch++) {
        if (!snapshots[ch]) continue;
        MidiChannel *channel = _file->channel(ch);
        channel->protocol(snapshots[ch], channel);
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

void LyricManager::addBlockDirect(const LyricBlock &block)
{
    int idx = insertSorted(block);
    emit blockAdded(idx);
    emit lyricsChanged();
}

void LyricManager::sortBlocks()
{
    std::sort(_blocks.begin(), _blocks.end(),
              [](const LyricBlock &a, const LyricBlock &b) {
                  return a.startTick < b.startTick;
              });
    emit lyricsChanged();
}
