/*
 * MidiEditor AI
 *
 * PrApply — apply a set of selected hunks from a PrBundle to the current
 * MidiFile, wrapped in a single Protocol action so the entire merge is
 * one undo step (per Plan §8 final paragraph).
 *
 * Best-effort by design:
 *   - `removed` events are looked up in the live file by identity tuple;
 *     missing ones are skipped with a warning, never a hard failure
 *     (the file may have diverged since the bundle was created).
 *   - `modified` entries are applied as remove-old + insert-new, which
 *     keeps NoteOn/Off pairing correct (the existing event-creation code
 *     handles the pair).
 *   - `added` events are deserialized via MidiEventSerializer.
 *
 * The result struct gives the caller enough info for a status message
 * ("Merged 12 added, 0 removed, 2 modified — 1 skipped").
 */

#ifndef PRAPPLY_H
#define PRAPPLY_H

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

class MidiEvent;
class MidiFile;

class PrApply {
public:
    struct Result {
        int addedCount = 0;
        int removedCount = 0;
        int modifiedCount = 0;
        int skippedCount = 0;
        QStringList warnings;
        bool success = false;
    };

    /**
     * \brief Apply the selected hunks to \a file.
     *
     * Wraps the apply in `Protocol::startNewAction("Merge PR from <author>: <message>")`
     * so the whole thing collapses to a single undo step. Returns the
     * counts and warnings for caller to display.
     *
     * \param silent  When true, suppress the auto-save + pending-merge
     *                marker side effects. Used by LAN Live Mode where
     *                hunks arrive every ~1 second and an auto-save per
     *                tick would be wrong (Plan §11.8). The local
     *                Protocol step + event-author tag for highlight
     *                still happen — only the file-save is skipped.
     *                Default false (legacy PR-merge behavior).
     */
    static Result apply(MidiFile *file,
                        const QList<QJsonObject> &hunks,
                        const QString &author,
                        const QString &message,
                        bool silent = false);

    /**
     * \brief Find a single live MidiEvent matching the identity of a
     *        serialized event (channel, track, tick + per-type extra).
     *
     * Returns nullptr when no match exists in \a file. Used both during
     * apply (to remove or modify existing events) and during pre-flight
     * conflict checking by PrReviewDialog (to estimate how many of a
     * hunk's `removed` / `modified.before` events still exist in the
     * current file). Identity matching mirrors MidiDiff::identityKey.
     */
    static MidiEvent *findMatchingEvent(MidiFile *file, const QJsonObject &serializedEvent);

    /**
     * \brief Apply hunks in their **inverted** form.
     *
     * Used by the AI-as-PR-creator review (Phase 9.3): when the user
     * rejects hunks that were already applied to the live file by the
     * agent, we revert them by:
     *   - Treating each `added` event as if it were `removed` → delete it
     *   - Treating each `removed` event as if it were `added` → insert it
     *   - Swapping `modified` before↔after
     *
     * Same Protocol-action wrapping as apply(). Returns a Result with
     * inverted counts (addedCount = events un-added; removedCount =
     * events restored; modifiedCount = events reverted).
     */
    static Result applyInverted(MidiFile *file,
                                const QList<QJsonObject> &hunks,
                                const QString &author,
                                const QString &message);
};

#endif // PRAPPLY_H
