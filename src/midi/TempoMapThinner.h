#ifndef TEMPOMAPTHINNER_H
#define TEMPOMAPTHINNER_H

/*
 * Phase 49 (v2.3): thin a dense tempo map (DAW-exported ramps: one tempo event
 * every few ticks) down to the events that matter for TIMING, not for the BPM
 * list. See Planning/02_ROADMAP.md "Phase 49" - keep an event only when
 * dropping it would push the accumulated ms drift at any later kept anchor
 * beyond the tolerance; a plain BPM-delta greedy accumulates drift and is not
 * good enough. The tick-0 anchor is always kept.
 */

#include <QString>

class MidiFile;

class TempoMapThinner {
public:
    struct Result {
        bool ok = false;       ///< false = nothing was changed (see error)
        int before = 0;        ///< tempo events before thinning
        int removed = 0;       ///< events removed (dryRun: events that would go)
        int kept = 0;          ///< events surviving
        double maxDriftMs = 0; ///< worst absolute ms drift introduced
        QString error;         ///< user-facing reason when !ok
    };

    /**
     * \brief Thin the file's tempo map (channel 17).
     * \param toleranceMs maximum absolute ms drift allowed at any kept anchor.
     * \param dryRun analyse only - the file is not touched, no protocol action.
     * Wraps the edit in ONE protocol action using the bulk snapshot idiom.
     */
    static Result thin(MidiFile *file, double toleranceMs = 2.0,
                       bool dryRun = false);
};

#endif // TEMPOMAPTHINNER_H
