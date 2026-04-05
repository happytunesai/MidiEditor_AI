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

#include "MidiVisualizerWidget.h"
#include "Appearance.h"
#include "../midi/MidiOutput.h"
#include "../midi/MidiPlayer.h"

#include <QPainter>
#include <QPaintEvent>
#include <QShowEvent>
#include <QHideEvent>
#include <cmath>

static const int BAR_COUNT = 16;
static const int BAR_WIDTH = 4;
static const int BAR_GAP = 2;
static const int WIDGET_HEIGHT = 24;
static const float DECAY_RATE = 0.82f;   // per-frame multiplier (lower = faster decay)
static const int REFRESH_MS = 33;        // ~30 fps

MidiVisualizerWidget::MidiVisualizerWidget(QWidget *parent)
    : QWidget(parent), _playing(false)
{
    for (int i = 0; i < BAR_COUNT; i++)
        _levels[i] = 0.0f;

    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    setMinimumSize(sizeHint());

    connect(&_timer, &QTimer::timeout, this, &MidiVisualizerWidget::refresh);
}

void MidiVisualizerWidget::playbackStarted()
{
    _playing = true;
    if (!_timer.isActive())
        _timer.start(REFRESH_MS);
}

void MidiVisualizerWidget::playbackStopped()
{
    _playing = false;
    // Let decay animation finish naturally — timer keeps running
    // until refresh() sees all levels decayed and widget is idle
    MidiOutput::resetChannelActivity();
}

void MidiVisualizerWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // Always start the timer when the widget becomes visible.
    // refresh() polls MidiPlayer::isPlaying() directly, so this
    // works even if the playbackStarted signal was missed.
    if (!_timer.isActive())
        _timer.start(REFRESH_MS);
}

void MidiVisualizerWidget::hideEvent(QHideEvent *event)
{
    _timer.stop();
    QWidget::hideEvent(event);
}

void MidiVisualizerWidget::refresh()
{
    // Poll playback state directly — don't rely solely on signal delivery
    _playing = MidiPlayer::isPlaying();

    bool anyActive = false;

    for (int ch = 0; ch < BAR_COUNT; ch++) {
        // Atomically read and clear — prevents race with player thread
        int raw = MidiOutput::channelActivity[ch].exchange(0, std::memory_order_relaxed);

        // Normalize velocity (0-127) to 0.0-1.0
        float target = raw / 127.0f;

        // Attack: jump up instantly. Decay: smooth falloff.
        if (target > _levels[ch]) {
            _levels[ch] = target;
        } else {
            _levels[ch] *= DECAY_RATE;
            if (_levels[ch] < 0.01f)
                _levels[ch] = 0.0f;
        }

        if (_levels[ch] > 0.0f)
            anyActive = true;
    }

    if (anyActive || _playing)
        update();
    // Timer keeps running while widget is visible — hideEvent() stops it.
    // This ensures we always detect new playback via polling, even when
    // signal connections are broken (MidiPlayer::play() recreates filePlayer).
}

void MidiVisualizerWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    bool dark = Appearance::shouldUseDarkMode();
    QColor bgColor     = dark ? QColor(0x0d, 0x11, 0x17) : QColor(0xf6, 0xf8, 0xfa);
    QColor idleColor   = dark ? QColor(0x48, 0x4f, 0x58) : QColor(0xb0, 0xb8, 0xc0);
    QColor borderColor = dark ? QColor(0x48, 0x4f, 0x58) : QColor(0xd0, 0xd7, 0xde);
    QColor barLow      = dark ? QColor(0x3f, 0xb9, 0x50) : QColor(0x1a, 0x7f, 0x37);
    QColor barHigh     = dark ? QColor(0x58, 0xa6, 0xff) : QColor(0x09, 0x69, 0xda);

    p.fillRect(rect(), bgColor);
    p.setPen(borderColor);
    p.drawRect(0, 0, width() - 1, height() - 1);

    int x = 2;
    int maxH = height() - 4;

    for (int ch = 0; ch < BAR_COUNT; ch++) {
        int barH = static_cast<int>(std::round(_levels[ch] * maxH));
        if (barH < 1 && _levels[ch] > 0.01f)
            barH = 1;

        if (barH > 0) {
            float t = _levels[ch];
            int r = barLow.red()   + static_cast<int>(t * (barHigh.red()   - barLow.red()));
            int g = barLow.green() + static_cast<int>(t * (barHigh.green() - barLow.green()));
            int b = barLow.blue()  + static_cast<int>(t * (barHigh.blue()  - barLow.blue()));
            p.fillRect(x, height() - 2 - barH, BAR_WIDTH, barH, QColor(r, g, b));
        } else {
            p.fillRect(x, height() - 4, BAR_WIDTH, 2, idleColor);
        }

        x += BAR_WIDTH + BAR_GAP;
    }
}

QSize MidiVisualizerWidget::sizeHint() const
{
    int w = BAR_COUNT * (BAR_WIDTH + BAR_GAP) + BAR_GAP;
    return QSize(w, WIDGET_HEIGHT);
}

QSize MidiVisualizerWidget::minimumSizeHint() const
{
    return sizeHint();
}
