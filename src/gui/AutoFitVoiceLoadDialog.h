/*
 * MidiEditor AI
 *
 * AutoFitVoiceLoadDialog — v2.1.0 feature #1 (roadmap Phase 35).
 *
 * Confirmation dialog for the Auto-Fit Voice Load action. Runs a DRY RUN of
 * AutoFitVoiceLoadService on open and after every option change; the
 * thinning-intensity slider recomputes live and (with live preview on)
 * highlights the would-be victims in the editor while dragging. Only the
 * explicit Apply button mutates the file (single undoable protocol step).
 */

#ifndef AUTOFITVOICELOADDIALOG_H
#define AUTOFITVOICELOADDIALOG_H

#include <QDialog>
#include <QList>
#include <QMap>
#include <QSet>

#include "../converter/AutoFitVoiceLoadService.h"

class MidiFile;
class MidiEvent;
class QLabel;
class QCheckBox;
class QSpinBox;
class QSlider;
class QComboBox;
class QPushButton;
class QTimer;

class AutoFitVoiceLoadDialog : public QDialog {
    Q_OBJECT

public:
    /// startTick/endTick scope the fit; pass -1/-1 for the whole file.
    /// A non-empty selectionScope narrows the material to exactly those
    /// notes (captured at open; compared by pointer, never dereferenced).
    AutoFitVoiceLoadDialog(MidiFile *file, int startTick, int endTick,
                           const QList<MidiEvent *> &selectionScope = {},
                           QWidget *parent = nullptr);

    /// Status-bar text describing what Apply did (valid after accept()).
    QString resultSummary() const { return _resultSummary; }

    /// True when the dialog closed via "Select in editor": the caller must
    /// KEEP the emitted selection instead of clearing it, so the user can
    /// run any editor operation on the notes the analysis found (e.g.
    /// transpose every 2nd note of a dense run an octave up to build an
    /// arpeggio). After a real Apply this stays false - those pointers are
    /// dangling and the selection must be dropped.
    bool keepSelectionOnClose() const { return _keepSelection; }

signals:
    /// Emitted with the current would-be victims (live preview + button).
    void previewSelectionRequested(const QList<MidiEvent *> &events);

public slots:
    /// Re-runs the dry run and re-emits the preview. Public so the opener
    /// can trigger it once AFTER wiring previewSelectionRequested - the
    /// constructor's own initial run fires before any connection exists.
    void refreshPreview();

private slots:
    void onPreviewClicked();
    void onApplyClicked();
    void onSelectClicked();

private:
    AutoFitOptions currentOptions(bool dryRun) const;

    /// Slider debounce: while a slider handle is held down the (full-file)
    /// preview is coalesced into one run per 100 ms; every other change - and
    /// the release of the handle - refreshes immediately.
    void schedulePreview(bool dragging);

    // Test-only hook, no production behaviour: tests/test_auto_fit_dialog.cpp
    // pins the option mapping (the ALL/SUBSET/NONE track filter, the
    // selection-scope pre-check, the per-track percents, the structure
    // guard) directly on currentOptions() and the widgets it reads.
    // Friendship keeps all of that private instead of widening the API.
    friend class TestAutoFitDialog;

    MidiFile *_file;
    int _startTick;
    int _endTick;
    QSet<quintptr> _selectionScope;

    /// Track list (right column): checkbox per track for multi-selection, an
    /// eye button mirroring the Tracks panel's visibility toggle (the modal
    /// dialog blocks the panel, and hiding tracks drives the lane's
    /// track-share display), and a live stats label. Parallel lists.
    QList<QCheckBox *> _trackChecks;
    QList<class QToolButton *> _trackEyes;
    QList<QLabel *> _trackStatLabels;
    QList<int> _trackNumbers;
    QList<class MidiTrack *> _tracks;

    /// Per-track thinning target (percent of the track's notes). The slider
    /// edits the CHECKED tracks; values survive check/uncheck cycles, so
    /// each track can be tuned individually and all settings apply together.
    QMap<int, int> _trackPercents;
    bool _sliderGuard = false; ///< true while the code moves the slider

    QSpinBox *_ceilingSpin;
    QCheckBox *_chordCheck;
    QSpinBox *_chordLimitSpin;
    QCheckBox *_rateCheck;
    QSlider *_intensitySlider;
    QLabel *_intensityLabel;
    QSlider *_rateKeepSlider;
    QLabel *_rateKeepLabel;
    QCheckBox *_preferLoudestCheck;
    QCheckBox *_livePreviewCheck;
    QLabel *_summaryLabel;
    QPushButton *_previewButton;
    QPushButton *_selectButton;
    QPushButton *_applyButton;
    QTimer *_previewDebounce = nullptr; ///< slider-drag preview debounce
    bool _keepSelection = false;

    AutoFitResult _lastDry;
    QString _resultSummary;
};

#endif // AUTOFITVOICELOADDIALOG_H
