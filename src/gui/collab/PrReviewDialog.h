/*
 * MidiEditor AI
 *
 * PrReviewDialog — cherry-pick UI for an incoming PR.
 *
 * Receives a parsed PrBundle (from a smart-paste token, a bundle file
 * drop, or a manual import) plus the target MidiFile. Renders a header
 * with author / time / session-match status and a checkbox list of
 * hunks. The user can select a subset and click Apply to merge them.
 *
 * On Apply, calls PrApply::apply() which wraps the merge in a single
 * Protocol action so it's one undo step (per Plan §8 final paragraph).
 *
 * v1 deliberately omits "Show diff..." per-hunk modal and "Preview audio"
 * per Plan §10.6 — those are 9.1e+ polish items.
 */

#ifndef PRREVIEWDIALOG_H
#define PRREVIEWDIALOG_H

#include <QDialog>
#include <QList>

#include "../../collab/PrBundle.h"

class MidiFile;
class QCheckBox;
class QLabel;

class PrReviewDialog : public QDialog {
    Q_OBJECT

public:
    /**
     * \brief Operating mode of the review dialog.
     *
     * - PreApply  — default. Hunks are NOT yet in \a targetFile. On
     *               Accept, selected hunks are PrApply::apply()'d.
     *               Used for incoming PRs (Ctrl+V token, drag-drop bundle,
     *               File → Import PR). The "Apply selected" button is the
     *               accept action; Cancel does nothing.
     *
     * - ReviewApplied — Phase 9.3 (AI-as-PR-Creator). Hunks are ALREADY
     *               in \a targetFile (the AI agent applied them live).
     *               On Accept with subset, the rejected hunks are
     *               PrApply::applyInverted()'d (= reverted). On Cancel,
     *               ALL hunks are reverted (= revert the entire AI run).
     */
    enum class Mode {
        PreApply,
        ReviewApplied,
    };

    PrReviewDialog(const PrBundle &bundle, MidiFile *targetFile,
                   Mode mode = Mode::PreApply,
                   QWidget *parent = nullptr);

    /**
     * \brief Hunks the user kept (was-checked at apply time).
     *
     * Populated after the dialog exits with QDialog::Accepted. Used
     * by callers that need to forward the user's decision elsewhere
     * — e.g. the LAN returning-peer flow needs to know which hunks
     * to broadcast to the peer as the merge result.
     */
    QList<QJsonObject> selectedHunks() const { return _selectedHunks; }

    /** \brief Hunks the user explicitly unchecked at apply time. */
    QList<QJsonObject> rejectedHunks() const { return _rejectedHunks; }

private slots:
    void onSelectAll();
    void onSelectNone();
    void onInvert();
    void onApply();
    void onCancel();

private:
    void buildLayout();
    QString formatHunkLabel(const QJsonObject &hunk) const;
    bool sessionMatchesTarget() const;

    PrBundle _bundle;
    MidiFile *_file;
    Mode _mode;
    QList<QCheckBox *> _hunkChecks;
    QList<QJsonObject> _hunks;

    /** \brief User's accept/reject decision, captured in onApply.
     *  Non-empty after a successful Accept; remain empty after Cancel. */
    QList<QJsonObject> _selectedHunks;
    QList<QJsonObject> _rejectedHunks;
};

#endif // PRREVIEWDIALOG_H
