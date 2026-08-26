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
 * reference. Between two consecutive REFERENCE tempo events both the reference
 * and the thinned function are straight lines, so their largest distance in
 * that interval always sits on one of its ends: checking the deviation at
 * every reference event tick (plus the file's end tick) bounds the timing error
 * EVERYWHERE, which is why note positions stay inside the tolerance and not
 * only the anchors do.
 *
 * The walk keeps the deviation that has already accumulated: a segment is
 * extended over the next event only while
 *     | driftAtAnchor + msPerTick(anchor) * (tick - anchorTick) - referenceMs |
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
 * What the corridor is measured against (THIN-RERUN-001)
 * ------------------------------------------------------
 * Measuring the drift against the map as it CURRENTLY stands makes a single
 * run honest and every further run a lie: the second run starts from a map
 * that has already used up the corridor, cannot see that, and happily spends
 * the whole tolerance again - three runs, three times the promised shift, and
 * every one of them reporting "<= tolerance". So the thinner remembers, per
 * open document, the tempo map it was FIRST asked about (ticks and ms per
 * tick - values, never event pointers, so nothing here can dangle) and always
 * measures against that remembered map:
 *
 *   - The corridor test, the reported maxDriftMs and endDriftMs are all
 *     relative to the remembered map, so they stay truthful across re-runs:
 *     "your music moves at most <tolerance> from where this document was when
 *     you opened it", no matter how often the tool is run.
 *   - Because the walk is deterministic and sees the same reference every
 *     time, a second run at the same tolerance provably reproduces the first
 *     run's anchors and removes nothing. Idempotence is now a property of the
 *     algorithm rather than an observation about one ramp.
 *   - Running again at a LARGER tolerance thins further, still bounded by the
 *     new tolerance against the remembered map. Running again at a SMALLER
 *     one cannot undo what is already spent; maxDriftMs then reports a value
 *     above the requested tolerance instead of pretending otherwise, and the
 *     dialog says so.
 *   - When the tempo map is edited elsewhere (a tempo event added, a BPM
 *     changed, a tempo conversion) the remembered map no longer describes this
 *     document - the current map is no longer a subsequence of it. The
 *     reference is then reset to the map as it is now, which is the only
 *     honest reference left, and alreadyDriftedMs comes back as 0.
 *   - The memory lives for the lifetime of the MidiFile object (a bounded
 *     handful of documents; the oldest is dropped). Saving and reopening the
 *     file therefore starts a fresh corridor - correct, because the saved file
 *     IS the original from that point on.
 */

#include <QString>
#include <QVector>

class MidiEvent;
class MidiFile;

class TempoMapThinner {
public:
    struct Result {
        bool ok = false;       ///< false = nothing was changed (see error)
        bool dryRun = false;   ///< the run only analysed
        int before = 0;        ///< tempo events before thinning
        int removed = 0;       ///< events removed (dryRun: events that would go)
        int kept = 0;          ///< events surviving
        double maxDriftMs = 0; ///< worst absolute ms drift of the RESULT against
                               ///< the remembered original map, measured at
                               ///< every original tempo event tick and at the
                               ///< end tick. Cumulative: it already contains
                               ///< whatever earlier runs spent.
        double endDriftMs = 0; ///< signed ms difference of the file's end time,
                               ///< also against the remembered original
        double alreadyDriftedMs = 0; ///< worst absolute ms drift the map carried
                               ///< BEFORE this run (0 on the first run and
                               ///< whenever the reference was just reset)
        QString error;         ///< user-facing reason when !ok
    };

    /** Default ms corridor - inaudible, and small enough that a whole file
     *  worth of thinning cannot move a note off its beat. */
    static constexpr double kDefaultToleranceMs = 2.0;

    /**
     * \brief Thin the file's tempo map (channel 17).
     * \param toleranceMs maximum absolute ms drift allowed at any kept anchor,
     *        measured against the remembered original map (see the file
     *        comment) - not against the map as it currently stands.
     * \param dryRun analyse only - the file is not touched, no protocol action.
     *        A dry run still establishes the remembered original when there is
     *        none yet, so the preview and the run that follows it agree.
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

    /**
     * \brief Called from inside the thinner's protocol action with the events
     *        it just removed, so a caller can fix up state that references
     *        them - the editor uses it to drop them from the document's
     *        Selection (THIN-SELECTION-001: EventTool re-inserts every selected
     *        event into its channel as soon as one is dragged, which would put
     *        the whole dropped tempo map back).
     *
     * A hook instead of a direct call keeps this class GUI-free (the midi core
     * and its test targets link without src/tool/Selection.cpp) and covers
     * every call site - menu, ruler context menu, playability workbench and the
     * AI/MCP tool - with a single registration. Default is none: headless and
     * test runs behave exactly as before. Not thread safe; register once during
     * start-up, from the thread that owns the documents.
     */
    using RemovedEventsHook = void (*)(MidiFile *file,
                                       const QVector<MidiEvent *> &removed);
    static void setRemovedEventsHook(RemovedEventsHook hook);

    /** Forget the remembered original map of a document that is being closed.
     *  Optional - the memory is capped and evicts by itself - but calling it
     *  from the document-close path keeps the cap for documents still open. */
    static void forgetFile(MidiFile *file);
};

#endif // TEMPOMAPTHINNER_H
