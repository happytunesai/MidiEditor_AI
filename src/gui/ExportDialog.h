/*
 * MidiEditor
 * Copyright (C) 2010  Markus Schwenk
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef EXPORTDIALOG_H
#define EXPORTDIALOG_H

#ifdef FLUIDSYNTH_SUPPORT

#include <QDialog>

#include "../midi/FluidSynthEngine.h"

class MidiFile;
class QRadioButton;
class QSpinBox;
class QComboBox;
class QSlider;
class QCheckBox;
class QLabel;
class QPushButton;
class QButtonGroup;

/**
 * \class ExportDialog
 * \brief Dialog for configuring audio export (format, quality, range).
 */
class ExportDialog : public QDialog {
    Q_OBJECT

public:
    ExportDialog(MidiFile *file, QWidget *parent = nullptr);

    /** Set selection range (from context menu). Enables selection radio. */
    void setSelectionRange(int startTick, int endTick);

    /** Returns configured export options. */
    ExportOptions exportOptions() const;

    /** Returns the output file path chosen by the user. */
    QString outputFilePath() const;

private slots:
    void onFormatChanged(int index);
    void onQualityPresetChanged(int index);
    void onRangeChanged();
    void updateEstimatedSize();
    void onExportClicked();

private:
    void setupUi();
    void populateFormats();
    void populateQualityPresets();
    QString formatFilter() const;
    QString formatExtension() const;
    qint64 estimateFileSize() const;
    double rangeDurationSec() const;

    MidiFile *_file;
    int _selectionStartTick = -1;
    int _selectionEndTick = -1;
    QString _outputFilePath;

    // Range
    QButtonGroup *_rangeGroup;
    QRadioButton *_fullSongRadio;
    QRadioButton *_selectionRadio;
    QRadioButton *_customRangeRadio;
    QSpinBox *_fromMeasure;
    QSpinBox *_toMeasure;

    // Format
    QComboBox *_formatCombo;
    QComboBox *_qualityPresetCombo;
    QSlider *_oggQualitySlider;
    QLabel *_oggQualityLabel;
    QLabel *_oggQualityValueLabel;
    QComboBox *_mp3BitrateCombo;
    QLabel *_mp3BitrateLabel;

    // Options
    QCheckBox *_reverbTailCheck;
    QLabel *_estimatedSizeLabel;
    QLabel *_soundFontLabel;
    QLabel *_durationLabel;

    // Buttons
    QPushButton *_exportBtn;
    QPushButton *_cancelBtn;
};

#endif // FLUIDSYNTH_SUPPORT
#endif // EXPORTDIALOG_H
