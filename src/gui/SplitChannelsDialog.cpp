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

#include "SplitChannelsDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QLabel>

SplitChannelsDialog::SplitChannelsDialog(MidiFile *file, MidiTrack *sourceTrack,
                                         const QList<ChannelInfo> &channels, QWidget *parent)
    : QDialog(parent) {

    Q_UNUSED(file)
    Q_UNUSED(sourceTrack)

    setWindowTitle(tr("Split Channels to Tracks"));
    setModal(true);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    // Info label
    QLabel *infoLabel = new QLabel(
        tr("This will create a separate track for each MIDI channel.\n"
           "Track names are derived from GM Program Change events."), this);
    infoLabel->setWordWrap(true);
    mainLayout->addWidget(infoLabel);

    // Channel preview table
    QGroupBox *previewGroup = new QGroupBox(tr("Channels found"), this);
    QVBoxLayout *previewLayout = new QVBoxLayout(previewGroup);

    _channelTable = new QTableWidget(channels.size(), 4, this);
    _channelTable->setHorizontalHeaderLabels({tr("Channel"), tr("Program"), tr("Track Name"), tr("Notes")});
    _channelTable->horizontalHeader()->setStretchLastSection(true);
    _channelTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    _channelTable->verticalHeader()->setVisible(false);
    _channelTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    _channelTable->setSelectionMode(QAbstractItemView::NoSelection);

    for (int row = 0; row < channels.size(); ++row) {
        const ChannelInfo &info = channels[row];

        auto *chItem = new QTableWidgetItem(QString::number(info.channel));
        chItem->setTextAlignment(Qt::AlignCenter);
        _channelTable->setItem(row, 0, chItem);

        QString progStr = (info.channel == 9) ? tr("Drums") :
                          (info.programNumber >= 0) ? QString::number(info.programNumber) : QStringLiteral("-");
        auto *progItem = new QTableWidgetItem(progStr);
        progItem->setTextAlignment(Qt::AlignCenter);
        _channelTable->setItem(row, 1, progItem);

        _channelTable->setItem(row, 2, new QTableWidgetItem(info.instrumentName));

        auto *noteItem = new QTableWidgetItem(QString::number(info.noteCount));
        noteItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        _channelTable->setItem(row, 3, noteItem);
    }

    _channelTable->resizeColumnsToContents();
    _channelTable->setMinimumHeight(qMin(channels.size() * 30 + 30, 300));
    previewLayout->addWidget(_channelTable);
    mainLayout->addWidget(previewGroup);

    // Options
    QGroupBox *optionsGroup = new QGroupBox(tr("Options"), this);
    QVBoxLayout *optionsLayout = new QVBoxLayout(optionsGroup);

    _keepDrumsCheck = new QCheckBox(tr("Keep Channel 9 (Drums) on original track"), this);
    _keepDrumsCheck->setToolTip(tr("When checked, drum events stay on the source track instead of getting their own track"));
    optionsLayout->addWidget(_keepDrumsCheck);

    _removeSourceCheck = new QCheckBox(tr("Remove empty source track after split"), this);
    _removeSourceCheck->setChecked(true);
    _removeSourceCheck->setToolTip(tr("Deletes the original track if no events remain on it"));
    optionsLayout->addWidget(_removeSourceCheck);

    mainLayout->addWidget(optionsGroup);

    // Track position
    QGroupBox *positionGroup = new QGroupBox(tr("New track position"), this);
    QVBoxLayout *positionLayout = new QVBoxLayout(positionGroup);

    _insertAfterRadio = new QRadioButton(tr("Insert after source track"), this);
    _insertAfterRadio->setChecked(true);
    positionLayout->addWidget(_insertAfterRadio);

    _insertAtEndRadio = new QRadioButton(tr("Insert at end of track list"), this);
    positionLayout->addWidget(_insertAtEndRadio);

    mainLayout->addWidget(positionGroup);

    // Drum kit preset (only if channel 9 is present)
    _hasDrumChannel = false;
    for (const auto &ch : channels) {
        if (ch.channel == 9) { _hasDrumChannel = true; break; }
    }
    _drumPresetCombo = new QComboBox(this);
    _drumPresetCombo->addItem(tr("None (single drum track)"));
    for (const auto &preset : DrumKitPreset::presets()) {
        _drumPresetCombo->addItem(preset.name);
    }
    if (_hasDrumChannel) {
        QGroupBox *drumGroup = new QGroupBox(tr("Drum Kit Preset"), this);
        QVBoxLayout *drumLayout = new QVBoxLayout(drumGroup);
        QLabel *drumInfo = new QLabel(
            tr("Split Channel 9 (Drums) into separate tracks by instrument group:"), this);
        drumInfo->setWordWrap(true);
        drumLayout->addWidget(drumInfo);
        drumLayout->addWidget(_drumPresetCombo);
        mainLayout->addWidget(drumGroup);
    }

    // Buttons
    QDialogButtonBox *buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttonBox->button(QDialogButtonBox::Ok)->setText(tr("Split"));
    connect(buttonBox, SIGNAL(accepted()), this, SLOT(accept()));
    connect(buttonBox, SIGNAL(rejected()), this, SLOT(reject()));
    mainLayout->addWidget(buttonBox);

    setLayout(mainLayout);
    resize(520, qMin(channels.size() * 30 + 380, 600));
}

bool SplitChannelsDialog::keepDrumsOnSource() const {
    return _keepDrumsCheck->isChecked();
}

bool SplitChannelsDialog::removeEmptySource() const {
    return _removeSourceCheck->isChecked();
}

bool SplitChannelsDialog::insertAtEnd() const {
    return _insertAtEndRadio->isChecked();
}

DrumKitPreset SplitChannelsDialog::selectedDrumPreset() const {
    int idx = _drumPresetCombo->currentIndex() - 1; // 0 = "None"
    QList<DrumKitPreset> all = DrumKitPreset::presets();
    if (idx >= 0 && idx < all.size()) return all[idx];
    return DrumKitPreset();
}

bool SplitChannelsDialog::hasDrumPreset() const {
    return _hasDrumChannel && _drumPresetCombo->currentIndex() > 0;
}
