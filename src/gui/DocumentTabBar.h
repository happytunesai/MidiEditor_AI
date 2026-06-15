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

#ifndef DOCUMENTTABBAR_H_
#define DOCUMENTTABBAR_H_

#include <QPoint>
#include <QTabBar>

/**
 * \class DocumentTabBar
 * \brief Phase 28 (editor groups): a QTabBar whose tabs can be dragged - both
 * to REORDER within the bar and to MOVE to another DocumentTabBar (a different
 * editor group, VS Code-style).
 *
 * QTabBar's built-in setMovable() only reorders within one bar; there is no
 * native way to drag a tab to a different bar. So this disables the built-in
 * mover and implements the gesture with QDrag: on drop it reports the requested
 * move through a single signal and lets the owner (MainWindow) perform it.
 * MainWindow owns the DocumentManager that is the source of truth for tab
 * order, so doing the move there keeps the model and the bars in sync (the bars
 * are rebuilt from the manager after each move).
 *
 * The drag carries the source tab index in custom mime data; the source bar
 * itself is recovered from QDropEvent::source(), which is the QObject that
 * started the drag - reliable because drags are synchronous within one process.
 */
class DocumentTabBar : public QTabBar {
    Q_OBJECT

public:
    explicit DocumentTabBar(QWidget *parent = nullptr);

    /** \brief The in-process mime type identifying a tab drag (payload = source
     *  tab index as text; source bar = QDropEvent::source()). Shared so other
     *  widgets (e.g. a group pane) can accept the same drag. */
    static QString tabMimeType();

    /** \brief Show the insertion caret at the append (end) position. Used while
     *  a tab is dragged over this group's editor area (drop there = append), so
     *  the landing spot is visible before the cursor reaches the thin bar. */
    void showAppendDropIndicator();

    /** \brief Hide the insertion caret. */
    void clearDropIndicator();

signals:
    /**
     * \brief The user dropped tab \a sourceIndex of \a source onto \a target at
     * tab position \a targetIndex. source == target is an in-bar reorder;
     * otherwise the document moves between groups. targetIndex may equal
     * count() ("after the last tab"); the handler clamps as needed.
     */
    void tabMoveRequested(DocumentTabBar *source, int sourceIndex,
                          DocumentTabBar *target, int targetIndex);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    /** \brief Tab gap (0..count) where a drop would insert the tab; the drop
     *  indicator is drawn there. -1 = no indicator. */
    int dropIndexAt(const QPoint &pos) const;
    void setDropIndicator(int gap);

    QPoint _pressPos;          // where the left button went down (drag threshold)
    int _pressIndex = -1;      // the tab under that press, or -1
    int _dropIndicator = -1;   // insertion gap being shown, or -1
};

#endif // DOCUMENTTABBAR_H_
