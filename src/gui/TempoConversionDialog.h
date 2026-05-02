/*
 * MidiEditor AI
 *
 * TempoConversionDialog — Phase 33
 *
 * Front-end for TempoConversionService. Lets the user pick a source/target
 * BPM, a scope (whole project / selected tracks / selected channels /
 * selected events) and a tempo-map handling mode. Shows a live preview of
 * how many events will move and the resulting project duration before the
 * Convert button is pressed.
 */

#ifndef TEMPO_CONVERSION_DIALOG_H_
#define TEMPO_CONVERSION_DIALOG_H_

#include <QDialog>
#include <QSet>

#include "../converter/TempoConversionService.h"

class MidiFile;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QRadioButton;
class QTimer;

/**
 * \brief Pre-filled hint for the dialog when launched from a context menu.
 */
struct TempoConversionScopeHint {
    TempoConversionScope scope = TempoConversionScope::WholeProject;
    QSet<int> trackIds;
    QSet<int> channelIds;
    QSet<quintptr> selectedEventPtrs;
};

class TempoConversionDialog : public QDialog {
    Q_OBJECT

public:
    explicit TempoConversionDialog(MidiFile *file,
                                   const TempoConversionScopeHint &hint,
                                   QWidget *parent = nullptr);

private slots:
    void schedulePreview();
    void runPreviewNow();
    void onAccept();

private:
    void buildUi();
    TempoConversionOptions currentOptions() const;
    static QString formatDuration(qint64 ms);

    MidiFile *_file = nullptr;
    TempoConversionScopeHint _hint;

    QDoubleSpinBox *_sourceBpm = nullptr;
    QDoubleSpinBox *_targetBpm = nullptr;
    QComboBox *_scopeCombo = nullptr;
    QRadioButton *_modeReplaceFixed = nullptr;
    QRadioButton *_modeScaleMap = nullptr;
    QRadioButton *_modeEventsOnly = nullptr;
    QLabel *_previewLabel = nullptr;
    QLabel *_warningLabel = nullptr;
    QTimer *_previewTimer = nullptr;
};

#endif // TEMPO_CONVERSION_DIALOG_H_
