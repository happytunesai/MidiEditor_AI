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

#ifdef FLUIDSYNTH_SUPPORT

#include "ExportDialog.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QStandardPaths>
#include <QVBoxLayout>

#include "../midi/MidiFile.h"
#include "../midi/FluidSynthEngine.h"

// ============================================================================
// Constructor
// ============================================================================

ExportDialog::ExportDialog(MidiFile *file, QWidget *parent)
    : QDialog(parent), _file(file) {
    setWindowTitle(tr("Export Audio"));
    setMinimumWidth(460);
    setupUi();

    // Restore last-used settings
    QSettings settings;
    settings.beginGroup("Export");
    int fmtIdx = _formatCombo->findText(settings.value("format").toString(),
                                        Qt::MatchContains);
    if (fmtIdx >= 0) _formatCombo->setCurrentIndex(fmtIdx);
    int qIdx = settings.value("qualityPreset", 1).toInt();
    if (qIdx >= 0 && qIdx < _qualityPresetCombo->count())
        _qualityPresetCombo->setCurrentIndex(qIdx);
    _reverbTailCheck->setChecked(settings.value("reverbTail", true).toBool());
    _oggQualitySlider->setValue(
        static_cast<int>(settings.value("oggQuality", 50).toInt()));
    int mp3Idx = _mp3BitrateCombo->findData(settings.value("mp3Bitrate", 192).toInt());
    if (mp3Idx >= 0) _mp3BitrateCombo->setCurrentIndex(mp3Idx);
    settings.endGroup();

    onFormatChanged(_formatCombo->currentIndex());
    onRangeChanged();
    updateEstimatedSize();
}

// ============================================================================
// Setup UI
// ============================================================================

void ExportDialog::setupUi() {
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(12);

    // --- Source info ---
    QString fileName = _file ? QFileInfo(_file->path()).fileName() : tr("(no file)");
    double durationSec = _file ? _file->msOfTick(_file->endTick()) / 1000.0 : 0;
    int mins = static_cast<int>(durationSec) / 60;
    int secs = static_cast<int>(durationSec) % 60;

    QLabel *sourceLabel = new QLabel(
        tr("Source: <b>%1</b>").arg(fileName), this);
    mainLayout->addWidget(sourceLabel);

    _durationLabel = new QLabel(
        tr("Duration: %1:%2").arg(mins).arg(secs, 2, 10, QChar('0')), this);
    mainLayout->addWidget(_durationLabel);

    // --- Range group ---
    QGroupBox *rangeGroup = new QGroupBox(tr("Range"), this);
    QVBoxLayout *rangeLayout = new QVBoxLayout(rangeGroup);
    _rangeGroup = new QButtonGroup(this);

    _fullSongRadio = new QRadioButton(tr("Full song"), rangeGroup);
    _selectionRadio = new QRadioButton(tr("Selection"), rangeGroup);
    _selectionRadio->setEnabled(false); // Enabled only when setSelectionRange() called
    _customRangeRadio = new QRadioButton(tr("Custom range (measures):"), rangeGroup);

    _rangeGroup->addButton(_fullSongRadio, 0);
    _rangeGroup->addButton(_selectionRadio, 1);
    _rangeGroup->addButton(_customRangeRadio, 2);
    _fullSongRadio->setChecked(true);

    rangeLayout->addWidget(_fullSongRadio);
    rangeLayout->addWidget(_selectionRadio);

    QHBoxLayout *customRow = new QHBoxLayout();
    customRow->addWidget(_customRangeRadio);
    _fromMeasure = new QSpinBox(rangeGroup);
    _fromMeasure->setPrefix(tr("From: "));
    _fromMeasure->setMinimum(1);
    _fromMeasure->setMaximum(9999);
    _fromMeasure->setValue(1);
    _toMeasure = new QSpinBox(rangeGroup);
    _toMeasure->setPrefix(tr("To: "));
    _toMeasure->setMinimum(1);
    _toMeasure->setMaximum(9999);
    _toMeasure->setValue(1);
    if (_file) {
        int dummy1, dummy2;
        int totalMeasures = _file->measure(_file->endTick(), &dummy1, &dummy2) + 1;
        _fromMeasure->setMaximum(totalMeasures);
        _toMeasure->setMaximum(totalMeasures);
        _toMeasure->setValue(totalMeasures);
    }
    _fromMeasure->setEnabled(false);
    _toMeasure->setEnabled(false);
    customRow->addWidget(_fromMeasure);
    customRow->addWidget(_toMeasure);
    customRow->addStretch();
    rangeLayout->addLayout(customRow);

    mainLayout->addWidget(rangeGroup);

    // --- Format group ---
    QGroupBox *formatGroup = new QGroupBox(tr("Format"), this);
    QVBoxLayout *formatLayout = new QVBoxLayout(formatGroup);

    QHBoxLayout *fmtRow = new QHBoxLayout();
    fmtRow->addWidget(new QLabel(tr("Format:"), formatGroup));
    _formatCombo = new QComboBox(formatGroup);
    populateFormats();
    _formatCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    fmtRow->addWidget(_formatCombo);
    formatLayout->addLayout(fmtRow);

    QHBoxLayout *qualRow = new QHBoxLayout();
    qualRow->addWidget(new QLabel(tr("Quality:"), formatGroup));
    _qualityPresetCombo = new QComboBox(formatGroup);
    populateQualityPresets();
    _qualityPresetCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    qualRow->addWidget(_qualityPresetCombo);
    formatLayout->addLayout(qualRow);

    // OGG quality slider (hidden unless OGG selected)
    QHBoxLayout *oggRow = new QHBoxLayout();
    _oggQualityLabel = new QLabel(tr("OGG Quality:"), formatGroup);
    _oggQualitySlider = new QSlider(Qt::Horizontal, formatGroup);
    _oggQualitySlider->setRange(10, 100);
    _oggQualitySlider->setValue(50);
    _oggQualityValueLabel = new QLabel("0.50", formatGroup);
    oggRow->addWidget(_oggQualityLabel);
    oggRow->addWidget(_oggQualitySlider);
    oggRow->addWidget(_oggQualityValueLabel);
    formatLayout->addLayout(oggRow);
    _oggQualityLabel->setVisible(false);
    _oggQualitySlider->setVisible(false);
    _oggQualityValueLabel->setVisible(false);

    // MP3 bitrate combo (hidden unless MP3 selected)
    QHBoxLayout *mp3Row = new QHBoxLayout();
    _mp3BitrateLabel = new QLabel(tr("Bitrate:"), formatGroup);
    _mp3BitrateCombo = new QComboBox(formatGroup);
    _mp3BitrateCombo->addItem(tr("128 kbps (small file)"), 128);
    _mp3BitrateCombo->addItem(tr("192 kbps (recommended)"), 192);
    _mp3BitrateCombo->addItem(tr("256 kbps (high quality)"), 256);
    _mp3BitrateCombo->addItem(tr("320 kbps (maximum)"), 320);
    _mp3BitrateCombo->setCurrentIndex(1); // default 192
    mp3Row->addWidget(_mp3BitrateLabel);
    mp3Row->addWidget(_mp3BitrateCombo);
    formatLayout->addLayout(mp3Row);
    _mp3BitrateLabel->setVisible(false);
    _mp3BitrateCombo->setVisible(false);

    mainLayout->addWidget(formatGroup);

    // --- Options group ---
    QGroupBox *optionsGroup = new QGroupBox(tr("Options"), this);
    QVBoxLayout *optLayout = new QVBoxLayout(optionsGroup);

    _reverbTailCheck = new QCheckBox(tr("Include reverb tail (2s after last note)"),
                                     optionsGroup);
    _reverbTailCheck->setChecked(true);
    optLayout->addWidget(_reverbTailCheck);

    // Show current SoundFont stack info
    FluidSynthEngine *engine = FluidSynthEngine::instance();
    auto fonts = engine->loadedSoundFonts();
    QString sfInfo;
    if (fonts.isEmpty()) {
        sfInfo = tr("<i>No SoundFont loaded</i>");
    } else {
        sfInfo = tr("SoundFont: %1").arg(QFileInfo(fonts.last().second).fileName());
        if (fonts.size() > 1) {
            sfInfo += tr(" (+%1 more)").arg(fonts.size() - 1);
        }
    }
    _soundFontLabel = new QLabel(sfInfo, optionsGroup);
    optLayout->addWidget(_soundFontLabel);

    mainLayout->addWidget(optionsGroup);

    // --- Estimated size ---
    _estimatedSizeLabel = new QLabel(this);
    mainLayout->addWidget(_estimatedSizeLabel);

    // --- Buttons ---
    QHBoxLayout *btnRow = new QHBoxLayout();
    btnRow->addStretch();
    _cancelBtn = new QPushButton(tr("Cancel"), this);
    _exportBtn = new QPushButton(tr("Export..."), this);
    _exportBtn->setDefault(true);
    btnRow->addWidget(_cancelBtn);
    btnRow->addWidget(_exportBtn);
    mainLayout->addLayout(btnRow);

    // Disable export if no SoundFonts loaded
    if (fonts.isEmpty()) {
        _exportBtn->setEnabled(false);
        _exportBtn->setToolTip(tr("Load a SoundFont in Settings first"));
    }

    // --- Connections ---
    connect(_formatCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ExportDialog::onFormatChanged);
    connect(_qualityPresetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ExportDialog::onQualityPresetChanged);
    connect(_oggQualitySlider, &QSlider::valueChanged, this, [this](int v) {
        _oggQualityValueLabel->setText(QString::number(v / 100.0, 'f', 2));
        updateEstimatedSize();
    });
    connect(_mp3BitrateCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { updateEstimatedSize(); });
    connect(_rangeGroup, QOverload<int>::of(&QButtonGroup::idClicked),
            this, [this](int) { onRangeChanged(); });
    connect(_fromMeasure, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int) { updateEstimatedSize(); });
    connect(_toMeasure, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int) { updateEstimatedSize(); });
    connect(_reverbTailCheck, &QCheckBox::toggled,
            this, [this](bool) { updateEstimatedSize(); });
    connect(_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(_exportBtn, &QPushButton::clicked, this, &ExportDialog::onExportClicked);
}

// ============================================================================
// Populate
// ============================================================================

void ExportDialog::populateFormats() {
    // Data: FluidSynth audio.file.type value (or "mp3" for LAME pipeline)
    _formatCombo->addItem(tr("WAV — Uncompressed PCM (.wav)"), "wav");
    _formatCombo->addItem(tr("FLAC — Lossless Compressed (.flac)"), "flac");
    _formatCombo->addItem(tr("OGG Vorbis — Lossy Compressed (.ogg)"), "oga");

#ifdef LAME_SUPPORT
    _formatCombo->addItem(tr("MP3 — Lossy Compressed (.mp3)"), "mp3");
#endif
}

void ExportDialog::populateQualityPresets() {
    // Data format: "sampleFormat|sampleRate"
    _qualityPresetCombo->addItem(tr("Draft (16-bit, 22050 Hz)"), "s16|22050");
    _qualityPresetCombo->addItem(tr("CD Quality (16-bit, 44100 Hz)"), "s16|44100");
    _qualityPresetCombo->addItem(tr("Studio (24-bit, 48000 Hz)"), "s24|48000");
    _qualityPresetCombo->addItem(tr("Hi-Res (24-bit, 96000 Hz)"), "s24|96000");
}

// ============================================================================
// Slots
// ============================================================================

void ExportDialog::onFormatChanged(int /*index*/) {
    QString fmt = _formatCombo->currentData().toString();
    bool isOgg = (fmt == "oga");
    bool isMp3 = (fmt == "mp3");
    _oggQualityLabel->setVisible(isOgg);
    _oggQualitySlider->setVisible(isOgg);
    _oggQualityValueLabel->setVisible(isOgg);
    _mp3BitrateLabel->setVisible(isMp3);
    _mp3BitrateCombo->setVisible(isMp3);
    updateEstimatedSize();
}

void ExportDialog::onQualityPresetChanged(int /*index*/) {
    updateEstimatedSize();
}

void ExportDialog::onRangeChanged() {
    bool custom = _customRangeRadio->isChecked();
    _fromMeasure->setEnabled(custom);
    _toMeasure->setEnabled(custom);
    updateEstimatedSize();
}

void ExportDialog::updateEstimatedSize() {
    qint64 size = estimateFileSize();
    QString sizeStr;
    if (size < 1024) {
        sizeStr = QString("%1 B").arg(size);
    } else if (size < 1048576) {
        sizeStr = QString("%1 KB").arg(size / 1024.0, 0, 'f', 1);
    } else {
        sizeStr = QString("%1 MB").arg(size / 1048576.0, 0, 'f', 1);
    }
    _estimatedSizeLabel->setText(tr("Estimated file size: ~%1").arg(sizeStr));
}

void ExportDialog::onExportClicked() {
    // Build default path
    QSettings settings;
    settings.beginGroup("Export");
    QString lastDir = settings.value("lastDirectory",
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)).toString();
    settings.endGroup();

    QString baseName = _file ? QFileInfo(_file->path()).baseName() : "export";
    if (baseName.isEmpty()) baseName = "export";
    QString ext = formatExtension();
    QString defaultPath = QDir(lastDir).filePath(baseName + "." + ext);

    _outputFilePath = QFileDialog::getSaveFileName(
        this, tr("Export Audio"), defaultPath, formatFilter());

    if (_outputFilePath.isEmpty()) {
        return; // User cancelled file dialog
    }

    // Save settings for next time
    settings.beginGroup("Export");
    settings.setValue("format", _formatCombo->currentText());
    settings.setValue("qualityPreset", _qualityPresetCombo->currentIndex());
    settings.setValue("reverbTail", _reverbTailCheck->isChecked());
    settings.setValue("oggQuality", _oggQualitySlider->value());
    settings.setValue("mp3Bitrate", _mp3BitrateCombo->currentData().toInt());
    settings.setValue("lastDirectory", QFileInfo(_outputFilePath).absolutePath());
    settings.endGroup();

    accept();
}

// ============================================================================
// Public API
// ============================================================================

void ExportDialog::setSelectionRange(int startTick, int endTick) {
    _selectionStartTick = startTick;
    _selectionEndTick = endTick;
    _selectionRadio->setEnabled(true);

    // Build display text for selection range
    if (_file) {
        int dummy1, dummy2;
        int startMeasure = _file->measure(startTick, &dummy1, &dummy2) + 1;
        int endMeasure = _file->measure(endTick, &dummy1, &dummy2) + 1;
        double startSec = _file->msOfTick(startTick) / 1000.0;
        double endSec = _file->msOfTick(endTick) / 1000.0;
        _selectionRadio->setText(
            tr("Selection (Measure %1–%2, %3:%4–%5:%6)")
                .arg(startMeasure)
                .arg(endMeasure)
                .arg(static_cast<int>(startSec) / 60)
                .arg(static_cast<int>(startSec) % 60, 2, 10, QChar('0'))
                .arg(static_cast<int>(endSec) / 60)
                .arg(static_cast<int>(endSec) % 60, 2, 10, QChar('0')));
    }
    _selectionRadio->setChecked(true);
    onRangeChanged();
}

ExportOptions ExportDialog::exportOptions() const {
    ExportOptions opts;
    opts.outputFilePath = _outputFilePath;
    opts.fileType = _formatCombo->currentData().toString();
    opts.includeReverbTail = _reverbTailCheck->isChecked();
    opts.encodingQuality = _oggQualitySlider->value() / 100.0;

    // MP3 bitrate
    opts.mp3Bitrate = _mp3BitrateCombo->currentData().toInt();

    // Parse quality preset: "sampleFormat|sampleRate"
    QString preset = _qualityPresetCombo->currentData().toString();
    QStringList parts = preset.split('|');
    if (parts.size() == 2) {
        opts.sampleFormat = parts[0];
        opts.sampleRate = parts[1].toDouble();
    } else {
        opts.sampleFormat = "s16";
        opts.sampleRate = 44100.0;
    }

    // Range
    if (_selectionRadio->isChecked() && _selectionStartTick >= 0) {
        opts.startTick = _selectionStartTick;
        opts.endTick = _selectionEndTick;
    } else if (_customRangeRadio->isChecked() && _file) {
        int fromMeasure = _fromMeasure->value() - 1; // 0-based
        int toMeasure = _toMeasure->value() - 1;
        opts.startTick = _file->startTickOfMeasure(fromMeasure);
        opts.endTick = _file->startTickOfMeasure(toMeasure + 1);
    }
    // else full song: startTick/endTick remain -1

    return opts;
}

QString ExportDialog::outputFilePath() const {
    return _outputFilePath;
}

// ============================================================================
// Helpers
// ============================================================================

QString ExportDialog::formatFilter() const {
    QString type = _formatCombo->currentData().toString();
    if (type == "wav") return tr("WAV Audio (*.wav)");
    if (type == "flac") return tr("FLAC Audio (*.flac)");
    if (type == "oga") return tr("OGG Vorbis Audio (*.ogg)");
    if (type == "mp3") return tr("MP3 Audio (*.mp3)");
    return tr("All Files (*)");
}

QString ExportDialog::formatExtension() const {
    QString type = _formatCombo->currentData().toString();
    if (type == "wav") return "wav";
    if (type == "flac") return "flac";
    if (type == "oga") return "ogg";
    if (type == "mp3") return "mp3";
    return "wav";
}

double ExportDialog::rangeDurationSec() const {
    if (!_file) return 0;

    if (_selectionRadio->isChecked() && _selectionStartTick >= 0) {
        double startMs = _file->msOfTick(_selectionStartTick);
        double endMs = _file->msOfTick(_selectionEndTick);
        return (endMs - startMs) / 1000.0;
    }

    if (_customRangeRadio->isChecked()) {
        int fromMeasure = _fromMeasure->value() - 1; // 0-based
        int toMeasure = _toMeasure->value() - 1;
        int startTick = _file->startTickOfMeasure(fromMeasure);
        int endTick = _file->startTickOfMeasure(toMeasure + 1);
        double startMs = _file->msOfTick(startTick);
        double endMs = _file->msOfTick(endTick);
        return (endMs - startMs) / 1000.0;
    }

    // Full song
    return _file->msOfTick(_file->endTick()) / 1000.0;
}

qint64 ExportDialog::estimateFileSize() const {
    double durationSec = rangeDurationSec();
    if (_reverbTailCheck->isChecked()) durationSec += 2.0;

    QString preset = _qualityPresetCombo->currentData().toString();
    QStringList parts = preset.split('|');
    double sampleRate = (parts.size() == 2) ? parts[1].toDouble() : 44100.0;
    int bytesPerSample = parts[0].contains("24") ? 3 : 2;
    int channels = 2; // Stereo

    QString type = _formatCombo->currentData().toString();
    qint64 rawSize = static_cast<qint64>(durationSec * sampleRate * channels * bytesPerSample);

    if (type == "wav") return rawSize + 44; // WAV header
    if (type == "flac") return static_cast<qint64>(rawSize * 0.55); // ~55% compression
    if (type == "oga") {
        // Estimate based on quality slider → bitrate
        double quality = _oggQualitySlider->value() / 100.0;
        double kbps = 64 + quality * 256; // ~64–320 kbps range
        return static_cast<qint64>(durationSec * kbps * 1000 / 8);
    }
    if (type == "mp3") {
        int bitrate = _mp3BitrateCombo->currentData().toInt();
        return static_cast<qint64>(durationSec * bitrate * 1000 / 8);
    }
    return rawSize;
}

#endif // FLUIDSYNTH_SUPPORT
