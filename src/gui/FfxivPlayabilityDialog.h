/*
 * MidiEditor AI
 *
 * FfxivPlayabilityDialog — Phase 46
 *
 * The FFXIV playability WORKBENCH (grown from a plain report on first QA
 * feedback): shows problems AND offers the repairs.
 *
 *   - A check row on top: every check category is a checkbox (chords,
 *     stacked notes, range, track names, channel spread, empty tracks,
 *     voice load), so single aspects can be re-checked in isolation;
 *     "Run checks" re-runs. Chords and stacked notes are separate boxes on
 *     purpose - a chord is often an intentional decision, a stacked note
 *     almost always a defect, and hunting the latter must not mean
 *     scrolling past thousands of the former.
 *   - The findings tree, grouped by type. Clicking an issue selects its
 *     notes in the editor and moves the cursor there. The tree is
 *     MULTI-select (Ctrl/Shift) and group rows are selectable too, meaning
 *     "all of my children" - because the headline job on a real file is
 *     "delete the 482 stacked duplicates and keep the 531 chords".
 *   - A contextual fix row: only the tools matching the FOUND problems are
 *     offered - Delete Overlaps (scoped to the tree selection when there is
 *     one, otherwise every collision), the channel fixer, Auto-Fit Voice
 *     Load. The same two actions sit in the tree's right-click menu.
 *   - "Analyze with MidiPilot": submits the findings to the AI through the
 *     normal chat path and mirrors the answer into the dialog.
 *
 * Modeless (like the Auto-Fit dialog): the fix buttons hand work to tools
 * that need a usable editor - a modal dialog would block Auto-Fit's own
 * modeless window. Single instance via QPointer in MainWindow; closed on
 * tab switch; re-validated after every finished protocol action, so the
 * tree never holds stale note pointers and repairs show their effect live.
 *
 * The validator itself stays dependency-free; the voice-limit / note-rate
 * rows are synthesized HERE from an Auto-Fit dry run and the voice
 * analyzer (see MainWindow::runPlayabilityChecks).
 */

#ifndef FFXIVPLAYABILITYDIALOG_H_
#define FFXIVPLAYABILITYDIALOG_H_

#include <QDialog>

#include "../ai/FfxivPlayabilityValidator.h"

class QCheckBox;
class QLabel;
class QPushButton;
class QTextBrowser;
class QTreeWidget;
class QTreeWidgetItem;
class MidiEvent;

class FfxivPlayabilityDialog : public QDialog {
    Q_OBJECT

public:
    explicit FfxivPlayabilityDialog(QWidget *parent = nullptr);

    /** The currently selected check categories (voice load is dialog-side,
     *  reported via voiceLoadEnabled()). */
    FfxivPlayabilityChecks selectedChecks() const;
    bool voiceLoadEnabled() const;

    /** Replaces the report and rebuilds the tree. Called by MainWindow with
     *  a fresh validation whenever the file changed underneath the open
     *  dialog, and after every "Run checks". */
    void refresh(const FfxivPlayabilityReport &report);

    /** Shows the AI's answer in the analysis pane (MainWindow forwards
     *  MidiPilotWidget::assistantReplied here after an analysis request). */
    void showAnalysis(const QString &text);

    /** Whether this dialog is waiting for a MidiPilot answer it requested -
     *  MainWindow only forwards replies while this is true, so unrelated
     *  chat traffic never lands in the pane. */
    bool awaitingAnalysis() const { return _awaitingAnalysis; }

    /** Enables/disables the MidiPilot button (provider configured?). */
    void setMidiPilotAvailable(bool available);

signals:
    /** Re-run the checks with the current selection. */
    void runChecksRequested();

    /** Select these notes in the editor. */
    void selectEventsRequested(const QList<MidiEvent *> &events);

    /** Move the edit cursor to this tick. */
    void jumpToTickRequested(int tick);

    /** Double-click: additionally SCROLL the piano roll so the spot is on
     *  screen (single click only selects and moves the cursor, which may be
     *  outside the visible range). `line` is the pitch line to bring on screen
     *  as well - without it a reveal scrolls to the right bar and still leaves
     *  an out-of-range note above or below the visible pitch band. -1 means
     *  "leave the vertical scroll alone": findings that span several tracks or
     *  the whole file have no single meaningful line. */
    void revealTickRequested(int tick, int line);

    /** Double-click: focus mode - show ONLY the affected track so the
     *  finding is not buried under seven other tracks' notes. MainWindow
     *  hides the others via the silent (non-undoable) path and restores the
     *  previous visibility when this dialog closes. Not emitted for
     *  file-level findings (track -1). */
    void focusTrackRequested(int track);

    /** Run a repair tool. actionId is a MainWindow action-map id:
     *  "fix_ffxiv_channels", "auto_fit_voice_load", "thin_tempo_map". */
    void fixRequested(const QString &actionId);

    /** Delete exactly these events as ONE undo step ("Delete colliding
     *  notes"). Emitted with the computed surplus of the SELECTED
     *  collisions (every collision when nothing is selected): duplicates
     *  keep one copy, simultaneous chords keep the highest note (the melody
     *  rule Auto-Fit documents). Unlike the Tools-menu Delete Overlaps this
     *  needs no mode dialog - the workbench already knows exactly which
     *  notes are surplus. */
    void deleteEventsRequested(const QList<MidiEvent *> &events);

    /** Submit this prompt to MidiPilot (normal chat path, answer mirrored
     *  back via showAnalysis). */
    void analyzeRequested(const QString &prompt);

private slots:
    void onItemClicked(QTreeWidgetItem *item, int column);
    void onItemDoubleClicked(QTreeWidgetItem *item, int column);
    void onSelectAllClicked();
    void onAnalyzeClicked();

    /** Runs the collision repair: scoped to the tree selection when there
     *  is one, over every collision when there is not. */
    void onDeleteCollisionsClicked();

    /** Right-click menu on the tree (select / delete / expand / collapse). */
    void onTreeContextMenu(const QPoint &pos);

    /** Keeps the collision button's text and enabled state in step with the
     *  tree selection. */
    void updateCollisionButton();

private:
    void rebuildTree();
    void rebuildFixRow();
    QString buildAnalysisPrompt() const;

    /** The current tree selection as issue indices into _report.issues: a
     *  selected GROUP row contributes all of its children (group rows carry
     *  no issue index - that absence is what tells them apart), a selected
     *  finding contributes itself. Deduplicated, in report order; empty
     *  when nothing is selected. */
    QList<int> selectedIssueIndices() const;

    /** The union of these issues' offending notes, deduplicated. */
    QList<MidiEvent *> eventsOf(const QList<int> &issueIndices) const;

    /** The surplus notes of these issues (non-collision types skipped) -
     *  the rule itself lives in ffxivCollisionSurplus() next to the
     *  detection, so it is testable without a widget. */
    QList<MidiEvent *> collisionVictimsFor(const QList<int> &issueIndices) const;

    FfxivPlayabilityReport _report;

    QCheckBox *_checkSimultaneous = nullptr;
    QCheckBox *_checkDuplicates = nullptr;
    QCheckBox *_checkRange = nullptr;
    QCheckBox *_checkNames = nullptr;
    QCheckBox *_checkChannels = nullptr;
    QCheckBox *_checkEmpty = nullptr;
    QCheckBox *_checkTempoMap = nullptr;
    QCheckBox *_checkVoiceLoad = nullptr;

    QLabel *_summaryLabel = nullptr;
    QTreeWidget *_tree = nullptr;

    QPushButton *_selectAllButton = nullptr;
    QPushButton *_fixOverlapsButton = nullptr;
    QPushButton *_fixChannelsButton = nullptr;
    QPushButton *_fixVoiceButton = nullptr;
    QPushButton *_fixTempoMapButton = nullptr;
    QPushButton *_analyzeButton = nullptr;

    QTextBrowser *_analysisView = nullptr;
    bool _awaitingAnalysis = false;
    bool _midiPilotAvailable = false;
};

#endif // FFXIVPLAYABILITYDIALOG_H_
