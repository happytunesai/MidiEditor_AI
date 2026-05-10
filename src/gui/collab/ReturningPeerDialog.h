/*
 * MidiEditor AI
 *
 * ReturningPeerDialog — host-side prompt shown when a peer rejoins a
 * LAN Live Session with commits the host doesn't have (Plan §11.10b).
 *
 * Three actions, all expressed as one-click buttons per the
 * "convenience guarantees" table in the plan:
 *
 *   • Accept all      — merge every hunk in the proposed bundle and
 *                        broadcast the result.
 *   • Review…         — open the existing PrReviewDialog so the host
 *                        can cherry-pick a subset; on accept, the
 *                        rejected hunks are still tracked so the peer
 *                        un-applies them and ends up at the host's
 *                        chosen state.
 *   • Reject          — disconnect the peer with a reason.
 *
 * The dialog itself doesn't apply anything — it dispatches the
 * answer back to LanLiveSession via accept-/rejectReturningPeerMerge.
 */

#ifndef RETURNINGPEERDIALOG_H
#define RETURNINGPEERDIALOG_H

#include <QDialog>
#include <QJsonArray>

#include "../../collab/PrBundle.h"

class MidiFile;
class QLabel;
class QPushButton;

class ReturningPeerDialog : public QDialog {
    Q_OBJECT

public:
    /**
     * \param peerName        Display name of the rejoining peer.
     * \param peerToken       Opaque token identifying the peer in
     *                        LanLiveSession's pending-merge map.
     * \param proposedBundle  Synthesized bundle of the peer's
     *                        commits-since-fork.
     * \param targetFile      The host's MidiFile (passed through to
     *                        PrReviewDialog for cherry-pick).
     */
    ReturningPeerDialog(const QString &peerName,
                        const QString &peerToken,
                        const PrBundle &proposedBundle,
                        MidiFile *targetFile,
                        QWidget *parent = nullptr);

private slots:
    void onAcceptAll();
    void onReview();
    void onReject();

private:
    QString _peerName;
    QString _peerToken;
    PrBundle _bundle;
    MidiFile *_file;
};

#endif // RETURNINGPEERDIALOG_H
