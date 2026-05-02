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

#include "FfxivVoiceAnalyzer.h"

#include "../midi/MidiChannel.h"
#include "../midi/MidiFile.h"
#include "../MidiEvent/MidiEvent.h"
#include "../MidiEvent/NoteOnEvent.h"
#include "../MidiEvent/OffEvent.h"
#include "../MidiEvent/ProgChangeEvent.h"
#include "../protocol/Protocol.h"

#include <QMultiMap>
#include <QMutexLocker>
#include <algorithm>
#include <deque>

static constexpr int kDebounceMs = 100;

FfxivVoiceAnalyzer *FfxivVoiceAnalyzer::instance()
{
    static FfxivVoiceAnalyzer s_instance;
    return &s_instance;
}

FfxivVoiceAnalyzer::FfxivVoiceAnalyzer(QObject *parent)
    : QObject(parent)
{
    _debounce.setSingleShot(true);
    _debounce.setInterval(kDebounceMs);
    connect(&_debounce, &QTimer::timeout, this, &FfxivVoiceAnalyzer::onDebounceTimeout);
}

bool FfxivVoiceAnalyzer::isEnabled() const
{
    QMutexLocker lock(&_mutex);
    return _enabled;
}

void FfxivVoiceAnalyzer::setEnabled(bool on)
{
    bool changed = false;
    {
        QMutexLocker lock(&_mutex);
        if (_enabled == on)
            return;
        _enabled = on;
        changed = true;
        if (!on) {
            _cache.clear();
        }
    }
    if (changed && on) {
        // Re-prime any files we already know about.
        QList<MidiFile *> files;
        {
            QMutexLocker lock(&_mutex);
            files = _protocolConns.keys();
        }
        for (MidiFile *f : files)
            scheduleRebuild(f);
    }
}

void FfxivVoiceAnalyzer::watchFile(MidiFile *file)
{
    if (!file)
        return;
    hookProtocol(file);
    scheduleRebuild(file);
}

void FfxivVoiceAnalyzer::forgetFile(MidiFile *file)
{
    if (!file)
        return;
    QMutexLocker lock(&_mutex);
    _cache.remove(file);
    auto it = _protocolConns.find(file);
    if (it != _protocolConns.end()) {
        QObject::disconnect(it.value());
        _protocolConns.erase(it);
    }
}

FfxivVoiceAnalyzer::Result FfxivVoiceAnalyzer::resultFor(MidiFile *file) const
{
    QMutexLocker lock(&_mutex);
    if (!_enabled)
        return Result();
    auto it = _cache.find(file);
    if (it == _cache.end())
        return Result();
    return it.value();
}

int FfxivVoiceAnalyzer::voiceCountAt(MidiFile *file, int tick) const
{
    Result r = resultFor(file);
    if (!r.valid || r.voiceSamples.isEmpty())
        return 0;
    // Binary search for the latest sample with sample.tick <= tick.
    auto it = std::upper_bound(r.voiceSamples.begin(), r.voiceSamples.end(),
                               tick,
                               [](int t, const VoiceSample &s) {
                                   return t < s.tick;
                               });
    if (it == r.voiceSamples.begin())
        return 0;
    --it;
    return it->voiceCount;
}

FfxivVoiceAnalyzer::Result FfxivVoiceAnalyzer::recomputeNow(MidiFile *file)
{
    if (!file)
        return Result();
    Result r = computeResult(file);
    {
        QMutexLocker lock(&_mutex);
        if (_enabled)
            _cache.insert(file, r);
    }
    emit analysisUpdated(file);
    return r;
}

void FfxivVoiceAnalyzer::onActionFinished()
{
    // Sender is the Protocol; find which MidiFile it belongs to.
    QObject *s = sender();
    if (!s)
        return;
    QList<MidiFile *> files;
    {
        QMutexLocker lock(&_mutex);
        files = _protocolConns.keys();
    }
    for (MidiFile *f : files) {
        if (f && f->protocol() == s) {
            scheduleRebuild(f);
            return;
        }
    }
}

void FfxivVoiceAnalyzer::onDebounceTimeout()
{
    MidiFile *file = _pendingRebuild.data();
    _pendingRebuild.clear();
    if (!file)
        return;
    {
        QMutexLocker lock(&_mutex);
        if (!_enabled)
            return;
        if (!_protocolConns.contains(file))
            return; // file was forgotten while we waited
    }
    Result r = computeResult(file);
    {
        QMutexLocker lock(&_mutex);
        if (!_enabled || !_protocolConns.contains(file))
            return;
        _cache.insert(file, r);
    }
    emit analysisUpdated(file);
}

void FfxivVoiceAnalyzer::hookProtocol(MidiFile *file)
{
    QMutexLocker lock(&_mutex);
    if (_protocolConns.contains(file))
        return;
    Protocol *p = file->protocol();
    if (!p) {
        _protocolConns.insert(file, QMetaObject::Connection());
        return;
    }
    QMetaObject::Connection c = connect(p, &Protocol::actionFinished,
                                        this, &FfxivVoiceAnalyzer::onActionFinished,
                                        Qt::QueuedConnection);
    _protocolConns.insert(file, c);
}

void FfxivVoiceAnalyzer::scheduleRebuild(MidiFile *file)
{
    if (!_enabled || !file)
        return;
    _pendingRebuild = file;
    _debounce.start();
}

FfxivVoiceAnalyzer::Result FfxivVoiceAnalyzer::computeResult(MidiFile *file)
{
    Result r;
    if (!file)
        return r;

    // Build NoteSpans (start, end, channel, program, pitch).  We track
    // ProgramChange events per channel by walking the channel's event map
    // in tick order so each NoteOn picks up the program that was active
    // at its tick.  This lets the FFXIV sample-tail model
    // (FfxivVoiceLoad::sampleTailMs) extend each note's voice lifetime
    // to match BMP / MogNotate's "active voices" display, where plucked
    // and percussive samples ring out for ~1.0 – 1.7 seconds after key
    // release.
    QVector<FfxivVoiceLoad::NoteSpan> notes;
    notes.reserve(8192);

    for (int ch = 0; ch < 16; ++ch) {
        MidiChannel *channel = file->channel(ch);
        if (!channel)
            continue;
        QMultiMap<int, MidiEvent *> *events = channel->eventMap();
        if (!events)
            continue;

        int currentProgram = 0;
        const bool isDrum = (ch == 9);

        // QMultiMap iteration is in key order (ascending tick).  Within a
        // single tick, ProgChangeEvent should apply *before* NoteOn — we
        // walk the map in order and update currentProgram first, then
        // emit NoteSpans.  This matches the typical authoring order.
        for (auto it = events->begin(); it != events->end(); ++it) {
            MidiEvent *ev = it.value();
            if (auto *pc = dynamic_cast<ProgChangeEvent *>(ev)) {
                currentProgram = pc->program();
                continue;
            }
            auto *on = dynamic_cast<NoteOnEvent *>(ev);
            if (!on)
                continue;

            int startTick = on->midiTime();
            int endTick   = startTick + 1;
            OffEvent *off = on->offEvent();
            if (off) {
                int t = off->midiTime();
                if (t > startTick)
                    endTick = t;
            }

            FfxivVoiceLoad::NoteSpan ns;
            ns.channel       = ch;
            ns.startTick     = startTick;
            ns.endTick       = endTick;
            ns.program       = currentProgram;
            ns.pitch         = on->note();
            ns.isDrumChannel = isDrum;
            notes.push_back(ns);
        }
    }

    auto msAtTick = [file](int tick) { return file->timeMS(tick); };
    auto tickAtMs = [file](int ms)   { return file->tick(ms); };

    FfxivVoiceLoad::AnalyzeOptions opts;
    opts.simulateSampleTail = true;
    opts.tickAtMs           = tickAtMs;
    return FfxivVoiceLoad::computeFromNotes(notes, msAtTick, opts);
}

// computeFromNotes (test entry point) is now inline in the header,
// delegating to FfxivVoiceLoad::computeFromNotes.
