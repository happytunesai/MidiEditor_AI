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

#include "DocumentTabBar.h"

#include <QApplication>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QPixmap>

QString DocumentTabBar::tabMimeType() {
    // In-process mime type for a tab drag; payload = source tab index as text,
    // the source bar comes from QDropEvent::source().
    return QStringLiteral("application/x-midieditor-tab");
}

DocumentTabBar::DocumentTabBar(QWidget *parent) : QTabBar(parent) {
    // We implement our own drag (reorder + move-to-other-group), so the built-in
    // intra-bar mover stays off to avoid two movers fighting over the gesture.
    setMovable(false);
    setAcceptDrops(true);
}

int DocumentTabBar::dropIndexAt(const QPoint &pos) const {
    const int over = tabAt(pos);
    if (over < 0) {
        return count(); // past the last tab -> append
    }
    // Drop on the left half inserts before the tab, on the right half after it.
    const QRect r = tabRect(over);
    return pos.x() < r.center().x() ? over : over + 1;
}

void DocumentTabBar::setDropIndicator(int gap) {
    if (_dropIndicator == gap) {
        return;
    }
    _dropIndicator = gap;
    update();
}

void DocumentTabBar::showAppendDropIndicator() {
    setDropIndicator(count());
}

void DocumentTabBar::clearDropIndicator() {
    setDropIndicator(-1);
}

void DocumentTabBar::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        _pressPos = event->position().toPoint();
        _pressIndex = tabAt(_pressPos);
    }
    // Keep normal click-to-select behaviour.
    QTabBar::mousePressEvent(event);
}

void DocumentTabBar::mouseMoveEvent(QMouseEvent *event) {
    // Only a left-button drag that started on a real tab and moved past the
    // platform drag threshold becomes a tab drag; everything else is normal.
    if (!(event->buttons() & Qt::LeftButton) || _pressIndex < 0) {
        QTabBar::mouseMoveEvent(event);
        return;
    }
    if ((event->position().toPoint() - _pressPos).manhattanLength() <
        QApplication::startDragDistance()) {
        QTabBar::mouseMoveEvent(event);
        return;
    }

    const int dragIndex = _pressIndex;
    _pressIndex = -1; // consume so we start at most one drag per press

    QDrag *drag = new QDrag(this);
    QMimeData *mime = new QMimeData();
    mime->setData(tabMimeType(), QByteArray::number(dragIndex));
    drag->setMimeData(mime);

    // A ghost of the dragged tab follows the cursor.
    const QRect r = tabRect(dragIndex);
    if (r.isValid()) {
        QPixmap pm(r.size());
        pm.fill(Qt::transparent);
        render(&pm, QPoint(), QRegion(r));
        drag->setPixmap(pm);
        drag->setHotSpot(_pressPos - r.topLeft());
    }

    drag->exec(Qt::MoveAction);
    setDropIndicator(-1); // clear any leftover caret if the drag was cancelled
}

void DocumentTabBar::dragEnterEvent(QDragEnterEvent *event) {
    if (event->mimeData()->hasFormat(tabMimeType()) &&
        qobject_cast<DocumentTabBar *>(event->source())) {
        event->acceptProposedAction();
        return;
    }
    QTabBar::dragEnterEvent(event);
}

void DocumentTabBar::dragMoveEvent(QDragMoveEvent *event) {
    if (event->mimeData()->hasFormat(tabMimeType())) {
        // Show where the tab would land (the usual insertion caret).
        setDropIndicator(dropIndexAt(event->position().toPoint()));
        event->acceptProposedAction();
        return;
    }
    QTabBar::dragMoveEvent(event);
}

void DocumentTabBar::dragLeaveEvent(QDragLeaveEvent *event) {
    setDropIndicator(-1);
    QTabBar::dragLeaveEvent(event);
}

void DocumentTabBar::dropEvent(QDropEvent *event) {
    DocumentTabBar *src = qobject_cast<DocumentTabBar *>(event->source());
    if (!src || !event->mimeData()->hasFormat(tabMimeType())) {
        QTabBar::dropEvent(event);
        return;
    }
    const int srcIndex = event->mimeData()->data(tabMimeType()).toInt();
    const int dropIndex = dropIndexAt(event->position().toPoint());
    setDropIndicator(-1);
    event->acceptProposedAction();
    emit tabMoveRequested(src, srcIndex, this, dropIndex);
}

void DocumentTabBar::paintEvent(QPaintEvent *event) {
    QTabBar::paintEvent(event);
    if (_dropIndicator < 0) {
        return;
    }
    // A vertical caret at the insertion gap.
    int x;
    if (count() == 0) {
        x = 0;
    } else if (_dropIndicator >= count()) {
        x = tabRect(count() - 1).right();
    } else {
        x = tabRect(_dropIndicator).left();
    }
    QPainter p(this);
    p.setPen(QPen(QColor(QStringLiteral("#3daee9")), 2));
    p.drawLine(x, 0, x, height());
}
