#include "ChannelVisibilityManager.h"

ChannelVisibilityManager::ChannelVisibilityManager() {
    // Initialize the default (no-document) state as all-visible.
    for (int i = 0; i < 19; i++) {
        channelVisibility[i] = true;
    }
}

ChannelVisibilityManager &ChannelVisibilityManager::instance() {
    static ChannelVisibilityManager instance;
    return instance;
}

bool *ChannelVisibilityManager::activeArray() {
    if (!_activeFile) {
        return channelVisibility;
    }
    auto it = _perFile.find(_activeFile);
    if (it == _perFile.end()) {
        // First access for this document: default to all channels visible.
        State s;
        for (int i = 0; i < 19; i++) {
            s.v[i] = true;
        }
        it = _perFile.insert(_activeFile, s);
    }
    return it->v;
}

void ChannelVisibilityManager::setActiveFile(MidiFile *file) {
    _activeFile = file;
}

void ChannelVisibilityManager::forgetFile(MidiFile *file) {
    if (file) {
        _perFile.remove(file);
    }
    if (_activeFile == file) {
        _activeFile = nullptr;
    }
}

bool ChannelVisibilityManager::isChannelVisible(int channel) {
    // Bounds checking
    if (channel < 0 || channel >= 19) {
        return true; // Default to visible for invalid channels
    }

    bool *vis = activeArray();

    // Special inheritance: channels > 16 inherit from channel 16
    if (channel > 16) {
        return vis[16];
    }

    return vis[channel];
}

void ChannelVisibilityManager::setChannelVisible(int channel, bool visible) {
    // Bounds checking
    if (channel < 0 || channel >= 19) {
        return; // Ignore invalid channels
    }

    activeArray()[channel] = visible;
}

void ChannelVisibilityManager::resetAllVisible() {
    bool *vis = activeArray();
    for (int i = 0; i < 19; i++) {
        vis[i] = true;
    }
}
