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

#include "LyricMetadataDialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QVBoxLayout>

LyricMetadataDialog::LyricMetadataDialog(const LyricMetadata &meta, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Lyric Settings"));
    setMinimumWidth(400);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    QLabel *infoLabel = new QLabel(
        tr("These fields are used when exporting to LRC format.\n"
           "They correspond to standard LRC header tags."), this);
    infoLabel->setStyleSheet("color: gray; font-size: 11px; margin-bottom: 8px;");
    mainLayout->addWidget(infoLabel);

    QFormLayout *form = new QFormLayout();
    form->setSpacing(8);

    _artistEdit = new QLineEdit(meta.artist, this);
    _artistEdit->setPlaceholderText(tr("e.g. Happy Tunes"));
    form->addRow(tr("Artist [ar]:"), _artistEdit);

    _titleEdit = new QLineEdit(meta.title, this);
    _titleEdit->setPlaceholderText(tr("e.g. Saufen"));
    form->addRow(tr("Title [ti]:"), _titleEdit);

    _albumEdit = new QLineEdit(meta.album, this);
    _albumEdit->setPlaceholderText(tr("e.g. Greatest Hits"));
    form->addRow(tr("Album [al]:"), _albumEdit);

    _lyricsByEdit = new QLineEdit(meta.lyricsBy, this);
    _lyricsByEdit->setPlaceholderText(tr("e.g. John Doe"));
    form->addRow(tr("Lyrics by [by]:"), _lyricsByEdit);

    _offsetSpin = new QSpinBox(this);
    _offsetSpin->setRange(-30000, 30000);
    _offsetSpin->setValue(meta.offsetMs);
    _offsetSpin->setSuffix(tr(" ms"));
    _offsetSpin->setToolTip(tr("Positive = lyrics appear later, Negative = earlier"));
    form->addRow(tr("Offset [offset]:"), _offsetSpin);

    mainLayout->addLayout(form);

    QDialogButtonBox *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttons);
}

LyricMetadata LyricMetadataDialog::result() const
{
    LyricMetadata meta;
    meta.artist = _artistEdit->text().trimmed();
    meta.title = _titleEdit->text().trimmed();
    meta.album = _albumEdit->text().trimmed();
    meta.lyricsBy = _lyricsByEdit->text().trimmed();
    meta.offsetMs = _offsetSpin->value();
    return meta;
}
