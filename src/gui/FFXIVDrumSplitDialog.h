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

#ifndef FFXIVDRUMSPLITDIALOG_H_
#define FFXIVDRUMSPLITDIALOG_H_

#include <QDialog>
#include <QHash>
#include <QList>

#include "DrumKitPreset.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QVBoxLayout;

// Preview dialog for the FFXIV drum split (v2.0). Two modes, chosen via the
// pitch-mapping combo:
//   * "Keep GM drum notes" (default): cosmetic split - tracks stay on
//     channel 10, pitches untouched (grouping = DrumKitPreset::ffxivPreset()).
//   * A community kit (Mog Amp / Bard Forge 1/2 / Bard Metal): the split also
//     TRANSPOSES each drum onto its kit pitch. Tracks stay on channel 10 - the
//     per-hit program injection keyed on the TRACK NAME picks the FFXIV
//     percussion preset, so the note number is free to carry the kit pitch.
//     Unmapped drums stay on channel 10 in "Other Percussion", untransposed.
// Pure UI: shows per-group note counts from the caller's channel-10 note
// histogram and returns the user's choices; MainWindow::ffxivDrumSplit()
// performs the split.
class FFXIVDrumSplitDialog : public QDialog {
    Q_OBJECT

public:
    // How to handle overlapping same-pitch notes (drum reinforcement layers)
    // in the split result. FFXIV percussion can't sound two stacked hits at
    // once - they play sequentially and change the beat - so cleaning them up
    // is the sensible default.
    enum OverlapAction {
        RemoveOverlaps = 0, // delete the redundant overlapping notes (default)
        MoveOverlaps,       // move them to a separate "Overlaps" track
        KeepOverlaps        // leave everything as-is
    };

    FFXIVDrumSplitDialog(const QHash<int, int> &noteHistogram, QWidget *parent = nullptr);

    /// True when a pitch-mapping kit is selected (transpose mode).
    bool transposeMode() const;

    /// Chosen overlap handling for the split result.
    OverlapAction overlapAction() const;

    /// The selected kit (only meaningful in transpose mode).
    FfxivDrumMapPreset selectedMapPreset() const;

    /// Cosmetic mode: the kept named groups (subset of ffxivPreset(), without
    /// "Other Percussion" - see includeOtherPercussion()).
    QList<DrumGroup> selectedGroups() const;

    /// Transpose mode: the kept kit groups (only those with notes present).
    QList<FfxivDrumMapGroup> selectedMapGroups() const;

    /// Whether leftover channel-10 events go to an "Other Percussion" track.
    bool includeOtherPercussion() const;

    bool removeEmptySource() const;

private slots:
    void rebuildGroupRows();

private:
    int countForNotes(const QList<int> &notes) const;
    void updateOtherLabel();

    QHash<int, int> _histogram;    // channel-10 note -> NoteOn count
    int _totalNotes;

    QComboBox *_mappingCombo;      // 0 = cosmetic, 1.. = FfxivDrumMapPreset
    QList<FfxivDrumMapPreset> _mapPresets;
    DrumKitPreset _cosmeticPreset;

    QVBoxLayout *_groupsLayout;    // rebuilt when the combo changes
    QList<QCheckBox *> _groupChecks;   // named groups of the active mapping
    QStringList _groupNames;           // parallel to _groupChecks
    QList<int> _groupCounts;           // parallel to _groupChecks
    QCheckBox *_otherCheck;
    QLabel *_modeHint;
    QCheckBox *_removeSourceCheck;
    QComboBox *_overlapCombo;      // 0 remove / 1 move / 2 keep
};

#endif // FFXIVDRUMSPLITDIALOG_H_
