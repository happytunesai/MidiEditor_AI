/*
 * MidiEditor AI - TimeDisplayWidget implementation (Phase 41).
 */

#include "TimeDisplayWidget.h"

#include "../midi/MidiFile.h"
#include "../MidiEvent/MidiEvent.h"
#include "../MidiEvent/TempoChangeEvent.h"
#include "../protocol/Protocol.h"

#include <QColor>
#include <QFont>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QMultiMap>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPen>
#include <QPointF>
#include <QPolygonF>
#include <QRect>
#include <QRectF>
#include <QSettings>
#include <QtGlobal>

// Geometry of one seven-segment digit cell and the surrounding layout.
static const int DIGIT_W       = 14;
static const int DIGIT_H       = 24;
static const int SEG_T         = 4;   // segment thickness
static const int GLYPH_GAP     = 3;   // space after each glyph
static const int SEP_W         = 7;   // width of ':' '.' '/' ' '
static const int MARGIN        = 5;
static const int TAG_W         = 30;  // mode-tag label column
static const int FIELD_W       = 130; // reserved value field (stable width)
static const int WIDGET_HEIGHT = 32;
static const int BLINK_MS      = 500;

// LED colour palettes, cycled by right-click. Named to echo the app's
// built-in themes (Sakura = ThemePink, Amber = brand/Amoled, etc.). Each
// entry: bright "on" segment, dim "off" ghost, bezel gradient (top/bottom),
// border, and the mode-tag label colour.
struct ClockTheme {
    const char *name;
    QColor on, off, bezelTop, bezelBot, border, tag;
};
static const ClockTheme kClockThemes[] = {
    {"Amber",  QColor(0xff, 0xb6, 0x1e), QColor(0x3c, 0x2a, 0x0e),
               QColor(0x1b, 0x12, 0x09), QColor(0x09, 0x06, 0x03),
               QColor(0x46, 0x32, 0x16), QColor(0xd0, 0x90, 0x18)},
    {"Blue",   QColor(0x4c, 0xc2, 0xff), QColor(0x10, 0x2a, 0x3a),
               QColor(0x0a, 0x14, 0x1e), QColor(0x03, 0x07, 0x0c),
               QColor(0x1c, 0x3a, 0x52), QColor(0x58, 0xa6, 0xff)},
    {"Green",  QColor(0x3d, 0xf5, 0x6a), QColor(0x0e, 0x33, 0x1a),
               QColor(0x07, 0x16, 0x0c), QColor(0x02, 0x08, 0x04),
               QColor(0x1c, 0x44, 0x26), QColor(0x4a, 0xd0, 0x70)},
    {"Sakura", QColor(0xff, 0x8f, 0xc4), QColor(0x3a, 0x18, 0x28),
               QColor(0x1e, 0x10, 0x16), QColor(0x0b, 0x05, 0x08),
               QColor(0x52, 0x22, 0x38), QColor(0xff, 0xb3, 0xd6)},
    {"Mono",   QColor(0xf0, 0xf2, 0xf5), QColor(0x33, 0x36, 0x3a),
               QColor(0x16, 0x18, 0x1a), QColor(0x07, 0x08, 0x09),
               QColor(0x44, 0x48, 0x4d), QColor(0xc8, 0xcc, 0xd2)},
    {"Red",    QColor(0xff, 0x4d, 0x4d), QColor(0x3a, 0x10, 0x10),
               QColor(0x1c, 0x0a, 0x0a), QColor(0x09, 0x03, 0x03),
               QColor(0x50, 0x1e, 0x1e), QColor(0xff, 0x8a, 0x8a)},
};
static const int kClockThemeCount =
    static_cast<int>(sizeof(kClockThemes) / sizeof(kClockThemes[0]));

// Width a glyph consumes - kept in lock-step with drawGlyph's return values
// so the right-alignment math matches what gets painted.
static int glyphAdvance(QChar c) {
    if (c.isDigit() || c == QLatin1Char('-'))
        return DIGIT_W + GLYPH_GAP;
    if (c == QLatin1Char(':') || c == QLatin1Char('.') || c == QLatin1Char('/'))
        return SEP_W + GLYPH_GAP;
    return SEP_W; // space / unknown
}

TimeDisplayWidget::TimeDisplayWidget(QWidget *parent)
    : QWidget(parent) {
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    setMinimumSize(sizeHint());
    setCursor(Qt::PointingHandCursor);
    // Right-click is ours (colour cycle) - stop the toolbar's context menu
    // from popping up over the widget.
    setContextMenuPolicy(Qt::PreventContextMenu);
    setToolTip(tr("Cursor / playback time.\n"
                  "Left-click: cycle readout (position, length, remaining, BPM, bar).\n"
                  "Right-click: cycle colour theme."));

    QSettings s("MidiEditor", "NONE");
    _mode = TimeDisplay::modeFromInt(s.value("View/timeDisplayMode", 0).toInt());
    _colorTheme = s.value("View/timeDisplayTheme", 1).toInt(); // 1 = Blue (default)
    if (_colorTheme < 0 || _colorTheme >= kClockThemeCount)
        _colorTheme = 1;

    _blinkTimer.setInterval(BLINK_MS);
    connect(&_blinkTimer, &QTimer::timeout, this, &TimeDisplayWidget::onBlinkTick);
    _blinkTimer.start();
}

void TimeDisplayWidget::setFile(MidiFile *file) {
    if (_file == file)
        return;
    if (_file) {
        disconnect(_file, nullptr, this, nullptr);
        if (_file->protocol())
            disconnect(_file->protocol(), nullptr, this, nullptr);
    }
    _file = file;
    if (_file) {
        connect(_file, &MidiFile::cursorPositionChanged,
                this, &TimeDisplayWidget::onCursorMoved);
        if (_file->protocol()) {
            // Length (and bar/meter) can change when the file is edited
            // (e.g. Insert measures) - recompute on every finished action.
            connect(_file->protocol(), &Protocol::actionFinished, this, [this]() {
                recomputeLength();
                if (!_playing)
                    onCursorMoved();
                update();
            });
        }
        recomputeLength();
        _curTick = _file->cursorTick();
        _cursorMs = _file->msOfTick(_curTick);
    } else {
        _curTick = 0;
        _cursorMs = 0;
        _lengthMs = 0;
    }
    update();
}

void TimeDisplayWidget::recomputeLength() {
    _lengthMs = _file ? _file->msOfTick(_file->endTick()) : 0;
}

void TimeDisplayWidget::onCursorMoved() {
    if (!_file || _playing)
        return; // the player drives the position while playing
    _curTick = _file->cursorTick();
    _cursorMs = _file->msOfTick(_curTick);
    update();
}

void TimeDisplayWidget::onPlaybackPositionChanged(int ms) {
    _playing = true;
    _cursorMs = ms;
    _curTick = _file ? _file->tick(ms) : 0;
    update();
}

void TimeDisplayWidget::onPlaybackStopped() {
    _playing = false;
    _colonOn = true;
    if (_file) {
        _curTick = _file->cursorTick();
        _cursorMs = _file->msOfTick(_curTick);
    }
    update();
}

void TimeDisplayWidget::onBlinkTick() {
    if (_playing) {
        _colonOn = !_colonOn;
        update();
    } else if (!_colonOn) {
        _colonOn = true;
        update();
    }
}

int TimeDisplayWidget::bpmAtTick(int tick) const {
    if (!_file)
        return 0;
    QMultiMap<int, MidiEvent *> *tempos = _file->tempoEvents();
    int bpm = 120; // sensible fallback if a file somehow has no tempo event
    if (tempos) {
        // tempoEvents() is key-ordered ascending; the last entry at/before
        // the tick is the tempo in effect there.
        for (auto it = tempos->begin(); it != tempos->end(); ++it) {
            if (it.key() > tick)
                break;
            if (TempoChangeEvent *tc = dynamic_cast<TempoChangeEvent *>(it.value()))
                bpm = tc->beatsPerQuarter();
        }
    }
    return bpm;
}

void TimeDisplayWidget::barBeatAtTick(int tick, int *bar, int *beat,
                                      int *num, int *den) const {
    int m = 1, n = 4, d = 4, beatNo = 1;
    if (_file) {
        int startOfMeasure = 0, endOfMeasure = 0;
        m = _file->measure(tick, &startOfMeasure, &endOfMeasure);
        _file->meterAt(tick, &n, &d);
        const int tpq = _file->ticksPerQuarter();
        int ticksPerBeat = (d > 0 && tpq > 0) ? (tpq * 4 / d) : tpq;
        if (ticksPerBeat <= 0)
            ticksPerBeat = (tpq > 0) ? tpq : 1;
        beatNo = (tick - startOfMeasure) / ticksPerBeat + 1; // 1-based
    }
    if (bar)  *bar  = m;
    if (beat) *beat = beatNo;
    if (num)  *num  = n;
    if (den)  *den  = d;
}

QString TimeDisplayWidget::currentValue() const {
    switch (_mode) {
    case TimeDisplay::Mode::Position:
        return TimeDisplay::formatClock(_cursorMs);
    case TimeDisplay::Mode::Length:
        return TimeDisplay::formatClock(_lengthMs);
    case TimeDisplay::Mode::Remaining:
        return TimeDisplay::formatRemaining(_lengthMs, _cursorMs);
    case TimeDisplay::Mode::Bpm:
        return QString::number(bpmAtTick(_curTick));
    case TimeDisplay::Mode::Bar: {
        int bar = 1, beat = 1, num = 4, den = 4;
        barBeatAtTick(_curTick, &bar, &beat, &num, &den);
        return TimeDisplay::formatBar(bar, beat, num, den);
    }
    }
    return TimeDisplay::formatClock(_cursorMs);
}

int TimeDisplayWidget::drawGlyph(QPainter &p, int x, int yTop, QChar c,
                                 const QColor &on, const QColor &off,
                                 bool colonVisible) const {
    const qreal w = DIGIT_W, h = DIGIT_H, t = SEG_T;
    const qreal midY = yTop + (h - t) / 2.0;
    const qreal vH = (h - 3 * t) / 2.0;

    QColor glow = on;
    glow.setAlpha(110);

    // Beveled (pointed-end) segment polygons - the classic 7-seg shape.
    // Anti-aliased so the diagonals stay smooth instead of pixelated.
    auto hSeg = [t](qreal sx, qreal sy, qreal len) {
        const qreal b = t / 2.0;
        QPolygonF poly;
        poly << QPointF(sx, sy + b) << QPointF(sx + b, sy)
             << QPointF(sx + len - b, sy) << QPointF(sx + len, sy + b)
             << QPointF(sx + len - b, sy + t) << QPointF(sx + b, sy + t);
        return poly;
    };
    auto vSeg = [t](qreal sx, qreal sy, qreal len) {
        const qreal b = t / 2.0;
        QPolygonF poly;
        poly << QPointF(sx + b, sy) << QPointF(sx + t, sy + b)
             << QPointF(sx + t, sy + len - b) << QPointF(sx + b, sy + len)
             << QPointF(sx, sy + len - b) << QPointF(sx, sy + b);
        return poly;
    };
    auto drawSeg = [&](bool active, const QPolygonF &poly) {
        if (active) {
            p.setPen(QPen(glow, 3.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.setBrush(Qt::NoBrush);
            p.drawPolygon(poly);   // soft halo
            p.setPen(Qt::NoPen);
            p.setBrush(on);
            p.drawPolygon(poly);   // crisp segment
        } else {
            p.setPen(Qt::NoPen);
            p.setBrush(off);
            p.drawPolygon(poly);   // dim ghost
        }
    };

    if (c.isDigit() || c == QLatin1Char('-')) {
        // a, b, c, d, e, f, g
        static const bool MAP[11][7] = {
            {1, 1, 1, 1, 1, 1, 0}, // 0
            {0, 1, 1, 0, 0, 0, 0}, // 1
            {1, 1, 0, 1, 1, 0, 1}, // 2
            {1, 1, 1, 1, 0, 0, 1}, // 3
            {0, 1, 1, 0, 0, 1, 1}, // 4
            {1, 0, 1, 1, 0, 1, 1}, // 5
            {1, 0, 1, 1, 1, 1, 1}, // 6
            {1, 1, 1, 0, 0, 0, 0}, // 7
            {1, 1, 1, 1, 1, 1, 1}, // 8
            {1, 1, 1, 1, 0, 1, 1}, // 9
            {0, 0, 0, 0, 0, 0, 1}, // '-'
        };
        const int idx = (c == QLatin1Char('-')) ? 10 : c.digitValue();
        const bool *m = MAP[idx];
        drawSeg(m[0], hSeg(x + t,     yTop,         w - 2 * t)); // a (top)
        drawSeg(m[1], vSeg(x + w - t, yTop + t,     vH));        // b (top-right)
        drawSeg(m[2], vSeg(x + w - t, midY + t,     vH));        // c (bot-right)
        drawSeg(m[3], hSeg(x + t,     yTop + h - t, w - 2 * t)); // d (bottom)
        drawSeg(m[4], vSeg(x,         midY + t,     vH));        // e (bot-left)
        drawSeg(m[5], vSeg(x,         yTop + t,     vH));        // f (top-left)
        drawSeg(m[6], hSeg(x + t,     midY,         w - 2 * t)); // g (middle)
        return DIGIT_W + GLYPH_GAP;
    }
    if (c == QLatin1Char(':')) {
        const qreal dot = t;
        const qreal cx = x + (SEP_W - dot) / 2.0;
        const QColor col = colonVisible ? on : off;
        QColor cglow = col;
        cglow.setAlpha(110);
        auto dotAt = [&](qreal cy) {
            const QRectF r(cx, cy - dot / 2.0, dot, dot);
            if (colonVisible) {
                p.setPen(QPen(cglow, 3.0));
                p.setBrush(Qt::NoBrush);
                p.drawEllipse(r);
            }
            p.setPen(Qt::NoPen);
            p.setBrush(col);
            p.drawEllipse(r);
        };
        dotAt(yTop + h / 3.0);
        dotAt(yTop + 2.0 * h / 3.0);
        return SEP_W + GLYPH_GAP;
    }
    if (c == QLatin1Char('.')) {
        const qreal dot = t;
        const QRectF r(x + (SEP_W - dot) / 2.0, yTop + h - dot, dot, dot);
        p.setPen(QPen(glow, 3.0));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(r);
        p.setPen(Qt::NoPen);
        p.setBrush(on);
        p.drawEllipse(r);
        return SEP_W + GLYPH_GAP;
    }
    if (c == QLatin1Char('/')) {
        p.setPen(QPen(glow, t + 2.0, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(x, yTop + h), QPointF(x + SEP_W, yTop));
        p.setPen(QPen(on, t, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(x, yTop + h), QPointF(x + SEP_W, yTop));
        return SEP_W + GLYPH_GAP;
    }
    return SEP_W; // space / unknown
}

void TimeDisplayWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    // Retro LED on a dark bezel. Colours come from the active palette
    // (right-click cycles through them). Soft glow + anti-aliased beveled
    // segments keep it legible instead of flat pixelated rectangles.
    const ClockTheme &th = kClockThemes[qBound(0, _colorTheme, kClockThemeCount - 1)];
    const QColor bezelTop = th.bezelTop;
    const QColor bezelBot = th.bezelBot;
    const QColor border   = th.border;
    const QColor on       = th.on;
    const QColor off      = th.off;
    const QColor tagCol   = th.tag;

    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath bg;
    bg.addRoundedRect(r, 4, 4);

    // Bezel with a subtle top-to-bottom gradient for depth.
    QLinearGradient bgGrad(0, 0, 0, height());
    bgGrad.setColorAt(0.0, bezelTop);
    bgGrad.setColorAt(1.0, bezelBot);
    p.fillPath(bg, bgGrad);

    p.save();
    p.setClipPath(bg);  // keep the glow inside the bezel

    // Mode tag - a small label (not seven-segment), left column.
    QFont tagFont = font();
    tagFont.setBold(true);
    p.setFont(tagFont);
    p.setPen(tagCol);
    p.drawText(QRectF(MARGIN, 0, TAG_W - 2, height()),
               Qt::AlignVCenter | Qt::AlignLeft,
               QString::fromLatin1(TimeDisplay::modeTag(_mode)));

    // Value - right-aligned within the fixed field so the digits don't
    // jitter horizontally as they change.
    const QString value = currentValue();
    int strW = 0;
    for (const QChar &c : value)
        strW += glyphAdvance(c);
    const int fieldLeft = MARGIN + TAG_W;
    int gx = fieldLeft + qMax(0, FIELD_W - strW);
    const int yTop = (height() - DIGIT_H) / 2;
    for (const QChar &c : value)
        gx += drawGlyph(p, gx, yTop, c, on, off, _colonOn);

    // Subtle glassy sheen across the top third.
    QLinearGradient sheen(0, 0, 0, height());
    sheen.setColorAt(0.0, QColor(255, 255, 255, 20));
    sheen.setColorAt(0.45, QColor(255, 255, 255, 0));
    p.fillRect(r, sheen);
    p.restore();

    // Crisp rounded border on top.
    p.setPen(border);
    p.setBrush(Qt::NoBrush);
    p.drawPath(bg);
}

QSize TimeDisplayWidget::sizeHint() const {
    return QSize(MARGIN + TAG_W + FIELD_W + MARGIN, WIDGET_HEIGHT);
}

QSize TimeDisplayWidget::minimumSizeHint() const {
    return sizeHint();
}

void TimeDisplayWidget::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        // Left-click cycles the readout (position / length / remaining / BPM / bar).
        _mode = TimeDisplay::nextMode(_mode);
        QSettings s("MidiEditor", "NONE");
        s.setValue("View/timeDisplayMode", static_cast<int>(_mode));
        update();
        event->accept();
        return;
    }
    if (event->button() == Qt::RightButton) {
        // Right-click cycles the LED colour theme.
        _colorTheme = (_colorTheme + 1) % kClockThemeCount;
        QSettings s("MidiEditor", "NONE");
        s.setValue("View/timeDisplayTheme", _colorTheme);
        update();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}
