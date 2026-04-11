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

#include "LyricImportDialog.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

LyricImportDialog::LyricImportDialog(int fileDurationMs, QWidget *parent)
    : QDialog(parent), _importMode(SyncLater), _fileDurationMs(fileDurationMs)
{
    setWindowTitle(tr("Import Lyrics"));
    setMinimumSize(550, 450);
    resize(600, 500);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    // Instruction label
    QLabel *instructionLabel = new QLabel(
        tr("Paste or type your lyrics below (one line = one phrase):"), this);
    mainLayout->addWidget(instructionLabel);

    // Text editor
    _textEdit = new QPlainTextEdit(this);
    _textEdit->setPlaceholderText(tr(
        "Verse 1:\n"
        "I walk along the empty road\n"
        "The only one that I have ever known\n"
        "\n"
        "Chorus:\n"
        "And the stars are shining bright"));
    _textEdit->setTabChangesFocus(false);
    mainLayout->addWidget(_textEdit, 1);

    // Options group
    QGroupBox *optionsGroup = new QGroupBox(tr("Options"), this);
    QVBoxLayout *optionsLayout = new QVBoxLayout(optionsGroup);

    _skipEmptyCheck = new QCheckBox(tr("Skip empty lines (treat as phrase separators)"), optionsGroup);
    _skipEmptyCheck->setChecked(true);
    optionsLayout->addWidget(_skipEmptyCheck);

    _skipHeadersCheck = new QCheckBox(tr("Skip lines starting with [ ] (section headers)"), optionsGroup);
    _skipHeadersCheck->setChecked(false);
    optionsLayout->addWidget(_skipHeadersCheck);

    // Duration and offset row
    QHBoxLayout *spinLayout = new QHBoxLayout();

    spinLayout->addWidget(new QLabel(tr("Default phrase duration:"), optionsGroup));
    _durationSpin = new QDoubleSpinBox(optionsGroup);
    _durationSpin->setRange(0.1, 30.0);
    _durationSpin->setValue(2.0);
    _durationSpin->setSuffix(tr(" sec"));
    _durationSpin->setSingleStep(0.5);
    spinLayout->addWidget(_durationSpin);

    spinLayout->addSpacing(20);

    spinLayout->addWidget(new QLabel(tr("Start offset:"), optionsGroup));
    _offsetSpin = new QDoubleSpinBox(optionsGroup);
    _offsetSpin->setRange(0.0, 600.0);
    _offsetSpin->setValue(0.0);
    _offsetSpin->setSuffix(tr(" sec"));
    _offsetSpin->setSingleStep(0.5);
    spinLayout->addWidget(_offsetSpin);

    spinLayout->addStretch();
    optionsLayout->addLayout(spinLayout);

    mainLayout->addWidget(optionsGroup);

    // Preview label
    _previewLabel = new QLabel(tr("Preview: 0 phrases detected"), this);
    _previewLabel->setStyleSheet("font-weight: bold; padding: 4px;");
    mainLayout->addWidget(_previewLabel);

    // Buttons
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    _syncLaterBtn = new QPushButton(tr("Import && Sync Later"), this);
    _syncLaterBtn->setToolTip(tr("Creates blocks with placeholder timings.\nUse Tap-to-Sync later to set precise timings."));
    _evenSpacingBtn = new QPushButton(tr("Import with Even Spacing"), this);
    _evenSpacingBtn->setToolTip(tr("Distributes phrases evenly across the file duration."));
    _syncNowBtn = new QPushButton(tr("Import && Sync Now"), this);
    _syncNowBtn->setToolTip(tr("Import with placeholder timings and immediately\nopen the Tap-to-Sync dialog."));
    QPushButton *cancelBtn = new QPushButton(tr("Cancel"), this);

    buttonLayout->addStretch();
    buttonLayout->addWidget(_syncLaterBtn);
    buttonLayout->addWidget(_syncNowBtn);
    buttonLayout->addWidget(_evenSpacingBtn);
    buttonLayout->addWidget(cancelBtn);
    mainLayout->addLayout(buttonLayout);

    // Connections
    connect(_textEdit, &QPlainTextEdit::textChanged, this, &LyricImportDialog::updatePreview);
    connect(_skipEmptyCheck, &QCheckBox::toggled, this, &LyricImportDialog::updatePreview);
    connect(_skipHeadersCheck, &QCheckBox::toggled, this, &LyricImportDialog::updatePreview);
    connect(_syncLaterBtn, &QPushButton::clicked, this, &LyricImportDialog::onImportSyncLater);
    connect(_evenSpacingBtn, &QPushButton::clicked, this, &LyricImportDialog::onImportEvenSpacing);
    connect(_syncNowBtn, &QPushButton::clicked, this, &LyricImportDialog::onImportSyncNow);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    updatePreview();
}

QString LyricImportDialog::lyricText() const {
    return _textEdit->toPlainText();
}

QStringList LyricImportDialog::parsedPhrases() const {
    QStringList lines = _textEdit->toPlainText().split('\n');
    QStringList phrases;

    for (const QString &line : lines) {
        QString trimmed = line.trimmed();

        if (trimmed.isEmpty()) {
            if (!_skipEmptyCheck->isChecked())
                phrases.append(trimmed);
            continue;
        }

        if (_skipHeadersCheck->isChecked() && trimmed.startsWith('[') && trimmed.contains(']'))
            continue;

        phrases.append(trimmed);
    }

    return phrases;
}

LyricImportDialog::ImportMode LyricImportDialog::importMode() const {
    return _importMode;
}

bool LyricImportDialog::skipEmptyLines() const {
    return _skipEmptyCheck->isChecked();
}

bool LyricImportDialog::skipSectionHeaders() const {
    return _skipHeadersCheck->isChecked();
}

double LyricImportDialog::phraseDurationSec() const {
    return _durationSpin->value();
}

double LyricImportDialog::startOffsetSec() const {
    return _offsetSpin->value();
}

void LyricImportDialog::updatePreview() {
    QStringList phrases = parsedPhrases();
    int count = phrases.size();
    _previewLabel->setText(tr("Preview: %1 phrase(s) detected").arg(count));

    bool hasContent = count > 0;
    _syncLaterBtn->setEnabled(hasContent);
    _evenSpacingBtn->setEnabled(hasContent);
    _syncNowBtn->setEnabled(hasContent);
}

void LyricImportDialog::onImportSyncLater() {
    _importMode = SyncLater;
    accept();
}

void LyricImportDialog::onImportEvenSpacing() {
    _importMode = EvenSpacing;
    accept();
}

void LyricImportDialog::onImportSyncNow() {
    _importMode = SyncNow;
    accept();
}
