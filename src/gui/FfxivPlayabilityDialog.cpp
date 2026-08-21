#include "FfxivPlayabilityDialog.h"

#include "../MidiEvent/MidiEvent.h"

#include <QAction>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QSet>
#include <QTextBrowser>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

namespace {

// Child items carry their issue's index in the report so a click can look
// up the events without duplicating the list into the item. GROUP rows
// carry NO such data - that absence is the only marker needed to tell a
// group from a finding.
constexpr int kIssueIndexRole = Qt::UserRole;

QString groupTitle(FfxivPlayabilityIssue::Type t, int count) {
    switch (t) {
    case FfxivPlayabilityIssue::Type::Overlap:
        return FfxivPlayabilityDialog::tr("Simultaneous notes (%1)").arg(count);
    case FfxivPlayabilityIssue::Type::DuplicateNote:
        return FfxivPlayabilityDialog::tr("Stacked duplicates (%1)").arg(count);
    case FfxivPlayabilityIssue::Type::OutOfRange:
        return FfxivPlayabilityDialog::tr("Notes outside C3-C6 (%1)").arg(count);
    case FfxivPlayabilityIssue::Type::VoiceCeiling:
        return FfxivPlayabilityDialog::tr("Voice limit (%1)").arg(count);
    case FfxivPlayabilityIssue::Type::NoteRate:
        return FfxivPlayabilityDialog::tr("Note-rate hotspots (%1)").arg(count);
    case FfxivPlayabilityIssue::Type::ChannelSpread:
        return FfxivPlayabilityDialog::tr("Channel spread - editor playback only (%1)").arg(count);
    case FfxivPlayabilityIssue::Type::TrackName:
        return FfxivPlayabilityDialog::tr("Track names (%1)").arg(count);
    case FfxivPlayabilityIssue::Type::EmptyTrack:
        return FfxivPlayabilityDialog::tr("Empty instrument tracks (%1)").arg(count);
    case FfxivPlayabilityIssue::Type::TempoMap:
        return FfxivPlayabilityDialog::tr("Tempo map (%1)").arg(count);
    }
    return QString();
}

} // namespace

FfxivPlayabilityDialog::FfxivPlayabilityDialog(QWidget *parent)
    : QDialog(parent) {
    setWindowTitle(tr("FFXIV Playability Check"));
    setMinimumSize(640, 520);

    auto *layout = new QVBoxLayout(this);

    // --- Check row: every category is individually re-runnable ------------
    auto *checksRow = new QHBoxLayout();
    _checkSimultaneous = new QCheckBox(tr("Chords"), this);
    _checkSimultaneous->setToolTip(tr("Several different pitches starting on the same "
                                      "tick on one performer. In game a performer plays "
                                      "one note at a time, so these play as a fast "
                                      "arpeggio. Often intentional - switch this off to "
                                      "hide them and look for real defects."));
    _checkDuplicates = new QCheckBox(tr("Stacked notes"), this);
    _checkDuplicates->setToolTip(tr("The same pitch starting twice on the same tick on "
                                    "one performer. Almost always a MIDI defect (a "
                                    "doubled note) - the game plays it once and the "
                                    "duplicate is wasted."));
    _checkRange = new QCheckBox(tr("Range"), this);
    _checkRange->setToolTip(tr("Notes outside C3-C6"));
    _checkNames = new QCheckBox(tr("Track names"), this);
    _checkNames->setToolTip(tr("Names matching no FFXIV instrument (in game the "
                               "track name selects the instrument)"));
    _checkChannels = new QCheckBox(tr("Channels"), this);
    _checkChannels->setToolTip(tr("Non-guitar tracks with notes spread over several "
                                  "channels - affects editor playback only"));
    _checkEmpty = new QCheckBox(tr("Empty tracks"), this);
    _checkEmpty->setToolTip(tr("Instrument-named tracks without any notes"));
    _checkTempoMap = new QCheckBox(tr("Tempo map"), this);
    _checkTempoMap->setToolTip(tr("Tempo maps dense enough to weigh the file "
                                  "down - a tempo ramp exported by a DAW can "
                                  "carry one event every few ticks, and both "
                                  "the editor and the game pay for every one "
                                  "of them."));
    _checkVoiceLoad = new QCheckBox(tr("Voice limit"), this);
    _checkVoiceLoad->setToolTip(tr("Raw voice peak vs the 16-voice ceiling and "
                                   "notes/sec hotspots"));
    for (QCheckBox *cb : {_checkSimultaneous, _checkDuplicates, _checkRange,
                          _checkNames, _checkChannels, _checkEmpty,
                          _checkTempoMap, _checkVoiceLoad}) {
        cb->setChecked(true);
        checksRow->addWidget(cb);
    }
    checksRow->addStretch();
    auto *runButton = new QPushButton(tr("Run checks"), this);
    connect(runButton, &QPushButton::clicked,
            this, &FfxivPlayabilityDialog::runChecksRequested);
    checksRow->addWidget(runButton);
    layout->addLayout(checksRow);

    _summaryLabel = new QLabel(this);
    _summaryLabel->setWordWrap(true);
    layout->addWidget(_summaryLabel);

    _tree = new QTreeWidget(this);
    _tree->setColumnCount(1);
    _tree->setHeaderHidden(true);
    // Multi-select: on a real file "delete the stacked duplicates, keep the
    // chords" is the whole point, so a group row, a Shift range and a
    // Ctrl-picked handful must all be addressable.
    _tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    _tree->setToolTip(tr("Click a finding to select its notes; Ctrl/Shift for "
                         "several; click a group to take all of it. "
                         "Right-click for actions."));
    _tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(_tree, &QTreeWidget::itemClicked,
            this, &FfxivPlayabilityDialog::onItemClicked);
    connect(_tree, &QTreeWidget::itemDoubleClicked,
            this, &FfxivPlayabilityDialog::onItemDoubleClicked);
    connect(_tree, &QTreeWidget::itemSelectionChanged,
            this, &FfxivPlayabilityDialog::updateCollisionButton);
    connect(_tree, &QTreeWidget::customContextMenuRequested,
            this, &FfxivPlayabilityDialog::onTreeContextMenu);
    layout->addWidget(_tree, 1);

    // --- AI analysis pane (hidden until an answer arrives) -----------------
    _analysisView = new QTextBrowser(this);
    _analysisView->setOpenExternalLinks(false);
    _analysisView->setMaximumHeight(180);
    _analysisView->hide();
    layout->addWidget(_analysisView);

    auto *hint = new QLabel(
        tr("Click an issue to select its notes and move the cursor there; "
           "double-click to jump to the spot with only the affected track "
           "shown (the previous visibility returns when this window closes). "
           "The buttons below offer the matching repair for what was found. "
           "Selecting findings - Ctrl/Shift for several, or a whole group "
           "row - limits the delete button and the right-click menu to "
           "exactly those."),
        this);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    // --- Fix row: contextual repairs + AI + close --------------------------
    auto *buttons = new QHBoxLayout();
    _selectAllButton = new QPushButton(tr("Select all offending notes"), this);
    connect(_selectAllButton, &QPushButton::clicked,
            this, &FfxivPlayabilityDialog::onSelectAllClicked);
    buttons->addWidget(_selectAllButton);

    // The workbench knows exactly which notes are surplus - no mode dialog
    // (second QA: the button must DELETE, not just mark). Its label follows
    // the tree selection, see updateCollisionButton().
    _fixOverlapsButton = new QPushButton(tr("Delete colliding notes"), this);
    connect(_fixOverlapsButton, &QPushButton::clicked,
            this, &FfxivPlayabilityDialog::onDeleteCollisionsClicked);
    buttons->addWidget(_fixOverlapsButton);

    _fixChannelsButton = new QPushButton(tr("Channel Fixer"), this);
    // Only offered for channel-spread findings: the fixer works from the
    // FFXIV instrument NAMES it recognises, so it cannot repair a rejected
    // track name (CHANFIX-PROMISE-001) - say what it does do.
    _fixChannelsButton->setToolTip(tr("Runs Fix X|V Channels: re-syncs the "
                                      "channels and programs of the tracks it "
                                      "recognises as FFXIV instruments. A "
                                      "rejected track name is not renamed for "
                                      "you - rename the track to an FFXIV "
                                      "instrument name instead."));
    connect(_fixChannelsButton, &QPushButton::clicked, this, [this]() {
        emit fixRequested(QStringLiteral("fix_ffxiv_channels"));
    });
    buttons->addWidget(_fixChannelsButton);

    _fixVoiceButton = new QPushButton(tr("Auto-Fit..."), this);
    _fixVoiceButton->setToolTip(tr("Opens Auto-Fit Voice Load to thin the "
                                   "overloaded passages"));
    connect(_fixVoiceButton, &QPushButton::clicked, this, [this]() {
        emit fixRequested(QStringLiteral("auto_fit_voice_load"));
    });
    buttons->addWidget(_fixVoiceButton);

    // Phase 49: the repair for a dense tempo map. Opens the Thin Tempo Map
    // dialog rather than thinning straight away - the tolerance is a real
    // decision and the preview shows what the file would end up with. The
    // check re-runs by itself once the repair's protocol action finishes.
    _fixTempoMapButton = new QPushButton(tr("Thin Tempo Map..."), this);
    _fixTempoMapButton->setToolTip(tr("Reduces the tempo map to the events "
                                      "that carry the timing, leaving the "
                                      "music where it is"));
    connect(_fixTempoMapButton, &QPushButton::clicked, this, [this]() {
        emit fixRequested(QStringLiteral("thin_tempo_map"));
    });
    buttons->addWidget(_fixTempoMapButton);

    buttons->addStretch();

    _analyzeButton = new QPushButton(tr("Analyze with MidiPilot"), this);
    _analyzeButton->setToolTip(tr("Sends the findings to MidiPilot and shows its "
                                  "assessment here"));
    connect(_analyzeButton, &QPushButton::clicked,
            this, &FfxivPlayabilityDialog::onAnalyzeClicked);
    buttons->addWidget(_analyzeButton);

    auto *closeButton = new QPushButton(tr("Close"), this);
    closeButton->setDefault(true);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
    buttons->addWidget(closeButton);
    layout->addLayout(buttons);

    rebuildTree();
}

FfxivPlayabilityChecks FfxivPlayabilityDialog::selectedChecks() const {
    FfxivPlayabilityChecks c;
    c.simultaneousNotes = _checkSimultaneous->isChecked();
    c.stackedDuplicates = _checkDuplicates->isChecked();
    c.range = _checkRange->isChecked();
    c.trackNames = _checkNames->isChecked();
    c.channelSpread = _checkChannels->isChecked();
    c.emptyTracks = _checkEmpty->isChecked();
    c.tempoMap = _checkTempoMap->isChecked();
    return c;
}

bool FfxivPlayabilityDialog::voiceLoadEnabled() const {
    return _checkVoiceLoad->isChecked();
}

void FfxivPlayabilityDialog::refresh(const FfxivPlayabilityReport &report) {
    _report = report;
    rebuildTree();
}

void FfxivPlayabilityDialog::showAnalysis(const QString &text) {
    _awaitingAnalysis = false;
    _analyzeButton->setEnabled(_midiPilotAvailable);
    _analyzeButton->setText(tr("Analyze with MidiPilot"));
    _analysisView->setMarkdown(text);
    _analysisView->show();
}

void FfxivPlayabilityDialog::setMidiPilotAvailable(bool available) {
    _midiPilotAvailable = available;
    _analyzeButton->setVisible(available);
    _analyzeButton->setEnabled(available && !_awaitingAnalysis);
}

void FfxivPlayabilityDialog::rebuildTree() {
    _tree->clear();

    const int problemCount = _report.issues.size();
    if (_report.valid()) {
        _summaryLabel->setText(
            tr("<b>File is FFXIV-playable.</b> %1 note(s) on %2 track(s) "
               "checked with the selected checks - nothing found.")
                .arg(_report.checkedNotes)
                .arg(_report.checkedTracks));
    } else {
        _summaryLabel->setText(
            tr("<b>%1 finding(s)</b> in %2 note(s) on %3 track(s). "
               "A performer plays one note at a time: chords are rolled as "
               "a fast arpeggio, stacked notes waste the duplicate.")
                .arg(problemCount)
                .arg(_report.checkedNotes)
                .arg(_report.checkedTracks));
    }

    // Fixed presentation order: what breaks playback first.
    const FfxivPlayabilityIssue::Type order[] = {
        FfxivPlayabilityIssue::Type::Overlap,
        FfxivPlayabilityIssue::Type::DuplicateNote,
        FfxivPlayabilityIssue::Type::VoiceCeiling,
        FfxivPlayabilityIssue::Type::NoteRate,
        FfxivPlayabilityIssue::Type::TempoMap,
        FfxivPlayabilityIssue::Type::OutOfRange,
        FfxivPlayabilityIssue::Type::TrackName,
        FfxivPlayabilityIssue::Type::ChannelSpread,
        FfxivPlayabilityIssue::Type::EmptyTrack,
    };
    for (FfxivPlayabilityIssue::Type type : order) {
        const int count = _report.countOf(type);
        if (count == 0) continue;
        auto *group = new QTreeWidgetItem(_tree);
        group->setText(0, groupTitle(type, count));
        // Group rows ARE selectable - selecting one means "all of my
        // children", the headline gesture ("delete the 482 stacked
        // duplicates, keep the 531 chords").
        for (int idx = 0; idx < _report.issues.size(); ++idx) {
            const FfxivPlayabilityIssue &issue = _report.issues.at(idx);
            if (issue.type != type) continue;
            auto *item = new QTreeWidgetItem(group);
            // File-level findings (voice limit, rate hotspots) carry track -1.
            item->setText(0, issue.track >= 0
                                 ? tr("Track %1: %2").arg(issue.track)
                                                     .arg(issue.details)
                                 : issue.details);
            item->setData(0, kIssueIndexRole, idx);
        }
        group->setExpanded(count <= 20);
    }

    rebuildFixRow();
}

void FfxivPlayabilityDialog::rebuildFixRow() {
    // Offer only the repairs matching what was FOUND - a wall of disabled
    // buttons is a console, contextual tools are a workbench.
    const bool hasCollisions =
        _report.countOf(FfxivPlayabilityIssue::Type::Overlap) > 0
        || _report.countOf(FfxivPlayabilityIssue::Type::DuplicateNote) > 0;
    // CHANFIX-PROMISE-001: ChannelSpread ONLY. Track-name findings are exactly
    // the case the channel fixer cannot repair - it re-syncs channels and
    // programs of tracks it RECOGNISES as FFXIV instruments and cannot invent a
    // name for a track that has none, so on a plain GM file the button did
    // nothing but pop "No FFXIV instrument names detected." A ChannelSpread
    // finding, in contrast, is only ever raised for an instrument-named track
    // (see FfxivPlayabilityValidator), so the fixer always has something to do
    // - which is also why gating on FFXIVChannelFixer::analyzeFile() would only
    // repeat that fact at the cost of a second full-file pass per re-check.
    const bool hasChannelIssues =
        _report.countOf(FfxivPlayabilityIssue::Type::ChannelSpread) > 0;
    const bool hasVoiceIssues =
        _report.countOf(FfxivPlayabilityIssue::Type::VoiceCeiling) > 0
        || _report.countOf(FfxivPlayabilityIssue::Type::NoteRate) > 0;

    const bool hasTempoMapIssue =
        _report.countOf(FfxivPlayabilityIssue::Type::TempoMap) > 0;

    _fixOverlapsButton->setVisible(hasCollisions);
    _fixChannelsButton->setVisible(hasChannelIssues);
    _fixVoiceButton->setVisible(hasVoiceIssues);
    _fixTempoMapButton->setVisible(hasTempoMapIssue);
    _selectAllButton->setEnabled(!_report.offendingNotes().isEmpty());
    updateCollisionButton();
}

QList<int> FfxivPlayabilityDialog::selectedIssueIndices() const {
    QSet<int> picked;
    const QList<QTreeWidgetItem *> items = _tree->selectedItems();
    for (QTreeWidgetItem *item : items) {
        if (!item) continue;
        const QVariant idxVar = item->data(0, kIssueIndexRole);
        if (idxVar.isValid()) {
            picked.insert(idxVar.toInt());
            continue;
        }
        // No issue index = GROUP row: it stands for all of its children.
        for (int c = 0; c < item->childCount(); ++c) {
            const QVariant childVar =
                item->child(c)->data(0, kIssueIndexRole);
            if (childVar.isValid()) picked.insert(childVar.toInt());
        }
    }
    // Report order, so the delete order matches what the tree shows.
    QList<int> out;
    for (int i = 0; i < _report.issues.size(); ++i) {
        if (picked.contains(i)) out.append(i);
    }
    return out;
}

QList<MidiEvent *> FfxivPlayabilityDialog::eventsOf(
    const QList<int> &issueIndices) const {
    QSet<MidiEvent *> seen;
    QList<MidiEvent *> out;
    for (int idx : issueIndices) {
        if (idx < 0 || idx >= _report.issues.size()) continue;
        for (MidiEvent *ev : _report.issues.at(idx).events) {
            if (ev && !seen.contains(ev)) {
                seen.insert(ev);
                out.append(ev);
            }
        }
    }
    return out;
}

QList<MidiEvent *> FfxivPlayabilityDialog::collisionVictimsFor(
    const QList<int> &issueIndices) const {
    return ffxivCollisionSurplus(_report.issues, issueIndices);
}

void FfxivPlayabilityDialog::updateCollisionButton() {
    if (!_fixOverlapsButton) return;
    const QList<int> selection = selectedIssueIndices();
    if (selection.isEmpty()) {
        // Nothing picked = the original meaning: every collision at once.
        _fixOverlapsButton->setText(tr("Delete colliding notes"));
        _fixOverlapsButton->setToolTip(
            tr("Deletes the surplus notes of every collision "
               "in one undo step: duplicates keep one copy, "
               "simultaneous chords keep the highest note"));
        _fixOverlapsButton->setEnabled(true);
        return;
    }
    const int n = collisionVictimsFor(selection).size();
    if (n == 0) {
        // A selection that holds no collisions (range findings, names, ...)
        // has nothing to delete - say so instead of silently doing nothing.
        _fixOverlapsButton->setText(tr("Delete selected (nothing to delete)"));
        _fixOverlapsButton->setToolTip(
            tr("The selected findings are not collisions, so there are no "
               "surplus notes to remove"));
        _fixOverlapsButton->setEnabled(false);
        return;
    }
    _fixOverlapsButton->setText(tr("Delete selected (%1 notes)").arg(n));
    _fixOverlapsButton->setToolTip(
        tr("Deletes the surplus notes of the selected findings only, in one "
           "undo step: duplicates keep one copy, simultaneous chords keep "
           "the highest note"));
    _fixOverlapsButton->setEnabled(true);
}

void FfxivPlayabilityDialog::onDeleteCollisionsClicked() {
    QList<int> scope = selectedIssueIndices();
    if (scope.isEmpty()) {
        for (int i = 0; i < _report.issues.size(); ++i) scope.append(i);
    }
    const QList<MidiEvent *> victims = collisionVictimsFor(scope);
    if (!victims.isEmpty())
        emit deleteEventsRequested(victims);
    // No refresh here: MainWindow re-runs the checks on Protocol::
    // actionFinished, which calls refresh() and rebuilds the tree.
}

void FfxivPlayabilityDialog::onTreeContextMenu(const QPoint &pos) {
    // Standard behaviour: a right-click on something outside the current
    // selection makes that item the selection first.
    QTreeWidgetItem *item = _tree->itemAt(pos);
    if (item && !item->isSelected())
        _tree->setCurrentItem(item);

    const QList<int> selection = selectedIssueIndices();
    const QList<MidiEvent *> events = eventsOf(selection);
    const QList<MidiEvent *> victims = collisionVictimsFor(selection);

    QMenu menu(this);
    QAction *selectAction = menu.addAction(tr("Select these notes in editor"));
    selectAction->setEnabled(!events.isEmpty());
    QAction *deleteAction = menu.addAction(
        tr("Delete surplus notes of selected (%1)").arg(victims.size()));
    deleteAction->setEnabled(!victims.isEmpty());
    menu.addSeparator();
    QAction *expandAction = menu.addAction(tr("Expand all"));
    QAction *collapseAction = menu.addAction(tr("Collapse all"));

    QAction *chosen = menu.exec(_tree->viewport()->mapToGlobal(pos));
    if (!chosen) return;
    if (chosen == selectAction) {
        emit selectEventsRequested(events);
    } else if (chosen == deleteAction) {
        // Same path as the button - MainWindow wraps it in one undo step.
        emit deleteEventsRequested(victims);
    } else if (chosen == expandAction) {
        _tree->expandAll();
    } else if (chosen == collapseAction) {
        _tree->collapseAll();
    }
}

QString FfxivPlayabilityDialog::buildAnalysisPrompt() const {
    QStringList lines;
    lines << QStringLiteral(
        "Please assess this FFXIV playability report for the current file "
        "and give a short, prioritized verdict: what MUST be fixed before "
        "playing in game, what is cosmetic, and in which order you would "
        "fix it (mention the matching tools). Be concise.");
    lines << QStringLiteral("Report: %1 finding(s), %2 notes on %3 tracks checked.")
                 .arg(_report.issues.size())
                 .arg(_report.checkedNotes)
                 .arg(_report.checkedTracks);
    // Compact findings list, capped so the prompt stays small - the agent
    // can always run validate_ffxiv itself for the full picture.
    const int cap = 40;
    for (int i = 0; i < _report.issues.size() && i < cap; ++i) {
        const FfxivPlayabilityIssue &issue = _report.issues.at(i);
        // Same guard as the tree: file-level findings (voice peak, note-rate
        // hotspots) carry track -1 and were reaching the AI as "Track -1: ...".
        lines << (issue.track >= 0
                      ? QStringLiteral("- Track %1: %2").arg(issue.track)
                                                        .arg(issue.details)
                      : QStringLiteral("- File: %1").arg(issue.details));
    }
    if (_report.issues.size() > cap)
        lines << QStringLiteral("(%1 more findings omitted)")
                     .arg(_report.issues.size() - cap);
    return lines.join(QStringLiteral("\n"));
}

void FfxivPlayabilityDialog::onAnalyzeClicked() {
    if (!_midiPilotAvailable || _awaitingAnalysis) return;
    _awaitingAnalysis = true;
    _analyzeButton->setEnabled(false);
    _analyzeButton->setText(tr("Waiting for MidiPilot..."));
    _analysisView->setMarkdown(tr("*Asking MidiPilot...*"));
    _analysisView->show();
    emit analyzeRequested(buildAnalysisPrompt());
}

void FfxivPlayabilityDialog::onItemClicked(QTreeWidgetItem *item, int) {
    if (!item) return;
    const QVariant idxVar = item->data(0, kIssueIndexRole);
    if (!idxVar.isValid()) {
        // A GROUP row: select the union of everything under it, so "click
        // Stacked duplicates" shows all of them in the editor at once.
        QList<int> indices;
        for (int c = 0; c < item->childCount(); ++c) {
            const QVariant childVar =
                item->child(c)->data(0, kIssueIndexRole);
            if (childVar.isValid()) indices.append(childVar.toInt());
        }
        const QList<MidiEvent *> events = eventsOf(indices);
        if (!events.isEmpty())
            emit selectEventsRequested(events);
        return;
    }
    const int idx = idxVar.toInt();
    if (idx < 0 || idx >= _report.issues.size()) return;
    const FfxivPlayabilityIssue &issue = _report.issues.at(idx);
    if (!issue.events.isEmpty())
        emit selectEventsRequested(issue.events);
    emit jumpToTickRequested(issue.tick);
}

void FfxivPlayabilityDialog::onItemDoubleClicked(QTreeWidgetItem *item, int column) {
    // A double-click always arrives after the single-click handler already
    // selected the notes and set the cursor - add view scroll + track focus.
    onItemClicked(item, column);
    if (!item) return;
    const QVariant idxVar = item->data(0, kIssueIndexRole);
    if (!idxVar.isValid()) return;
    const int idx = idxVar.toInt();
    if (idx < 0 || idx >= _report.issues.size()) return;
    const FfxivPlayabilityIssue &issue = _report.issues.at(idx);
    if (issue.track >= 0)
        emit focusTrackRequested(issue.track);
    // The pitch line to reveal along with the tick: only for the per-note,
    // single-track finding types, whose events sit within a few lines of each
    // other (one tick group, or a single note). File-level findings (voice
    // peak, rate hotspots) and name/empty-track findings have no meaningful
    // line - -1 leaves the vertical scroll where it is.
    int line = -1;
    if (issue.track >= 0 && !issue.events.isEmpty()
        && (issue.type == FfxivPlayabilityIssue::Type::OutOfRange
            || issue.type == FfxivPlayabilityIssue::Type::Overlap
            || issue.type == FfxivPlayabilityIssue::Type::DuplicateNote)) {
        int lo = -1;
        int hi = -1;
        for (MidiEvent *ev : issue.events) {
            if (!ev) continue;
            const int l = ev->line();
            if (lo < 0 || l < lo) lo = l;
            if (hi < 0 || l > hi) hi = l;
        }
        if (lo >= 0) line = (lo + hi) / 2;
    }
    emit revealTickRequested(issue.tick, line);
}

void FfxivPlayabilityDialog::onSelectAllClicked() {
    const QList<MidiEvent *> events = _report.offendingNotes();
    if (!events.isEmpty())
        emit selectEventsRequested(events);
}
