/*
 * MidiEditor AI
 *
 * MIDI-aware state diff: given two snapshots of serialized events
 * (parent and current), produce a list of hunks suitable for the
 * collaboration history sidecar and PR bundles.
 *
 * Operates purely on the JSON representation produced by
 * MidiEventSerializer — no Qt widget or MidiFile-class dependency
 * beyond QJson*. This makes unit testing trivial: tests construct
 * QJsonArrays by hand and assert on the output.
 *
 * Identity tuples per event type (matches Planning/09_COLLABORATION.md
 * §7.1):
 *   - note          : (channel, track, tick, note)
 *   - cc            : (channel, track, tick, control)
 *   - pitch_bend    : (channel, track, tick)
 *   - program       : (channel, track, tick)
 *
 * Two events with the same identity but different non-id payload
 * (e.g. velocity changed on the same NoteOn) are reported as
 * "modified". Otherwise: parent-only ⇒ removed, current-only ⇒ added.
 *
 * Hunks are grouped by (channel, track). Within a group, contiguous
 * runs of changed events are split where the tick gap exceeds
 * `gapTicksForNewHunk` (default = 16 × ticksPerQuarter, i.e. ~4
 * measures of 4/4). This keeps the cherry-pick UI readable on long
 * songs without producing one hunk per beat on small edits.
 */

#ifndef MIDIDIFF_H
#define MIDIDIFF_H

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

class MidiDiff {
public:
    /**
     * \brief Compute the diff between two event snapshots.
     *
     * \a parent and \a current are QJsonArrays of event objects in
     * the format produced by MidiEventSerializer::serialize().
     * \a ticksPerQuarter is the file's PPQ (used for measure-aware
     * hunk grouping; pass the source file's ticksPerQuarter).
     *
     * Returns a QJsonArray of hunk objects:
     *   { "scope": { "channel", "track", "tickStart", "tickEnd",
     *                "measureStart", "measureEnd" },
     *     "removed":  [...], "added": [...], "modified": [{before, after}] }
     *
     * The returned array is empty when the two snapshots are
     * identical at the identity-tuple + payload level.
     */
    static QJsonArray compute(const QJsonArray &parent,
                              const QJsonArray &current,
                              int ticksPerQuarter);

    /**
     * \brief True when \a a and \a b have identical observable state.
     *
     * Compares all fields except `id` (which is a per-call sequence
     * number from MidiEventSerializer and not part of identity).
     * Exposed for testing.
     */
    static bool eventsEqual(const QJsonObject &a, const QJsonObject &b);

    /**
     * \brief Build the identity tuple key for one event.
     *
     * Returns a stable string of the form "type|channel|track|tick|extra"
     * suitable for use as a map key. Returns an empty string if the
     * event type is unknown (those events are excluded from the diff).
     * Exposed for testing.
     */
    static QString identityKey(const QJsonObject &event);
};

#endif // MIDIDIFF_H
