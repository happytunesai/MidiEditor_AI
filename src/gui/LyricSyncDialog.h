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

#ifndef LYRICSYNCDIALOG_H
#define LYRICSYNCDIALOG_H

#include <QDialog>
#include <QList>
#include <QPair>
#include <QWidget>

class QLabel;
class QListWidget;
class QCheckBox;
class QPushButton;
class MidiFile;
class LyricManager;
class QWidget;

// Custom clickable timeline widget with sync markers
class SyncTimelineWidget : public QWidget {
    Q_OBJECT
public:
    explicit SyncTimelineWidget(QWidget *parent = nullptr);
    void setDuration(int durationMs);
    void setPosition(int ms);
    void addMarker(int startMs, int endMs);
    void removeLastMarker();
    void clearMarkers();

signals:
    void seekRequested(int ms);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    int _durationMs;
    int _positionMs;
    QList<QPair<int, int>> _markers; // start/end pairs
};

/**
 * \class LyricSyncDialog
 *
 * \brief Dialog for tap-to-sync lyric timing during playback.
 *
 * The song plays back and the user holds Space while a phrase is being sung.
 * Press = phrase starts, Release = phrase ends. Timestamps are captured
 * in real time and assigned to LyricBlocks.
 */
class LyricSyncDialog : public QDialog {
    Q_OBJECT

public:
    explicit LyricSyncDialog(MidiFile *file, QWidget *parent = nullptr);
    ~LyricSyncDialog() override;

protected:
    bool event(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onPlaybackPositionChanged(int ms);
    void onPlaybackStopped();
    void onStartPlayback();
    void onPausePlayback();
    void onSeek(int ms);
    void onRewind();
    void onUndoLast();
    void onDone();
    void onScrollToggled(bool checked);

private:
    void updateDisplay();
    void stopPlaybackIfNeeded();
    void advanceToNextPhrase();
    void connectBackgroundScroll();
    void disconnectBackgroundScroll();
    QString formatTime(int ms) const;

    bool _savedSmoothScroll;

    MidiFile *_file;
    LyricManager *_lyricManager;
    QWidget *_matrixWidgetContainer;

    int _currentPhraseIndex;
    int _totalPhrases;
    bool _spaceHeld;
    int _phraseStartMs;

    // Recorded timings: pairs of (startMs, endMs) for each phrase
    QList<QPair<int, int>> _recordedTimings;

    // UI elements
    QLabel *_timeLabel;
    SyncTimelineWidget *_timeline;
    QLabel *_currentPhraseLabel;
    QListWidget *_teleprompterList;
    QLabel *_instructionLabel;
    QLabel *_syncedCountLabel;
    QPushButton *_playBtn;
    QPushButton *_stopBtn;
    QPushButton *_rewindBtn;
    QPushButton *_undoLastBtn;
    QPushButton *_doneBtn;
    QCheckBox *_scrollCheck;

    bool _isPlaying;
    int _currentMs;
    int _fileDurationMs;
};

#endif // LYRICSYNCDIALOG_H
