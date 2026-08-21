#ifndef TEMPOMAPTHINNER_H
#define TEMPOMAPTHINNER_H

/*
 * Phase 49 (v2.3): thin a dense tempo map (DAW-exported ramps: one tempo event
 * every few ticks) down to the events that matter for TIMING, not for the BPM
 * list. See Planning/02_ROADMAP.md "Phase 49" - keep an event only when
 * dropping it would push the accumulated ms drift at any later kept anchor
 * beyond the tolerance; a plain BPM-delta greedy accumulates drift and is not
 * good enough. The tick-0 anchor is always kept.
 *
 * How the drift criterion works
 * -----------------------------
 * The tempo map defines a piecewise linear tick -> ms function, exactly the
 * one MidiFile::msOfTick() walks. Thinning replaces it with a coarser
 * piecewise linear function that must stay inside a ms corridor around the
 * original. Between two consecutive ORIGINAL tempo events both the original
 * and the thinned function are straight lines, so their largest distance in
 * that interval always sits on one of its ends: checking the deviation at
 * every original event tick (plus the file's end tick) bounds the timing error
 * EVERYWHERE, which is why note positions stay inside the tolerance and not
 * only the anchors do.
 *
 * The walk keeps the deviation that has already accumulated: a segment is
 * extended over the next event only while
 *     | driftAtAnchor + msPerTick(anchor) * (tick - anchorTick) - originalMs |
 * stays <= tolerance. A BPM-delta greedy ("drop it if the BPM barely changed")
 * has no such memory and lets thousands of tiny, same-signed roundings add up
 * to seconds by the end of the file.
 *
 * Why the surviving events keep their own BPM value
 * -------------------------------------------------
 * Merging a run into its time-weighted mean sounds better than it is here:
 * TempoChangeEvent stores whole BPM (see its constructor), so a computed mean
 * has to be rounded, and 1 BPM at 120 BPM is already 0.8% of the tempo - worse
 * than the run's own first value and, unlike it, a fresh error the original
 * file never had. The thinner therefore only ever REMOVES events; every
 * surviving tempo value is one the file already contained.
 *
 * The walk runs ONCE per call, against the map it was handed. Repeating it on
 * its own output inside one call would be unsound rather than merely wasteful:
 * a second walk can only see the surviving ticks, so its corridor no longer
 * passes through the events the first walk dropped, and it would spend the
 * whole tolerance a SECOND time - which is exactly how the drift the criterion
 * exists to prevent would come back in. One walk is therefore what makes
 * "|drift| <= toleranceMs at every original tick" a guarantee.
 *
 * Idempotence: running the tool again on an already thinned map removes
 * nothing. An anchor only survives because the corridor broke at the very next
 * event after it; on the thinned map, dropping that anchor would stretch the
 * previous tempo across a whole further segment instead of one event gap, so
 * the deviation it has to answer for is larger, not smaller.
 * test_tempo_map_thinner pins this on a ramp.
 */

#include <QString>

class MidiFile;

class TempoMapThinner {
public:
    struct Result {
        bool ok = false;       ///< false = nothing was changed (see error)
        bool dryRun = false;   ///< the run only analysed
        int before = 0;        ///< tempo events before thinning
        int removed = 0;       ///< events removed (dryRun: events that would go)
        int kept = 0;          ///< events surviving
        double maxDriftMs = 0; ///< worst absolute ms drift introduced, measured
                               ///< against the ORIGINAL timing at every
                               ///< original tempo event tick and at the end tick
        double endDriftMs = 0; ///< signed ms difference of the file's end time
        QString error;         ///< user-facing reason when !ok
    };

    /** Default ms corridor - inaudible, and small enough that a whole file
     *  worth of thinning cannot move a note off its beat. */
    static constexpr double kDefaultToleranceMs = 2.0;

    /**
     * \brief Thin the file's tempo map (channel 17).
     * \param toleranceMs maximum absolute ms drift allowed at any kept anchor.
     * \param dryRun analyse only - the file is not touched, no protocol action.
     * \param actionLabel optional Protocol-panel label. Empty = the service
     *        names itself ("Thin tempo map: 12,871 -> 47 events"); the AI/MCP
     *        tool passes a label carrying the actor attribution, exactly like
     *        TempoConversionOptions::actionLabel.
     *
     * Wraps the edit in ONE protocol action using the bulk snapshot idiom:
     * a single MidiChannel::copy() for channel 17, then removals with
     * toProtocol=false, then one commit - never a snapshot per event (that
     * costs ~1 MB per event on a dense channel).
     */
    static Result thin(MidiFile *file, double toleranceMs = kDefaultToleranceMs,
                       bool dryRun = false,
                       const QString &actionLabel = QString());

    /** Number of tempo events on channel 17 (0 when there is no file). */
    static int tempoEventCount(MidiFile *file);
};

#endif // TEMPOMAPTHINNER_H
