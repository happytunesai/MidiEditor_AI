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

#ifndef LYRICIMPORTDIALOG_H
#define LYRICIMPORTDIALOG_H

#include <QDialog>

class QPlainTextEdit;
class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;

/**
 * \class LyricImportDialog
 *
 * \brief Dialog for importing plain text lyrics into the MIDI file.
 *
 * Users can paste or type lyrics (one line = one phrase).
 * Options control how lines are parsed and how timing is assigned.
 */
class LyricImportDialog : public QDialog {
    Q_OBJECT

public:
    enum ImportMode {
        SyncLater,      ///< Create blocks with placeholder timings
        EvenSpacing,    ///< Distribute evenly across file duration
        SyncNow         ///< Import with placeholder timings, then open Sync dialog
    };

    explicit LyricImportDialog(int fileDurationMs, QWidget *parent = nullptr);

    /** \brief Returns the raw text from the editor */
    QString lyricText() const;

    /** \brief Returns the parsed phrases (after applying skip options) */
    QStringList parsedPhrases() const;

    /** \brief Returns the selected import mode */
    ImportMode importMode() const;

    /** \brief Whether to skip empty lines */
    bool skipEmptyLines() const;

    /** \brief Whether to skip section header lines starting with [ ] */
    bool skipSectionHeaders() const;

    /** \brief Default phrase duration in seconds */
    double phraseDurationSec() const;

    /** \brief Start offset in seconds */
    double startOffsetSec() const;

private slots:
    void updatePreview();
    void onImportSyncLater();
    void onImportEvenSpacing();
    void onImportSyncNow();

private:
    QPlainTextEdit *_textEdit;
    QCheckBox *_skipEmptyCheck;
    QCheckBox *_skipHeadersCheck;
    QDoubleSpinBox *_durationSpin;
    QDoubleSpinBox *_offsetSpin;
    QLabel *_previewLabel;
    QPushButton *_syncLaterBtn;
    QPushButton *_evenSpacingBtn;
    QPushButton *_syncNowBtn;

    ImportMode _importMode;
    int _fileDurationMs;
};

#endif // LYRICIMPORTDIALOG_H
