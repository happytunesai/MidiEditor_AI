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

#include "SizeChangeTool.h"

#include "../MidiEvent/MidiEvent.h"
#include "../MidiEvent/OffEvent.h"
#include "../MidiEvent/OnEvent.h"
#include "../gui/Appearance.h"
#include "../gui/MatrixWidget.h"
#include "../midi/MidiFile.h"
#include "../protocol/Protocol.h"
#include "StandardTool.h"

#include "Selection.h"

SizeChangeTool::SizeChangeTool()
    : EventTool() {
    inDrag = false;
    xPos = 0;
    dragsOnEvent = false;
    setImage(":/run_environment/graphics/tool/change_size.png");
    setToolTipText(QObject::tr("Change the duration of the selected event"));
}

SizeChangeTool::SizeChangeTool(SizeChangeTool &other)
    : EventTool(other) {
    return;
}

ProtocolEntry *SizeChangeTool::copy() {
    return new SizeChangeTool(*this);
}

void SizeChangeTool::reloadState(ProtocolEntry *entry) {
    SizeChangeTool *other = dynamic_cast<SizeChangeTool *>(entry);
    if (!other) {
        return;
    }
    EventTool::reloadState(entry);
}

void SizeChangeTool::draw(QPainter *painter) {
    int currentX = rasteredX(mouseX);

    // Helper lambda to set cursor on the correct widget
    auto setCursorOnWidget = [this](Qt::CursorShape shape) {
        if (_openglContainer) {
            _openglContainer->setCursor(shape);
        } else {
            matrixWidget->setCursor(shape);
        }
    };

    setCursorOnWidget(Qt::ArrowCursor);

    // Ghost note colors (DAW-style semi-transparent)
    bool darkMode = Appearance::shouldUseDarkMode();
    QColor ghostFill = darkMode ? QColor(255, 255, 255, 60) : QColor(0, 0, 0, 40);
    QColor ghostBorder = darkMode ? QColor(255, 255, 255, 120) : QColor(0, 0, 0, 80);

    if (!inDrag) {
        paintSelectedEvents(painter);

        // Show split cursor when hovering over note edges (even before clicking)
        foreach (MidiEvent *event, Selection::instance()->selectedEvents()) {
            bool show = event->shown();
            if (!show) {
                OnEvent *ev = dynamic_cast<OnEvent *>(event);
                if (ev) {
                    show = ev->offEvent() && ev->offEvent()->shown();
                }
            }
            if (show) {
                if (pointInRect(mouseX, mouseY, event->x() + event->width() - 2, event->y(),
                                event->x() + event->width() + 2, event->y() + event->height())) {
                    setCursorOnWidget(Qt::SplitHCursor);
                    break;
                }
                if (pointInRect(mouseX, mouseY, event->x() - 2, event->y(),
                                event->x() + 2, event->y() + event->height())) {
                    setCursorOnWidget(Qt::SplitHCursor);
                    break;
                }
            }
        }
        return;
    }

    // During drag: show guide line and ghost notes
    setCursorOnWidget(Qt::SplitHCursor);
    painter->setPen(QPen(Qt::gray, 1, Qt::DashLine));
    painter->drawLine(currentX, 0, currentX, matrixWidget->height());

    int endEventShift = 0;
    int startEventShift = 0;
    if (dragsOnEvent) {
        startEventShift = currentX - xPos;
    } else {
        endEventShift = currentX - xPos;
    }

    foreach (MidiEvent *event, Selection::instance()->selectedEvents()) {
        bool show = event->shown();
        if (!show) {
            OnEvent *ev = dynamic_cast<OnEvent *>(event);
            if (ev) {
                show = ev->offEvent() && ev->offEvent()->shown();
            }
        }
        if (show) {
            // Calculate ghost note position using pixel-accurate coordinates
            int ghostX = event->x() + startEventShift;
            int ghostW = event->width() - startEventShift + endEventShift;

            // Use raw tick-based coordinates for better accuracy when available
            OnEvent *onEvent = dynamic_cast<OnEvent *>(event);
            if (onEvent && onEvent->offEvent()) {
                int rawX = matrixWidget->xPosOfMs(file()->msOfTick(onEvent->midiTime()));
                int rawEndX = matrixWidget->xPosOfMs(file()->msOfTick(onEvent->offEvent()->midiTime()));
                ghostX = rawX + startEventShift;
                ghostW = (rawEndX - rawX) - startEventShift + endEventShift;
            }

            if (ghostW > 0) {
                QRect ghostRect(ghostX, event->y(), ghostW, event->height());
                painter->setBrush(ghostFill);
                painter->setPen(QPen(ghostBorder, 1, Qt::SolidLine));
                painter->drawRoundedRect(ghostRect, 1, 1);
            }
        }
    }
}

bool SizeChangeTool::press(bool leftClick) {
    inDrag = false;

    if (Selection::instance()->selectedEvents().isEmpty()) {
        return false;
    }

    xPos = mouseX;

    // First try to detect edge clicks
    foreach (MidiEvent *event, Selection::instance()->selectedEvents()) {
        // Check left edge (start of note)
        if (pointInRect(mouseX, mouseY, event->x() - 2, event->y(), event->x() + 2, event->y() + event->height())) {
            dragsOnEvent = true;
            xPos = event->x();
            inDrag = true;
            return true;
        }
        // Check right edge (end of note)
        if (pointInRect(mouseX, mouseY, event->x() + event->width() - 2, event->y(), event->x() + event->width() + 2, event->y() + event->height())) {
            dragsOnEvent = false;
            xPos = event->x() + event->width();
            inDrag = true;
            return true;
        }
    }

    // No edge found: left click = drag start edge, right click = drag end edge
    foreach (MidiEvent *event, Selection::instance()->selectedEvents()) {
        if (pointInRect(mouseX, mouseY, event->x(), event->y(),
                        event->x() + event->width(), event->y() + event->height())) {
            if (leftClick) {
                dragsOnEvent = true;
                xPos = event->x();
            } else {
                dragsOnEvent = false;
                xPos = event->x() + event->width();
            }
            inDrag = true;
            return true;
        }
    }

    return false;
}

bool SizeChangeTool::release() {
    int currentX = rasteredX(mouseX);

    inDrag = false;
    int endEventShift = 0;
    int startEventShift = 0;
    if (dragsOnEvent) {
        startEventShift = currentX - xPos;
    } else {
        endEventShift = currentX - xPos;
    }
    xPos = 0;
    if (Selection::instance()->selectedEvents().count() > 0) {
        currentProtocol()->startNewAction(QObject::tr("Change Event Duration"), image());
        foreach(MidiEvent* event, Selection::instance()->selectedEvents()) {
            OnEvent *on = dynamic_cast<OnEvent *>(event);
            OffEvent *off = dynamic_cast<OffEvent *>(event);
            if (on) {
                int onTick = file()->tick(file()->msOfTick(on->midiTime()) - matrixWidget->timeMsOfWidth(-startEventShift));
                int offTick = file()->tick(file()->msOfTick(on->offEvent()->midiTime()) - matrixWidget->timeMsOfWidth(-endEventShift));
                if (onTick < offTick) {
                    if (dragsOnEvent) {
                        changeTick(on, -startEventShift);
                    } else {
                        changeTick(on->offEvent(), -endEventShift);
                    }
                }
            } else if (off) {
                // do nothing; endEvents are shifted when touching their OnEvent
                continue;
            } else if (dragsOnEvent) {
                // normal events will be moved as normal
                changeTick(event, -startEventShift);
            }
        }
        currentProtocol()->endAction();
    }
    // Set cursor on OpenGL container if available, otherwise on matrix widget
    if (_openglContainer) {
        _openglContainer->setCursor(Qt::ArrowCursor);
    } else {
        matrixWidget->setCursor(Qt::ArrowCursor);
    }
    if (_standardTool) {
        Tool::setCurrentTool(_standardTool);
        _standardTool->move(mouseX, mouseY);
        _standardTool->release();
    }
    return true;
}

bool SizeChangeTool::move(int mouseX, int mouseY) {
    EventTool::move(mouseX, mouseY);

    // Helper lambda to set cursor on the correct widget
    auto setCursorOnWidget = [this](Qt::CursorShape shape) {
        if (_openglContainer) {
            _openglContainer->setCursor(shape);
        } else {
            matrixWidget->setCursor(shape);
        }
    };

    foreach (MidiEvent *event, Selection::instance()->selectedEvents()) {
        if (pointInRect(mouseX, mouseY, event->x() - 2, event->y(), event->x() + 2, event->y() + event->height())) {
            setCursorOnWidget(Qt::SplitHCursor);
            return inDrag;
        }
        if (pointInRect(mouseX, mouseY, event->x() + event->width() - 2, event->y(), event->x() + event->width() + 2, event->y() + event->height())) {
            setCursorOnWidget(Qt::SplitHCursor);
            return inDrag;
        }
    }
    setCursorOnWidget(inDrag ? Qt::SplitHCursor : Qt::ArrowCursor);
    return inDrag;
}

bool SizeChangeTool::releaseOnly() {
    inDrag = false;
    xPos = 0;
    return true;
}

bool SizeChangeTool::showsSelection() {
    return true;
}
