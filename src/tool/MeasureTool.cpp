#include "MeasureTool.h"

#include <QInputDialog>

#include "EventTool.h"
#include "../gui/MatrixWidget.h"
#include "../gui/Appearance.h"
#include "../midi/MidiFile.h"
#include "../protocol/Protocol.h"

MeasureTool::MeasureTool()
    : EventTool() {
    setImage(":/run_environment/graphics/tool/measure.png");
    setToolTipText("Insert or delete measures");
    _firstSelectedMeasure = -1;
    _secondSelectedMeasure = -1;
    _selectionFile = nullptr;
}

MeasureTool::MeasureTool(MeasureTool &other) : MeasureTool() {
    _firstSelectedMeasure = other._firstSelectedMeasure;
    _secondSelectedMeasure = other._secondSelectedMeasure;
    _selectionFile = other._selectionFile;
}

void MeasureTool::dropMeasureSelectionOfOtherFile() {
    // Tabs: this tool is one process-wide instance and nothing resets it when
    // the active document changes, so a range picked in document A stayed armed
    // in document B - where Delete removed B's bar at A's index. Measure indices
    // are only meaningful for the file they were computed in.
    if (_firstSelectedMeasure > -1 && _selectionFile != file()) {
        _firstSelectedMeasure = -1;
        _secondSelectedMeasure = -1;
        _selectionFile = nullptr;
    }
}

void MeasureTool::draw(QPainter *painter) {
    dropMeasureSelectionOfOtherFile();
    if (_firstSelectedMeasure > -1) {
        painter->setOpacity(0.3);
        fillMeasures(painter, _firstSelectedMeasure, _secondSelectedMeasure);
    }
    if (_firstSelectedMeasure > -1) {
        int ms = matrixWidget->msOfXPos(mouseX);
        int tick = file()->tick(ms);
        int measureStartTick, measureEndTick;
        int inMeasure = file()->measure(tick, &measureStartTick, &measureEndTick);

        int measureFrom = _firstSelectedMeasure;
        int measureTo;

        if (QApplication::keyboardModifiers().testFlag(Qt::ShiftModifier)) {
            measureTo = inMeasure;
        } else {
            measureTo = _secondSelectedMeasure;
        }

        // Draw selection
        if (measureFrom > measureTo) {
            int tmp = measureFrom;
            measureFrom = measureTo;
            measureTo = tmp;
        }
        int x1 = matrixWidget->xPosOfMs(file()->msOfTick(file()->startTickOfMeasure(measureFrom)));
        int x2 = matrixWidget->xPosOfMs(file()->msOfTick(file()->startTickOfMeasure(measureTo + 1)));
        painter->setOpacity(0.2);
        painter->fillRect(x1, 0, x2 - x1, matrixWidget->height(), Appearance::measureToolHighlightColor());
    }
    int dist, measureX;
    this->closestMeasureStart(&dist, &measureX);
    if (dist < 10) {
        painter->drawLine(measureX - 10, 0, measureX - 10, matrixWidget->height());
        painter->drawLine(measureX + 10, 0, measureX + 10, matrixWidget->height());
    } else {
        int ms = matrixWidget->msOfXPos(mouseX);
        int tick = file()->tick(ms);
        int measureStartTick, measureEndTick;
        file()->measure(tick, &measureStartTick, &measureEndTick);

        int x1 = matrixWidget->xPosOfMs(file()->msOfTick(measureStartTick));
        int x2 = matrixWidget->xPosOfMs(file()->msOfTick(measureEndTick));
        painter->setOpacity(0.5);
        painter->fillRect(x1, 0, x2 - x1, matrixWidget->height(), Appearance::measureToolHighlightColor());
    }
}

bool MeasureTool::press(bool leftClick) {
    return true;
}

bool MeasureTool::release() {
    dropMeasureSelectionOfOtherFile();
    if (_firstSelectedMeasure > -1 && QApplication::keyboardModifiers().testFlag(Qt::ShiftModifier)) {
        int ms = matrixWidget->msOfXPos(mouseX);
        int tick = file()->tick(ms);
        int measureStartTick, measureEndTick;
        _secondSelectedMeasure = file()->measure(tick, &measureStartTick, &measureEndTick);
        return true;
    }
    int dist, measureX;
    int measure = this->closestMeasureStart(&dist, &measureX);
    if (dist < 10) {
        bool ok;
        if (measure < 2) {
            return true;
        }
        // The dialog spins a nested event loop. Anything that switches the
        // active document while it is up (an MCP/collab file open) retargets
        // Tool::file(), so the insert is bound to the document the measure
        // index was computed on and abandoned if that is no longer the target.
        MidiFile *targetFile = file();
        int num = QInputDialog::getInt(matrixWidget, "Insert Measures",
                                       "Number of measures:", 1, 1, 100000, 1, &ok);
        if (ok && targetFile && targetFile == file()) {
            targetFile->protocol()->startNewAction("Insert measures", image());
            targetFile->insertMeasures(measure - 1, num);
            _firstSelectedMeasure = -1;
            _secondSelectedMeasure = -1;
            _selectionFile = nullptr;
            targetFile->protocol()->endAction();
        }
        return true;
    } else {
        int ms = matrixWidget->msOfXPos(mouseX);
        int tick = file()->tick(ms);
        int measureStartTick, measureEndTick;
        _firstSelectedMeasure = file()->measure(tick, &measureStartTick, &measureEndTick);
        _secondSelectedMeasure = _firstSelectedMeasure;
        _selectionFile = file();
        return true;
    }

    _firstSelectedMeasure = -1;
    _secondSelectedMeasure = -1;
    return true;
}

bool MeasureTool::releaseOnly() {
    _firstSelectedMeasure = -1;
    _secondSelectedMeasure = -1;
    _selectionFile = nullptr;
    return false;
}

bool MeasureTool::releaseKey(int key) {
    dropMeasureSelectionOfOtherFile();
    if (key == Qt::Key_Delete && _firstSelectedMeasure > -1) {
        file()->protocol()->startNewAction("Remove measures", image());
        if (_secondSelectedMeasure == -1) {
            _secondSelectedMeasure = _firstSelectedMeasure;
        }
        if (_firstSelectedMeasure > _secondSelectedMeasure) {
            int tmp = _firstSelectedMeasure;
            _firstSelectedMeasure = _secondSelectedMeasure;
            _secondSelectedMeasure = tmp;
        }
        file()->deleteMeasures(_firstSelectedMeasure, _secondSelectedMeasure);
        _firstSelectedMeasure = -1;
        _secondSelectedMeasure = -1;
        _selectionFile = nullptr;
        file()->protocol()->endAction();
        return true;
    }
    return false;
}

bool MeasureTool::move(int mouseX, int mouseY) {
    EventTool::move(mouseX, mouseY);
    return true;
}

ProtocolEntry *MeasureTool::copy() {
    return new MeasureTool(*this);
}

void MeasureTool::reloadState(ProtocolEntry *entry) {
    MeasureTool *other = dynamic_cast<MeasureTool *>(entry);
    if (!other) {
        return;
    }
    EventTool::reloadState(entry);
}

int MeasureTool::closestMeasureStart(int *distX, int *measureX) {
    int ms = matrixWidget->msOfXPos(mouseX);
    int tick = file()->tick(ms);
    int measureStartTick, measureEndTick;
    int measure = file()->measure(tick, &measureStartTick, &measureEndTick);

    int startX = matrixWidget->xPosOfMs(file()->msOfTick(measureStartTick));
    int endX = matrixWidget->xPosOfMs(file()->msOfTick(measureEndTick));

    int distStart = abs(mouseX - startX);
    int distEnd = abs(mouseX - endX);
    if (distEnd < distStart) {
        measure++;
        *distX = distEnd;
        *measureX = endX;
    } else {
        *distX = distStart;
        *measureX = startX;
    }
    return measure;
}

void MeasureTool::fillMeasures(QPainter *painter, int measureFrom, int measureTo) {
    // Draw selection
    if (measureFrom > measureTo) {
        int tmp = measureFrom;
        measureFrom = measureTo;
        measureTo = tmp;
    }
    int x1 = matrixWidget->xPosOfMs(file()->msOfTick(file()->startTickOfMeasure(measureFrom)));
    int x2 = matrixWidget->xPosOfMs(file()->msOfTick(file()->startTickOfMeasure(measureTo + 1)));
    painter->fillRect(x1, 0, x2 - x1, matrixWidget->height(), Appearance::measureToolHighlightColor());
}
