/*
 * MidiEditor — FFXIV Voice Load Lane (Phase 32.3)
 */

#include "FfxivVoiceLaneWidget.h"
#include "MatrixWidget.h"
#include "../midi/MidiFile.h"
#include "../ai/FfxivVoiceAnalyzer.h"

#include <QPainter>
#include <QMouseEvent>

FfxivVoiceLaneWidget::FfxivVoiceLaneWidget(MatrixWidget *matrixWidget, QWidget *parent)
    : PaintWidget(parent)
    , _matrixWidget(matrixWidget)
    , _file(nullptr) {
    setRepaintOnMouseMove(false);
    setRepaintOnMousePress(true);
    setRepaintOnMouseRelease(true);

    if (_matrixWidget) {
        connect(_matrixWidget, SIGNAL(objectListChanged()), this, SLOT(update()));
    }
    connect(FfxivVoiceAnalyzer::instance(), &FfxivVoiceAnalyzer::analysisUpdated,
            this, &FfxivVoiceLaneWidget::onAnalysisUpdated);
}

void FfxivVoiceLaneWidget::setFile(MidiFile *file) {
    _file = file;
    if (file) {
        FfxivVoiceAnalyzer::instance()->watchFile(file);
    }
    update();
}

void FfxivVoiceLaneWidget::onAnalysisUpdated(MidiFile *file) {
    if (file == _file) {
        update();
    }
}

int FfxivVoiceLaneWidget::xPosOfTick(int tick) const {
    if (!_file || !_matrixWidget) return LEFT_BORDER;
    return _matrixWidget->xPosOfMs(_file->msOfTick(tick)) - LEFT_BORDER;
}

int FfxivVoiceLaneWidget::tickOfXPos(int x) const {
    if (!_file || !_matrixWidget) return 0;
    return _file->tick(_matrixWidget->msOfXPos(x + LEFT_BORDER));
}

void FfxivVoiceLaneWidget::paintEvent(QPaintEvent * /*event*/) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    // Background
    QColor bg(40, 44, 52);
    p.fillRect(rect(), bg);
    p.setPen(QColor(80, 80, 80));
    p.drawRect(0, 0, width() - 1, height() - 1);

    if (!_file || !_matrixWidget) return;

    FfxivVoiceAnalyzer *an = FfxivVoiceAnalyzer::instance();
    FfxivVoiceAnalyzer::Result res = an->resultFor(_file);
    if (!res.valid) {
        // Hint when analyser is disabled.
        if (!an->isEnabled()) {
            p.setPen(QColor(160, 160, 160));
            p.drawText(rect(), Qt::AlignCenter,
                       tr("FFXIV Voice Limiter is disabled"));
        }
        return;
    }

    const int kCeiling = FfxivVoiceAnalyzer::kVoiceCeiling;
    // v1.5.4: visual thresholds substantially relaxed from the documented
    // 16-voice ceiling.  Real-world FFXIV ensembles routinely play scores
    // that the strict analyser flags 20+ voices over the limit, yet sound
    // perfectly clean in-game.  The dashed line at v=16 still marks the
    // documented hard limit so the user can see it; the colour bands
    // reflect *practical* audibility instead:
    //   green   <= 18   — safe in practice
    //   yellow  19..23  — over docs but typically fine
    //   red     >= 24   — likely audible voice drops
    // These are heuristic and can be tuned once authoritative sources
    // about FFXIV's voice-eviction policy become available.
    const int kSoftWarn     = 19;
    const int kRedThreshold = 24;

    int H = height();
    int W = width();

    // v1.5.4: auto-scale the y-axis so a piece whose peak is, say, 40
    // voices does not paint as one solid red bar across the whole lane.
    // We always show at least the ceiling + 4 voices of headroom so a
    // sparse song still looks like a real graph.
    int kVisualMax = kCeiling + 4;
    if (res.globalPeak > 0) {
        int scaled = static_cast<int>(res.globalPeak * 1.15) + 1;
        if (scaled > kVisualMax) kVisualMax = scaled;
    }
    if (kVisualMax > 96) kVisualMax = 96;

    // Horizontal grid lines at every 4 voices (the 16-voice ceiling line
    // is drawn LAST, on top of the bars, so it stays visible inside the
    // yellow / red zones).
    auto yForVoice = [&](int v) {
        if (v < 0) v = 0;
        if (v > kVisualMax) v = kVisualMax;
        return H - (v * H) / kVisualMax;
    };

    // Pick a sensible grid step so we never draw 30 lines on a tall axis.
    int gridStep = 4;
    if (kVisualMax > 32) gridStep = 8;
    if (kVisualMax > 64) gridStep = 16;
    p.setPen(QColor(70, 70, 70));
    for (int v = gridStep; v < kVisualMax; v += gridStep) {
        if (v == kCeiling) continue; // ceiling drawn separately on top
        int y = yForVoice(v);
        p.drawLine(0, y, W, y);
    }
    int ceilY = yForVoice(kCeiling);

    // Faint axis label so the user can tell what scale they're looking at.
    p.setPen(QColor(190, 190, 190));
    QFont axisFont = p.font();
    axisFont.setPixelSize(9);
    p.setFont(axisFont);
    p.drawText(2, 10, QString("max %1").arg(kVisualMax));

    // Bars per visible tick segment.
    const auto &samples = res.voiceSamples;
    int n = samples.size();
    if (n < 2) return;

    p.setPen(Qt::NoPen);
    QColor green(80, 200, 100, 200);
    QColor yellow(230, 200, 60, 220);
    QColor red(240, 80, 80, 230);

    for (int i = 0; i + 1 < n; ++i) {
        int v = samples[i].voiceCount;
        if (v <= 0) continue;
        int t0 = samples[i].tick;
        int t1 = samples[i + 1].tick;

        int x0 = xPosOfTick(t0);
        int x1 = xPosOfTick(t1);
        if (x1 <= 0) continue;
        if (x0 >= W) break;
        if (x0 < 0) x0 = 0;
        if (x1 > W) x1 = W;
        if (x1 <= x0) continue;

        int yTop = yForVoice(v);
        int barH = H - yTop;

        QColor c = (v >= kRedThreshold) ? red : (v >= kSoftWarn ? yellow : green);
        p.fillRect(x0, yTop, x1 - x0, barH, c);

        // Numeric overflow label, only at chunk start, when there's room.
        if (v >= kRedThreshold && (x1 - x0) >= 18) {
            p.setPen(QColor(255, 255, 255));
            QFont f = p.font();
            f.setPixelSize(9);
            f.setBold(true);
            p.setFont(f);
            p.drawText(x0 + 2, yTop - 1, QString::number(v));
            p.setPen(Qt::NoPen);
        }
    }

    // Per-channel rate hotspots: thin red marker at the top edge.
    if (!res.rateHotspots.isEmpty()) {
        p.setPen(QColor(255, 50, 50, 220));
        p.setBrush(QColor(255, 50, 50, 80));
        for (const auto &h : res.rateHotspots) {
            int x0 = xPosOfTick(h.startTick);
            int x1 = xPosOfTick(h.endTick);
            if (x1 <= 0 || x0 >= W) continue;
            if (x0 < 0) x0 = 0;
            if (x1 > W) x1 = W;
            if (x1 <= x0) continue;
            p.drawRect(x0, 0, x1 - x0, 3);
        }
    }

    // Ceiling marker — drawn LAST, on top of bars + hotspots, so the
    // documented 16-voice limit stays clearly visible inside the yellow
    // and red zones.  Two-tone line: a thin black halo for contrast on
    // light bar colours, then a bright dashed red line on top.
    {
        QPen halo(QColor(0, 0, 0, 160));
        halo.setWidth(3);
        p.setPen(halo);
        p.drawLine(0, ceilY, W, ceilY);

        QPen ceilPen(QColor(255, 80, 80));
        ceilPen.setStyle(Qt::DashLine);
        ceilPen.setWidth(1);
        p.setPen(ceilPen);
        p.drawLine(0, ceilY, W, ceilY);

        // Small "16" tag at the right edge so the line has a label.
        QFont tagFont = p.font();
        tagFont.setPixelSize(9);
        tagFont.setBold(true);
        p.setFont(tagFont);
        QFontMetrics tfm(tagFont);
        QString tag = QString::number(kCeiling);
        int tw = tfm.horizontalAdvance(tag) + 4;
        QRect tagRect(W - tw - 2, ceilY - tfm.height() / 2 - 1, tw, tfm.height());
        p.fillRect(tagRect, QColor(0, 0, 0, 180));
        p.setPen(QColor(255, 200, 200));
        p.drawText(tagRect, Qt::AlignCenter, tag);
    }
}

void FfxivVoiceLaneWidget::mousePressEvent(QMouseEvent *event) {
    if (!_file || !_matrixWidget) return;
    if (event->button() != Qt::LeftButton) return;

    int tick = tickOfXPos(event->position().x());
    if (tick < 0) tick = 0;
    _file->setCursorTick(tick);
    _matrixWidget->update();
    update();
}
