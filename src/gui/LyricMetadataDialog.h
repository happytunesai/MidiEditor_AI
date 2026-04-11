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

#ifndef LYRICMETADATADIALOG_H
#define LYRICMETADATADIALOG_H

#include <QDialog>

#include "../midi/LyricManager.h"

class QLineEdit;
class QSpinBox;

/**
 * \class LyricMetadataDialog
 * \brief Dialog for editing LRC metadata (artist, title, album, etc.)
 */
class LyricMetadataDialog : public QDialog {
    Q_OBJECT
public:
    explicit LyricMetadataDialog(const LyricMetadata &meta, QWidget *parent = nullptr);

    LyricMetadata result() const;

private:
    QLineEdit *_artistEdit;
    QLineEdit *_titleEdit;
    QLineEdit *_albumEdit;
    QLineEdit *_lyricsByEdit;
    QSpinBox *_offsetSpin;
};

#endif // LYRICMETADATADIALOG_H
