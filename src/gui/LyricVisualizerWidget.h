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

#ifndef LYRICVISUALIZERWIDGET_H
#define LYRICVISUALIZERWIDGET_H

#include <QWidget>
#include <QTimer>
#include <QString>

class MidiFile;

/**
 * \class LyricVisualizerWidget
 *
 * \brief A compact toolbar widget that displays the current lyric phrase
 *        during playback with a karaoke-style highlight animation.
 *
 * Two-line display: current phrase (large, with sweep highlight) and
 * next phrase (small, dimmed preview). Auto-hides when no lyrics exist.
 */
class LyricVisualizerWidget : public QWidget {
    Q_OBJECT

public:
    explicit LyricVisualizerWidget(QWidget *parent = nullptr);

    void setFile(MidiFile *file);

public slots:
    void playbackStarted();
    void playbackStopped();
    void onPlaybackPositionChanged(int ms);
    void onLyricsChanged();

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

private slots:
    void refresh();

private:
    void updateCurrentLyric(int ms);

    MidiFile *_file = nullptr;
    QTimer _timer;
    bool _playing = false;

    // Current lyric state
    int _currentBlockIndex = -1;
    QString _currentText;
    QString _nextText;
    float _phraseProgress = 0.0f;
    int _lastMs = 0;

    // Animation
    float _fadeIn = 1.0f;
    float _glowPhase = 0.0f;
};

#endif // LYRICVISUALIZERWIDGET_H
