#include "TempoMapThinner.h"

#include "MidiChannel.h"
#include "MidiFile.h"
#include "../MidiEvent/MidiEvent.h"
#include "../MidiEvent/TempoChangeEvent.h"
#include "../protocol/Protocol.h"
#include "../protocol/ProtocolEntry.h"

#include <QList>
#include <QLocale>
#include <QPair>
#include <QVector>

#include <algorithm>
#include <cmath>

namespace {

/** Floating point slack on the corridor test (THIN-ZEROTOL-001).
 *
 *  The reference times are a running SUM of msPerTick * tickDistance terms, so
 *  a deviation that is arithmetically exactly zero still arrives as +-1e-12
 *  unless the BPM happens to divide the tick grid exactly (120 BPM at 192 tpq
 *  is 60000/23040 ms per tick - not a binary fraction). With a strict
 *  `> toleranceMs` test and toleranceMs == 0 that noise breaks the corridor at
 *  the second event, so a user asking for "do not move my music at all" gets
 *  no thinning at all - not even a run of IDENTICAL tempo events, which is
 *  provably free to collapse. 1e-9 ms is far above the accumulation noise and
 *  nine orders of magnitude below anything audible. */
constexpr double kDriftEpsilonMs = 1e-9;

/** How many documents keep a remembered original tempo map. Bounded so a long
 *  session with many opened and closed tabs cannot grow without limit; the
 *  least recently thinned document is dropped first (its next run simply
 *  starts a fresh corridor). */
constexpr int kMaxRememberedFiles = 8;

/** The tempo map as the algorithm needs it: tick, ms per tick, and the event
 *  that carries them. The order is the CHANNEL MAP's order, so events sharing
 *  a tick keep the sequence MidiFile::msOfTick() sees - a zero length segment
 *  contributes nothing, and the last one at that tick is the one in force. */
struct TempoPoint {
    int tick = 0;
    double msPerTick = 0;
    MidiEvent *event = nullptr;
};

/** The tempo map a document was FIRST thinned against, kept by VALUE (tick and
 *  ms per tick). Deliberately not MidiEvent pointers: a tempo conversion or a
 *  document edit can delete tempo events for good, and a remembered pointer
 *  would then dangle. Values can only ever go stale, and staleness is
 *  detectable (see matchAgainstReference()). */
struct Reference {
    QVector<int> ticks;
    QVector<double> msPerTick;
};

/** Most recently used first. Function-local static: no static initialisation
 *  order dependency, and the MidiFile keys are only ever COMPARED, never
 *  dereferenced, so a stale key from a closed document is harmless. */
QList<QPair<MidiFile *, Reference *>> &referenceCache() {
    static QList<QPair<MidiFile *, Reference *>> cache;
    return cache;
}

Reference *rememberedReference(MidiFile *file) {
    QList<QPair<MidiFile *, Reference *>> &cache = referenceCache();
    for (int i = 0; i < cache.size(); ++i) {
        if (cache.at(i).first == file) {
            if (i != 0) {
                cache.move(i, 0);
            }
            return cache.at(0).second;
        }
    }
    return nullptr;
}

Reference *rememberReference(MidiFile *file, const QVector<int> &ticks,
                             const QVector<double> &msPerTick) {
    Reference *ref = rememberedReference(file);
    if (!ref) {
        ref = new Reference;
        QList<QPair<MidiFile *, Reference *>> &cache = referenceCache();
        cache.prepend(QPair<MidiFile *, Reference *>(file, ref));
        while (cache.size() > kMaxRememberedFiles) {
            delete cache.last().second;
            cache.removeLast();
        }
    }
    ref->ticks = ticks;
    ref->msPerTick = msPerTick;
    return ref;
}

/** The thinner's hook for the events it removed. Null unless the GUI installs
 *  one (TempoMapThinner::setRemovedEventsHook). */
TempoMapThinner::RemovedEventsHook g_removedEventsHook = nullptr;

/** Thousands separators for the Protocol label: QLocale::c() omits the group
 *  separator by default, and "12871 -> 47 events" reads like a serial number. */
QString groupedNumber(int n) {
    QLocale locale = QLocale::c();
    locale.setNumberOptions(locale.numberOptions() & ~QLocale::OmitGroupSeparator);
    return locale.toString(n);
}

/** Cumulative ms at every point of the map, plus the ms at `endTick` appended
 *  as the last element - the same accumulation MidiFile::msOfTick() performs
 *  (each segment contributes msPerTick(previous) * tick distance). */
QVector<double> cumulativeMs(const QVector<int> &ticks,
                             const QVector<double> &msPerTick, int endTick) {
    QVector<double> times(ticks.size() + 1, 0.0);
    double t = 0.0;
    for (int i = 1; i < ticks.size(); ++i) {
        t += msPerTick.at(i - 1) * (ticks.at(i) - ticks.at(i - 1));
        times[i] = t;
    }
    if (!ticks.isEmpty()) {
        times[ticks.size()] = t + msPerTick.last() * (endTick - ticks.last());
    }
    return times;
}

/** Worst absolute ms distance between the piecewise linear map
 *  (`ticks`, `msPerTick`) and the reference timing `refTimes`, sampled at every
 *  reference tick and at `endTick`. Both functions are straight lines between
 *  consecutive reference ticks, so sampling there bounds the error everywhere.
 *  `endDrift`, when given, receives the SIGNED difference at `endTick`. */
double driftAgainstReference(const QVector<int> &ticks,
                             const QVector<double> &msPerTick,
                             const QVector<int> &refTicks,
                             const QVector<double> &refTimes, int endTick,
                             double *endDrift) {
    if (endDrift) {
        *endDrift = 0.0;
    }
    if (ticks.isEmpty() || refTicks.isEmpty()) {
        return 0.0;
    }
    const QVector<double> times = cumulativeMs(ticks, msPerTick, endTick);
    double worst = 0.0;
    int seg = 0;
    for (int i = 0; i < refTicks.size(); ++i) {
        const int tick = refTicks.at(i);
        while (seg + 1 < ticks.size() && ticks.at(seg + 1) <= tick) {
            ++seg;
        }
        const double ms =
            times.at(seg) + msPerTick.at(seg) * (tick - ticks.at(seg));
        worst = std::max(worst, std::fabs(ms - refTimes.at(i)));
    }
    const double end = times.at(ticks.size()) - refTimes.at(refTicks.size());
    if (endDrift) {
        *endDrift = end;
    }
    return std::max(worst, std::fabs(end));
}

/** Is the current map still a thinned version of the remembered reference?
 *
 *  It is exactly when its (tick, msPerTick) sequence appears IN ORDER inside
 *  the reference's - which is all that thinning can ever do to a map - and its
 *  first point is the reference's first point (the tick-0 anchor the walk
 *  always keeps). Anything else means the tempo map was edited elsewhere and
 *  the remembered map no longer describes this document.
 *
 *  On success `mapping` receives, for every current index, its index in the
 *  reference. Greedy matching is optimal for subsequence detection, and both
 *  sequences are in channel-map order. */
bool matchAgainstReference(const Reference &ref, const QVector<int> &ticks,
                           const QVector<double> &msPerTick,
                           QVector<int> &mapping) {
    mapping.fill(-1, ticks.size());
    int r = 0;
    for (int c = 0; c < ticks.size(); ++c) {
        while (r < ref.ticks.size()
               && !(ref.ticks.at(r) == ticks.at(c)
                    && ref.msPerTick.at(r) == msPerTick.at(c))) {
            ++r;
        }
        if (r >= ref.ticks.size()) {
            return false;
        }
        if (c == 0 && r != 0) {
            return false; // the head of the map changed - not a thinning
        }
        mapping[c] = r;
        ++r;
    }
    return true;
}

/** One drift-bounded walk over the REFERENCE map.
 *
 *  `available` marks the reference points that still exist in the document and
 *  may therefore be kept as anchors; index 0 is always available. The walk
 *  measures the corridor at EVERY reference point (including the ones an
 *  earlier run already dropped - that is what keeps the bound cumulative) but
 *  only ever anchors on an available one. Returns the surviving reference
 *  indices, always including index 0.
 *
 *  Index refTicks.size() is a SENTINEL standing for the file's end tick: it is
 *  never kept (no event lives there), it only makes the trailing segment
 *  subject to the same corridor, so the very last tempo event can be dropped
 *  too when the end time survives it. */
QVector<int> thinningPass(const QVector<int> &refTicks,
                          const QVector<double> &refMsPerTick,
                          const QVector<bool> &available,
                          const QVector<double> &times, int endTick,
                          double toleranceMs) {
    QVector<int> keep;
    if (refTicks.isEmpty()) {
        return keep;
    }
    const int n = refTicks.size();
    const double corridor = toleranceMs + kDriftEpsilonMs;
    keep.append(0);

    int anchor = 0;
    double anchorMs = 0.0; // thinned ms at the anchor tick

    while (anchor < n - 1) {
        const double anchorMsPerTick = refMsPerTick.at(anchor);
        const int anchorTick = refTicks.at(anchor);

        int lastAccepted = -1;
        bool reachedEnd = false;
        for (int candidate = anchor + 1; candidate <= n; ++candidate) {
            const int tick = (candidate == n) ? endTick : refTicks.at(candidate);
            const double reference = times.at(candidate);
            const double drift =
                anchorMs + anchorMsPerTick * (tick - anchorTick) - reference;
            if (std::fabs(drift) > corridor) {
                break;
            }
            if (candidate == n) {
                reachedEnd = true;
                break;
            }
            if (available.at(candidate)) {
                lastAccepted = candidate;
            }
        }
        if (reachedEnd) {
            break; // one tempo carries the rest of the file
        }
        if (lastAccepted < 0) {
            // The corridor broke before the next event we are ALLOWED to keep.
            // On a first run this cannot happen (every point is available and
            // the drift carried into the segment is inside the corridor by
            // induction). On a re-run at a TIGHTER tolerance than a previous
            // one it can: the events the tighter corridor would need are gone
            // already. Advance to the next available point anyway - refusing
            // to advance would spin here forever - and let the honest drift
            // report below show that the request could not be met.
            lastAccepted = anchor + 1;
            while (lastAccepted < n && !available.at(lastAccepted)) {
                ++lastAccepted;
            }
            if (lastAccepted >= n) {
                break;
            }
        }
        // Drift at the new anchor = the value just verified to be within the
        // corridor, which is what keeps the induction (and the whole file's
        // drift bound) intact.
        anchorMs += anchorMsPerTick * (refTicks.at(lastAccepted) - anchorTick);
        anchor = lastAccepted;
        keep.append(anchor);
    }
    return keep;
}

} // namespace

void TempoMapThinner::setRemovedEventsHook(RemovedEventsHook hook) {
    g_removedEventsHook = hook;
}

void TempoMapThinner::forgetFile(MidiFile *file) {
    QList<QPair<MidiFile *, Reference *>> &cache = referenceCache();
    for (int i = 0; i < cache.size(); ++i) {
        if (cache.at(i).first == file) {
            delete cache.at(i).second;
            cache.removeAt(i);
            return;
        }
    }
}

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

    // --- The map as it currently stands, in channel order ------------------
    QVector<TempoPoint> current;
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
        current.append(p);
    }
    r.before = current.size();
    r.kept = current.size();
    if (current.size() < 2) {
        // Nothing to thin - a single anchor IS the minimal map.
        r.ok = true;
        return r;
    }

    QVector<int> currentTicks(current.size());
    QVector<double> currentMsPerTick(current.size());
    for (int i = 0; i < current.size(); ++i) {
        currentTicks[i] = current.at(i).tick;
        currentMsPerTick[i] = current.at(i).msPerTick;
    }

    // --- The reference: the map this document was FIRST asked about --------
    // Measuring against the CURRENT map would let every re-run spend the whole
    // tolerance again (THIN-RERUN-001); measuring against the remembered one
    // keeps "your music moves at most <tolerance>" true however often the tool
    // is run. When the tempo map was edited elsewhere the remembered map stops
    // describing this document and is replaced - see matchAgainstReference().
    QVector<int> referenceIndexOf;
    Reference *reference = rememberedReference(file);
    bool haveHistory = reference
        && matchAgainstReference(*reference, currentTicks, currentMsPerTick,
                                 referenceIndexOf);
    if (!haveHistory) {
        reference = rememberReference(file, currentTicks, currentMsPerTick);
        referenceIndexOf.resize(current.size());
        for (int i = 0; i < current.size(); ++i) {
            referenceIndexOf[i] = i;
        }
    }

    const int n = reference->ticks.size();
    int endTick = file->endTick();
    if (endTick < reference->ticks.last()) {
        endTick = reference->ticks.last();
    }
    const QVector<double> referenceTimes =
        cumulativeMs(reference->ticks, reference->msPerTick, endTick);

    // --- One walk, and one walk only --------------------------------------
    // Repeating the walk on its own output inside one call would be unsound,
    // not merely wasteful: the second walk would re-anchor on its own result
    // and spend the corridor twice over. One walk against the reference is
    // what makes "|drift| <= toleranceMs at every original tick" a guarantee -
    // and because the reference does not move between calls, the walk is its
    // own fixed point across calls too: a second run at the same tolerance
    // sees the same reference, the same corridor and the same anchors, so it
    // reproduces the first run's keep set exactly and removes nothing.
    QVector<bool> available(n, false);
    for (int i = 0; i < referenceIndexOf.size(); ++i) {
        available[referenceIndexOf.at(i)] = true;
    }
    const QVector<int> keep = thinningPass(reference->ticks,
                                           reference->msPerTick, available,
                                           referenceTimes, endTick, toleranceMs);

    QVector<int> currentIndexOfReference(n, -1);
    for (int i = 0; i < referenceIndexOf.size(); ++i) {
        currentIndexOfReference[referenceIndexOf.at(i)] = i;
    }
    QVector<TempoPoint> survivors;
    survivors.reserve(keep.size());
    for (int idx : keep) {
        const int c = currentIndexOfReference.at(idx);
        if (c >= 0) {
            survivors.append(current.at(c));
        }
    }

    r.kept = survivors.size();
    r.removed = current.size() - survivors.size();

    // --- Honest drift report, measured against the REFERENCE timing --------
    // Cumulative by construction: it contains whatever earlier runs on this
    // document already spent, so a re-run cannot report a small number while
    // the file has quietly walked away from where it started.
    {
        QVector<int> keptTicks(survivors.size());
        QVector<double> keptMsPerTick(survivors.size());
        for (int i = 0; i < survivors.size(); ++i) {
            keptTicks[i] = survivors.at(i).tick;
            keptMsPerTick[i] = survivors.at(i).msPerTick;
        }
        r.maxDriftMs = driftAgainstReference(keptTicks, keptMsPerTick,
                                             reference->ticks, referenceTimes,
                                             endTick, &r.endDriftMs);
        r.alreadyDriftedMs =
            haveHistory ? driftAgainstReference(currentTicks, currentMsPerTick,
                                                reference->ticks,
                                                referenceTimes, endTick, nullptr)
                        : 0.0;
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
        for (int i = 0; i < current.size(); ++i) {
            if (seg < survivors.size()
                && survivors.at(seg).event == current.at(i).event) {
                ++seg;
                continue;
            }
            doomed.append(current.at(i).event);
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
    QVector<MidiEvent *> removedEvents;
    removedEvents.reserve(doomed.size());
    for (MidiEvent *ev : doomed) {
        // removeEvent() refuses to drop the LAST tick-0 event of the tempo
        // channel. Index 0 of the map is always kept, so that guard never has
        // to fire here - and if it ever does, the count stays truthful.
        if (channel->removeEvent(ev, false)) {
            removedEvents.append(ev);
        }
    }
    channel->protocol(snapshot, channel);
    file->calcMaxTime();
    // THIN-SELECTION-001: still INSIDE the action, so whatever the hook changes
    // (the editor drops the removed events from the document's Selection) is
    // part of the same single undo step as the removal itself.
    if (g_removedEventsHook && !removedEvents.isEmpty()) {
        g_removedEventsHook(file, removedEvents);
    }
    protocol->endAction();

    r.removed = removedEvents.size();
    r.kept = r.before - r.removed;
    r.ok = true;
    return r;
}
