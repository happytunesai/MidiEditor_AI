/*
 * MidiEditor AI
 *
 * HistoryReconciliation — pure logic helpers for the returning-peer
 * merge flow (Plan §11.10b, Phase 9.5g).
 *
 * Given two collab-history chains (local + remote) that share some
 * common ancestor, this module:
 *
 *   - finds the merge base (most recent common commit hash);
 *   - slices each side's commits-since-fork from its history[];
 *   - synthesizes a PrBundle from a slice so the existing
 *     PrReviewDialog can drive cherry-pick review without code changes.
 *
 * No Qt-widget dependencies; safe to unit-test against fixture sidecars.
 */

#ifndef HISTORYRECONCILIATION_H
#define HISTORYRECONCILIATION_H

#include <QJsonArray>
#include <QString>
#include <QStringList>

#include "PrBundle.h"

class HistoryReconciliation {
public:
    /**
     * \brief Most-recent common ancestor between two newest-first
     *        commit-hash chains.
     *
     * Both inputs are expected newest-first (head at index 0). Returns
     * the first hash from \a localTail that also appears in
     * \a remoteTail, or empty string if no overlap exists in the
     * supplied tails.
     *
     * O(L * R) in the size of the inputs; both are bounded by
     * kHistoryTailLimit so this is comfortably linear in practice.
     */
    static QString findMergeBase(const QStringList &localTail,
                                 const QStringList &remoteTail);

    /**
     * \brief Newest-first hash chain extracted from a sidecar
     *        history[] (which is oldest-first).
     *
     * Caps the returned list at \a maxEntries (default 50, matching
     * the §11.10b proposal). Includes the head and walks backwards.
     * Hashes are not validated — caller trusts the sidecar.
     */
    static QStringList tailHashes(const QJsonArray &history,
                                  int maxEntries = kHistoryTailLimit);

    /**
     * \brief Chronological (oldest-first) slice of \a history starting
     *        AFTER \a ancestorHash up to the latest entry.
     *
     * If \a ancestorHash is empty or not found, returns the full
     * history (treats as "no common ancestor").
     */
    static QJsonArray commitsSinceFork(const QJsonArray &history,
                                       const QString &ancestorHash);

    /**
     * \brief Aggregate the hunks across an oldest-first slice into a
     *        single PrBundle suitable for feeding to PrReviewDialog.
     *
     * The bundle's `parentHash` is set to \a ancestorHash, `author` to
     * the most recent commit's author in the slice, and `message`
     * summarizes the slice ("N commits since <hash>: <last msg>").
     * Hunks are concatenated in chronological order; downstream
     * deduplication is the dialog's job, not ours.
     *
     * Returns an invalid (PrBundle::isValid() == false) bundle if
     * \a slice is empty.
     */
    static PrBundle synthesizeBundle(const QJsonArray &slice,
                                     const QString &sessionId,
                                     const QString &ancestorHash);

    /**
     * \brief One of the four fork-relation outcomes between two
     *        history chains. Drives the §11.10b decision tree.
     */
    enum class Relation {
        /** Heads are identical; nothing to merge. */
        SameHead,
        /** Local's head is on the remote's chain — local can fast-forward. */
        LocalBehindRemote,
        /** Remote's head is on the local's chain — remote should fast-forward. */
        RemoteBehindLocal,
        /** Both diverged from a common ancestor — cherry-pick required. */
        Diverged,
        /** No common ancestor in the supplied tails — treat as unrelated. */
        Unrelated,
    };

    /**
     * \brief Compute the relation between two chains by combining
     *        \ref findMergeBase with head equality.
     */
    static Relation classify(const QString &localHead,
                             const QString &remoteHead,
                             const QStringList &localTail,
                             const QStringList &remoteTail);

    /** \brief Default and maximum tail length per §11.10b. */
    static constexpr int kHistoryTailLimit = 50;
};

#endif // HISTORYRECONCILIATION_H
