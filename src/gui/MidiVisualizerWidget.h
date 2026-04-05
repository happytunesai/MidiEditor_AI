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

#ifndef MIDIVISUALIZERWIDGET_H
#define MIDIVISUALIZERWIDGET_H

#include <QWidget>
#include <QTimer>

class MidiVisualizerWidget : public QWidget {
    Q_OBJECT

public:
    explicit MidiVisualizerWidget(QWidget *parent = nullptr);

public slots:
    void playbackStarted();
    void playbackStopped();

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

private slots:
    void refresh();

private:
    QTimer _timer;
    float _levels[16];
    bool _playing;
};

#endif // MIDIVISUALIZERWIDGET_H
