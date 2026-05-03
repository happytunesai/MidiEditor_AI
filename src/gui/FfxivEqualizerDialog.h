/*
 * MidiEditor AI — FFXIV SoundFont Equalizer dialog (Phase 39)
 *
 * Modal volume mixer for the FFXIV bard SoundFont. One row per
 * known FFXIV instrument plus the GM Drum Kit, each with:
 *   - 0..200 % gain slider (1.0 = unity)
 *   - mute toggle
 *   - reset button (back to built-in default for this slot)
 *   - "▶ Preview" button that triggers FluidSynthEngine's
 *     test-tone arpeggio so the user can audition the change
 *     against the live SoundFont.
 *
 * The dialog is *live*: every slider tick mutates the singleton
 * service so playback reacts immediately. Cancel reverts to the
 * snapshot taken on construction.
 */

#ifndef FFXIVEQUALIZERDIALOG_H_
#define FFXIVEQUALIZERDIALOG_H_

#include <QDialog>
#include <QHash>

#include "../midi/FfxivEqualizerService.h"

class QComboBox;
class QPushButton;
class QSlider;
class QDoubleSpinBox;
class QLineEdit;
class QCheckBox;
class QScrollArea;
class QWidget;

class FfxivEqualizerDialog : public QDialog {
    Q_OBJECT
public:
    explicit FfxivEqualizerDialog(QWidget *parent = nullptr);
    ~FfxivEqualizerDialog() override = default;

protected:
    void reject() override;

private slots:
    void onPresetChanged(int idx);
    void onSavePresetAs();
    void onDeletePreset();
    void onMasterChanged(int sliderValue);
    void onSearchChanged(const QString &text);
    void onResetToBuiltin();

private:
    struct RowControls {
        QWidget *row = nullptr;
        QSlider *slider = nullptr;
        QDoubleSpinBox *spin = nullptr;
        QCheckBox *mute = nullptr;
        QPushButton *reset = nullptr;
        QPushButton *preview = nullptr;
        QString name;
        int program = 0;
        bool isDrum = false;
    };

    void buildUi();
    void seedRowsFromService();
    void refreshPresetCombo();
    void wireRow(RowControls &rc);
    void previewRow(const RowControls &rc);

    QComboBox *_presetCombo = nullptr;
    QPushButton *_saveAsBtn = nullptr;
    QPushButton *_deleteBtn = nullptr;
    QPushButton *_resetDefaultsBtn = nullptr;
    QSlider *_masterSlider = nullptr;
    QDoubleSpinBox *_masterSpin = nullptr;
    QLineEdit *_searchEdit = nullptr;
    QScrollArea *_scrollArea = nullptr;

    QList<RowControls> _rows;

    /// Snapshot taken on construction so Cancel can revert.
    QHash<int, FfxivEqualizerService::Slot> _initialSlots;
    float   _initialMaster = 1.0f;
    QString _initialPreset;

    bool _suspendRowSignals = false;
};

#endif // FFXIVEQUALIZERDIALOG_H_
