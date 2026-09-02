#include "AutoFitVoiceLoadDialog.h"
#include "Appearance.h"

#include "../MidiEvent/MidiEvent.h"
#include "../ai/FfxivVoiceAnalyzer.h"
#include "../midi/MidiFile.h"
#include "../midi/MidiTrack.h"
#include "../protocol/Protocol.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QMap>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QToolButton>
#include <QTimer>

#include <climits>
#include <QVBoxLayout>
#include <QHBoxLayout>


AutoFitVoiceLoadDialog::AutoFitVoiceLoadDialog(MidiFile *file, int startTick,
                                               int endTick,
                                               const QList<MidiEvent *> &selectionScope,
                                               QWidget *parent)
    : QDialog(parent),
      _file(file),
      _startTick(startTick),
      _endTick(endTick),
      _ceilingSpin(nullptr),
      _chordCheck(nullptr),
      _chordLimitSpin(nullptr),
      _rateCheck(nullptr),
      _intensitySlider(nullptr),
      _intensityLabel(nullptr),
      _rateKeepSlider(nullptr),
      _rateKeepLabel(nullptr),
      _preferLoudestCheck(nullptr),
      _livePreviewCheck(nullptr),
      _summaryLabel(nullptr),
      _previewButton(nullptr),
      _applyButton(nullptr) {

    // Which tracks actually carry selected notes - with a selection scope
    // only THOSE tracks start checked, so the dialog mirrors what the user
    // picked in the editor (the pointers are live at open time).
    QSet<int> selTracks;
    for (MidiEvent *ev : selectionScope) {
        if (!ev) continue;
        _selectionScope.insert(reinterpret_cast<quintptr>(ev));
        if (ev->track()) selTracks.insert(ev->track()->number());
    }

    setWindowTitle(tr("Auto-Fit Voice Load"));
    // Modeless on purpose: the editor stays usable behind the dialog, so the
    // user can scroll/zoom the timeline and lane while tuning. The dry run
    // re-computes on every protocol action, so edits made meanwhile can never
    // leave stale victim pointers behind.
    setModal(false);
    setMinimumWidth(760);
    setWindowIcon(Appearance::adjustIconForDarkMode(":/run_environment/graphics/icon.png"));

    // Two columns: options + summary on the left, the track list with
    // per-track live statistics on the right.
    QHBoxLayout *columns = new QHBoxLayout(this);
    QVBoxLayout *mainLayout = new QVBoxLayout();
    columns->addLayout(mainLayout, 3);

    // The tool is generic; only the wording follows the FFXIV limiter.
    const bool ffxiv = FfxivVoiceAnalyzer::instance()->isEnabled();
    QString infoText;
    if (!_selectionScope.isEmpty()) {
        infoText = (ffxiv
                        ? tr("Thin ONLY the %1 selected note(s) so they fit the "
                             "FFXIV mixer limits - everything outside the "
                             "selection stays untouched. Voices count notes "
                             "that really sound at the same time.")
                        : tr("Thin ONLY the %1 selected note(s) - everything "
                             "outside the selection stays untouched. Voices "
                             "count notes that really sound at the same time."))
                       .arg(_selectionScope.size());
    } else if (_startTick >= 0 || _endTick >= 0) {
        infoText = ffxiv
            ? tr("Thin the selected range so it fits the FFXIV mixer limits. "
                 "Voices count notes that really sound at the same time.")
            : tr("Thin the selected range where too many notes sound at once "
                 "or passages get too dense. Voices count notes that really "
                 "sound at the same time.");
    } else {
        infoText = ffxiv
            ? tr("Thin the song so it fits the FFXIV mixer limits. "
                 "Voices count notes that really sound at the same time.")
            : tr("Thin the song where too many notes sound at once or "
                 "passages get too dense. Voices count notes that really "
                 "sound at the same time.");
    }
    QLabel *info = new QLabel(infoText, this);
    info->setWordWrap(true);
    mainLayout->addWidget(info);

    // --- Density desaturation (the main control) ---------------------------
    QGroupBox *rateBox = new QGroupBox(tr("Density desaturation (per track)"), this);
    QVBoxLayout *rateLayout = new QVBoxLayout(rateBox);

    _rateCheck = new QCheckBox(
        ffxiv ? tr("Thin passages that are too dense for one FFXIV performer")
              : tr("Thin passages where the note rate is too dense"),
        this);
    _rateCheck->setChecked(true);
    rateLayout->addWidget(_rateCheck);

    QHBoxLayout *sliderRow = new QHBoxLayout();
    sliderRow->addWidget(new QLabel(tr("mild"), this));
    _intensitySlider = new QSlider(Qt::Horizontal, this);
    _intensitySlider->setRange(0, 80);          // percent of the track to thin
    _intensitySlider->setValue(10);
    _intensitySlider->setTracking(true);        // live recompute while dragging
    sliderRow->addWidget(_intensitySlider, 1);
    sliderRow->addWidget(new QLabel(tr("aggressive"), this));
    rateLayout->addLayout(sliderRow);

    _intensityLabel = new QLabel(this);
    _intensityLabel->setStyleSheet("QLabel { color: gray; }");
    rateLayout->addWidget(_intensityLabel);

    QHBoxLayout *keepRow = new QHBoxLayout();
    keepRow->addWidget(new QLabel(tr("In dense runs skip:"), this));
    _rateKeepSlider = new QSlider(Qt::Horizontal, this);
    _rateKeepSlider->setRange(1, 5);      // skip N per group = keep 1 of N+1
    _rateKeepSlider->setValue(1);         // skip 1 = halve
    _rateKeepSlider->setTracking(true);
    _rateKeepSlider->setTickPosition(QSlider::TicksBelow);
    _rateKeepSlider->setTickInterval(1);
    keepRow->addWidget(_rateKeepSlider, 1);
    rateLayout->addLayout(keepRow);
    _rateKeepLabel = new QLabel(this);
    _rateKeepLabel->setStyleSheet("QLabel { color: gray; }");
    rateLayout->addWidget(_rateKeepLabel);

    QLabel *rateHint = new QLabel(
        tr("The higher voice of each group survives (in two-voice passages the "
           "upper voice carries the melody)."), this);
    rateHint->setWordWrap(true);
    rateHint->setStyleSheet("QLabel { color: gray; font-size: 10px; }");
    rateLayout->addWidget(rateHint);

    mainLayout->addWidget(rateBox);

    // --- Advanced options --------------------------------------------------
    QGroupBox *optionsBox = new QGroupBox(tr("Advanced"), this);
    QVBoxLayout *optionsLayout = new QVBoxLayout(optionsBox);

    QHBoxLayout *ceilingRow = new QHBoxLayout();
    ceilingRow->addWidget(new QLabel(tr("Voice ceiling:"), this));
    _ceilingSpin = new QSpinBox(this);
    _ceilingSpin->setRange(8, 32);
    _ceilingSpin->setValue(16);
    _ceilingSpin->setToolTip(
        ffxiv ? tr("Maximum simultaneously sounding notes. 16 is the "
                   "documented in-game limit.")
              : tr("Maximum simultaneously sounding notes."));
    ceilingRow->addWidget(_ceilingSpin);
    ceilingRow->addWidget(new QLabel(tr("simultaneous voices"), this));
    ceilingRow->addStretch();
    optionsLayout->addLayout(ceilingRow);

    QHBoxLayout *chordRow = new QHBoxLayout();
    _chordCheck = new QCheckBox(tr("Limit chords to"), this);
    _chordCheck->setChecked(true);
    _chordLimitSpin = new QSpinBox(this);
    _chordLimitSpin->setRange(2, 8);
    _chordLimitSpin->setValue(3);
    QLabel *chordSuffix = new QLabel(tr("voices per channel (only in overflows)"), this);
    chordRow->addWidget(_chordCheck);
    chordRow->addWidget(_chordLimitSpin);
    chordRow->addWidget(chordSuffix);
    chordRow->addStretch();
    optionsLayout->addLayout(chordRow);

    _preferLoudestCheck = new QCheckBox(
        tr("Prefer louder notes (accents) - for files WITHOUT normalized velocity"), this);
    _preferLoudestCheck->setChecked(false);
    _preferLoudestCheck->setToolTip(
        tr("After Fix X|V Channels all velocities are 100, so loudness carries no "
           "information and the higher voice wins. Enable this only for raw files "
           "where accents should survive the thinning."));
    optionsLayout->addWidget(_preferLoudestCheck);

    _livePreviewCheck = new QCheckBox(
        tr("Live preview: highlight affected notes in the editor"), this);
    _livePreviewCheck->setChecked(true);
    optionsLayout->addWidget(_livePreviewCheck);

    mainLayout->addWidget(optionsBox);

    // --- Dry-run summary ---------------------------------------------------
    _summaryLabel = new QLabel(this);
    _summaryLabel->setWordWrap(true);
    _summaryLabel->setStyleSheet("QLabel { background-color: #404040; color: white; padding: 8px; }");
    mainLayout->addWidget(_summaryLabel);

    // --- Buttons -----------------------------------------------------------
    QDialogButtonBox *buttons = new QDialogButtonBox(this);
    _previewButton = buttons->addButton(tr("Preview as selection"),
                                        QDialogButtonBox::ActionRole);
    // Only useful while live preview is OFF: with it on, refreshPreview()
    // already emits the same selection after every change, so the button
    // would look active while doing nothing (see refreshPreview()).
    _previewButton->setToolTip(
        tr("Highlights the notes that would be removed. Only needed while "
           "\"Live preview\" is off - with it on the highlight already "
           "follows every change."));
    // The analysis is useful beyond deleting: closing with the notes SELECTED
    // turns this dialog into a rule-based selection tool, so any editor
    // operation can act on what it found (transpose every 2nd note of a dense
    // run an octave up and a tremolo becomes an arpeggio).
    _selectButton = buttons->addButton(tr("Select in editor"),
                                       QDialogButtonBox::ActionRole);
    _selectButton->setToolTip(
        tr("Closes the dialog and leaves these notes selected instead of "
           "removing them - then use any editor operation on them "
           "(transpose, move to another track, change velocity)."));
    _applyButton = buttons->addButton(tr("Apply"), QDialogButtonBox::AcceptRole);
    QPushButton *cancel = buttons->addButton(QDialogButtonBox::Cancel);
    Q_UNUSED(cancel);
    connect(_previewButton, &QPushButton::clicked,
            this, &AutoFitVoiceLoadDialog::onPreviewClicked);
    connect(_selectButton, &QPushButton::clicked,
            this, &AutoFitVoiceLoadDialog::onSelectClicked);
    connect(_applyButton, &QPushButton::clicked,
            this, &AutoFitVoiceLoadDialog::onApplyClicked);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttons);

    // --- Right column: track list with checkboxes + live stats -------------
    QGroupBox *tracksBox = new QGroupBox(tr("Apply to tracks"), this);
    QVBoxLayout *tracksLayout = new QVBoxLayout(tracksBox);

    QHBoxLayout *quickRow = new QHBoxLayout();
    QPushButton *allButton = new QPushButton(tr("All"), this);
    QPushButton *visibleButton = new QPushButton(tr("Visible"), this);
    QPushButton *noneButton = new QPushButton(tr("None"), this);
    quickRow->addWidget(allButton);
    quickRow->addWidget(visibleButton);
    quickRow->addWidget(noneButton);
    quickRow->addStretch();
    tracksLayout->addLayout(quickRow);

    QScrollArea *trackScroll = new QScrollArea(this);
    trackScroll->setWidgetResizable(true);
    trackScroll->setFrameShape(QFrame::NoFrame);
    QWidget *trackListWidget = new QWidget(trackScroll);
    QVBoxLayout *trackListLayout = new QVBoxLayout(trackListWidget);
    trackListLayout->setSpacing(2);

    if (_file && _file->tracks()) {
        for (MidiTrack *t : *_file->tracks()) {
            if (!t) continue;
            const QString name = t->name().isEmpty() ? tr("(unnamed)") : t->name();
            QHBoxLayout *row = new QHBoxLayout();
            QCheckBox *cb = new QCheckBox(
                tr("Track %1: %2").arg(t->number()).arg(name), trackListWidget);
            // No signals are wired yet, so this cannot fire a premature
            // refreshPreview; with a selection scope only the tracks whose
            // notes are selected start checked.
            cb->setChecked(_selectionScope.isEmpty()
                           || selTracks.contains(t->number()));
            row->addWidget(cb, 1);
            // Eye = the Tracks panel's visibility toggle, reachable while the
            // modal dialog is open. Hiding tracks switches the voice lane
            // into its track-share display, so the contribution of the
            // remaining tracks is visible behind the dialog.
            QToolButton *eye = new QToolButton(trackListWidget);
            eye->setCheckable(true);
            // hiddenByUser(), not hidden(): this eye WRITES the document flag
            // (setHidden below), so it must show that flag - a workbench focus
            // overlay must not make it read "hidden" (FOCUS-DEADEYE-001).
            eye->setChecked(!t->hiddenByUser());
            eye->setIcon(Appearance::adjustIconForDarkMode(
                ":/run_environment/graphics/trackwidget/visible.png"));
            eye->setToolTip(tr("Show/hide this track in the editor (undoable)"));
            row->addWidget(eye);
            trackListLayout->addLayout(row);
            QLabel *stat = new QLabel(trackListWidget);
            stat->setStyleSheet("QLabel { color: gray; font-size: 10px; margin-left: 20px; }");
            trackListLayout->addWidget(stat);
            _trackChecks.append(cb);
            _trackEyes.append(eye);
            _trackStatLabels.append(stat);
            _trackNumbers.append(t->number());
            _tracks.append(t);
            connect(cb, &QCheckBox::toggled, this, [this]() {
                // If the checked tracks share one stored threshold, show it
                // on the slider (without treating that as a user edit).
                int shared = INT_MIN;
                bool same = true;
                for (int i = 0; i < _trackChecks.size(); ++i) {
                    if (!_trackChecks[i]->isChecked()) continue;
                    const int v = _trackPercents.value(_trackNumbers[i]);
                    if (shared == INT_MIN) shared = v;
                    else if (v != shared) { same = false; break; }
                }
                if (same && shared != INT_MIN) {
                    _sliderGuard = true;
                    _intensitySlider->setValue(shared);
                    _sliderGuard = false;
                }
                refreshPreview();
            });
            connect(eye, &QToolButton::toggled, this, [this, t](bool visible) {
                _file->protocol()->startNewAction(
                    visible ? tr("Show track") : tr("Hide track"));
                t->setHidden(!visible);
                _file->protocol()->endAction();
            });
        }
    }
    trackListLayout->addStretch();
    trackScroll->setWidget(trackListWidget);
    tracksLayout->addWidget(trackScroll, 1);
    columns->addWidget(tracksBox, 2);

    connect(allButton, &QPushButton::clicked, this, [this]() {
        for (QCheckBox *cb : _trackChecks) {
            const QSignalBlocker b(cb);
            cb->setChecked(true);
        }
        refreshPreview();
    });
    connect(noneButton, &QPushButton::clicked, this, [this]() {
        for (QCheckBox *cb : _trackChecks) {
            const QSignalBlocker b(cb);
            cb->setChecked(false);
        }
        refreshPreview();
    });
    connect(visibleButton, &QPushButton::clicked, this, [this]() {
        // Reads the LIVE visibility - the eyes can change it mid-dialog.
        // Same document flag the eyes show (FOCUS-DEADEYE-001).
        for (int i = 0; i < _trackChecks.size(); ++i) {
            const QSignalBlocker b(_trackChecks[i]);
            _trackChecks[i]->setChecked(_tracks[i] && !_tracks[i]->hiddenByUser());
        }
        refreshPreview();
    });

    // Drag debounce: with tracking on, ONE slider drag fires dozens of
    // valueChanged events and every refreshPreview() is a full-file dry run
    // plus - with live preview on - a second full voice analysis and a
    // piano-roll repaint, all synchronous on the GUI thread. While the handle
    // is held down those are coalesced into one refresh per 100 ms (the same
    // guard TempoConversionDialog uses); a released or programmatic value
    // change still refreshes immediately.
    _previewDebounce = new QTimer(this);
    _previewDebounce->setSingleShot(true);
    _previewDebounce->setInterval(100);
    connect(_previewDebounce, &QTimer::timeout,
            this, &AutoFitVoiceLoadDialog::refreshPreview);

    connect(_intensitySlider, &QSlider::valueChanged, this, [this](int value) {
        if (!_sliderGuard) {
            // The slider edits the CHECKED tracks; unchecked tracks keep
            // their individually stored thresholds.
            for (int i = 0; i < _trackChecks.size(); ++i) {
                if (_trackChecks[i]->isChecked())
                    _trackPercents[_trackNumbers[i]] = value;
            }
        }
        // The stored percents must follow every intermediate value (Apply
        // reads them); only the expensive preview is coalesced.
        schedulePreview(_intensitySlider->isSliderDown());
    });
    connect(_intensitySlider, &QSlider::sliderReleased, this, [this]() {
        schedulePreview(false); // final value, no 100 ms wait
    });
    connect(_ceilingSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &AutoFitVoiceLoadDialog::refreshPreview);
    connect(_chordCheck, &QCheckBox::toggled, _chordLimitSpin, &QSpinBox::setEnabled);
    connect(_chordCheck, &QCheckBox::toggled,
            this, &AutoFitVoiceLoadDialog::refreshPreview);
    connect(_chordLimitSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &AutoFitVoiceLoadDialog::refreshPreview);
    connect(_rateCheck, &QCheckBox::toggled, _intensitySlider, &QSlider::setEnabled);
    connect(_rateCheck, &QCheckBox::toggled, _rateKeepSlider, &QSlider::setEnabled);
    connect(_rateCheck, &QCheckBox::toggled,
            this, &AutoFitVoiceLoadDialog::refreshPreview);
    connect(_rateKeepSlider, &QSlider::valueChanged, this, [this]() {
        schedulePreview(_rateKeepSlider->isSliderDown());
    });
    connect(_rateKeepSlider, &QSlider::sliderReleased, this, [this]() {
        schedulePreview(false);
    });
    connect(_preferLoudestCheck, &QCheckBox::toggled,
            this, &AutoFitVoiceLoadDialog::refreshPreview);
    connect(_livePreviewCheck, &QCheckBox::toggled,
            this, &AutoFitVoiceLoadDialog::refreshPreview);

    // Seed every track with the default target percentage.
    for (int number : _trackNumbers)
        _trackPercents.insert(number, _intensitySlider->value());

    // Modeless safety: any edit in the editor (or an undo) invalidates the
    // dry-run's event pointers - recompute on every finished action.
    if (_file && _file->protocol()) {
        connect(_file->protocol(), SIGNAL(actionFinished()),
                this, SLOT(refreshPreview()));
    }

    setLayout(columns);
    refreshPreview();
}

AutoFitOptions AutoFitVoiceLoadDialog::currentOptions(bool dryRun) const {
    AutoFitOptions opts;
    opts.startTick = _startTick;
    opts.endTick = _endTick;
    opts.selectionScope = _selectionScope;
    // Checked tracks form the filter. All checked -> empty filter (= all);
    // none checked -> impossible sentinel so nothing is removable.
    QSet<int> checked;
    for (int i = 0; i < _trackChecks.size(); ++i) {
        if (_trackChecks[i]->isChecked()) checked.insert(_trackNumbers[i]);
    }
    if (checked.isEmpty()) {
        opts.trackFilter.insert(-1);
    } else if (checked.size() < _trackChecks.size()) {
        opts.trackFilter = checked;
    } // all checked -> leave empty (= all tracks)
    opts.targetCeiling = _ceilingSpin->value();
    opts.chordLimit = _chordCheck->isChecked() ? _chordLimitSpin->value() : 0;
    opts.desaturateRates = _rateCheck->isChecked();
    opts.ratePercent = _intensitySlider->value();
    opts.ratePercentPerTrack = _trackPercents;
    opts.rateKeepOneOf = _rateKeepSlider->value() + 1;
    opts.preferLoudest = _preferLoudestCheck->isChecked();
    opts.dryRun = dryRun;
    return opts;
}

void AutoFitVoiceLoadDialog::schedulePreview(bool dragging) {
    if (dragging) {
        _previewDebounce->start(); // restarts on every step of the drag
        return;
    }
    _previewDebounce->stop();
    refreshPreview();
}

void AutoFitVoiceLoadDialog::refreshPreview() {
    // Modeless safety: a track add/remove/move renumbers the live tracks and
    // would misattribute every row (labels, checkboxes, stored percents,
    // stats) - and Apply would thin the wrong track. The world this dialog
    // was built for is gone, so close instead of guessing. Pointer identity
    // only, nothing stale is dereferenced.
    if (!_file || !_file->tracks()
        || _file->tracks()->size() != _tracks.size()) {
        close();
        return;
    }
    for (int i = 0; i < _tracks.size(); ++i) {
        if (_file->tracks()->at(i) != _tracks[i]) {
            close();
            return;
        }
    }

    // The eyes WRITE the document flag, so they have to follow it: the dialog
    // is modeless, so an undo behind it (or the Tracks panel) can flip _hidden
    // without us - a stale eye then makes the next click a no-op that still
    // pushes an empty protocol step and wipes the redo stack. Signals blocked
    // so the resync does not itself open an action.
    for (int i = 0; i < _trackEyes.size() && i < _tracks.size(); ++i) {
        if (!_trackEyes[i] || !_tracks[i]) continue;
        const QSignalBlocker b(_trackEyes[i]);
        _trackEyes[i]->setChecked(!_tracks[i]->hiddenByUser());
    }

    _lastDry = AutoFitVoiceLoadService::apply(_file, currentOptions(true));
    if (!_lastDry.ok) {
        _summaryLabel->setText(tr("Analysis failed: %1").arg(_lastDry.error));
        _previewButton->setEnabled(false);
        _selectButton->setEnabled(false);
        _applyButton->setEnabled(false);
        return;
    }

    const double pct = (_lastDry.totalNotesInScope > 0)
        ? (100.0 * _lastDry.removedCount / _lastDry.totalNotesInScope) : 0.0;
    _intensityLabel->setText(
        tr("Target: thin about %1% of each selected track (densest passages "
           "first) - overall removes %2% of the notes")
            .arg(_intensitySlider->value())
            .arg(QString::number(pct, 'f', 1)));
    {
        const int keepN = _rateKeepSlider->value() + 1;
        _rateKeepLabel->setText(
            tr("Skips %1 of every %2 notes in a dense run (about %3% of the run)")
                .arg(keepN - 1)
                .arg(keepN)
                .arg(100 * (keepN - 1) / keepN));
    }

    QString text;
    text += tr("Peak %1/%2 sounding voices, %3 overflow range(s).")
                .arg(_lastDry.peakBefore)
                .arg(_ceilingSpin->value())
                .arg(_lastDry.overflowRangeCount);
    text += QStringLiteral("\n");
    if (_lastDry.removedCount == 0) {
        text += tr("Nothing to remove - the range already fits.");
    } else {
        text += tr("Will remove %1 of %2 notes (%3%): %4 duplicates, %5 chord limit, "
                   "%6 voice ceiling, %7 note rate.")
                    .arg(_lastDry.removedCount)
                    .arg(_lastDry.totalNotesInScope)
                    .arg(QString::number(pct, 'f', 1))
                    .arg(_lastDry.duplicateRemoved)
                    .arg(_lastDry.chordRemoved)
                    .arg(_lastDry.ceilingRemoved)
                    .arg(_lastDry.rateRemoved);
        text += QStringLiteral("\n");
        text += tr("Peak afterwards: %1. One Ctrl+Z restores everything.")
                    .arg(_lastDry.remainingPeak);
    }
    _summaryLabel->setText(text);

    // Per-track live statistics in the right column.
    QMap<int, const AutoFitTrackSummary *> byTrack;
    for (const AutoFitTrackSummary &s : _lastDry.trackSummaries)
        byTrack.insert(s.track, &s);
    for (int i = 0; i < _trackChecks.size(); ++i) {
        QLabel *stat = _trackStatLabels[i];
        const AutoFitTrackSummary *s = byTrack.value(_trackNumbers[i], nullptr);
        if (!_trackChecks[i]->isChecked()) {
            stat->setText(s ? tr("not applied (%1 notes)").arg(s->notes)
                            : tr("not applied"));
            stat->setStyleSheet("QLabel { color: gray; font-size: 10px; margin-left: 20px; }");
        } else if (!s || s->notes == 0) {
            stat->setText(tr("no notes in range"));
            stat->setStyleSheet("QLabel { color: gray; font-size: 10px; margin-left: 20px; }");
        } else if (s->removed == 0) {
            stat->setText(tr("no removals (%1 notes)").arg(s->notes));
            stat->setStyleSheet("QLabel { color: gray; font-size: 10px; margin-left: 20px; }");
        } else {
            const double tp = 100.0 * s->removed / s->notes;
            stat->setText(tr("target %1% - -%2 of %3 notes (%4%)")
                              .arg(_trackPercents.value(_trackNumbers[i]))
                              .arg(s->removed)
                              .arg(s->notes)
                              .arg(QString::number(tp, 'f', 1)));
            stat->setStyleSheet("QLabel { color: #f0883e; font-size: 10px; margin-left: 20px; }");
        }
    }
    // Greyed out while live preview keeps the highlight current: pressing it
    // then re-emits an identical selection, i.e. nothing visibly happens.
    _previewButton->setEnabled(_lastDry.removedCount > 0
                               && !_livePreviewCheck->isChecked());
    // Select stays available whenever the analysis found something, live
    // preview or not: it does not re-highlight, it HANDS OVER the set.
    _selectButton->setEnabled(_lastDry.removedCount > 0);
    _applyButton->setEnabled(_lastDry.removedCount > 0);

    // Live preview: mirror the current victim set into the editor selection
    // while the user drags the slider (empty set clears the highlight).
    if (_livePreviewCheck->isChecked()) {
        emit previewSelectionRequested(_lastDry.victims);
    }
}

void AutoFitVoiceLoadDialog::onPreviewClicked() {
    if (_lastDry.ok) {
        emit previewSelectionRequested(_lastDry.victims);
    }
}

void AutoFitVoiceLoadDialog::onSelectClicked() {
    if (!_lastDry.ok || _lastDry.victims.isEmpty()) {
        return;
    }
    // Emit once more so the selection is exactly the current dry run even if
    // live preview is off, then flag the caller to KEEP it (its cleanup exists
    // for the Apply case, where the victim pointers are freed).
    emit previewSelectionRequested(_lastDry.victims);
    _keepSelection = true;
    _resultSummary = tr("Auto-fit voice load: %1 note(s) selected - unchanged")
                         .arg(_lastDry.victims.size());
    accept();
}

void AutoFitVoiceLoadDialog::onApplyClicked() {
    const AutoFitResult r = AutoFitVoiceLoadService::apply(_file, currentOptions(false));
    if (!r.ok) {
        _summaryLabel->setText(tr("Apply failed: %1").arg(r.error));
        return;
    }
    _resultSummary = tr("Auto-fit voice load: removed %1 note(s), peak %2 -> %3 voices")
                         .arg(r.removedCount)
                         .arg(r.peakBefore)
                         .arg(r.remainingPeak);
    accept();
}
