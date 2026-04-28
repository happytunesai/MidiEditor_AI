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

#ifndef FFXIVTOGGLEWIDGET_H
#define FFXIVTOGGLEWIDGET_H

#include <QPixmap>
#include <QWidget>

/**
 * \class FfxivToggleWidget
 *
 * \brief Compact toolbar button that shows the FFXIV SoundFont Mode state and toggles it on click.
 *
 * Full-color icon when FFXIV SoundFont Mode is active, dimmed icon when off.
 * Stays in sync with the FluidSynthEngine via its ffxivSoundFontModeChanged signal,
 * so flipping the checkbox in the MIDI Settings dialog updates the toolbar button too.
 */
class FfxivToggleWidget : public QWidget {
    Q_OBJECT

public:
    explicit FfxivToggleWidget(QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

private slots:
    void onModeChanged(bool enabled);

private:
    void updateTooltip(bool enabled);

    QPixmap _iconOn;
    QPixmap _iconOff;
    bool _hovered = false;
};

#endif // FFXIVTOGGLEWIDGET_H
