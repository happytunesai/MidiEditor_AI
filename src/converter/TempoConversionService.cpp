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
        // Meta/tempo/timesig channels follow the include* flags directly.
        return true;
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
        // Meta/tempo/timesig: allow if their include* flag is set.
        return true;
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

    const double scale = options.targetBpm / options.sourceBpm;
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
            ++affected;
        }
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
    const QString actionLabel = QStringLiteral(
        "Convert tempo (preserve duration): %1 \xE2\x86\x92 %2 BPM")
                                    .arg(options.sourceBpm, 0, 'f', 2)
                                    .arg(options.targetBpm, 0, 'f', 2);
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
            // ScaleTempoMap: also rewrite stored BPM.
            if (ci == kTempoChannel
                && options.tempoMode == TempoConversionTempoMode::ScaleTempoMap) {
                if (auto *tc = dynamic_cast<TempoChangeEvent *>(ev)) {
                    const double newBpm = static_cast<double>(tc->beatsPerQuarter()) * scale;
                    const int clamped = qBound(1, static_cast<int>(qRound(newBpm)), 999);
                    tc->setBeats(clamped);
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
            const int targetBpmInt = qBound(1,
                                            static_cast<int>(qRound(options.targetBpm)),
                                            999);
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
