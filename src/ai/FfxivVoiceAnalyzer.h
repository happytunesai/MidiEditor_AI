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

#ifndef FFXIVVOICEANALYZER_H_
#define FFXIVVOICEANALYZER_H_

#include <QObject>
#include <QVector>
#include <QHash>
#include <QPointer>
#include <QTimer>
#include <QMutex>
#include <functional>

#include "FfxivVoiceLoadCore.h"

class MidiFile;

/**
 * \class FfxivVoiceAnalyzer
 *
 * \brief Headless analyser for FFXIV Bard Performance Mode constraints.
 *
 * Computes simultaneous voice counts and per-channel note rates so the
 * UI can warn the user about passages that the FFXIV client will silently
 * truncate or drop. See Phase 32 in Planning/02_ROADMAP.md.
 *
 * Hard FFXIV constraints (constants below):
 *  - Polyphony / voice ceiling: 16 voices total across the whole ensemble.
 *  - Note rate: <= 14 notes/sec per channel (~50 ms minimum interval).
 *  - Range: C3..C6 (MIDI 48..84). Enforced elsewhere (FFXIVChannelFixer
 *    + bard accuracy path); not the analyser's job.
 *
 * Singleton. Owns one analysis cache per MidiFile* and re-runs (debounced)
 * whenever Protocol::actionFinished is emitted on that file.
 */
class FfxivVoiceAnalyzer : public QObject {
    Q_OBJECT

public:
    /// Hard FFXIV game ceilings. Public so widgets can reference them.
    static constexpr int kVoiceCeiling = FfxivVoiceLoad::kVoiceCeiling;
    static constexpr int kNoteRateCeilingPerChannel = FfxivVoiceLoad::kNoteRateCeilingPerChannel;
    static constexpr int kNoteRateWindowMs = FfxivVoiceLoad::kNoteRateWindowMs;

    using VoiceSample = FfxivVoiceLoad::VoiceSample;
    using RateHotspot = FfxivVoiceLoad::RateHotspot;
    using Result = FfxivVoiceLoad::Result;
    using NoteSpan = FfxivVoiceLoad::NoteSpan;

    /// Singleton accessor.
    static FfxivVoiceAnalyzer *instance();

    /// Whether the analyser is currently doing work.
    /// When disabled, queries return an empty Result and rebuilds are skipped.
    bool isEnabled() const;
    void setEnabled(bool on);

    /// Track a file (idempotent). Triggers a rebuild on the next event loop tick.
    void watchFile(MidiFile *file);

    /// Stop tracking and free the cache for `file`.
    void forgetFile(MidiFile *file);

    /// Snapshot of the current cached result. Empty Result if file unknown / disabled.
    Result resultFor(MidiFile *file) const;

    /// Voice count active at `tick` (inclusive). Returns 0 if no result.
    /// O(log N) binary search over voiceSamples.
    int voiceCountAt(MidiFile *file, int tick) const;

    /// Force an immediate (synchronous) recompute and return the new Result.
    /// Used by tests and by UI elements that need a result before idle.
    Result recomputeNow(MidiFile *file);

    /// Pure-data compute exposed for tests. Delegates to FfxivVoiceLoad::computeFromNotes.
    static Result computeFromNotes(const QVector<NoteSpan> &notes,
                                   const std::function<int(int)> &msAtTick) {
        return FfxivVoiceLoad::computeFromNotes(notes, msAtTick);
    }

signals:
    /// Emitted (from the GUI thread) when a Result was rebuilt.
    void analysisUpdated(MidiFile *file);

private slots:
    void onActionFinished();
    void onDebounceTimeout();

private:
    explicit FfxivVoiceAnalyzer(QObject *parent = nullptr);
    Q_DISABLE_COPY(FfxivVoiceAnalyzer)

    /// Pure compute. Safe to call without locking.
    static Result computeResult(MidiFile *file);

    /// Connect Protocol::actionFinished from `file->protocol()` if present.
    void hookProtocol(MidiFile *file);

    void scheduleRebuild(MidiFile *file);

    mutable QMutex _mutex;
    QHash<MidiFile *, Result> _cache;
    QHash<MidiFile *, QMetaObject::Connection> _protocolConns;
    QTimer _debounce;
    QPointer<MidiFile> _pendingRebuild;
    bool _enabled = true;
};

#endif // FFXIVVOICEANALYZER_H_
