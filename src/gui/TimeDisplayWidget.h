/*
 * MidiEditor AI - TimeDisplayWidget (Phase 41).
 *
 * A small, self-sizing toolbar widget that shows the edit-cursor time
 * (or the live player time during playback) as retro seven-segment
 * digits. A single click cycles the readout between position, total
 * length, remaining time, tempo (BPM), and musical bar.beat + meter.
 *
 * Placed via the Customize Toolbar system exactly like MidiVisualizerWidget
 * (registered as the `time_display` action, instantiated on demand in the
 * toolbar build). The pure formatting/mode logic lives in the header-only
 * TimeDisplayFormat.h so it can be unit-tested without a QWidget.
 */

#ifndef TIMEDISPLAYWIDGET_H
#define TIMEDISPLAYWIDGET_H

#include <QWidget>
#include <QTimer>

#include "TimeDisplayFormat.h"

class MidiFile;

class TimeDisplayWidget : public QWidget {
    Q_OBJECT

public:
    explicit TimeDisplayWidget(QWidget *parent = nullptr);

    /** \brief Bind the widget to a file. Re-wires the cursor + length
     *  signals to this file (disconnecting any previous one). Safe to
     *  call with nullptr to detach. */
    void setFile(MidiFile *file);

public slots:
    /** \brief Live player position (ms) - the same signal that drives the
     *  lyric timeline / voice lane. Switches the widget into "playing"
     *  state so the colon blinks. */
    void onPlaybackPositionChanged(int ms);
    /** \brief Player stopped - snap the readout back to the edit cursor
     *  and stop the colon blink. */
    void onPlaybackStopped();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

private slots:
    /** \brief Edit cursor moved while idle. */
    void onCursorMoved();
    /** \brief Blink driver - toggles the colon while playing. */
    void onBlinkTick();

private:
    /** \brief Recompute the cached total length from the current file. */
    void recomputeLength();
    /** \brief The value string for the active mode (digits + separators). */
    QString currentValue() const;
    /** \brief Tempo (BPM) in effect at \a tick, 120 fallback. */
    int bpmAtTick(int tick) const;
    /** \brief Resolve bar/beat (1-based) + time signature at \a tick. */
    void barBeatAtTick(int tick, int *bar, int *beat, int *num, int *den) const;
    /** \brief Paint one seven-segment / separator glyph; returns the x
     *  advance. \a on selects bright vs. ghost rendering. */
    int drawGlyph(QPainter &p, int x, int yTop, QChar c,
                  const QColor &onColor, const QColor &offColor,
                  bool colonVisible) const;

    MidiFile *_file = nullptr;
    TimeDisplay::Mode _mode = TimeDisplay::Mode::Position;
    int  _colorTheme = 0;   ///< index into the LED colour palette (right-click cycles)
    int  _cursorMs = 0;     ///< current position in ms (cursor or player)
    int  _curTick  = 0;     ///< current position in ticks (for BPM / BAR)
    int  _lengthMs = 0;     ///< cached total duration
    bool _playing  = false;
    bool _colonOn  = true;  ///< blink phase
    QTimer _blinkTimer;
};

#endif // TIMEDISPLAYWIDGET_H
