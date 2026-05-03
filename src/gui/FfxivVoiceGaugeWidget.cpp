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

#include "FfxivVoiceGaugeWidget.h"

#include "Appearance.h"
#include "../ai/FfxivVoiceAnalyzer.h"
#include "../midi/MidiFile.h"
#include "../midi/MidiPlayer.h"

#ifdef FLUIDSYNTH_SUPPORT
#include "../midi/FluidSynthEngine.h"
#endif

#include <QPainter>
#include <QPaintEvent>
#include <QShowEvent>
#include <QHideEvent>

static const int WIDGET_HEIGHT = 26;
static const int WIDGET_WIDTH  = 180;
static const int REFRESH_MS    = 50; // ~20 Hz; cheap

// Stereo-style LED meter constants
static const int kMeterSegments = 24;   // total LED blocks (covers up to red zone)
static const int kSegGreenMax   = 18;   // segments 1..18 = green band
static const int kSegYellowMax  = 23;   // segments 19..23 = yellow band; 24+ = red

FfxivVoiceGaugeWidget::FfxivVoiceGaugeWidget(QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    setMinimumSize(sizeHint());
    setToolTip(tr("FFXIV voices: 0 / %1").arg(FfxivVoiceAnalyzer::kVoiceCeiling));

    connect(&_timer, &QTimer::timeout, this, &FfxivVoiceGaugeWidget::refresh);

    connect(FfxivVoiceAnalyzer::instance(), &FfxivVoiceAnalyzer::analysisUpdated,
            this, &FfxivVoiceGaugeWidget::onAnalysisUpdated);

#ifdef FLUIDSYNTH_SUPPORT
    if (FluidSynthEngine *engine = FluidSynthEngine::instance()) {
        _ffxivOn = engine->ffxivSoundFontMode();
        connect(engine, &FluidSynthEngine::ffxivSoundFontModeChanged,
                this, &FfxivVoiceGaugeWidget::onFfxivModeChanged);
    }
#endif

    updateVisibility();
}

void FfxivVoiceGaugeWidget::setFile(MidiFile *file)
{
    _file = file;
    if (file)
        FfxivVoiceAnalyzer::instance()->watchFile(file);
    refresh();
}

QSize FfxivVoiceGaugeWidget::sizeHint() const
{
    return QSize(WIDGET_WIDTH, WIDGET_HEIGHT);
}

QSize FfxivVoiceGaugeWidget::minimumSizeHint() const
{
    return sizeHint();
}

void FfxivVoiceGaugeWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (!_timer.isActive())
        _timer.start(REFRESH_MS);
}

void FfxivVoiceGaugeWidget::hideEvent(QHideEvent *event)
{
    _timer.stop();
    QWidget::hideEvent(event);
}

void FfxivVoiceGaugeWidget::refresh()
{
    int v = currentVoiceCount();
    if (v != _displayedVoices) {
        _displayedVoices = v;
        update();
    }
    // Rebuild peak/overflow tooltip whenever the cached result changes.
    auto r = FfxivVoiceAnalyzer::instance()->resultFor(_file.data());
    if (r.globalPeak != _peakVoices || r.overflowEvents != _overflowEvents) {
        _peakVoices = r.globalPeak;
        _peakTick = r.globalPeakTick;
        _overflowEvents = r.overflowEvents;
        QString tip = tr("FFXIV voices: %1 / %2").arg(v).arg(FfxivVoiceAnalyzer::kVoiceCeiling);
        if (_peakVoices > 0) {
            tip += tr("\nMax in piece: %1 voices").arg(_peakVoices);
            if (_peakVoices > FfxivVoiceAnalyzer::kVoiceCeiling)
                tip += tr(" (overflow: %1 events)").arg(_overflowEvents);
        }
        setToolTip(tip);
    }
}

void FfxivVoiceGaugeWidget::onAnalysisUpdated(MidiFile *file)
{
    if (file == _file.data())
        refresh();
}

void FfxivVoiceGaugeWidget::onFfxivModeChanged(bool enabled)
{
    _ffxivOn = enabled;
    updateVisibility();
}

void FfxivVoiceGaugeWidget::updateVisibility()
{
#ifdef FLUIDSYNTH_SUPPORT
    // Always show the gauge once FluidSynth support is compiled in, even when
    // FFXIV SoundFont mode is currently OFF. The meter then sits at "0/16"
    // as a visible placeholder so first-run users can see where the gauge
    // lives in the toolbar (B-FFXIV-GAUGE-VIS-001). When FFXIV mode is off
    // the painter naturally renders an empty meter.
    setVisible(true);
#else
    setVisible(false);
#endif
}

int FfxivVoiceGaugeWidget::currentVoiceCount() const
{
    if (!_file)
        return 0;
    int tick = 0;
    if (MidiPlayer::isPlaying() && _file) {
        tick = _file->tick(MidiPlayer::timeMs());
    } else if (_file) {
        // When stopped, show the maximum across the file.
        auto r = FfxivVoiceAnalyzer::instance()->resultFor(_file.data());
        return r.globalPeak;
    }
    return FfxivVoiceAnalyzer::instance()->voiceCountAt(_file.data(), tick);
}

void FfxivVoiceGaugeWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    bool dark = Appearance::shouldUseDarkMode();
    QColor bgColor     = dark ? QColor(0x0d, 0x11, 0x17) : QColor(0xf6, 0xf8, 0xfa);
    QColor borderColor = dark ? QColor(0x48, 0x4f, 0x58) : QColor(0xd0, 0xd7, 0xde);
    QColor textColor   = dark ? QColor(0xe6, 0xed, 0xf3) : QColor(0x1f, 0x23, 0x28);
    QColor mutedColor  = dark ? QColor(0x6e, 0x76, 0x81) : QColor(0x86, 0x8f, 0x99);
    QColor segOff      = dark ? QColor(0x1c, 0x22, 0x2b) : QColor(0xe6, 0xea, 0xef);
    QColor green       = dark ? QColor(0x3f, 0xb9, 0x50) : QColor(0x1a, 0x7f, 0x37);
    QColor yellow      = dark ? QColor(0xd2, 0x9b, 0x22) : QColor(0xbf, 0x83, 0x00);
    QColor red         = dark ? QColor(0xf8, 0x51, 0x49) : QColor(0xcf, 0x22, 0x2e);
    QColor ceilingMark = dark ? QColor(0xf8, 0x51, 0x49, 220) : QColor(0xcf, 0x22, 0x2e, 220);

    p.fillRect(rect(), bgColor);
    p.setPen(borderColor);
    p.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 3, 3);

    const int v = _displayedVoices;
    const int ceiling = FfxivVoiceAnalyzer::kVoiceCeiling;

    // Layout: [N/16 readout] [LED meter] [reserved +N badge slot]
    QFont numFont = p.font();
    numFont.setPointSizeF(qMax(7.5, numFont.pointSizeF() * 0.92));
    numFont.setBold(true);
    p.setFont(numFont);
    QFontMetrics nfm(numFont);
    QString numTxt = QStringLiteral("%1/%2").arg(v).arg(ceiling);
    // Reserve a fixed-width slot for the readout so the LED meter never
    // jitters when the count grows from 1 to 2 digits.
    int numW = nfm.horizontalAdvance(QStringLiteral("99/16")) + 4;

    // Reserve a permanent slot on the right for the "+NN" overflow badge so
    // the meter doesn't shift when the badge appears mid-playback.
    int badgeW = nfm.horizontalAdvance(QStringLiteral("+99")) + 4;

    QColor numColor = textColor;
    if (v > kSegYellowMax)      numColor = red;
    else if (v > kSegGreenMax)  numColor = yellow;

    // Left readout (where the "VOICES" label used to be)
    QRect numRect(4, 0, numW, height());
    p.setPen(numColor);
    p.drawText(numRect, Qt::AlignVCenter | Qt::AlignLeft, numTxt);

    // LED meter rectangle in between
    int meterX = numRect.right() + 4;
    int meterRight = width() - 4 - badgeW;
    QRect meterRect(meterX, 4, meterRight - meterX, height() - 8);

    if (meterRect.width() > kMeterSegments * 2) {
        // Compute per-segment width (float for even distribution)
        double segW = double(meterRect.width()) / double(kMeterSegments);
        int yTop = meterRect.top();
        int segH = meterRect.height();
        for (int i = 0; i < kMeterSegments; ++i) {
            int x0 = meterRect.left() + int(i * segW);
            int x1 = meterRect.left() + int((i + 1) * segW);
            int w  = qMax(1, x1 - x0 - 1); // 1px gap between blocks
            QRect seg(x0, yTop, w, segH);

            // Segment number is i+1 (1..kMeterSegments)
            int segIdx = i + 1;
            QColor segBaseColor;
            if (segIdx <= kSegGreenMax)        segBaseColor = green;
            else if (segIdx <= kSegYellowMax)  segBaseColor = yellow;
            else                               segBaseColor = red;

            if (segIdx <= v) {
                p.fillRect(seg, segBaseColor);
            } else {
                // off-state: very faint tint of the segment's color so the
                // user can see where the bands are even when silent.
                QColor faint = segBaseColor;
                faint.setAlpha(dark ? 38 : 30);
                p.fillRect(seg, segOff);
                p.fillRect(seg, faint);
            }
        }

        // Ceiling tick mark — thin bright line between segment 16 and 17
        // so the documented 16-voice limit is always visible at a glance.
        int tickX = meterRect.left() + int(ceiling * segW) - 1;
        QPen ceilPen(ceilingMark);
        ceilPen.setWidth(2);
        p.setPen(ceilPen);
        p.drawLine(tickX, meterRect.top() - 1, tickX, meterRect.bottom() + 1);
    }

    // Overflow badge in its reserved slot (only painted when v > ceiling,
    // but the slot is reserved either way so layout never shifts).
    if (v > ceiling) {
        QString badge = QStringLiteral("+%1").arg(v - ceiling);
        p.setPen(red);
        QRect badgeRect(meterRect.right() + 1, 0, badgeW, height());
        p.drawText(badgeRect, Qt::AlignVCenter | Qt::AlignRight, badge);
    }
}
