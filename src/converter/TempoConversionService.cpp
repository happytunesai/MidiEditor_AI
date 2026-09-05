/*
 * MidiEditor AI
 *
 * TempoConversionService — Phase 33 implementation.
 */

#include "TempoConversionService.h"

#include <QHash>
#include <QList>
#include <QMultiMap>
#include <QtMath>

#include <limits>

#include "../MidiEvent/MidiEvent.h"
#include "../MidiEvent/OffEvent.h"
#include "../MidiEvent/TempoChangeEvent.h"
#include "../midi/MidiChannel.h"
#include "../midi/MidiFile.h"
#include "../midi/MidiTrack.h"
#include "../protocol/Protocol.h"

namespace {

constexpr int kMetaChannel = 16;
constexpr int kTempoChannel = 17;
constexpr int kTimeSigChannel = 18;
constexpr double kBpmEpsilon = 1e-6;
// A tempo event stores 60000000 / bpm microseconds per quarter, so the map can
// only ever express a WHOLE bpm in this range - see storableBpm().
constexpr int kBpmMinInt = 1;
constexpr int kBpmMaxInt = 999;
constexpr double kBpmMin = 1.0;
constexpr double kBpmMax = 999.0;

// The tempo value that would really be written for `bpm`. The range test comes
// first: qRound() on a far out-of-range double is undefined, and this is fed
// with bpm * scale.
int storableBpm(double bpm) {
    if (bpm <= kBpmMin) {
        return kBpmMinInt;
    }
    if (bpm >= kBpmMax) {
        return kBpmMaxInt;
    }
    return qBound(kBpmMinInt, static_cast<int>(qRound(bpm)), kBpmMaxInt);
}

bool channelInScope(int channelIndex,
                    const TempoConversionOptions &opts) {
    switch (opts.scope) {
    case TempoConversionScope::WholeProject:
        return true;
    case TempoConversionScope::SelectedTracks:
        // Track filter is applied per event.
        return true;
    case TempoConversionScope::SelectedChannels:
        if (channelIndex >= 0 && channelIndex < 16) {
            return opts.channelIds.contains(channelIndex);
        }
        // Channels 16-18 (meta / tempo / time signature) are file-GLOBAL, and
        // channelIds can only name 0..15 - a channel scope can never ask for
        // them. See eventInScope() for the full reasoning.
        return false;
    case TempoConversionScope::SelectedEvents:
        return true;
    }
    return true;
}

bool eventInScope(MidiEvent *ev,
                  int channelIndex,
                  const TempoConversionOptions &opts) {
    if (!ev) {
        return false;
    }
    switch (opts.scope) {
    case TempoConversionScope::WholeProject:
        return true;
    case TempoConversionScope::SelectedTracks: {
        MidiTrack *t = ev->track();
        if (!t) {
            return false;
        }
        return opts.trackIds.contains(t->number());
    }
    case TempoConversionScope::SelectedChannels:
        if (channelIndex >= 0 && channelIndex < 16) {
            return opts.channelIds.contains(channelIndex);
        }
        // Channels 16 (meta: lyrics, text, key signature), 17 (tempo) and
        // 18 (time signature) are file-GLOBAL: every track and channel is
        // played back through them. `channelIds` only holds MIDI channels
        // 0..15, so a channel scope never names them - and re-ticking a shared
        // time signature or meta event would move the bar grid (and the marker
        // texts) for the material OUTSIDE the scope, which is exactly what the
        // partial-scope contract promises not to do. Same reasoning that makes
        // scopeModeConflict() refuse a shared tempo-map rewrite from a partial
        // scope; the include* flags stay honoured for scope == WholeProject.
        // SelectedTracks / SelectedEvents need no equivalent: their track
        // filter / pointer set applies to channels 16-18 as well, so those
        // scopes only ever touch what the caller actually named.
        return false;
    case TempoConversionScope::SelectedEvents:
        return opts.selectedEventPtrs.contains(reinterpret_cast<quintptr>(ev));
    }
    return false;
}

bool channelTypeIncluded(int channelIndex,
                         const TempoConversionOptions &opts) {
    if (channelIndex == kMetaChannel) {
        return opts.includeMeta;
    }
    if (channelIndex == kTempoChannel) {
        return opts.includeTempo;
    }
    if (channelIndex == kTimeSigChannel) {
        return opts.includeTimeSig;
    }
    return true; // 0..15 always included
}

qint64 scaledTick(int oldTick, double scale) {
    if (oldTick <= 0) {
        return 0;
    }
    return static_cast<qint64>(qRound64(static_cast<double>(oldTick) * scale));
}

QList<MidiEvent *> snapshotChannel(MidiChannel *channel) {
    QList<MidiEvent *> out;
    if (!channel) {
        return out;
    }
    QMultiMap<int, MidiEvent *> *map = channel->eventMap();
    if (!map) {
        return out;
    }
    out.reserve(map->size());
    for (auto it = map->begin(); it != map->end(); ++it) {
        out.append(it.value());
    }
    return out;
}

} // namespace

QString TempoConversionService::scopeModeConflict(
    const TempoConversionOptions &options) {
    if (options.scope == TempoConversionScope::WholeProject) {
        return QString();
    }
    if (options.tempoMode == TempoConversionTempoMode::EventsOnly) {
        return QString();
    }
    // ReplaceFixed inserts a fixed tempo at tick 0 (and drops the old map);
    // ScaleTempoMap rewrites the stored BPMs and moves the tempo events. Both
    // touch the ONE map that every channel and track is played back through,
    // so doing it from a partial scope silently retimes the rest of the file.
    return QStringLiteral(
        "The tempo map is shared, so changing it from a partial scope would retime "
        "everything outside the scope. Use \"Scale events only\" for partial scopes.");
}

TempoConversionResult TempoConversionService::preview(
    MidiFile *file, const TempoConversionOptions &options) {
    TempoConversionResult result;
    if (!file) {
        result.error = QStringLiteral("No file loaded.");
        return result;
    }
    if (options.sourceBpm <= kBpmEpsilon || options.targetBpm <= kBpmEpsilon) {
        result.error = QStringLiteral("Source and target BPM must be > 0.");
        return result;
    }
    // Refuse what the tempo map cannot express: the ticks would be scaled by the
    // requested ratio while the written tempo is clamped to 1..999, which silently
    // destroys the duration preservation this service promises. The dialog already
    // enforces this range; callers without a UI (the AI/MCP tool) did not.
    if (options.sourceBpm < kBpmMin || options.sourceBpm > kBpmMax
        || options.targetBpm < kBpmMin || options.targetBpm > kBpmMax) {
        result.error = QStringLiteral("Source and target BPM must be between 1 and 999.");
        return result;
    }
    const QString conflict = scopeModeConflict(options);
    if (!conflict.isEmpty()) {
        result.error = conflict;
        return result;
    }

    // ReplaceFixed stamps ONE whole-bpm tempo event, so the file will play at that
    // rounded tempo: scale the ticks by the ACHIEVABLE ratio, or a fractional
    // target (128.50 written as 129) leaves a permanent timing error behind.
    const double effectiveTargetBpm =
        options.tempoMode == TempoConversionTempoMode::ReplaceFixed
            ? static_cast<double>(storableBpm(options.targetBpm))
            : options.targetBpm;
    const double scale = effectiveTargetBpm / options.sourceBpm;
    result.scaleFactor = scale;
    result.oldDurationMs = file->msOfTick(file->endTick());

    if (qFuzzyCompare(scale, 1.0)) {
        result.warning = QStringLiteral(
            "Source and target BPM are identical — nothing to convert.");
        result.newDurationMs = result.oldDurationMs;
        result.ok = true;
        return result;
    }

    int affected = 0;
    int tempoRemoved = 0;
    int tempoInserted = 0;
    int clampedTempoEvents = 0;
    qint64 maxNewTick = 0;

    for (int ci = 0; ci < 19; ++ci) {
        if (!channelTypeIncluded(ci, options)) {
            continue;
        }
        if (!channelInScope(ci, options)) {
            continue;
        }
        MidiChannel *ch = file->channel(ci);
        if (!ch) {
            continue;
        }
        const QList<MidiEvent *> events = snapshotChannel(ch);
        for (MidiEvent *ev : events) {
            if (!eventInScope(ev, ci, options)) {
                continue;
            }
            if (ci == kTempoChannel
                && options.tempoMode == TempoConversionTempoMode::ReplaceFixed) {
                ++tempoRemoved;
                continue;
            }
            if (ci == kTempoChannel
                && options.tempoMode == TempoConversionTempoMode::EventsOnly) {
                continue;
            }
            maxNewTick = qMax(maxNewTick, scaledTick(ev->midiTime(), scale));
            // A rewritten bpm outside 1..999 gets clamped by convert(), and that
            // passage then no longer keeps its real-time duration - say so.
            if (ci == kTempoChannel
                && options.tempoMode == TempoConversionTempoMode::ScaleTempoMap) {
                if (auto *tc = dynamic_cast<TempoChangeEvent *>(ev)) {
                    // Same exact (microsecond-based) tempo convert() scales.
                    const double newBpm =
                        (60000000.0 / tc->microsPerQuarter()) * scale;
                    if (newBpm < kBpmMin - 0.5 || newBpm > kBpmMax + 0.5) {
                        ++clampedTempoEvents;
                    }
                }
            }
            ++affected;
        }
    }

    // MidiEvent keeps its tick in an int and convert() casts the scaled qint64
    // down to one: past INT_MAX that wraps to a negative position instead of
    // simply being far away, so refuse the conversion rather than corrupt it.
    if (maxNewTick > static_cast<qint64>(std::numeric_limits<int>::max())) {
        result.error = QStringLiteral(
            "The scaled tick positions would leave the representable range. "
            "Use a smaller target BPM.");
        return result;
    }

    if (options.tempoMode == TempoConversionTempoMode::ReplaceFixed
        && options.includeTempo) {
        tempoInserted = 1;
    }

    result.affectedEvents = affected;
    result.tempoEventsRemoved = tempoRemoved;
    result.tempoEventsInserted = tempoInserted;
    // Predicted new duration: in ReplaceFixed mode the project plays at
    // targetBpm exactly, and ticks scale by `scale`, so real time is
    // preserved. In ScaleTempoMap, both ticks and stored BPMs scale, so
    // real time is also preserved. In EventsOnly the user owns the tempo
    // map; we predict assuming the existing average tempo still applies,
    // which is just the old duration multiplied by (1 / scale) of the
    // tick movement vs unchanged tempo — too speculative, so we just
    // mirror oldDurationMs as a best-effort.
    if (options.tempoMode == TempoConversionTempoMode::EventsOnly) {
        // ticks scaled by `scale` but tempo map unchanged → duration scales by `scale`.
        result.newDurationMs = static_cast<qint64>(
            qRound64(static_cast<double>(result.oldDurationMs) * scale));
    } else {
        result.newDurationMs = result.oldDurationMs;
    }
    // Report the substitutions instead of quietly delivering something else than
    // the requested conversion.
    if (!qFuzzyCompare(effectiveTargetBpm, options.targetBpm)) {
        result.warning = QStringLiteral(
            "The tempo map stores whole BPM values: converting to %1 BPM "
            "instead of %2.")
                             .arg(storableBpm(options.targetBpm))
                             .arg(options.targetBpm, 0, 'f', 2);
    } else if (clampedTempoEvents > 0) {
        result.warning = QStringLiteral(
            "%1 tempo event(s) would scale beyond the 1-999 BPM range and are "
            "clamped, so those passages do NOT keep their duration.")
                             .arg(clampedTempoEvents);
    }
    result.ok = true;
    return result;
}

TempoConversionResult TempoConversionService::convert(
    MidiFile *file, const TempoConversionOptions &options) {
    TempoConversionResult result = preview(file, options);
    if (!result.ok) {
        return result;
    }
    if (qFuzzyCompare(result.scaleFactor, 1.0)) {
        // Nothing to do.
        return result;
    }

    const double scale = result.scaleFactor;
    const qint64 oldDurationMs = result.oldDurationMs;

    Protocol *protocol = file->protocol();
    // The caller may own the label (the AI/MCP tool does, so the action names
    // the actor); otherwise the service names itself.
    const QString actionLabel =
        options.actionLabel.isEmpty()
            ? QStringLiteral("Convert tempo (preserve duration): %1 \xE2\x86\x92 %2 BPM")
                  .arg(options.sourceBpm, 0, 'f', 2)
                  .arg(options.targetBpm, 0, 'f', 2)
            : options.actionLabel;
    protocol->startNewAction(actionLabel);

    int affected = 0;
    int tempoRemoved = 0;
    int tempoInserted = 0;

    // Pass 1: collect tempo events for ReplaceFixed handling.
    QList<MidiEvent *> tempoEventsToRemove;
    if (options.includeTempo
        && options.tempoMode == TempoConversionTempoMode::ReplaceFixed) {
        MidiChannel *ch = file->channel(kTempoChannel);
        if (ch) {
            const QList<MidiEvent *> events = snapshotChannel(ch);
            for (MidiEvent *ev : events) {
                if (eventInScope(ev, kTempoChannel, options)) {
                    tempoEventsToRemove.append(ev);
                }
            }
        }
    }

    // Pass 2: scale ticks for non-tempo events (and for tempo events when not
    // in ReplaceFixed mode).
    for (int ci = 0; ci < 19; ++ci) {
        if (!channelTypeIncluded(ci, options)) {
            continue;
        }
        if (!channelInScope(ci, options)) {
            continue;
        }
        if (ci == kTempoChannel) {
            if (options.tempoMode == TempoConversionTempoMode::ReplaceFixed
                || options.tempoMode == TempoConversionTempoMode::EventsOnly) {
                continue;
            }
        }
        MidiChannel *ch = file->channel(ci);
        if (!ch) {
            continue;
        }
        const QList<MidiEvent *> events = snapshotChannel(ch);
        for (MidiEvent *ev : events) {
            if (!eventInScope(ev, ci, options)) {
                continue;
            }
            const int oldTick = ev->midiTime();
            const qint64 newTick = scaledTick(oldTick, scale);
            if (newTick != oldTick) {
                ev->setMidiTime(static_cast<int>(newTick), true);
                ++affected;
            }
            // ScaleTempoMap: also rewrite the stored tempo - EXACTLY. Ticks are
            // scaled by the unrounded ratio, so writing a whole BPM here (the
            // pre-2.3.1 setBeats path) silently drifted every passage by the
            // rounding error (review R231-12). The event stores microseconds
            // per quarter, which represents any fractional BPM; only the 1-999
            // BPM range is still clamped (reported by preview()).
            if (ci == kTempoChannel
                && options.tempoMode == TempoConversionTempoMode::ScaleTempoMap) {
                if (auto *tc = dynamic_cast<TempoChangeEvent *>(ev)) {
                    const double oldBpm = 60000000.0 / tc->microsPerQuarter();
                    const double newBpm = qBound(static_cast<double>(kBpmMin),
                                                 oldBpm * scale,
                                                 static_cast<double>(kBpmMax));
                    tc->setMicrosPerQuarter(qRound(60000000.0 / newBpm));
                }
            }
        }
    }

    // Pass 3: ReplaceFixed tempo handling.
    //
    // Order matters: MidiChannel::removeEvent refuses to delete the only
    // tempo (or time-sig) event at tick 0, because the channel-17/18
    // guard treats that as the project's permanent anchor. We therefore
    // insert the new tempo first so the guard sees ≥ 2 entries and lets
    // the originals be removed cleanly.
    if (options.includeTempo
        && options.tempoMode == TempoConversionTempoMode::ReplaceFixed) {
        MidiChannel *tempoCh = file->channel(kTempoChannel);
        if (tempoCh) {
            MidiTrack *generalTrack = file->track(0);
            // Same value preview() scaled the ticks by - the two must not drift.
            const int targetBpmInt = storableBpm(options.targetBpm);
            auto *newTempo = new TempoChangeEvent(
                kTempoChannel,
                60000000 / targetBpmInt,
                generalTrack);
            tempoCh->insertEvent(newTempo, 0);
            ++tempoInserted;

            for (MidiEvent *ev : tempoEventsToRemove) {
                if (tempoCh->removeEvent(ev)) {
                    ++tempoRemoved;
                }
            }
        }
    }

    file->calcMaxTime();
    protocol->endAction();

    result.affectedEvents = affected;
    result.tempoEventsRemoved = tempoRemoved;
    result.tempoEventsInserted = tempoInserted;
    result.oldDurationMs = oldDurationMs;
    result.newDurationMs = file->msOfTick(file->endTick());
    return result;
}
