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

#include "Tool.h"
#include "../midi/MidiFile.h"
#include "../protocol/Protocol.h"
#include "EditorTool.h"
#include "ToolButton.h"

EditorTool *Tool::_currentTool = 0;
MidiFile *Tool::_currentFile = 0;
// Phase 9.9f §15.2: tool-change observer (collab follow-the-host).
// Null by default; MainWindow registers a callback that triggers a
// viewState broadcast. See Tool::setToolChangedCallback.
static Tool::ToolChangedFn s_toolChangedCb = nullptr;

Tool::Tool() {
    _image = 0;
    _imagePath = "";
    _button = 0;
    _toolTip = "";
    _standardTool = 0;
}

Tool::Tool(Tool &other) {
    _image = other._image ? new QImage(*other._image) : nullptr;
    _imagePath = other._imagePath;
    _button = other._button;
    _toolTip = other._toolTip;
    _standardTool = other._standardTool;
}

void Tool::buttonClick() {
    return;
}

void Tool::setImage(QString name) {
    _imagePath = name;
    delete _image;
    _image = new QImage(name);
}

QImage *Tool::image() {
    return _image;
}

QString Tool::iconPath() const {
    return _imagePath;
}

void Tool::setToolTipText(QString text) {
    _toolTip = text;
}

bool Tool::selected() {
    return false;
}

QString Tool::toolTip() {
    return _toolTip;
}

ProtocolEntry *Tool::copy() {
    Tool *t = new Tool(*this);
    return t;
}

void Tool::reloadState(ProtocolEntry *entry) {
    Tool *other = (Tool *) entry;
    if (!other) {
        return;
    }
    _image = other->_image;
    _imagePath = other->_imagePath;
    _button = other->_button;
    _toolTip = other->_toolTip;
    _standardTool = other->_standardTool;
}

MidiFile *Tool::currentFile() {
    return _currentFile;
}

MidiFile *Tool::file() {
    return currentFile();
}

void Tool::setCurrentTool(EditorTool *tool) {
    _currentTool = tool;
    _currentTool->select();
    if (s_toolChangedCb) s_toolChangedCb(tool);
}

void Tool::setToolChangedCallback(ToolChangedFn cb) {
    s_toolChangedCb = cb;
}

Protocol *Tool::currentProtocol() {
    if (currentFile()) {
        return currentFile()->protocol();
    }
    return 0;
}

void Tool::setButton(ToolButton *b) {
    _button = b;
}

ToolButton *Tool::button() {
    return _button;
}

EditorTool *Tool::currentTool() {
    return _currentTool;
}

void Tool::setStandardTool(StandardTool *stdTool) {
    _standardTool = stdTool;
}

void Tool::setFile(MidiFile *file) {
    _currentFile = file;
}
