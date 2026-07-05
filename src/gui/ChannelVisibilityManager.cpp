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
    return arrayFor(_activeFile);
}

bool *ChannelVisibilityManager::arrayFor(MidiFile *file) {
    if (!file) {
        file = _activeFile;
    }
    if (!file) {
        return channelVisibility;
    }
    auto it = _perFile.find(file);
    if (it == _perFile.end()) {
        // First access for this document: default to all channels visible.
        State s;
        for (int i = 0; i < 19; i++) {
            s.v[i] = true;
        }
        it = _perFile.insert(file, s);
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
    return isChannelVisible(channel, nullptr);
}

bool ChannelVisibilityManager::isChannelVisible(int channel, MidiFile *file) {
    // Bounds checking
    if (channel < 0 || channel >= 19) {
        return true; // Default to visible for invalid channels
    }

    bool *vis = arrayFor(file);

    // Special inheritance: channels > 16 inherit from channel 16
    if (channel > 16) {
        return vis[16];
    }

    return vis[channel];
}

void ChannelVisibilityManager::setChannelVisible(int channel, bool visible) {
    setChannelVisible(channel, visible, nullptr);
}

void ChannelVisibilityManager::setChannelVisible(int channel, bool visible, MidiFile *file) {
    // Bounds checking
    if (channel < 0 || channel >= 19) {
        return; // Ignore invalid channels
    }

    arrayFor(file)[channel] = visible;
}

void ChannelVisibilityManager::resetAllVisible() {
    bool *vis = activeArray();
    for (int i = 0; i < 19; i++) {
        vis[i] = true;
    }
}
