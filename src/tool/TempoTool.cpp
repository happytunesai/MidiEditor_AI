#include "TempoTool.h"

#include "EventTool.h"
#include "../gui/MatrixWidget.h"
#include "../gui/TempoDialog.h"
#include "../gui/Appearance.h"
#include "../midi/MidiFile.h"


TempoTool::TempoTool()
    : EventTool() {
    setImage(":/run_environment/graphics/tool/tempo.png");
    setToolTipText("Edit tempo");
    _startX = -1;
}

TempoTool::TempoTool(TempoTool &other)
    : EventTool(other) {
    // copy like the other tools instead of delegating to the default constructor, which
    // allocated a fresh icon per snapshot that nothing freed
    _startX = other._startX;
}

void TempoTool::draw(QPainter *painter) {
    if (_startX > -1) {
        int start = rasteredX(_startX);
        int end = rasteredX(mouseX);
        painter->setOpacity(0.5);
        painter->fillRect(start, 0, end - start, matrixWidget->height(), Appearance::tempoToolHighlightColor());
    } else {
        int x = rasteredX(mouseX);
        painter->setPen(Appearance::foregroundColor());
        painter->drawLine(x, 0, x, matrixWidget->height());
    }
}

bool TempoTool::press(bool leftClick) {
    _startX = rasteredX(mouseX);
    return true;
}

bool TempoTool::release() {
    // WHY: MatrixWidget delivers release() for any release inside the tool area, even when the
    // matching press never reached this tool (piano column / ruler). _startX is the press
    // sentinel; without a press there is no gesture, and msOfXPos(-1) would otherwise turn the
    // release into a range edit from the left viewport edge.
    if (_startX < 0) {
        return true;
    }
    int endTick;
    rasteredX(mouseX, &endTick);
    int startTick = file()->tick(matrixWidget->msOfXPos(_startX));

    // Make sure order is correct
    if (endTick < startTick) {
        int tmp = endTick;
        endTick = startTick;
        startTick = tmp;
    }

    if (startTick == endTick) {
        TempoDialog dialog(file(), endTick);
        dialog.exec();
    } else {
        TempoDialog dialog(file(), startTick, endTick);
        dialog.exec();
    }

    _startX = -1;
    return true;
}

bool TempoTool::releaseOnly() {
    _startX = -1;
    return false;
}

bool TempoTool::move(int mouseX, int mouseY) {
    EventTool::move(mouseX, mouseY);
    return true;
}

ProtocolEntry *TempoTool::copy() {
    return new TempoTool(*this);
}

void TempoTool::reloadState(ProtocolEntry *entry) {
    TempoTool *other = dynamic_cast<TempoTool *>(entry);
    if (!other) {
        return;
    }
    EventTool::reloadState(entry);
}
