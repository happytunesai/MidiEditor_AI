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

#ifndef LYRICTIMELINEWIDGET_H
#define LYRICTIMELINEWIDGET_H

#include "PaintWidget.h"

#include <QList>
#include <QPair>
#include <QSet>

class MatrixWidget;
class MidiFile;
class MidiEvent;
class QLineEdit;

/**
 * \class LyricTimelineWidget
 *
 * \brief Widget that displays lyric blocks along the time axis.
 *
 * LyricTimelineWidget provides a dedicated timeline below the piano roll
 * that shows lyric text events as colored rectangles. It synchronizes
 * horizontal scrolling and zoom with the MatrixWidget.
 *
 * Features:
 * - Displays MIDI Lyric events (type 0x05) and general Text events (0x01)
 *   as colored blocks with text labels
 * - Horizontal scroll sync with MatrixWidget via shared scrollbar
 * - Zoom sync with MatrixWidget
 * - Playback position highlight (current lyric block)
 * - Measure/beat grid lines matching MatrixWidget
 * - Interactive editing: drag, resize, double-click to edit text, context menu
 */
class LyricTimelineWidget : public PaintWidget {
    Q_OBJECT

public:
    LyricTimelineWidget(MatrixWidget *matrixWidget, QWidget *parent = nullptr);

    void setFile(MidiFile *file);

    /**
     * \brief Collects all lyric/text events from the file sorted by tick.
     * \return List of (tick, TextEvent*) pairs sorted by tick
     */
    QList<QPair<int, MidiEvent *>> collectLyricEvents();

public slots:
    void onPlaybackPositionChanged(int ms);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    void onInlineEditFinished();

private:
    /** \brief Offset matching MatrixWidget's piano key width */
    static const int LEFT_BORDER = 110;

    /** \brief Edge detection zone width in pixels */
    static const int EDGE_ZONE = 6;

    MatrixWidget *_matrixWidget;
    MidiFile *_file;
    int _currentPlaybackMs;

    // Editing state
    enum DragMode { NoDrag, DragMove, DragResizeLeft, DragResizeRight };
    DragMode _dragMode;
    int _selectedBlockIndex;
    QSet<int> _selectedBlockIndices;
    bool _dragActive;
    int _dragStartX;
    int _dragStartTick;
    int _dragOrigStartTick;
    int _dragOrigEndTick;

    // Inline editor
    QLineEdit *_inlineEditor;

    /** \brief Converts MIDI tick to x pixel in this widget's coordinate space */
    int xPosOfTick(int tick);

    /** \brief Converts x pixel to MIDI tick */
    int tickOfXPos(int x);

    /** \brief Returns the block index at the given position, or -1 */
    int blockIndexAtPos(int x, int y);

    /** \brief Returns DragMode depending on whether x is near block edges */
    DragMode dragModeForPos(int x, int blockIndex);
};

#endif // LYRICTIMELINEWIDGET_H
