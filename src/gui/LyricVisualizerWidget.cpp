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

#include "LyricVisualizerWidget.h"
#include "Appearance.h"
#include "../midi/MidiFile.h"
#include "../midi/MidiPlayer.h"
#include "../midi/LyricManager.h"
#include "../midi/LyricBlock.h"

#include <QPainter>
#include <QPaintEvent>
#include <QFontMetrics>
#include <cmath>

static const int MIN_WIDGET_WIDTH = 200;
static const int MAX_WIDGET_WIDTH = 600;
static const int WIDGET_HEIGHT = 42;
static const int H_PADDING = 16;         // horizontal padding inside the box
static const int REFRESH_MS = 33;        // ~30 fps
static const float FADE_SPEED = 0.08f;   // per-frame fade-in increment

LyricVisualizerWidget::LyricVisualizerWidget(QWidget *parent)
    : QWidget(parent), _playing(false)
{
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    setMinimumSize(minimumSizeHint());

    connect(&_timer, &QTimer::timeout, this, &LyricVisualizerWidget::refresh);
}

void LyricVisualizerWidget::setFile(MidiFile *file)
{
    // Disconnect old
    if (_file && _file->lyricManager()) {
        disconnect(_file->lyricManager(), &LyricManager::lyricsChanged,
                   this, &LyricVisualizerWidget::onLyricsChanged);
    }

    _file = file;
    _currentBlockIndex = -1;
    _currentText.clear();
    _nextText.clear();
    _phraseProgress = 0.0f;
    _lastMs = 0;
    // V131-P2-06: start faded-out so the first phrase after setFile() fades
    // in cleanly. Previously set to 1.0f which skipped the fade-in entirely
    // when playback began inside an existing block (seek+play case).
    _fadeIn = 0.0f;

    // Connect new
    if (_file && _file->lyricManager()) {
        connect(_file->lyricManager(), &LyricManager::lyricsChanged,
                this, &LyricVisualizerWidget::onLyricsChanged);
    }

    onLyricsChanged();
}

void LyricVisualizerWidget::onLyricsChanged()
{
    // Don't toggle visibility — QToolBar doesn't reliably relayout when a
    // widget hidden via setVisible(false) is later shown.  Instead, stay
    // visible and let paintEvent draw an idle state (♪ ♪ ♪) when there
    // are no lyrics, matching the MIDI Visualizer's always-visible pattern.
    _currentBlockIndex = -1;  // Reset stale index (P3-009)
    update();
}

void LyricVisualizerWidget::playbackStarted()
{
    _playing = true;
    if (!_timer.isActive())
        _timer.start(REFRESH_MS);
}

void LyricVisualizerWidget::playbackStopped()
{
    _playing = false;
    _currentBlockIndex = -1;
    _currentText.clear();
    _nextText.clear();
    _phraseProgress = 0.0f;
    _fadeIn = 1.0f;
    update();
}

void LyricVisualizerWidget::onPlaybackPositionChanged(int ms)
{
    _lastMs = ms;
    updateCurrentLyric(ms);
    // P3-008 residual: if the timer was stopped by the idle branch in refresh()
    // and a new playback position arrives (scrub, AI tool play toggle, etc.),
    // the widget would stay animation-dead until the next showEvent. Restart
    // defensively so the karaoke sweep resumes.
    if (!_timer.isActive())
        _timer.start(REFRESH_MS);
}

void LyricVisualizerWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (!_timer.isActive())
        _timer.start(REFRESH_MS);
}

void LyricVisualizerWidget::hideEvent(QHideEvent *event)
{
    _timer.stop();
    QWidget::hideEvent(event);
}

void LyricVisualizerWidget::refresh()
{
    _playing = MidiPlayer::isPlaying();

    // Advance fade-in
    if (_fadeIn < 1.0f) {
        _fadeIn = qMin(1.0f, _fadeIn + FADE_SPEED);
    }

    // Advance glow phase (full cycle ~3 seconds)
    _glowPhase += 0.02f;
    if (_glowPhase > 6.2831853f)
        _glowPhase -= 6.2831853f;

    if (_playing || _fadeIn < 1.0f) {
        update();
    } else {
        // Not playing and fully faded in — stop wasting CPU (P3-008)
        _timer.stop();
    }
}

void LyricVisualizerWidget::updateCurrentLyric(int ms)
{
    if (!_file || !_file->lyricManager())
        return;

    LyricManager *mgr = _file->lyricManager();
    int tick = _file->tick(ms);
    const auto &blocks = mgr->allBlocks();

    // Find the block containing this tick
    int newIndex = -1;
    for (int i = 0; i < blocks.size(); i++) {
        if (tick >= blocks[i].startTick && tick < blocks[i].endTick) {
            newIndex = i;
            break;
        }
    }

    // If between blocks, check if we're close to the next one (within 500ms)
    // to keep showing the previous phrase's tail
    if (newIndex == -1 && _currentBlockIndex >= 0) {
        // Keep showing the last block until its end
        if (_currentBlockIndex < blocks.size()) {
            int endMs = _file->msOfTick(blocks[_currentBlockIndex].endTick);
            if (ms < endMs + 200) {
                // Still in the "tail" of the last phrase
                _phraseProgress = 1.0f;
                return;
            }
        }
    }

    if (newIndex != _currentBlockIndex) {
        _currentBlockIndex = newIndex;
        _fadeIn = 0.0f;  // Trigger fade-in animation

        if (newIndex >= 0 && newIndex < blocks.size()) {
            _currentText = blocks[newIndex].text.trimmed();
            // Look ahead for next phrase
            if (newIndex + 1 < blocks.size()) {
                _nextText = blocks[newIndex + 1].text.trimmed();
            } else {
                _nextText.clear();
            }
        } else {
            _currentText.clear();
            _nextText.clear();
        }

        // Tell the toolbar to re-query our sizeHint for the new text
        updateGeometry();
    }

    // Calculate progress within current block
    if (newIndex >= 0 && newIndex < blocks.size()) {
        int startMs = _file->msOfTick(blocks[newIndex].startTick);
        int endMs = _file->msOfTick(blocks[newIndex].endTick);
        int duration = endMs - startMs;
        if (duration > 0) {
            _phraseProgress = qBound(0.0f, static_cast<float>(ms - startMs) / duration, 1.0f);
        } else {
            _phraseProgress = 1.0f;
        }
    } else {
        _phraseProgress = 0.0f;
    }
}

void LyricVisualizerWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    bool dark = Appearance::shouldUseDarkMode();

    // Colors
    QColor bgColor       = dark ? QColor(0x0d, 0x11, 0x17) : QColor(0xf6, 0xf8, 0xfa);
    QColor borderColor   = dark ? QColor(0x48, 0x4f, 0x58) : QColor(0xd0, 0xd7, 0xde);
    QColor textColor     = dark ? QColor(0xe6, 0xea, 0xf0) : QColor(0x24, 0x29, 0x2f);
    QColor dimColor      = dark ? QColor(0x8b, 0x94, 0x9e) : QColor(0x65, 0x6d, 0x76);
    QColor idleColor     = dark ? QColor(0x48, 0x4f, 0x58) : QColor(0xb0, 0xb8, 0xc0);

    // Use lyric color for the karaoke highlight
    QColor highlightColor;
    if (Appearance::useFixedLyricColor()) {
        highlightColor = Appearance::fixedLyricColor();
    } else {
        highlightColor = dark ? QColor(0x58, 0xa6, 0xff) : QColor(0x09, 0x69, 0xda);
    }

    // Background with rounded corners
    p.setPen(borderColor);
    p.setBrush(bgColor);
    p.drawRoundedRect(QRectF(0.5, 0.5, width() - 1, height() - 1), 4, 4);

    // No file or no lyrics — idle state
    if (!_file || !_file->lyricManager() || !_file->lyricManager()->hasLyrics()) {
        QFont idleFont = font();
        idleFont.setPointSize(10);
        p.setFont(idleFont);
        p.setPen(idleColor);
        p.drawText(rect(), Qt::AlignCenter, QString::fromUtf8("\xe2\x99\xaa \xe2\x99\xaa \xe2\x99\xaa"));
        return;
    }

    // Not playing — show title or idle
    if (!_playing && _currentText.isEmpty()) {
        LyricManager *mgr = _file->lyricManager();
        QString idleText;
        if (mgr->hasMetadata() && !mgr->metadata().title.isEmpty()) {
            idleText = QString::fromUtf8("\xe2\x99\xaa ") + mgr->metadata().title + QString::fromUtf8(" \xe2\x99\xaa");
        } else {
            idleText = QString::fromUtf8("\xe2\x99\xaa \xe2\x99\xaa \xe2\x99\xaa");
        }
        QFont idleFont = font();
        idleFont.setPointSize(10);
        p.setFont(idleFont);
        p.setPen(dimColor);
        p.drawText(rect(), Qt::AlignCenter, idleText);
        return;
    }

    int currentLineY = 3;
    int nextLineY = height() / 2 + 4;
    int textAreaW = width() - H_PADDING;

    // === Current phrase (large, with karaoke sweep) ===
    if (!_currentText.isEmpty()) {
        QFont mainFont = font();
        mainFont.setPointSizeF(12.0);
        mainFont.setBold(true);
        p.setFont(mainFont);

        QFontMetrics fm(mainFont);
        // Elide if text is wider than available area
        QString displayText = fm.elidedText(_currentText, Qt::ElideRight, textAreaW);
        int textW = fm.horizontalAdvance(displayText);
        int textX = (width() - textW) / 2;
        int textY = currentLineY + fm.ascent();

        // Apply fade-in opacity
        float alpha = qBound(0.0f, _fadeIn, 1.0f);

        // Draw the "already sung" part (highlighted)
        if (_phraseProgress > 0.0f && _playing) {
            // Calculate the character split point based on progress
            int splitPixel = static_cast<int>(_phraseProgress * textW);

            // Sung part: highlight color
            QColor sungColor = highlightColor;
            sungColor.setAlphaF(alpha);
            p.setPen(sungColor);
            p.setClipRect(textX, 0, splitPixel, height());
            p.drawText(textX, textY, displayText);

            // Unsong part: normal text color
            QColor unsungColor = textColor;
            unsungColor.setAlphaF(alpha);
            p.setPen(unsungColor);
            p.setClipRect(textX + splitPixel, 0, textW - splitPixel + 1, height());
            p.drawText(textX, textY, displayText);

            p.setClipping(false);

            // Subtle glow behind current character position
            float glowAlpha = 0.15f + 0.05f * std::sin(_glowPhase);
            QColor glow = highlightColor;
            glow.setAlphaF(glowAlpha * alpha);
            int glowX = textX + splitPixel;
            p.fillRect(QRectF(glowX - 3, currentLineY, 6, fm.height()), glow);
        } else {
            // Not playing or no progress — static text
            QColor staticColor = textColor;
            staticColor.setAlphaF(alpha);
            p.setPen(staticColor);
            p.drawText(textX, textY, displayText);
        }

        // Progress bar at the bottom (thin 2px line)
        if (_playing && _phraseProgress > 0.0f) {
            QColor barColor = highlightColor;
            barColor.setAlphaF(0.6f * alpha);
            int barW = static_cast<int>(_phraseProgress * (width() - 8));
            p.fillRect(4, height() - 3, barW, 2, barColor);
        }
    }

    // === Next phrase (small, dimmed preview) ===
    if (!_nextText.isEmpty()) {
        QFont previewFont = font();
        previewFont.setPointSizeF(8.5);
        p.setFont(previewFont);

        QFontMetrics fm(previewFont);
        QString elidedNext = fm.elidedText(_nextText, Qt::ElideRight, textAreaW);
        int textW = fm.horizontalAdvance(elidedNext);
        int textX = (width() - textW) / 2;
        int textY = nextLineY + fm.ascent();

        QColor previewColor = dimColor;
        float alpha = qBound(0.0f, _fadeIn, 1.0f);
        previewColor.setAlphaF(0.6f * alpha);
        p.setPen(previewColor);
        p.drawText(textX, textY, elidedNext);
    }
}

QSize LyricVisualizerWidget::sizeHint() const
{
    // Dynamically size based on the current phrase text
    if (!_currentText.isEmpty()) {
        QFont mainFont = font();
        mainFont.setPointSizeF(12.0);
        mainFont.setBold(true);
        QFontMetrics fm(mainFont);
        int textW = fm.horizontalAdvance(_currentText) + H_PADDING;
        int w = qBound(MIN_WIDGET_WIDTH, textW, MAX_WIDGET_WIDTH);
        return QSize(w, WIDGET_HEIGHT);
    }
    return QSize(MIN_WIDGET_WIDTH, WIDGET_HEIGHT);
}

QSize LyricVisualizerWidget::minimumSizeHint() const
{
    return QSize(MIN_WIDGET_WIDTH, WIDGET_HEIGHT);
}
