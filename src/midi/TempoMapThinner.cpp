#include "TempoMapThinner.h"

#include "MidiChannel.h"
#include "MidiFile.h"
#include "../MidiEvent/MidiEvent.h"
#include "../MidiEvent/TempoChangeEvent.h"
#include "../protocol/Protocol.h"
#include "../protocol/ProtocolEntry.h"

#include <QLocale>
#include <QVector>

#include <cmath>

namespace {

/** The tempo map as the algorithm needs it: tick, ms per tick, and the event
 *  that carries them. The order is the CHANNEL MAP's order, so events sharing
 *  a tick keep the sequence MidiFile::msOfTick() sees - a zero length segment
 *  contributes nothing, and the last one at that tick is the one in force. */
struct TempoPoint {
    int tick = 0;
    double msPerTick = 0;
    MidiEvent *event = nullptr;
};

/** Thousands separators for the Protocol label: QLocale::c() omits the group
 *  separator by default, and "12871 -> 47 events" reads like a serial number. */
QString groupedNumber(int n) {
    QLocale locale = QLocale::c();
    locale.setNumberOptions(locale.numberOptions() & ~QLocale::OmitGroupSeparator);
    return locale.toString(n);
}

/** Cumulative ms at every point of `map`, plus the ms at `endTick` appended as
 *  the last element - the same accumulation MidiFile::msOfTick() performs
 *  (each segment contributes msPerTick(previous) * tick distance). */
QVector<double> cumulativeMs(const QVector<TempoPoint> &map, int endTick) {
    QVector<double> times(map.size() + 1, 0.0);
    double t = 0.0;
    for (int i = 1; i < map.size(); ++i) {
        t += map.at(i - 1).msPerTick * (map.at(i).tick - map.at(i - 1).tick);
        times[i] = t;
    }
    if (!map.isEmpty()) {
        times[map.size()] =
            t + map.last().msPerTick * (endTick - map.last().tick);
    }
    return times;
}

/** One drift-bounded walk. `map` is the tempo map as it currently stands,
 *  `times` its cumulative ms (cumulativeMs(), end tick included). Returns the
 *  indices into `map` that survive - always including index 0.
 *
 *  Index map.size() is a SENTINEL standing for the file's end tick: it is
 *  never kept (no event lives there), it only makes the trailing segment
 *  subject to the same corridor, so the very last tempo event can be dropped
 *  too when the end time survives it. */
QVector<int> thinningPass(const QVector<TempoPoint> &map,
                          const QVector<double> &times, int endTick,
                          double toleranceMs) {
    QVector<int> keep;
    if (map.isEmpty()) {
        return keep;
    }
    const int n = map.size();
    keep.append(0);

    int anchor = 0;
    double anchorMs = 0.0; // thinned ms at the anchor tick

    while (true) {
        const double anchorMsPerTick = map.at(anchor).msPerTick;
        const int anchorTick = map.at(anchor).tick;

        int lastAccepted = anchor;
        bool reachedEnd = false;
        int candidate = anchor + 1;
        for (; candidate <= n; ++candidate) {
            const int tick = (candidate == n) ? endTick : map.at(candidate).tick;
            const double reference = times.at(candidate);
            const double drift =
                anchorMs + anchorMsPerTick * (tick - anchorTick) - reference;
            if (std::fabs(drift) > toleranceMs) {
                break;
            }
            if (candidate == n) {
                reachedEnd = true;
                break;
            }
            lastAccepted = candidate;
        }
        if (reachedEnd) {
            break; // one tempo carries the rest of the file
        }
        // The corridor broke at `candidate`, so the last point that still fit
        // becomes the next anchor. lastAccepted == anchor can only happen if
        // the drift CARRIED INTO this segment already exceeded the tolerance,
        // which the induction below rules out - the guard is there so a
        // pathological map cannot spin here forever.
        if (lastAccepted == anchor) {
            lastAccepted = anchor + 1;
        }
        // Drift at the new anchor = the value just verified to be within the
        // corridor, which is what keeps the induction (and the whole file's
        // drift bound) intact.
        anchorMs += anchorMsPerTick * (map.at(lastAccepted).tick - anchorTick);
        anchor = lastAccepted;
        keep.append(anchor);
        if (anchor >= n - 1) {
            break;
        }
    }
    return keep;
}

} // namespace

int TempoMapThinner::tempoEventCount(MidiFile *file) {
    if (!file || !file->channel(17)) {
        return 0;
    }
    int count = 0;
    QMultiMap<int, MidiEvent *> *events = file->channel(17)->eventMap();
    for (auto it = events->constBegin(); it != events->constEnd(); ++it) {
        if (dynamic_cast<TempoChangeEvent *>(it.value())) {
            ++count;
        }
    }
    return count;
}

TempoMapThinner::Result TempoMapThinner::thin(MidiFile *file, double toleranceMs,
                                              bool dryRun,
                                              const QString &actionLabel) {
    Result r;
    r.dryRun = dryRun;
    if (!file) {
        r.error = QStringLiteral("No file loaded.");
        return r;
    }
    MidiChannel *channel = file->channel(17);
    if (!channel) {
        r.error = QStringLiteral("This file has no tempo channel.");
        return r;
    }
    if (toleranceMs < 0.0) {
        toleranceMs = 0.0;
    }

    // --- The original map, in channel order -------------------------------
    QVector<TempoPoint> original;
    QMultiMap<int, MidiEvent *> *events = channel->eventMap();
    for (auto it = events->constBegin(); it != events->constEnd(); ++it) {
        auto *tempo = dynamic_cast<TempoChangeEvent *>(it.value());
        if (!tempo) {
            continue;
        }
        TempoPoint p;
        p.tick = tempo->midiTime();
        p.msPerTick = tempo->msPerTick();
        p.event = tempo;
        original.append(p);
    }
    r.before = original.size();
    r.kept = original.size();
    if (original.size() < 2) {
        // Nothing to thin - a single anchor IS the minimal map.
        r.ok = true;
        return r;
    }

    int endTick = file->endTick();
    if (endTick < original.last().tick) {
        endTick = original.last().tick;
    }
    const QVector<double> originalTimes = cumulativeMs(original, endTick);

    // --- One walk, and one walk only --------------------------------------
    // Repeating the walk on its own output would be unsound, not merely
    // wasteful: the second walk can only see the surviving ticks, so the
    // corridor it measures no longer runs through the dropped events - it
    // would happily spend the whole tolerance a SECOND time and double the
    // real drift. One walk against the original map is what makes
    // "|drift| <= toleranceMs at every original tick" a guarantee, and it is
    // its own fixed point in practice: an anchor only survives because the
    // corridor broke at the very next event after it, and on the thinned map
    // dropping it would stretch the previous tempo over a whole further
    // segment - a bigger deviation, not a smaller one.
    QVector<TempoPoint> current;
    {
        const QVector<int> keep =
            thinningPass(original, originalTimes, endTick, toleranceMs);
        current.reserve(keep.size());
        for (int idx : keep) {
            current.append(original.at(idx));
        }
    }

    r.kept = current.size();
    r.removed = original.size() - current.size();

    // --- Honest drift report, measured against the ORIGINAL timing ---------
    const QVector<double> finalTimes = cumulativeMs(current, endTick);
    {
        int seg = 0;
        for (int i = 0; i < original.size(); ++i) {
            const int tick = original.at(i).tick;
            while (seg + 1 < current.size() && current.at(seg + 1).tick <= tick) {
                ++seg;
            }
            const double thinnedMs =
                finalTimes.at(seg)
                + current.at(seg).msPerTick * (tick - current.at(seg).tick);
            const double drift = std::fabs(thinnedMs - originalTimes.at(i));
            if (drift > r.maxDriftMs) {
                r.maxDriftMs = drift;
            }
        }
        r.endDriftMs =
            finalTimes.at(current.size()) - originalTimes.at(original.size());
        if (std::fabs(r.endDriftMs) > r.maxDriftMs) {
            r.maxDriftMs = std::fabs(r.endDriftMs);
        }
    }

    if (dryRun || r.removed == 0) {
        r.ok = true;
        return r;
    }

    // --- Apply: ONE protocol action, ONE channel snapshot ------------------
    QVector<MidiEvent *> doomed;
    doomed.reserve(r.removed);
    {
        int seg = 0;
        for (int i = 0; i < original.size(); ++i) {
            if (seg < current.size() && current.at(seg).event == original.at(i).event) {
                ++seg;
                continue;
            }
            doomed.append(original.at(i).event);
        }
    }

    const QString label =
        actionLabel.isEmpty()
            ? QStringLiteral("Thin tempo map: %1 -> %2 events")
                  .arg(groupedNumber(r.before), groupedNumber(r.kept))
            : actionLabel;

    Protocol *protocol = file->protocol();
    protocol->startNewAction(label);
    // BULK-OP UNDO (same idiom as MidiFile::deleteMeasures): one snapshot of
    // channel 17, then removals with toProtocol=false, then a single commit.
    // A snapshot per event would clone the whole event map thousands of times.
    ProtocolEntry *snapshot = channel->copy();
    int actuallyRemoved = 0;
    for (MidiEvent *ev : doomed) {
        // removeEvent() refuses to drop the LAST tick-0 event of the tempo
        // channel. Index 0 of the map is always kept, so that guard never has
        // to fire here - and if it ever does, the count stays truthful.
        if (channel->removeEvent(ev, false)) {
            ++actuallyRemoved;
        }
    }
    channel->protocol(snapshot, channel);
    file->calcMaxTime();
    protocol->endAction();

    r.removed = actuallyRemoved;
    r.kept = r.before - actuallyRemoved;
    r.ok = true;
    return r;
}
