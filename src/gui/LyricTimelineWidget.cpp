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

#include "LyricTimelineWidget.h"
#include "Appearance.h"
#include "LyricMetadataDialog.h"
#include "MatrixWidget.h"
#include "../MidiEvent/TextEvent.h"
#include "../midi/MidiFile.h"
#include "../midi/MidiChannel.h"
#include "../midi/MidiTrack.h"
#include "../midi/LyricManager.h"
#include "../midi/LyricBlock.h"
#include "../protocol/Protocol.h"

#include <QPainter>
#include <QPaintEvent>
#include <QFontMetrics>
#include <QMultiMap>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QAction>
#include <QLineEdit>
#include <QInputDialog>
#include <QContextMenuEvent>

LyricTimelineWidget::LyricTimelineWidget(MatrixWidget *matrixWidget, QWidget *parent)
    : PaintWidget(parent)
    , _matrixWidget(matrixWidget)
    , _file(nullptr)
    , _currentPlaybackMs(-1)
    , _dragMode(NoDrag)
    , _selectedBlockIndex(-1)
    , _dragActive(false)
    , _dragStartX(0)
    , _dragStartTick(0)
    , _dragOrigStartTick(0)
    , _dragOrigEndTick(0)
    , _inlineEditor(nullptr)
{
    setRepaintOnMouseMove(false);
    setRepaintOnMousePress(false);
    setRepaintOnMouseRelease(false);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);

    // Repaint when the matrix changes (scroll, zoom, file load)
    connect(_matrixWidget, SIGNAL(objectListChanged()), this, SLOT(update()));
}

void LyricTimelineWidget::setFile(MidiFile *file)
{
    // Disconnect old file's signals (P3-006)
    if (_file && _file->lyricManager()) {
        disconnect(_file->lyricManager(), &LyricManager::lyricsChanged, this, nullptr);
    }

    _file = file;

    // Clear stale selection when lyrics rebuild (P2-008)
    // Guard: skip during active drag to avoid clearing selection mid-move (P3-001)
    if (_file && _file->lyricManager()) {
        connect(_file->lyricManager(), &LyricManager::lyricsChanged, this, [this]() {
            if (_dragActive)
                return;
            _selectedBlockIndex = -1;
            _selectedBlockIndices.clear();
            update();
        });
    }

    update();
}

void LyricTimelineWidget::onPlaybackPositionChanged(int ms)
{
    _currentPlaybackMs = ms;
    update();
}

int LyricTimelineWidget::xPosOfTick(int tick)
{
    if (!_file) return 0;
    return _matrixWidget->xPosOfMs(_file->msOfTick(tick)) - LEFT_BORDER;
}

int LyricTimelineWidget::tickOfXPos(int x)
{
    if (!_file) return 0;
    return _file->tick(_matrixWidget->msOfXPos(x + LEFT_BORDER));
}

QList<QPair<int, MidiEvent *>> LyricTimelineWidget::collectLyricEvents()
{
    QList<QPair<int, MidiEvent *>> result;
    if (!_file) return result;

    // Delegate to LyricManager if available
    LyricManager *mgr = _file->lyricManager();
    if (mgr) {
        const QList<LyricBlock> &blocks = mgr->allBlocks();
        for (const LyricBlock &block : blocks) {
            if (block.sourceEvent) {
                result.append(qMakePair(block.startTick, block.sourceEvent));
            }
        }
        return result;
    }

    // Fallback: scan channels directly (only if no LyricManager)
    for (int ch = 0; ch < 17; ch++) {
        QMultiMap<int, MidiEvent *> *map = _file->channelEvents(ch);
        if (!map) continue;

        for (auto it = map->constBegin(); it != map->constEnd(); ++it) {
            TextEvent *te = dynamic_cast<TextEvent *>(it.value());
            if (!te) continue;

            int t = te->type();
            if (t == TextEvent::LYRIK || t == TextEvent::TEXT) {
                if (te->text().trimmed().isEmpty()) continue;
                result.append(qMakePair(it.key(), static_cast<MidiEvent *>(te)));
            }
        }
    }

    std::sort(result.begin(), result.end(),
              [](const QPair<int, MidiEvent *> &a, const QPair<int, MidiEvent *> &b) {
                  return a.first < b.first;
              });

    return result;
}

void LyricTimelineWidget::paintEvent(QPaintEvent * /*event*/)
{
    if (!_matrixWidget->midiFile())
        return;

    // Use local variable — don't overwrite _file, which bypasses setFile() connections (P3-004)
    MidiFile *file = _matrixWidget->midiFile();
    if (file != _file) {
        // File changed externally (e.g., via MatrixWidget); update through setFile
        setFile(file);
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Background
    QColor bgColor = Appearance::velocityBackgroundColor();
    painter.setPen(Appearance::grayColor());
    painter.setBrush(bgColor);
    painter.drawRect(0, 0, width() - 1, height() - 1);

    // Measure/beat grid lines (same as MiscWidget)
    painter.setPen(Appearance::velocityGridColor());
    typedef QPair<int, int> TMPPair;
    foreach (TMPPair p, _matrixWidget->divs()) {
        int x = p.first - LEFT_BORDER;
        painter.drawLine(x, 0, x, height());
    }

    // Use LyricManager blocks directly for consistent paint/hit-test
    LyricManager *mgr = _file->lyricManager();
    if (!mgr || !mgr->hasLyrics()) {
        // Draw placeholder text
        QFont f = painter.font();
        f.setPixelSize(11);
        f = Appearance::improveFont(f);
        painter.setFont(f);
        painter.setPen(Appearance::grayColor());
        painter.drawText(rect(), Qt::AlignCenter, "No lyrics — add Text or Lyric events to see them here");
        return;
    }

    // === Metadata block (grey "Lyric Settings" indicator at tick 0) ===
    {
        int metaX1 = xPosOfTick(0);
        int metaX2 = metaX1 + 100; // fixed 100px width
        if (metaX2 > 0 && metaX1 < width()) {
            int drawX1 = qMax(metaX1, 0);
            int drawX2 = qMin(metaX2, width());
            int margin = 6;
            QRect metaRect(drawX1, margin, drawX2 - drawX1, height() - margin * 2);

            // Grey block with slight transparency
            QColor metaColor(140, 140, 140, mgr->hasMetadata() ? 200 : 120);
            painter.setPen(Qt::NoPen);
            painter.setBrush(metaColor);
            painter.drawRoundedRect(metaRect, 4, 4);

            // Dashed border
            QPen dashPen(QColor(100, 100, 100, 180));
            dashPen.setStyle(Qt::DashLine);
            painter.setPen(dashPen);
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(metaRect, 4, 4);

            // Draw gear icon + text
            QFont metaFont = painter.font();
            metaFont.setPixelSize(qBound(8, height() - 16, 11));
            metaFont = Appearance::improveFont(metaFont);
            painter.setFont(metaFont);
            painter.setPen(Qt::white);
            QRect textRect = metaRect.adjusted(2, 0, -2, 0);
            painter.drawText(textRect, Qt::AlignVCenter | Qt::AlignHCenter,
                             QString::fromUtf8("\u2699 Settings"));
        }
    }

    // Determine the current playback tick for highlighting
    int playbackTick = -1;
    if (_currentPlaybackMs >= 0 && _file) {
        playbackTick = _file->tick(_currentPlaybackMs);
    }

    // Dynamic font size based on widget height
    int margin = 6;
    int blockHeight = height() - margin * 2;
    if (blockHeight < 10) blockHeight = 10;

    QFont lyricFont = painter.font();
    int fontSize = qBound(9, blockHeight - 10, 18);
    lyricFont.setPixelSize(fontSize);
    lyricFont = Appearance::improveFont(lyricFont);
    painter.setFont(lyricFont);
    QFontMetrics fm(lyricFont);

    // Paint each lyric block using stored start/end ticks
    for (int i = 0; i < mgr->count(); i++) {
        LyricBlock block = mgr->blockAt(i);
        TextEvent *te = dynamic_cast<TextEvent *>(block.sourceEvent);
        QString text = block.text;
        if (text.isEmpty() && te)
            text = te->text();
        if (text.isEmpty())
            continue;

        // Block start/end x — using stored endTick for consistency with hit-test
        int x1 = xPosOfTick(block.startTick);
        int x2 = xPosOfTick(block.endTick);
        // Leave a small gap between blocks
        x2 -= 2;

        // Skip blocks that are entirely off-screen
        if (x2 < 0 || x1 > width()) continue;

        // Clamp to visible area
        int drawX1 = qMax(x1, 0);
        int drawX2 = qMin(x2, width());
        if (drawX2 - drawX1 < 4) continue;

        // Determine if this block is currently playing
        bool isPlaying = false;
        if (playbackTick >= 0) {
            isPlaying = (playbackTick >= block.startTick && playbackTick < block.endTick);
        }

        // Block color — fixed color or track color based on settings
        QColor blockColor;
        if (Appearance::useFixedLyricColor()) {
            blockColor = Appearance::fixedLyricColor();
        } else if (te && te->track()) {
            blockColor = *Appearance::trackColor(te->track()->number());
        } else {
            blockColor = Appearance::foregroundColor();
        }

        // Playing block: pop effect — expand vertically, brighter color, glow border
        QRect blockRect;
        if (isPlaying) {
            blockColor = blockColor.lighter(150);
            blockColor.setAlpha(240);
            // Expand block: 2px extra in each direction for "pop" effect
            int popMargin = qMax(margin - 3, 1);
            blockRect = QRect(drawX1, popMargin, drawX2 - drawX1, height() - popMargin * 2);
        } else {
            blockColor.setAlpha(140);
            blockRect = QRect(drawX1, margin, drawX2 - drawX1, blockHeight);
        }

        // Draw block shadow for playing block (subtle depth effect)
        if (isPlaying) {
            QColor shadowColor(0, 0, 0, 60);
            painter.setPen(Qt::NoPen);
            painter.setBrush(shadowColor);
            painter.drawRoundedRect(blockRect.adjusted(2, 2, 2, 2), 4, 4);
        }

        // Draw block rectangle
        painter.setPen(Qt::NoPen);
        painter.setBrush(blockColor);
        painter.drawRoundedRect(blockRect, 4, 4);

        // Draw border — playing block gets a bright glow border
        if (isPlaying) {
            QPen borderPen(QColor(255, 255, 255, 200));
            borderPen.setWidth(2);
            painter.setPen(borderPen);
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(blockRect.adjusted(1, 1, -1, -1), 4, 4);
        } else {
            // Subtle border for non-playing blocks
            QColor borderColor = blockColor.darker(130);
            borderColor.setAlpha(100);
            painter.setPen(borderColor);
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(blockRect, 4, 4);
        }

        // Draw text — choose contrasting color
        QColor textColor;
        if (isPlaying) {
            textColor = (blockColor.lightness() > 160) ? Qt::black : Qt::white;
        } else {
            textColor = (blockColor.lightness() > 128) ? QColor(40, 40, 40) : QColor(220, 220, 220);
        }
        painter.setPen(textColor);

        // Use bold font for playing block
        QFont blockFont = lyricFont;
        if (isPlaying) {
            blockFont.setBold(true);
            painter.setFont(blockFont);
        }

        // Elide text if block is too narrow
        QFontMetrics blockFm(blockFont);
        QRect textRect = blockRect.adjusted(4, 0, -4, 0);
        QString elidedText = blockFm.elidedText(text, Qt::ElideRight, textRect.width());
        painter.drawText(textRect, Qt::AlignVCenter | Qt::AlignHCenter, elidedText);

        // Reset font if we changed it
        if (isPlaying) {
            painter.setFont(lyricFont);
        }
    }

    // Draw playback cursor line
    if (_currentPlaybackMs >= 0) {
        int cursorX = _matrixWidget->xPosOfMs(_currentPlaybackMs) - LEFT_BORDER;
        if (cursorX >= 0 && cursorX <= width()) {
            QPen cursorPen(Appearance::cursorLineColor());
            cursorPen.setWidth(2);
            painter.setPen(cursorPen);
            painter.drawLine(cursorX, 0, cursorX, height());
        }
    }

    // Draw selection highlight on selected block(s)
    if (_file && _file->lyricManager()) {
        // Merge single selection into multi-select set (read-only local copy)
        QSet<int> selIndices = _selectedBlockIndices;
        if (_selectedBlockIndex >= 0 && _selectedBlockIndex < mgr->count()) {
            selIndices.insert(_selectedBlockIndex);
        }
        for (int idx : selIndices) {
            if (idx < 0 || idx >= mgr->count()) continue;
            LyricBlock block = mgr->blockAt(idx);
            int sx1 = xPosOfTick(block.startTick);
            int sx2 = xPosOfTick(block.endTick);
            if (sx2 > 0 && sx1 < width()) {
                QPen selPen(QColor(0, 120, 255, 200));
                selPen.setWidth(2);
                painter.setPen(selPen);
                painter.setBrush(Qt::NoBrush);
                painter.drawRoundedRect(QRect(sx1, 2, sx2 - sx1, height() - 4), 4, 4);
            }
        }
    }
}

int LyricTimelineWidget::blockIndexAtPos(int x, int /*y*/)
{
    if (!_file || !_file->lyricManager())
        return -1;

    LyricManager *mgr = _file->lyricManager();
    int tick = tickOfXPos(x);

    for (int i = 0; i < mgr->count(); i++) {
        LyricBlock block = mgr->blockAt(i);
        if (tick >= block.startTick && tick < block.endTick)
            return i;
    }
    return -1;
}

LyricTimelineWidget::DragMode LyricTimelineWidget::dragModeForPos(int x, int blockIndex)
{
    if (blockIndex < 0 || !_file || !_file->lyricManager())
        return NoDrag;

    LyricBlock block = _file->lyricManager()->blockAt(blockIndex);
    int leftX = xPosOfTick(block.startTick);
    int rightX = xPosOfTick(block.endTick);

    if (qAbs(x - leftX) <= EDGE_ZONE)
        return DragResizeLeft;
    if (qAbs(x - rightX) <= EDGE_ZONE)
        return DragResizeRight;
    return DragMove;
}

void LyricTimelineWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        PaintWidget::mousePressEvent(event);
        return;
    }

    setFocus();

    int x = event->pos().x();
    int y = event->pos().y();
    int idx = blockIndexAtPos(x, y);

    bool shiftHeld = (event->modifiers() & Qt::ShiftModifier);

    if (shiftHeld && idx >= 0) {
        // Shift+Click: toggle multi-select or range select
        if (_selectedBlockIndices.contains(idx)) {
            _selectedBlockIndices.remove(idx);
            if (_selectedBlockIndex == idx)
                _selectedBlockIndex = _selectedBlockIndices.isEmpty() ? -1 : *_selectedBlockIndices.begin();
        } else {
            // Range select: from last selected to clicked
            if (_selectedBlockIndex >= 0) {
                int lo = qMin(_selectedBlockIndex, idx);
                int hi = qMax(_selectedBlockIndex, idx);
                for (int i = lo; i <= hi; i++)
                    _selectedBlockIndices.insert(i);
            } else {
                _selectedBlockIndices.insert(idx);
            }
            _selectedBlockIndex = idx;
        }
        _dragMode = NoDrag;
    } else if (idx >= 0 && _file && _file->lyricManager()) {
        // Normal click: select single block, start drag
        if (!_selectedBlockIndices.contains(idx)) {
            _selectedBlockIndices.clear();
            _selectedBlockIndices.insert(idx);
        }
        _selectedBlockIndex = idx;

        LyricBlock block = _file->lyricManager()->blockAt(idx);
        _dragMode = dragModeForPos(x, idx);
        _dragStartX = x;
        _dragStartTick = tickOfXPos(x);
        _dragOrigStartTick = block.startTick;
        _dragOrigEndTick = block.endTick;
        _dragActive = false;  // Not yet — set to true on first actual move
    } else {
        // Click on empty area: deselect
        _selectedBlockIndex = -1;
        _selectedBlockIndices.clear();
        _dragMode = NoDrag;
    }

    update();
    event->accept();
}

void LyricTimelineWidget::mouseMoveEvent(QMouseEvent *event)
{
    int x = event->pos().x();
    int y = event->pos().y();

    // Update cursor shape based on hover position
    if (_dragMode == NoDrag) {
        int idx = blockIndexAtPos(x, y);
        if (idx >= 0) {
            DragMode mode = dragModeForPos(x, idx);
            if (mode == DragResizeLeft || mode == DragResizeRight)
                setCursor(Qt::SizeHorCursor);
            else
                setCursor(Qt::OpenHandCursor);
        } else {
            setCursor(Qt::ArrowCursor);
        }
        return;
    }

    // Active drag
    if (!_file || !_file->lyricManager() || _selectedBlockIndex < 0)
        return;

    // Start Protocol action on first actual move (LYRIC-005 fix)
    if (!_dragActive) {
        _dragActive = true;
        if (_file->protocol()) {
            _file->protocol()->startNewAction("Edit Lyric Block");
        }
    }

    int currentTick = tickOfXPos(x);
    int deltaTick = currentTick - _dragStartTick;
    LyricManager *mgr = _file->lyricManager();

    if (_dragMode == DragMove) {
        setCursor(Qt::ClosedHandCursor);

        if (_selectedBlockIndices.size() > 1) {
            // Multi-select move: move all selected blocks by the same delta
            // We need to move them without re-sorting to avoid index invalidation
            for (int idx : _selectedBlockIndices) {
                if (idx < 0 || idx >= mgr->count()) continue;
                LyricBlock block = mgr->blockAt(idx);
                int newStart = qMax(0, block.startTick + deltaTick);
                mgr->moveBlockDirect(idx, newStart);
            }
            _dragStartTick = currentTick;
        } else {
            int newStart = qMax(0, _dragOrigStartTick + deltaTick);
            mgr->moveBlockDirect(_selectedBlockIndex, newStart);
        }
        update();
    } else if (_dragMode == DragResizeLeft) {
        int newStart = qMax(0, _dragOrigStartTick + deltaTick);
        if (newStart < _dragOrigEndTick - 10) {
            mgr->moveBlockDirect(_selectedBlockIndex, newStart);
            mgr->resizeBlockDirect(_selectedBlockIndex, _dragOrigEndTick);
            update();
        }
    } else if (_dragMode == DragResizeRight) {
        int newEnd = qMax(_dragOrigStartTick + 10, _dragOrigEndTick + deltaTick);
        mgr->resizeBlockDirect(_selectedBlockIndex, newEnd);
        update();
    }

    event->accept();
}

void LyricTimelineWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && _dragMode != NoDrag) {
        // End Protocol action if a drag was actually performed
        if (_dragActive && _file && _file->protocol()) {
            _file->protocol()->endAction();
        }
        _dragMode = NoDrag;
        _dragActive = false;
        setCursor(Qt::ArrowCursor);
        update();
        event->accept();
        return;
    }
    PaintWidget::mouseReleaseEvent(event);
}

void LyricTimelineWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;

    int x = event->pos().x();
    int y = event->pos().y();

    // Check if double-click is on the metadata block (first 100px from tick 0)
    if (_file && _file->lyricManager() && _file->lyricManager()->hasLyrics()) {
        int metaX1 = xPosOfTick(0);
        int metaX2 = metaX1 + 100;
        if (x >= metaX1 && x <= metaX2) {
            LyricManager *mgr = _file->lyricManager();
            LyricMetadataDialog dlg(mgr->metadata(), this);
            if (dlg.exec() == QDialog::Accepted) {
                mgr->setMetadata(dlg.result());
                update();
            }
            event->accept();
            return;
        }
    }

    int idx = blockIndexAtPos(x, y);

    if (idx >= 0 && _file && _file->lyricManager()) {
        // Edit text inline
        LyricBlock block = _file->lyricManager()->blockAt(idx);
        int x1 = xPosOfTick(block.startTick);
        int x2 = xPosOfTick(block.endTick);

        if (!_inlineEditor) {
            _inlineEditor = new QLineEdit(this);
            _inlineEditor->installEventFilter(this);
            connect(_inlineEditor, &QLineEdit::editingFinished, this, &LyricTimelineWidget::onInlineEditFinished);
        }

        _selectedBlockIndex = idx;
        _selectedBlockIndices.clear();
        _selectedBlockIndices.insert(idx);
        _inlineEditor->setText(block.text);
        _inlineEditor->setGeometry(x1, 2, qMax(x2 - x1, 80), height() - 4);
        _inlineEditor->setAlignment(Qt::AlignCenter);
        _inlineEditor->selectAll();
        _inlineEditor->show();
        _inlineEditor->setFocus();
    } else if (_file && _file->lyricManager()) {
        // Double-click on empty area: insert new block
        // Guard: don't insert if LyricManager says this tick is inside a block (LYRIC-003)
        int tick = tickOfXPos(x);
        if (_file->lyricManager()->indexAtTick(tick) >= 0)
            return;

        LyricBlock newBlock;
        newBlock.startTick = tick;
        newBlock.endTick = tick + 480;
        newBlock.text = tr("New lyric");

        // Clamp endTick to avoid overlapping the next block (P2-007)
        const auto &allBlocks = _file->lyricManager()->allBlocks();
        for (const auto &b : allBlocks) {
            if (b.startTick > tick) {
                if (newBlock.endTick > b.startTick)
                    newBlock.endTick = b.startTick;
                break;
            }
        }
        if (newBlock.endTick <= newBlock.startTick)
            return;

        _file->lyricManager()->addBlock(newBlock);
        update();
    }

    event->accept();
}

void LyricTimelineWidget::onInlineEditFinished()
{
    if (!_inlineEditor || !_inlineEditor->isVisible() || _selectedBlockIndex < 0)
        return;

    _inlineEditor->hide(); // Hide FIRST to prevent re-entry via focus-loss

    if (_file && _file->lyricManager()) {
        QString newText = _inlineEditor->text().trimmed();
        if (!newText.isEmpty()) {
            _file->lyricManager()->editBlockText(_selectedBlockIndex, newText);
        }
    }

    update();
}

void LyricTimelineWidget::keyPressEvent(QKeyEvent *event)
{
    if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace)
        && _file && _file->lyricManager()) {

        LyricManager *mgr = _file->lyricManager();

        if (!_selectedBlockIndices.isEmpty()) {
            // Delete all selected blocks (reverse order to keep indices valid)
            QList<int> sorted = _selectedBlockIndices.values();
            std::sort(sorted.begin(), sorted.end(), std::greater<int>());

            if (_file->protocol())
                _file->protocol()->startNewAction("Delete Lyric Blocks");

            for (int idx : sorted) {
                if (idx >= 0 && idx < mgr->count())
                    mgr->removeBlockDirect(idx);
            }

            if (_file->protocol())
                _file->protocol()->endAction();

            _selectedBlockIndex = -1;
            _selectedBlockIndices.clear();
            update();
            event->accept();
            return;
        } else if (_selectedBlockIndex >= 0 && _selectedBlockIndex < mgr->count()) {
            mgr->removeBlock(_selectedBlockIndex);
            _selectedBlockIndex = -1;
            _selectedBlockIndices.clear();
            update();
            event->accept();
            return;
        }
    }
    PaintWidget::keyPressEvent(event);
}

bool LyricTimelineWidget::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == _inlineEditor && event->type() == QEvent::KeyPress) {
        QKeyEvent *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_Escape) {
            _inlineEditor->hide();
            setFocus();
            return true;
        }
    }
    return PaintWidget::eventFilter(obj, event);
}

void LyricTimelineWidget::contextMenuEvent(QContextMenuEvent *event)
{
    if (!_file || !_file->lyricManager())
        return;

    int x = event->pos().x();
    int y = event->pos().y();
    int idx = blockIndexAtPos(x, y);
    LyricManager *mgr = _file->lyricManager();

    QMenu menu(this);

    if (idx >= 0) {
        _selectedBlockIndex = idx;
        LyricBlock block = mgr->blockAt(idx);

        QAction *editAction = menu.addAction(tr("Edit Text..."));
        QAction *deleteAction = menu.addAction(tr("Delete Block"));
        menu.addSeparator();
        QAction *splitAction = menu.addAction(tr("Split at Cursor"));
        QAction *mergeAction = menu.addAction(tr("Merge with Next"));
        mergeAction->setEnabled(idx + 1 < mgr->count());
        menu.addSeparator();
        QAction *insertBeforeAction = menu.addAction(tr("Insert Block Before"));
        QAction *insertAfterAction = menu.addAction(tr("Insert Block After"));

        QAction *chosen = menu.exec(event->globalPos());
        if (!chosen) return;

        if (chosen == editAction) {
            bool ok;
            QString newText = QInputDialog::getText(this, tr("Edit Lyric"),
                tr("Lyric text:"), QLineEdit::Normal, block.text, &ok);
            if (ok && !newText.isEmpty())
                mgr->editBlockText(idx, newText);
        } else if (chosen == deleteAction) {
            mgr->removeBlock(idx);
            _selectedBlockIndex = -1;
            _selectedBlockIndices.clear();
        } else if (chosen == splitAction) {
            int splitTick = tickOfXPos(x);
            if (splitTick > block.startTick && splitTick < block.endTick) {
                // Wrap split in single Protocol action (P2-005)
                if (_file->protocol())
                    _file->protocol()->startNewAction("Split Lyric Block");

                // Resize original to end at split point
                mgr->resizeBlockDirect(idx, splitTick);

                // Create second half with new MIDI event
                LyricBlock secondHalf;
                secondHalf.startTick = splitTick;
                secondHalf.endTick = block.endTick;
                secondHalf.text = block.text;
                secondHalf.trackIndex = block.trackIndex;

                if (_file->numTracks() > 0) {
                    MidiTrack *track = _file->track(0);
                    if (block.trackIndex >= 0 && block.trackIndex < _file->numTracks())
                        track = _file->track(block.trackIndex);
                    TextEvent *te = new TextEvent(16, track);
                    te->setText(secondHalf.text);
                    te->setType(TextEvent::LYRIK);
                    _file->channel(16)->insertEvent(te, splitTick);
                    secondHalf.sourceEvent = te;
                }
                mgr->addBlockDirect(secondHalf);

                if (_file->protocol())
                    _file->protocol()->endAction();
            }
        } else if (chosen == mergeAction) {
            LyricBlock nextBlock = mgr->blockAt(idx + 1);
            QString mergedText = block.text + " " + nextBlock.text;

            if (_file->protocol())
                _file->protocol()->startNewAction("Merge Lyric Blocks");
            mgr->resizeBlockDirect(idx, nextBlock.endTick);
            mgr->editBlockTextDirect(idx, mergedText);
            mgr->removeBlockDirect(idx + 1);
            if (_file->protocol())
                _file->protocol()->endAction();
        } else if (chosen == insertBeforeAction) {
            LyricBlock newBlock;
            newBlock.endTick = block.startTick;
            newBlock.startTick = qMax(0, block.startTick - 480);
            newBlock.text = tr("New lyric");
            // Clamp startTick to avoid overlapping the previous block (P2-007)
            if (idx > 0) {
                int prevEnd = mgr->blockAt(idx - 1).endTick;
                if (newBlock.startTick < prevEnd)
                    newBlock.startTick = prevEnd;
            }
            if (newBlock.startTick >= newBlock.endTick)
                return;
            mgr->addBlock(newBlock);
        } else if (chosen == insertAfterAction) {
            LyricBlock newBlock;
            newBlock.startTick = block.endTick;
            newBlock.endTick = block.endTick + 480;
            newBlock.text = tr("New lyric");
            // Clamp endTick to avoid overlapping the next block (P2-007)
            if (idx + 1 < mgr->count()) {
                int nextStart = mgr->blockAt(idx + 1).startTick;
                if (newBlock.endTick > nextStart)
                    newBlock.endTick = nextStart;
            }
            if (newBlock.endTick <= newBlock.startTick)
                return;
            mgr->addBlock(newBlock);
        }

        update();
    } else {
        // Empty area context menu
        QAction *insertAction = menu.addAction(tr("Insert Block Here"));
        menu.addSeparator();
        QAction *settingsAction = menu.addAction(tr("Lyric Settings..."));

        QAction *chosen = menu.exec(event->globalPos());
        if (chosen == insertAction) {
            int tick = tickOfXPos(x);
            LyricBlock newBlock;
            newBlock.startTick = tick;
            newBlock.endTick = tick + 480;
            newBlock.text = tr("New lyric");
            mgr->addBlock(newBlock);
            update();
        } else if (chosen == settingsAction) {
            LyricMetadataDialog dlg(mgr->metadata(), this);
            if (dlg.exec() == QDialog::Accepted) {
                mgr->setMetadata(dlg.result());
                update();
            }
        }
    }
}
