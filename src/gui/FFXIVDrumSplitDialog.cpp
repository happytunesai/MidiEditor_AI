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

#include "FFXIVDrumSplitDialog.h"

#include <QVBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QPushButton>

FFXIVDrumSplitDialog::FFXIVDrumSplitDialog(const QHash<int, int> &noteHistogram,
                                           QWidget *parent)
    : QDialog(parent),
      _histogram(noteHistogram),
      _totalNotes(0),
      _mappingCombo(nullptr),
      _mapPresets(FfxivDrumMapPreset::presets()),
      _cosmeticPreset(DrumKitPreset::ffxivPreset()),
      _groupsLayout(nullptr),
      _otherCheck(nullptr),
      _modeHint(nullptr),
      _removeSourceCheck(nullptr) {

    for (auto it = _histogram.constBegin(); it != _histogram.constEnd(); ++it)
        _totalNotes += it.value();

    setWindowTitle(tr("FFXIV Drum Split"));
    setModal(true);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    QLabel *infoLabel = new QLabel(
        tr("Split the drum kit (channel 10) into separate FFXIV percussion tracks."), this);
    infoLabel->setWordWrap(true);
    mainLayout->addWidget(infoLabel);

    // --- Pitch mapping selection ---
    QGroupBox *mappingBox = new QGroupBox(tr("Pitch mapping"), this);
    QVBoxLayout *mappingLayout = new QVBoxLayout(mappingBox);
    _mappingCombo = new QComboBox(this);
    _mappingCombo->addItem(tr("Keep GM drum notes (tracks stay on channel 10)"));
    for (const FfxivDrumMapPreset &p : _mapPresets)
        _mappingCombo->addItem(p.name);
    mappingLayout->addWidget(_mappingCombo);
    _modeHint = new QLabel(this);
    _modeHint->setWordWrap(true);
    _modeHint->setStyleSheet("QLabel { color: gray; font-size: 10px; }");
    mappingLayout->addWidget(_modeHint);
    mainLayout->addWidget(mappingBox);

    // --- Percussion groups (rebuilt per mapping) ---
    QGroupBox *groupBox = new QGroupBox(tr("Percussion groups"), this);
    _groupsLayout = new QVBoxLayout(groupBox);
    mainLayout->addWidget(groupBox);

    // --- Overlap handling ---
    QGroupBox *overlapBox = new QGroupBox(tr("Overlapping notes"), this);
    QVBoxLayout *overlapLayout = new QVBoxLayout(overlapBox);
    _overlapCombo = new QComboBox(this);
    _overlapCombo->addItem(tr("Remove overlapping notes (recommended for FFXIV)"));
    _overlapCombo->addItem(tr("Move overlapping notes to a separate track"));
    _overlapCombo->addItem(tr("Keep overlapping notes"));
    _overlapCombo->setCurrentIndex(0);
    overlapLayout->addWidget(_overlapCombo);
    QLabel *overlapHint = new QLabel(
        tr("Drum lines often stack same-pitch notes to reinforce a hit. FFXIV plays those "
           "sequentially instead of together, which changes the beat, so redundant overlaps "
           "are removed by default."), this);
    overlapHint->setWordWrap(true);
    overlapHint->setStyleSheet("QLabel { color: gray; font-size: 10px; }");
    overlapLayout->addWidget(overlapHint);
    mainLayout->addWidget(overlapBox);

    _removeSourceCheck =
        new QCheckBox(tr("Remove the original drum track if it becomes empty"), this);
    _removeSourceCheck->setChecked(false);
    mainLayout->addWidget(_removeSourceCheck);

    QDialogButtonBox *buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttonBox->button(QDialogButtonBox::Ok)->setText(tr("Split"));
    connect(buttonBox, SIGNAL(accepted()), this, SLOT(accept()));
    connect(buttonBox, SIGNAL(rejected()), this, SLOT(reject()));
    mainLayout->addWidget(buttonBox);

    connect(_mappingCombo, SIGNAL(currentIndexChanged(int)), this, SLOT(rebuildGroupRows()));
    rebuildGroupRows();

    setLayout(mainLayout);
}

FFXIVDrumSplitDialog::OverlapAction FFXIVDrumSplitDialog::overlapAction() const {
    switch (_overlapCombo ? _overlapCombo->currentIndex() : 0) {
    case 1:  return MoveOverlaps;
    case 2:  return KeepOverlaps;
    default: return RemoveOverlaps;
    }
}

int FFXIVDrumSplitDialog::countForNotes(const QList<int> &notes) const {
    int n = 0;
    for (int note : notes)
        n += _histogram.value(note, 0);
    return n;
}

void FFXIVDrumSplitDialog::rebuildGroupRows() {
    // Clear previous rows.
    while (QLayoutItem *item = _groupsLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    _groupChecks.clear();
    _groupNames.clear();
    _groupCounts.clear();
    _otherCheck = nullptr;

    const bool transpose = transposeMode();
    _modeHint->setText(transpose
        ? tr("Drums are transposed onto the kit's pitches. All tracks stay on the "
             "drum channel; the FFXIV instrument follows the track name. Drums "
             "the kit does not map (e.g. hi-hats) go to \"Other Percussion\" "
             "unchanged.")
        : tr("Notes keep their GM pitches and stay on the drum channel - only the "
             "track layout changes."));

    auto addRow = [this](const QString &name, const QList<int> &notes) {
        int n = countForNotes(notes);
        QCheckBox *cb = new QCheckBox(tr("%1  (%2 notes)").arg(name).arg(n), this);
        cb->setChecked(n > 0);
        cb->setEnabled(n > 0);
        _groupChecks.append(cb);
        _groupNames.append(name);
        _groupCounts.append(n);
        _groupsLayout->addWidget(cb);
        // Unchecking a group sends its notes to "Other Percussion" - keep
        // that row's count live.
        connect(cb, &QCheckBox::toggled, this,
                [this](bool) { updateOtherLabel(); });
    };

    if (transpose) {
        const FfxivDrumMapPreset preset = selectedMapPreset();
        for (const FfxivDrumMapGroup &g : preset.groups) {
            QList<int> notes;
            for (const FfxivDrumNoteMap &m : g.mappings)
                notes.append(m.sourceNote);
            addRow(g.trackName, notes);
        }
    } else {
        for (const DrumGroup &g : _cosmeticPreset.groups) {
            if (g.name == QStringLiteral("Other Percussion"))
                continue; // remainder row below covers it
            addRow(g.name, g.noteNumbers);
        }
    }

    _otherCheck = new QCheckBox(this);
    _groupsLayout->addWidget(_otherCheck);
    updateOtherLabel();
    _otherCheck->setChecked(_otherCheck->isEnabled());

    adjustSize();
}

void FFXIVDrumSplitDialog::updateOtherLabel() {
    if (!_otherCheck)
        return;
    // Everything not claimed by a CHECKED group ends up in "Other Percussion"
    // (unchecked groups' notes fall through to the sweep).
    int assigned = 0;
    for (int i = 0; i < _groupChecks.size() && i < _groupCounts.size(); ++i) {
        if (_groupChecks.at(i)->isChecked())
            assigned += _groupCounts.at(i);
    }
    const int other = _totalNotes - assigned;
    _otherCheck->setText(
        tr("Other Percussion  (%1 notes, keeps channel 10)").arg(other));
    _otherCheck->setEnabled(other > 0);
}

bool FFXIVDrumSplitDialog::transposeMode() const {
    return _mappingCombo && _mappingCombo->currentIndex() > 0;
}

FfxivDrumMapPreset FFXIVDrumSplitDialog::selectedMapPreset() const {
    const int idx = _mappingCombo ? _mappingCombo->currentIndex() - 1 : -1;
    if (idx >= 0 && idx < _mapPresets.size())
        return _mapPresets.at(idx);
    return FfxivDrumMapPreset();
}

QList<DrumGroup> FFXIVDrumSplitDialog::selectedGroups() const {
    QList<DrumGroup> out;
    if (transposeMode())
        return out;
    for (const DrumGroup &g : _cosmeticPreset.groups) {
        if (g.name == QStringLiteral("Other Percussion"))
            continue;
        int idx = _groupNames.indexOf(g.name);
        if (idx >= 0 && _groupChecks.at(idx)->isChecked())
            out.append(g);
    }
    return out;
}

QList<FfxivDrumMapGroup> FFXIVDrumSplitDialog::selectedMapGroups() const {
    QList<FfxivDrumMapGroup> out;
    if (!transposeMode())
        return out;
    const FfxivDrumMapPreset preset = selectedMapPreset();
    for (const FfxivDrumMapGroup &g : preset.groups) {
        int idx = _groupNames.indexOf(g.trackName);
        if (idx >= 0 && _groupChecks.at(idx)->isChecked())
            out.append(g);
    }
    return out;
}

bool FFXIVDrumSplitDialog::includeOtherPercussion() const {
    return _otherCheck && _otherCheck->isChecked();
}

bool FFXIVDrumSplitDialog::removeEmptySource() const {
    return _removeSourceCheck && _removeSourceCheck->isChecked();
}
