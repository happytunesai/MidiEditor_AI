/*
 * MidiEditor — FFXIV Voice Load Lane (Phase 32.3)
 *
 * Read-only graph beneath the velocity lane that visualises the simultaneous
 * voice count vs the FFXIV 16-voice ceiling. Shares horizontal scroll and
 * zoom with MatrixWidget the same way LyricTimelineWidget does.
 */

#ifndef FFXIVVOICELANEWIDGET_H
#define FFXIVVOICELANEWIDGET_H

#include "PaintWidget.h"

class MatrixWidget;
class MidiFile;

class FfxivVoiceLaneWidget : public PaintWidget {
    Q_OBJECT

public:
    explicit FfxivVoiceLaneWidget(MatrixWidget *matrixWidget, QWidget *parent = nullptr);

    void setFile(MidiFile *file);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private slots:
    void onAnalysisUpdated(MidiFile *file);

private:
    /// Matches LyricTimelineWidget / MiscWidget — piano key column width.
    static constexpr int LEFT_BORDER = 110;

    MatrixWidget *_matrixWidget;
    MidiFile *_file;

    int xPosOfTick(int tick) const;
    int tickOfXPos(int x) const;
};

#endif // FFXIVVOICELANEWIDGET_H
