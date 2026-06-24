/*
 * MidiEditor AI
 *
 * PasteSpecialDialog \u2014 Phase 34
 *
 * Modal dialog presented before a cross-instance / cross-file paste so
 * the user picks how source track + channel info should map onto the
 * target file. See Planning/02_ROADMAP.md \u00a7 34.1.
 */

#ifndef PASTE_SPECIAL_DIALOG_H_
#define PASTE_SPECIAL_DIALOG_H_

#include <QDialog>
#include <QList>
#include <QPair>
#include <QString>

class QRadioButton;
class QCheckBox;
class QLabel;

/**
 * \brief Three behaviours offered by the Paste Special flow.
 */
enum class PasteAssignment {
    NewTracksPerSource = 0,    ///< one new MidiTrack per source trackId
    PreserveSourceMapping = 1, ///< (default) reuse target tracks by name, else create
    CurrentEditTarget = 2      ///< Legacy: collapse onto NewNoteTool::editTrack/Channel
};

/**
 * \brief Per-paste options consumed by EventTool::pasteFromSharedClipboard().
 */
struct PasteSpecialOptions {
    PasteAssignment assignment = PasteAssignment::PreserveSourceMapping;
    bool applyTempoConversion = true;
    int targetCursorTick = 0;
    /// When true, note events that would land EXACTLY on an identical existing
    /// note (same channel + tick + pitch) in the target are skipped instead of
    /// inserted, so a repeated paste does not silently stack invisible duplicates.
    bool skipDuplicates = false;
};

/**
 * \brief Read-only summary of the current shared clipboard, displayed
 * at the top of the dialog so the user knows what they are about to paste.
 */
struct PasteClipboardSummary {
    int totalEvents = 0;
    int sourceTrackCount = 0;
    QList<QPair<int, QString>> sourceTracks; ///< (trackId, name)
    QList<int> distinctChannels;
    int approxDurationMs = 0;
    /// How many note events would land exactly on an identical existing note in
    /// the target (i.e. "already pasted here"). When > 0 the dialog shows a
    /// warning and pre-checks "skip already-present notes".
    int duplicateNoteCount = 0;
};

class PasteSpecialDialog : public QDialog {
    Q_OBJECT
public:
    explicit PasteSpecialDialog(const PasteClipboardSummary &summary,
                                PasteAssignment defaultAssignment,
                                QWidget *parent = nullptr);

    PasteAssignment chosenAssignment() const;
    bool dontAskAgainThisSession() const;
    bool makeThisTheNewDefault() const;
    /// Whether the user wants notes that already exist at the target skipped.
    bool skipAlreadyPresent() const;

private slots:
    void updateMakeDefaultEnabled();

private:
    QRadioButton *_radioNewTracks;
    QRadioButton *_radioPreserveMapping;
    QRadioButton *_radioCurrentTarget;
    QCheckBox *_dontAskAgain;
    QCheckBox *_makeDefault;
    QCheckBox *_skipDuplicates;
    QLabel *_summaryLabel;
    QLabel *_warningLabel;
};

#endif // PASTE_SPECIAL_DIALOG_H_
