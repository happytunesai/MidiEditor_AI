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

#include "LyricSyncDialog.h"
#include "Appearance.h"
#include "MainWindow.h"
#include "MatrixWidget.h"
#include "OpenGLMatrixWidget.h"

#include "../midi/LyricBlock.h"
#include "../midi/LyricManager.h"
#include "../midi/MidiFile.h"
#include "../midi/MidiPlayer.h"
#include "../midi/PlayerThread.h"
#include "../protocol/Protocol.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

// === SyncTimelineWidget implementation ===

SyncTimelineWidget::SyncTimelineWidget(QWidget *parent)
    : QWidget(parent), _durationMs(1), _positionMs(0)
{
    setMinimumHeight(28);
    setMaximumHeight(28);
    setCursor(Qt::PointingHandCursor);
    setMouseTracking(true);
}

void SyncTimelineWidget::setDuration(int durationMs) {
    _durationMs = qMax(1, durationMs);
    update();
}

void SyncTimelineWidget::setPosition(int ms) {
    _positionMs = ms;
    update();
}

void SyncTimelineWidget::addMarker(int startMs, int endMs) {
    _markers.append(qMakePair(startMs, endMs));
    update();
}

void SyncTimelineWidget::removeLastMarker() {
    if (!_markers.isEmpty()) {
        _markers.removeLast();
        update();
    }
}

void SyncTimelineWidget::clearMarkers() {
    _markers.clear();
    update();
}

void SyncTimelineWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    int w = width();
    int h = height();

    // Background track
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(60, 60, 60));
    p.drawRoundedRect(0, 0, w, h, 4, 4);

    // Progress fill
    if (_durationMs > 0 && _positionMs > 0) {
        int fillW = qMin(w, (int)((qint64)_positionMs * w / _durationMs));
        p.setBrush(QColor(100, 100, 120));
        p.drawRoundedRect(0, 0, fillW, h, 4, 4);
    }

    // Sync markers (colored bands)
    for (int i = 0; i < _markers.size(); i++) {
        int x1 = (int)((qint64)_markers[i].first * w / _durationMs);
        int x2 = (int)((qint64)_markers[i].second * w / _durationMs);
        int mw = qMax(3, x2 - x1);

        // Alternate colors: green/teal for visual variety
        QColor markerColor = (i % 2 == 0) ? QColor(76, 175, 80, 200) : QColor(0, 188, 212, 200);
        p.setBrush(markerColor);
        p.drawRect(x1, 2, mw, h - 4);
    }

    // Playback cursor
    if (_durationMs > 0 && _positionMs > 0) {
        int cx = (int)((qint64)_positionMs * w / _durationMs);
        p.setPen(QPen(Qt::white, 2));
        p.drawLine(cx, 0, cx, h);
    }

    // Border
    p.setPen(QColor(120, 120, 120));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(0, 0, w - 1, h - 1, 4, 4);
}

void SyncTimelineWidget::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && _durationMs > 0) {
        int ms = (int)((qint64)event->pos().x() * _durationMs / width());
        ms = qBound(0, ms, _durationMs);
        emit seekRequested(ms);
        event->accept();
    }
}

// === LyricSyncDialog implementation ===

LyricSyncDialog::LyricSyncDialog(MidiFile *file, QWidget *parent)
    : QDialog(parent),
      _file(file),
      _lyricManager(file ? file->lyricManager() : nullptr),
      _matrixWidgetContainer(nullptr),
      _currentPhraseIndex(0),
      _totalPhrases(0),
      _spaceHeld(false),
      _phraseStartMs(0),
      _isPlaying(false),
      _currentMs(0),
      _fileDurationMs(0),
      _savedSmoothScroll(false)
{
    setWindowTitle(tr("Sync Lyrics"));
    setMinimumSize(550, 520);
    resize(600, 560);
    setFocusPolicy(Qt::StrongFocus);

    // Force smooth scrolling on while syncing, restore on close
    _savedSmoothScroll = Appearance::smoothPlaybackScrolling();
    Appearance::setSmoothPlaybackScrolling(true);

    // Get the matrix widget container for background scrolling
    MainWindow *mw = qobject_cast<MainWindow *>(parent);
    if (mw) {
        _matrixWidgetContainer = mw->matrixWidgetContainer();
    }

    if (_file) {
        _fileDurationMs = _file->msOfTick(_file->endTick());
    }
    if (_lyricManager) {
        _totalPhrases = _lyricManager->count();
    }

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(12);

    // Playback time and progress
    QHBoxLayout *topLayout = new QHBoxLayout();
    _timeLabel = new QLabel(tr("Stopped: 00:00 / %1").arg(formatTime(_fileDurationMs)), this);
    _timeLabel->setStyleSheet("font-size: 14px;");
    topLayout->addWidget(_timeLabel);
    topLayout->addStretch();
    mainLayout->addLayout(topLayout);

    // Custom clickable timeline with sync markers
    _timeline = new SyncTimelineWidget(this);
    _timeline->setDuration(_fileDurationMs);
    mainLayout->addWidget(_timeline);
    connect(_timeline, &SyncTimelineWidget::seekRequested, this, &LyricSyncDialog::onSeek);

    // Current phrase display
    QLabel *currentLabel = new QLabel(tr("Current phrase (%1 / %2):").arg(1).arg(_totalPhrases), this);
    currentLabel->setObjectName("currentPhraseHeader");
    mainLayout->addWidget(currentLabel);

    _currentPhraseLabel = new QLabel(this);
    _currentPhraseLabel->setAlignment(Qt::AlignCenter);
    _currentPhraseLabel->setWordWrap(true);
    _currentPhraseLabel->setMinimumHeight(60);
    _currentPhraseLabel->setStyleSheet(
        "QLabel { font-size: 18px; font-weight: bold; padding: 12px; "
        "border: 2px solid palette(mid); border-radius: 6px; "
        "background: palette(base); }");
    mainLayout->addWidget(_currentPhraseLabel);

    // Next phrases (teleprompter view)
    QLabel *upcomingLabel = new QLabel(tr("Upcoming:"), this);
    upcomingLabel->setStyleSheet("color: gray; font-size: 12px;");
    mainLayout->addWidget(upcomingLabel);

    _teleprompterList = new QListWidget(this);
    _teleprompterList->setFocusPolicy(Qt::NoFocus);
    _teleprompterList->setSelectionMode(QAbstractItemView::NoSelection);
    _teleprompterList->setMinimumHeight(120);
    _teleprompterList->setMaximumHeight(180);
    _teleprompterList->setStyleSheet(
        "QListWidget { font-size: 13px; border: 1px solid palette(mid); "
        "border-radius: 4px; background: palette(base); }"
        "QListWidget::item { padding: 3px 6px; }");
    mainLayout->addWidget(_teleprompterList);

    // Instruction
    QFrame *instructionFrame = new QFrame(this);
    instructionFrame->setFrameStyle(QFrame::StyledPanel | QFrame::Raised);
    QVBoxLayout *instrLayout = new QVBoxLayout(instructionFrame);
    _instructionLabel = new QLabel(
        tr("HOLD [Space] while the phrase is being sung\n"
           "Press = phrase starts    Release = phrase ends"), this);
    _instructionLabel->setAlignment(Qt::AlignCenter);
    _instructionLabel->setStyleSheet("font-size: 13px; padding: 8px;");
    instrLayout->addWidget(_instructionLabel);
    mainLayout->addWidget(instructionFrame);

    // Synced count and scroll toggle
    QHBoxLayout *statusLayout = new QHBoxLayout();
    _syncedCountLabel = new QLabel(tr("Synced: 0 / %1 phrases").arg(_totalPhrases), this);
    _syncedCountLabel->setStyleSheet("font-weight: bold; font-size: 13px;");
    statusLayout->addWidget(_syncedCountLabel);
    statusLayout->addStretch();
    _scrollCheck = new QCheckBox(tr("Scroll editor"), this);
    _scrollCheck->setChecked(true);
    _scrollCheck->setFocusPolicy(Qt::NoFocus);
    _scrollCheck->setToolTip(tr("Scroll the background editor during playback"));
    statusLayout->addWidget(_scrollCheck);
    mainLayout->addLayout(statusLayout);

    // Buttons — disable autoDefault so Space/Enter never activate them
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    _rewindBtn = new QPushButton(tr("Rewind 5s"), this);
    _playBtn = new QPushButton(tr("Play"), this);
    _stopBtn = new QPushButton(tr("Stop"), this);
    _stopBtn->setEnabled(false);
    _undoLastBtn = new QPushButton(tr("Undo Last"), this);
    _undoLastBtn->setEnabled(false);
    _doneBtn = new QPushButton(tr("Done"), this);
    QPushButton *cancelBtn = new QPushButton(tr("Cancel"), this);

    // Prevent Space/Enter from triggering focused buttons
    for (QPushButton *btn : {_rewindBtn, _playBtn, _stopBtn, _undoLastBtn, _doneBtn, cancelBtn}) {
        btn->setAutoDefault(false);
        btn->setDefault(false);
        btn->setFocusPolicy(Qt::NoFocus);
    }

    buttonLayout->addWidget(_rewindBtn);
    buttonLayout->addWidget(_playBtn);
    buttonLayout->addWidget(_stopBtn);
    buttonLayout->addWidget(_undoLastBtn);
    buttonLayout->addStretch();
    buttonLayout->addWidget(_doneBtn);
    buttonLayout->addWidget(cancelBtn);
    mainLayout->addLayout(buttonLayout);

    // Connections
    connect(_playBtn, &QPushButton::clicked, this, &LyricSyncDialog::onStartPlayback);
    connect(_stopBtn, &QPushButton::clicked, this, &LyricSyncDialog::onPausePlayback);
    connect(_rewindBtn, &QPushButton::clicked, this, &LyricSyncDialog::onRewind);
    connect(_undoLastBtn, &QPushButton::clicked, this, &LyricSyncDialog::onUndoLast);
    connect(_doneBtn, &QPushButton::clicked, this, &LyricSyncDialog::onDone);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(_scrollCheck, &QCheckBox::toggled, this, &LyricSyncDialog::onScrollToggled);

    updateDisplay();
}

LyricSyncDialog::~LyricSyncDialog() {
    stopPlaybackIfNeeded();
    // Restore the user's smooth scrolling preference
    Appearance::setSmoothPlaybackScrolling(_savedSmoothScroll);
}

bool LyricSyncDialog::event(QEvent *e) {
    // Intercept ShortcutOverride so the global Space shortcut (play/stop)
    // does not fire while this dialog has focus.
    // Also block Enter/Return to prevent button activation.
    if (e->type() == QEvent::ShortcutOverride) {
        QKeyEvent *ke = static_cast<QKeyEvent *>(e);
        if (ke->key() == Qt::Key_Space ||
            ke->key() == Qt::Key_Return ||
            ke->key() == Qt::Key_Enter) {
            e->accept();
            return true;
        }
    }
    return QDialog::event(e);
}

void LyricSyncDialog::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        // Always consume Space in this dialog
        event->accept();

        if (!_isPlaying || _spaceHeld)
            return;

        if (_currentPhraseIndex >= _totalPhrases)
            return;

        _spaceHeld = true;
        _phraseStartMs = _currentMs;

        // Visual feedback
        _currentPhraseLabel->setStyleSheet(
            "QLabel { font-size: 18px; font-weight: bold; padding: 12px; "
            "border: 2px solid #4CAF50; border-radius: 6px; "
            "background: #E8F5E9; color: #2E7D32; }");

        return;
    }
    QDialog::keyPressEvent(event);
}

void LyricSyncDialog::keyReleaseEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        // Always consume Space in this dialog
        event->accept();

        if (!_spaceHeld)
            return;

        _spaceHeld = false;
        int phraseEndMs = _currentMs;

        // Enforce minimum duration of 100ms
        if (phraseEndMs - _phraseStartMs < 100)
            phraseEndMs = _phraseStartMs + 100;

        // Record timing
        _recordedTimings.append(qMakePair(_phraseStartMs, phraseEndMs));

        // Reset visual
        _currentPhraseLabel->setStyleSheet(
            "QLabel { font-size: 18px; font-weight: bold; padding: 12px; "
            "border: 2px solid palette(mid); border-radius: 6px; "
            "background: palette(base); }");

        advanceToNextPhrase();
        return;
    }
    QDialog::keyReleaseEvent(event);
}

void LyricSyncDialog::closeEvent(QCloseEvent *event) {
    stopPlaybackIfNeeded();
    QDialog::closeEvent(event);
}

void LyricSyncDialog::onPlaybackPositionChanged(int ms) {
    _currentMs = ms;
    _timeLabel->setText(tr("Playing: %1 / %2").arg(formatTime(ms)).arg(formatTime(_fileDurationMs)));
    _timeline->setPosition(ms);
}

void LyricSyncDialog::onPlaybackStopped() {
    disconnectBackgroundScroll();
    _isPlaying = false;
    _playBtn->setEnabled(true);
    _stopBtn->setEnabled(false);
    _timeLabel->setText(tr("Stopped: %1 / %2").arg(formatTime(_currentMs)).arg(formatTime(_fileDurationMs)));

    // If space was held when playback stopped, end the phrase
    if (_spaceHeld) {
        _spaceHeld = false;
        int phraseEndMs = _currentMs;
        if (phraseEndMs - _phraseStartMs < 100)
            phraseEndMs = _phraseStartMs + 100;
        _recordedTimings.append(qMakePair(_phraseStartMs, phraseEndMs));
        _currentPhraseLabel->setStyleSheet(
            "QLabel { font-size: 18px; font-weight: bold; padding: 12px; "
            "border: 2px solid palette(mid); border-radius: 6px; "
            "background: palette(base); }");
        advanceToNextPhrase();
    }
}

void LyricSyncDialog::onStartPlayback() {
    if (!_file || _isPlaying)
        return;

    // Sync background editor to current position before playback starts
    if (_matrixWidgetContainer && _scrollCheck->isChecked()) {
        QMetaObject::invokeMethod(_matrixWidgetContainer, "timeMsChanged",
                                  Q_ARG(int, _currentMs));
    }

    MidiPlayer::play(_file);
    PlayerThread *pt = MidiPlayer::playerThread();
    if (pt) {
        connect(pt, &PlayerThread::timeMsChanged, this, &LyricSyncDialog::onPlaybackPositionChanged, Qt::UniqueConnection);
        connect(pt, &PlayerThread::playerStopped, this, &LyricSyncDialog::onPlaybackStopped, Qt::UniqueConnection);
    }
    connectBackgroundScroll();
    _isPlaying = true;
    _playBtn->setEnabled(false);
    _stopBtn->setEnabled(true);
}

void LyricSyncDialog::onPausePlayback() {
    if (!_file || !_isPlaying)
        return;

    _file->setPauseTick(_file->tick(_currentMs));
    disconnectBackgroundScroll();
    MidiPlayer::stop();
    _isPlaying = false;
    _playBtn->setEnabled(true);
    _stopBtn->setEnabled(false);
}

void LyricSyncDialog::onSeek(int ms) {
    if (!_file)
        return;

    int targetMs = qBound(0, ms, _fileDurationMs);
    int targetTick = _file->tick(targetMs);

    bool wasPlaying = _isPlaying;
    if (wasPlaying) {
        disconnectBackgroundScroll();
        MidiPlayer::stop();
        _isPlaying = false;
    }

    _file->setPauseTick(targetTick);
    _currentMs = targetMs;
    _timeLabel->setText(tr("Stopped: %1 / %2").arg(formatTime(_currentMs)).arg(formatTime(_fileDurationMs)));
    _timeline->setPosition(_currentMs);

    // Update background editor scroll position to match
    if (_matrixWidgetContainer && _scrollCheck->isChecked()) {
        QMetaObject::invokeMethod(_matrixWidgetContainer, "timeMsChanged",
                                  Q_ARG(int, targetMs));
    }

    if (wasPlaying) {
        onStartPlayback();
    }
}

void LyricSyncDialog::onRewind() {
    onSeek(_currentMs - 5000);
}

void LyricSyncDialog::onUndoLast() {
    if (_recordedTimings.isEmpty() || _currentPhraseIndex <= 0)
        return;

    _recordedTimings.removeLast();
    _timeline->removeLastMarker();
    _currentPhraseIndex--;
    _undoLastBtn->setEnabled(!_recordedTimings.isEmpty());

    updateDisplay();
}

void LyricSyncDialog::onDone() {
    stopPlaybackIfNeeded();

    if (_recordedTimings.isEmpty()) {
        reject();
        return;
    }

    // Apply recorded timings to LyricManager blocks
    if (_lyricManager && _file && _file->protocol()) {
        _file->protocol()->startNewAction(tr("Sync Lyrics"));

        for (int i = 0; i < _recordedTimings.size() && i < _totalPhrases; i++) {
            int startMs = _recordedTimings[i].first;
            int endMs = _recordedTimings[i].second;

            int startTick = _file->tick(startMs);
            int endTick = _file->tick(endMs);

            // Use Direct methods to avoid nested Protocol actions (LYRIC-009)
            // and to avoid re-sorting which invalidates indices (LYRIC-008)
            _lyricManager->moveBlockDirect(i, startTick);
            _lyricManager->resizeBlockDirect(i, endTick);
        }

        // Re-sort blocks in case timings were applied out of order (P3-005)
        _lyricManager->sortBlocks();

        _file->protocol()->endAction();
    }

    accept();
}

void LyricSyncDialog::updateDisplay() {
    // Update current phrase header
    QLabel *header = findChild<QLabel *>("currentPhraseHeader");
    if (header)
        header->setText(tr("Current phrase (%1 / %2):").arg(_currentPhraseIndex + 1).arg(_totalPhrases));

    // Update current phrase text
    if (_lyricManager && _currentPhraseIndex < _totalPhrases) {
        LyricBlock block = _lyricManager->blockAt(_currentPhraseIndex);
        _currentPhraseLabel->setText(QString("\"%1\"").arg(block.text));
    } else if (_currentPhraseIndex >= _totalPhrases) {
        _currentPhraseLabel->setText(tr("All phrases synced!"));
    } else {
        _currentPhraseLabel->setText(tr("No lyrics available"));
    }

    // Update teleprompter - show next 8 phrases
    _teleprompterList->clear();
    if (_lyricManager) {
        int startIdx = _currentPhraseIndex + 1;
        int endIdx = qMin(startIdx + 8, _totalPhrases);
        for (int i = startIdx; i < endIdx; i++) {
            LyricBlock block = _lyricManager->blockAt(i);
            QString prefix = QString("[%1] ").arg(i + 1);
            _teleprompterList->addItem(prefix + block.text);
        }
        if (endIdx >= _totalPhrases && startIdx < _totalPhrases) {
            _teleprompterList->addItem(tr("--- end of lyrics ---"));
        }
    }

    // Update synced count
    int synced = _recordedTimings.size();
    _syncedCountLabel->setText(tr("Synced: %1 / %2 phrases").arg(synced).arg(_totalPhrases));
    _undoLastBtn->setEnabled(synced > 0);
}

void LyricSyncDialog::stopPlaybackIfNeeded() {
    if (_isPlaying) {
        disconnectBackgroundScroll();
        MidiPlayer::stop();
        _isPlaying = false;
    }
}

void LyricSyncDialog::connectBackgroundScroll() {
    if (!_scrollCheck->isChecked())
        return;
    Appearance::setSmoothPlaybackScrolling(true);
    PlayerThread *pt = MidiPlayer::playerThread();
    if (pt && _matrixWidgetContainer) {
        connect(pt, SIGNAL(timeMsChanged(int)), _matrixWidgetContainer, SLOT(timeMsChanged(int)), Qt::UniqueConnection);
    }
}

void LyricSyncDialog::disconnectBackgroundScroll() {
    PlayerThread *pt = MidiPlayer::playerThread();
    if (pt && _matrixWidgetContainer) {
        disconnect(pt, SIGNAL(timeMsChanged(int)), _matrixWidgetContainer, SLOT(timeMsChanged(int)));
    }
}

void LyricSyncDialog::onScrollToggled(bool checked) {
    if (checked) {
        Appearance::setSmoothPlaybackScrolling(true);
        if (_isPlaying)
            connectBackgroundScroll();
    } else {
        disconnectBackgroundScroll();
        Appearance::setSmoothPlaybackScrolling(_savedSmoothScroll);
    }
}

void LyricSyncDialog::advanceToNextPhrase() {
    // Add marker on the timeline for this synced phrase
    if (!_recordedTimings.isEmpty()) {
        auto &last = _recordedTimings.last();
        _timeline->addMarker(last.first, last.second);
    }

    _currentPhraseIndex++;
    updateDisplay();

    // If all phrases synced, notify user
    if (_currentPhraseIndex >= _totalPhrases) {
        _instructionLabel->setText(tr("All phrases synced! Press Done to apply timings."));
    }
}

QString LyricSyncDialog::formatTime(int ms) const {
    int totalSec = ms / 1000;
    int min = totalSec / 60;
    int sec = totalSec % 60;
    return QString("%1:%2").arg(min, 2, 10, QChar('0')).arg(sec, 2, 10, QChar('0'));
}
