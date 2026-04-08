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

#ifdef FLUIDSYNTH_SUPPORT

#include "FluidSynthEngine.h"

#include <fluidsynth.h>

#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSettings>

// ============================================================================
// Singleton
// ============================================================================

FluidSynthEngine* FluidSynthEngine::instance() {
    static FluidSynthEngine engine;
    return &engine;
}

FluidSynthEngine::FluidSynthEngine()
    : QObject(nullptr),
      _settings(nullptr),
      _synth(nullptr),
      _audioDriver(nullptr),
      _initialized(false),
      _audioDriverName(),
      _reverbEngine("fdn"),
      _gain(0.5),
      _sampleRate(44100.0),
      _reverbEnabled(true),
      _chorusEnabled(true),
      _ffxivSoundFontMode(false) {
}

FluidSynthEngine::~FluidSynthEngine() {
    shutdown();
}

// ============================================================================
// Lifecycle
// ============================================================================

bool FluidSynthEngine::initialize() {
    if (_initialized) {
        return true;
    }

    // Create FluidSynth settings
    _settings = new_fluid_settings();
    if (!_settings) {
        emit initializationFailed(tr("Failed to create FluidSynth settings"));
        return false;
    }

    // Apply audio settings
    fluid_settings_setnum(_settings, "synth.gain", _gain);
    fluid_settings_setnum(_settings, "synth.sample-rate", _sampleRate);
    fluid_settings_setint(_settings, "synth.reverb.active", _reverbEnabled ? 1 : 0);
    fluid_settings_setint(_settings, "synth.chorus.active", _chorusEnabled ? 1 : 0);
    fluid_settings_setstr(_settings, "synth.reverb.engine", _reverbEngine.toUtf8().constData());

    // Create the synthesizer
    _synth = new_fluid_synth(_settings);
    if (!_synth) {
        delete_fluid_settings(_settings);
        _settings = nullptr;
        emit initializationFailed(tr("Failed to create FluidSynth synthesizer"));
        return false;
    }

    // Try to create the audio driver — first the preferred driver, then fallbacks
    QStringList driversToTry;
    if (!_audioDriverName.isEmpty()) {
        driversToTry << _audioDriverName;
    }
    // Windows fallback chain
    for (const QString &fb : {"wasapi", "dsound", "waveout", "sdl3", "sdl2"}) {
        if (!driversToTry.contains(fb)) {
            driversToTry << fb;
        }
    }

    for (const QString &driver : driversToTry) {
        fluid_settings_setstr(_settings, "audio.driver", driver.toUtf8().constData());
        _audioDriver = new_fluid_audio_driver(_settings, _synth);
        if (_audioDriver) {
            if (driver != _audioDriverName) {
                qDebug() << "FluidSynth: preferred driver" << _audioDriverName
                         << "failed, using fallback:" << driver;
                _audioDriverName = driver;
            }
            break;
        }
        qWarning() << "FluidSynth: audio driver" << driver << "failed, trying next...";
    }

    if (!_audioDriver) {
        delete_fluid_synth(_synth);
        _synth = nullptr;
        delete_fluid_settings(_settings);
        _settings = nullptr;
        emit initializationFailed(tr("Failed to create FluidSynth audio driver"));
        return false;
    }

    _initialized = true;
    qDebug() << "FluidSynth engine initialized successfully";
    qDebug() << "  Audio driver:" << _audioDriverName;
    qDebug() << "  Sample rate:" << _sampleRate;
    qDebug() << "  Gain:" << _gain;

    // Load any pending SoundFonts from saved settings
    // (must happen BEFORE channel mode setup, because sfload with
    //  reset_presets=1 calls program_reset which overrides channel types)
    if (!_pendingSoundFontPaths.isEmpty()) {
        _disabledSoundFontPaths = _pendingDisabledPaths;
        setSoundFontStack(_pendingSoundFontPaths);
        _pendingSoundFontPaths.clear();
        _pendingDisabledPaths.clear();
    }

    // Apply channel mode AFTER SoundFont loading
    applyChannelMode();

    return true;
}

void FluidSynthEngine::shutdown() {
    if (!_initialized) {
        return;
    }

    if (_audioDriver) {
        delete_fluid_audio_driver(_audioDriver);
        _audioDriver = nullptr;
    }

    if (_synth) {
        // Preserve the FULL SoundFont stack (enabled + disabled) so they
        // reload on next initialize().  _soundFontStack is in internal
        // order (last = highest priority) while _pendingSoundFontPaths
        // must be in UI order (first = highest priority).
        _pendingSoundFontPaths = allSoundFontPaths();
        _pendingDisabledPaths = _disabledSoundFontPaths;

        // Unload all SoundFonts
        for (const auto &pair : _loadedFonts) {
            fluid_synth_sfunload(_synth, pair.first, 1);
        }
        _loadedFonts.clear();

        delete_fluid_synth(_synth);
        _synth = nullptr;
    }

    // Clear runtime state that is rebuilt on next initialize()
    _soundFontStack.clear();
    _disabledSoundFontPaths.clear();

    if (_settings) {
        delete_fluid_settings(_settings);
        _settings = nullptr;
    }

    _initialized = false;
    qDebug() << "FluidSynth engine shut down";
}

bool FluidSynthEngine::isInitialized() const {
    return _initialized;
}

// ============================================================================
// SoundFont Management
// ============================================================================

int FluidSynthEngine::loadSoundFont(const QString &path) {
    if (!_initialized || !_synth) {
        qWarning() << "FluidSynth: Cannot load SoundFont - engine not initialized";
        return -1;
    }

    QFileInfo fi(path);
    if (!fi.exists() || !fi.isReadable()) {
        qWarning() << "FluidSynth: SoundFont file not found or not readable:" << path;
        return -1;
    }

    // Check for duplicates
    QString canonicalPath = fi.canonicalFilePath();
    for (const auto &pair : _loadedFonts) {
        if (QFileInfo(pair.second).canonicalFilePath() == canonicalPath) {
            qDebug() << "FluidSynth: SoundFont already loaded:" << path;
            return pair.first;
        }
    }

    // reset_presets=1 means FluidSynth recalculates preset assignments after loading
    int sfontId = fluid_synth_sfload(_synth, path.toUtf8().constData(), 1);
    if (sfontId == FLUID_FAILED) {
        qWarning() << "FluidSynth: Failed to load SoundFont:" << path;
        return -1;
    }

    _loadedFonts.append(qMakePair(sfontId, path));

    // Also track in the full stack if not already present
    if (!_soundFontStack.contains(path)) {
        _soundFontStack.append(path);
    }

    qDebug() << "FluidSynth: Loaded SoundFont" << path << "with id" << sfontId;

    // Re-apply channel mode after every SoundFont load, because sfload
    // with reset_presets=1 resets all channels to default bank/program
    applyChannelMode();

    emit soundFontLoaded(sfontId, path);
    return sfontId;
}

bool FluidSynthEngine::unloadSoundFont(int sfontId) {
    if (!_initialized || !_synth) {
        return false;
    }

    if (fluid_synth_sfunload(_synth, sfontId, 1) == FLUID_OK) {
        QString removedPath;
        for (int i = 0; i < _loadedFonts.size(); ++i) {
            if (_loadedFonts[i].first == sfontId) {
                removedPath = _loadedFonts[i].second;
                _loadedFonts.removeAt(i);
                break;
            }
        }
        // Also remove from full stack and disabled set
        if (!removedPath.isEmpty()) {
            _soundFontStack.removeAll(removedPath);
            _disabledSoundFontPaths.remove(removedPath);
        }
        emit soundFontUnloaded(sfontId);
        return true;
    }
    return false;
}

void FluidSynthEngine::removeSoundFontByPath(const QString &path) {
    // If it's loaded in FluidSynth, unload it
    for (int i = 0; i < _loadedFonts.size(); ++i) {
        if (_loadedFonts[i].second == path) {
            if (_initialized && _synth) {
                fluid_synth_sfunload(_synth, _loadedFonts[i].first, 1);
            }
            _loadedFonts.removeAt(i);
            break;
        }
    }
    // Remove from full stack and disabled set
    _soundFontStack.removeAll(path);
    _disabledSoundFontPaths.remove(path);
    // Also remove from pending state (pre-init)
    _pendingSoundFontPaths.removeAll(path);
    _pendingDisabledPaths.remove(path);
}

void FluidSynthEngine::unloadAllSoundFonts() {
    if (!_initialized || !_synth) {
        return;
    }

    // Unload in reverse order (highest priority first)
    for (int i = _loadedFonts.size() - 1; i >= 0; --i) {
        fluid_synth_sfunload(_synth, _loadedFonts[i].first, 1);
    }
    _loadedFonts.clear();
}

QList<QPair<int, QString>> FluidSynthEngine::loadedSoundFonts() const {
    return _loadedFonts;
}

void FluidSynthEngine::setSoundFontStack(const QStringList &paths) {
    if (!_initialized || !_synth) {
        // Not yet initialized — update the pending paths so the new order
        // is applied when the engine starts (and persists on save)
        _pendingSoundFontPaths = paths;
        return;
    }

    // Update the full stack (includes disabled fonts)
    _soundFontStack.clear();
    // paths is in UI order (first = highest priority), store reversed (last = highest)
    for (int i = paths.size() - 1; i >= 0; --i) {
        _soundFontStack.append(paths[i]);
    }

    // Unload all current SoundFonts
    unloadAllSoundFonts();

    // Load only enabled fonts in reverse order so that the first item
    // (highest UI priority) is loaded last
    for (int i = paths.size() - 1; i >= 0; --i) {
        if (!_disabledSoundFontPaths.contains(paths[i])) {
            loadSoundFont(paths[i]);
        }
    }
}

QStringList FluidSynthEngine::allSoundFontPaths() const {
    // Return in UI order (highest priority first)
    if (!_soundFontStack.isEmpty()) {
        QStringList result;
        for (int i = _soundFontStack.size() - 1; i >= 0; --i) {
            result.append(_soundFontStack[i]);
        }
        return result;
    }
    // Before initialize(), fall back to pending paths loaded from settings
    return _pendingSoundFontPaths;
}

void FluidSynthEngine::setSoundFontEnabled(const QString &path, bool enabled) {
    if (enabled) {
        _disabledSoundFontPaths.remove(path);
        _pendingDisabledPaths.remove(path);
    } else {
        _disabledSoundFontPaths.insert(path);
        _pendingDisabledPaths.insert(path);
    }

    if (_initialized && _synth) {
        // Rebuild the synth stack: unload all, reload only enabled
        QStringList uiOrder = allSoundFontPaths();
        unloadAllSoundFonts();
        for (int i = uiOrder.size() - 1; i >= 0; --i) {
            if (!_disabledSoundFontPaths.contains(uiOrder[i])) {
                loadSoundFont(uiOrder[i]);
            }
        }
    }
}

bool FluidSynthEngine::isSoundFontEnabled(const QString &path) const {
    // Check both runtime and pending (pre-init) disabled sets
    return !_disabledSoundFontPaths.contains(path) &&
           !_pendingDisabledPaths.contains(path);
}

void FluidSynthEngine::addPendingSoundFontPaths(const QStringList &paths) {
    // Add new paths at the beginning (highest priority) of the pending list
    for (int i = paths.size() - 1; i >= 0; --i) {
        if (!_pendingSoundFontPaths.contains(paths[i])) {
            _pendingSoundFontPaths.prepend(paths[i]);
        }
    }
}

// ============================================================================
// Drum Track Name → SF2 Program Mapping
// ============================================================================

int FluidSynthEngine::drumProgramForTrackName(const QString &trackName) const {
    if (!_ffxivSoundFontMode || trackName.isEmpty()) {
        return -1;
    }

    // Strip octave suffix (+N or -N) from track name
    QString base = trackName;
    static const QRegularExpression suffixRe(QStringLiteral("[+-]\\d+$"));
    base.remove(suffixRe);

    // FFXIV SoundFont Bank 0 preset numbers for percussion instruments
    static const QHash<QString, int> drumMap = {
        {"Timpani", 47},
        {"Bongo", 116},
        {"Bass Drum", 117},
        {"Snare Drum", 118},
        {"Cymbal", 119},
        {"Cymbals", 119},
    };

    return drumMap.value(base, -1);
}

// ============================================================================
// MIDI Message Routing
// ============================================================================

void FluidSynthEngine::sendMidiData(const QByteArray &data) {
    if (!_initialized || !_synth || data.isEmpty()) {
        return;
    }

    unsigned char status = static_cast<unsigned char>(data[0]);
    unsigned char type = status & 0xF0;
    int channel = status & 0x0F;

    // Handle SysEx
    if (status == 0xF0) {
        fluid_synth_sysex(_synth,
                          data.constData() + 1,
                          data.size() - 1,
                          nullptr, nullptr, nullptr, 0);
        return;
    }

    // System real-time messages (single byte) ÔÇö ignore
    if (status >= 0xF8) {
        return;
    }

    // Channel messages
    switch (type) {
    case 0x90: // Note On
        if (data.size() >= 3) {
            int key = static_cast<unsigned char>(data[1]);
            int vel = static_cast<unsigned char>(data[2]);
            if (vel == 0) {
                fluid_synth_noteoff(_synth, channel, key);
            } else {
                fluid_synth_noteon(_synth, channel, key, vel);
            }
        }
        break;

    case 0x80: // Note Off
        if (data.size() >= 3) {
            int key = static_cast<unsigned char>(data[1]);
            fluid_synth_noteoff(_synth, channel, key);
        }
        break;

    case 0xC0: // Program Change
        if (data.size() >= 2) {
            int program = static_cast<unsigned char>(data[1]);
            // In melodic mode, force bank 0 on ALL channels so FFXIV
            // SoundFont presets are always found (they only exist in bank 0)
            if (_ffxivSoundFontMode) {
                fluid_synth_bank_select(_synth, channel, 0);
            }
            fluid_synth_program_change(_synth, channel, program);
            qDebug() << "FluidSynth: Program change ch" << channel
                     << "prog" << program
                     << ((_ffxivSoundFontMode) ? "(FFXIV mode/bank0)" : "");
        }
        break;

    case 0xB0: // Control Change
        if (data.size() >= 3) {
            int ctrl = static_cast<unsigned char>(data[1]);
            int value = static_cast<unsigned char>(data[2]);
            // In melodic mode, intercept bank select CC#0 (MSB) and CC#32 (LSB)
            // to force bank 0 — FFXIV SoundFont only has bank 0 presets
            if (_ffxivSoundFontMode && (ctrl == 0 || ctrl == 32)) {
                value = 0;
            }
            fluid_synth_cc(_synth, channel, ctrl, value);
        }
        break;

    case 0xE0: // Pitch Bend
        if (data.size() >= 3) {
            int lsb = static_cast<unsigned char>(data[1]);
            int msb = static_cast<unsigned char>(data[2]);
            int value = (msb << 7) | lsb;
            fluid_synth_pitch_bend(_synth, channel, value);
        }
        break;

    case 0xD0: // Channel Pressure (Aftertouch)
        if (data.size() >= 2) {
            int pressure = static_cast<unsigned char>(data[1]);
            fluid_synth_channel_pressure(_synth, channel, pressure);
        }
        break;

    case 0xA0: // Key Pressure (Polyphonic Aftertouch)
        if (data.size() >= 3) {
            int key = static_cast<unsigned char>(data[1]);
            int pressure = static_cast<unsigned char>(data[2]);
            fluid_synth_key_pressure(_synth, channel, key, pressure);
        }
        break;

    default:
        break;
    }
}

// ============================================================================
// Audio Export
// ============================================================================

void FluidSynthEngine::exportAudio(const ExportOptions &options) {
    if (!_initialized) {
        emit exportFinished(false, options.outputFilePath);
        return;
    }

    _cancelExport.store(false);

    // Create a separate FluidSynth instance for offline rendering
    // (does not interfere with live playback)
    fluid_settings_t *expSettings = new_fluid_settings();
    if (!expSettings) {
        emit exportFinished(false, options.outputFilePath);
        return;
    }

    fluid_settings_setstr(expSettings, "audio.driver", "file");
    fluid_settings_setstr(expSettings, "audio.file.name",
                          options.outputFilePath.toUtf8().constData());
    fluid_settings_setstr(expSettings, "audio.file.type",
                          options.fileType.toUtf8().constData());
    fluid_settings_setstr(expSettings, "audio.file.format",
                          options.sampleFormat.toUtf8().constData());
    fluid_settings_setnum(expSettings, "synth.sample-rate", options.sampleRate);
    fluid_settings_setnum(expSettings, "synth.gain", _gain);
    fluid_settings_setint(expSettings, "synth.reverb.active", _reverbEnabled ? 1 : 0);
    fluid_settings_setint(expSettings, "synth.chorus.active", _chorusEnabled ? 1 : 0);
    fluid_settings_setstr(expSettings, "synth.reverb.engine",
                          _reverbEngine.toUtf8().constData());

    // Collect loaded SoundFont paths (under no lock needed — read-only snapshot)
    // Only include enabled SoundFonts
    QStringList fontsToLoad;
    for (int i = 0; i < _loadedFonts.size(); ++i) {
        if (!_disabledSoundFontPaths.contains(_loadedFonts[i].second)) {
            fontsToLoad.append(_loadedFonts[i].second);
        }
    }

    if (fontsToLoad.isEmpty()) {
        delete_fluid_settings(expSettings);
        emit exportFinished(false, options.outputFilePath);
        return;
    }

    fluid_synth_t *expSynth = new_fluid_synth(expSettings);
    if (!expSynth) {
        delete_fluid_settings(expSettings);
        emit exportFinished(false, options.outputFilePath);
        return;
    }

    // Load SoundFonts into the export synth
    for (const QString &f : fontsToLoad) {
        fluid_synth_sfload(expSynth, f.toUtf8().constData(), 1);
    }

    // Apply FFXIV channel mode if active
    if (_ffxivSoundFontMode) {
        for (int ch = 0; ch < 16; ++ch) {
            fluid_synth_set_channel_type(expSynth, ch, CHANNEL_TYPE_MELODIC);
            fluid_synth_bank_select(expSynth, ch, 0);
            fluid_synth_program_change(expSynth, ch, 0);
        }
    }

    // Create MIDI player and load the file
    fluid_player_t *player = new_fluid_player(expSynth);
    if (!player) {
        delete_fluid_synth(expSynth);
        delete_fluid_settings(expSettings);
        emit exportFinished(false, options.outputFilePath);
        return;
    }

    if (fluid_player_add(player, options.midiFilePath.toUtf8().constData()) != FLUID_OK) {
        delete_fluid_player(player);
        delete_fluid_synth(expSynth);
        delete_fluid_settings(expSettings);
        emit exportFinished(false, options.outputFilePath);
        return;
    }

    fluid_player_play(player);

    // If a start tick is specified, render silently (to a dummy buffer) until we
    // reach the start position.  This processes all MIDI events (program changes,
    // CC, tempo, etc.) so the synth state is correct when we begin recording.
    bool hasRange = (options.startTick >= 0);
    if (hasRange && options.startTick > 0) {
        float dummyL[64], dummyR[64];
        while (fluid_player_get_status(player) == FLUID_PLAYER_PLAYING) {
            if (_cancelExport.load()) {
                delete_fluid_player(player);
                delete_fluid_synth(expSynth);
                delete_fluid_settings(expSettings);
                emit exportCancelled();
                return;
            }
            int curTick = fluid_player_get_current_tick(player);
            if (curTick >= options.startTick) break;
            fluid_synth_write_float(expSynth, 64, dummyL, 0, 1, dummyR, 0, 1);
        }
    }

    // Create file renderer — starts writing from the current position
    fluid_file_renderer_t *renderer = new_fluid_file_renderer(expSynth);
    if (!renderer) {
        delete_fluid_player(player);
        delete_fluid_synth(expSynth);
        delete_fluid_settings(expSettings);
        emit exportFinished(false, options.outputFilePath);
        return;
    }

    // Set encoding quality for lossy formats (e.g., OGG Vorbis)
    fluid_file_set_encoding_quality(renderer, options.encodingQuality);

    int totalTicks = fluid_player_get_total_ticks(player);
    if (totalTicks <= 0) totalTicks = 1;
    int rangeStart = hasRange ? options.startTick : 0;
    int rangeEnd = (hasRange && options.endTick > 0) ? options.endTick : totalTicks;
    int rangeLen = rangeEnd - rangeStart;
    if (rangeLen <= 0) rangeLen = 1;
    int lastPercent = -1;

    // Render loop
    while (fluid_player_get_status(player) == FLUID_PLAYER_PLAYING) {
        if (_cancelExport.load()) {
            // User cancelled — clean up and remove partial file
            delete_fluid_file_renderer(renderer);
            delete_fluid_player(player);
            delete_fluid_synth(expSynth);
            delete_fluid_settings(expSettings);
            QFile::remove(options.outputFilePath);
            emit exportCancelled();
            return;
        }

        int curTick = fluid_player_get_current_tick(player);

        // Stop rendering when we reach the end of the requested range
        if (hasRange && options.endTick > 0 && curTick >= options.endTick) {
            break;
        }

        if (fluid_file_renderer_process_block(renderer) != FLUID_OK) {
            break;
        }

        int percent = static_cast<int>(100.0 * (curTick - rangeStart) / rangeLen);
        if (percent > 100) percent = 100;
        if (percent < 0) percent = 0;
        if (percent != lastPercent) {
            lastPercent = percent;
            emit exportProgress(percent);
        }
    }

    // Render reverb/chorus tail (~2 seconds of silence after playback ends)
    if (options.includeReverbTail && !_cancelExport.load()) {
        int tailBlocks = static_cast<int>((options.sampleRate * 2.0) / 64.0);
        for (int i = 0; i < tailBlocks; ++i) {
            if (_cancelExport.load()) break;
            if (fluid_file_renderer_process_block(renderer) != FLUID_OK) break;
        }
    }

    // Cleanup
    delete_fluid_file_renderer(renderer);
    delete_fluid_player(player);
    delete_fluid_synth(expSynth);
    delete_fluid_settings(expSettings);

    if (_cancelExport.load()) {
        QFile::remove(options.outputFilePath);
        emit exportCancelled();
        return;
    }

    emit exportProgress(100);
    emit exportFinished(true, options.outputFilePath);
}

void FluidSynthEngine::cancelExport() {
    _cancelExport.store(true);
}

// ============================================================================
// Audio Settings
// ============================================================================

void FluidSynthEngine::setAudioDriver(const QString &driver) {
    _audioDriverName = driver;
    if (_initialized) {
        // Audio driver change requires full restart.
        // shutdown() preserves the full SoundFont state (enabled + disabled)
        // in _pendingSoundFontPaths/_pendingDisabledPaths, and initialize()
        // restores it automatically — no manual setSoundFontStack() needed.
        shutdown();
        initialize();
    }
}

void FluidSynthEngine::setGain(double gain) {
    _gain = gain;
    if (_initialized && _synth) {
        fluid_synth_set_gain(_synth, static_cast<float>(gain));
    }
}

void FluidSynthEngine::setSampleRate(double rate) {
    _sampleRate = rate;
    if (_initialized) {
        // Sample rate change requires full restart.
        // shutdown() preserves the full SoundFont state (enabled + disabled)
        // in _pendingSoundFontPaths/_pendingDisabledPaths, and initialize()
        // restores it automatically.
        shutdown();
        initialize();
    }
}

void FluidSynthEngine::setReverbEngine(const QString &engine) {
    _reverbEngine = engine;
    if (_initialized) {
        // Reverb engine change requires full restart.
        // shutdown() preserves the full SoundFont state (enabled + disabled)
        // in _pendingSoundFontPaths/_pendingDisabledPaths, and initialize()
        // restores it automatically.
        shutdown();
        initialize();
    }
}

void FluidSynthEngine::setReverbEnabled(bool enabled) {
    _reverbEnabled = enabled;
    if (_initialized && _synth) {
        fluid_synth_reverb_on(_synth, -1, enabled ? 1 : 0);
    }
}

void FluidSynthEngine::setChorusEnabled(bool enabled) {
    _chorusEnabled = enabled;
    if (_initialized && _synth) {
        fluid_synth_chorus_on(_synth, -1, enabled ? 1 : 0);
    }
}

QString FluidSynthEngine::audioDriver() const {
    return _audioDriverName;
}

double FluidSynthEngine::gain() const {
    return _gain;
}

double FluidSynthEngine::sampleRate() const {
    return _sampleRate;
}

QString FluidSynthEngine::reverbEngine() const {
    return _reverbEngine;
}

bool FluidSynthEngine::reverbEnabled() const {
    return _reverbEnabled;
}

bool FluidSynthEngine::chorusEnabled() const {
    return _chorusEnabled;
}

void FluidSynthEngine::setFfxivSoundFontMode(bool enabled) {
    _ffxivSoundFontMode = enabled;
    applyChannelMode();
}

void FluidSynthEngine::applyChannelMode() {
    if (!_initialized || !_synth) {
        return;
    }
    if (_ffxivSoundFontMode) {
        // FFXIV SoundFont Mode: all channels melodic + bank 0 + drum program injection
        for (int ch = 0; ch < 16; ++ch) {
            fluid_synth_set_channel_type(_synth, ch, CHANNEL_TYPE_MELODIC);
            fluid_synth_bank_select(_synth, ch, 0);
            fluid_synth_program_change(_synth, ch, 0);
        }
        qDebug() << "FluidSynth: FFXIV SoundFont Mode — all channels melodic, bank 0";
    } else {
        // Restore GM default: ch9 = drum, all others = melodic
        for (int ch = 0; ch < 16; ++ch) {
            if (ch == 9) {
                fluid_synth_set_channel_type(_synth, ch, CHANNEL_TYPE_DRUM);
                fluid_synth_bank_select(_synth, ch, 128);
                fluid_synth_program_change(_synth, ch, 0);
            } else {
                fluid_synth_set_channel_type(_synth, ch, CHANNEL_TYPE_MELODIC);
                fluid_synth_bank_select(_synth, ch, 0);
                fluid_synth_program_change(_synth, ch, 0);
            }
        }
        qDebug() << "FluidSynth: GM channel mode restored (ch9=drum, bank 128)";
    }
}

bool FluidSynthEngine::ffxivSoundFontMode() const {
    return _ffxivSoundFontMode;
}

QStringList FluidSynthEngine::availableAudioDrivers() const {
    QStringList drivers;
    fluid_settings_t *tmpSettings = new_fluid_settings();
    if (tmpSettings) {
        fluid_settings_foreach_option(tmpSettings, "audio.driver",
            &drivers,
            [](void *data, const char * /*name*/, const char *option) {
                QStringList *list = static_cast<QStringList*>(data);
                QString driverName = QString::fromUtf8(option);
                // Filter out the "file" driver (not useful for real-time playback)
                if (driverName != QLatin1String("file")) {
                    list->append(driverName);
                }
            });
        delete_fluid_settings(tmpSettings);
    }
    return drivers;
}

QString FluidSynthEngine::audioDriverDisplayName(const QString &driver) {
    static const QMap<QString, QString> displayNames = {
        {"dsound", "DirectSound"},
        {"wasapi", "WASAPI"},
        {"waveout", "WaveOut"},
        {"pulseaudio", "PulseAudio"},
        {"alsa", "ALSA"},
        {"jack", "JACK"},
        {"coreaudio", "CoreAudio"},
        {"portaudio", "PortAudio"},
        {"sdl2", "SDL2"},
        {"pipewire", "PipeWire"},
        {"opensles", "OpenSL ES"},
        {"oboe", "Oboe"}
    };
    return displayNames.value(driver, driver);
}

bool FluidSynthEngine::isSoundFontLoaded(const QString &path) const {
    QString canonicalPath = QFileInfo(path).canonicalFilePath();
    for (const auto &pair : _loadedFonts) {
        if (QFileInfo(pair.second).canonicalFilePath() == canonicalPath) {
            return true;
        }
    }
    return false;
}

// ============================================================================
// Persistence
// ============================================================================

void FluidSynthEngine::saveSettings(QSettings *settings) {
    settings->beginGroup("FluidSynth");

    settings->setValue("audioDriver", _audioDriverName);
    settings->setValue("reverbEngine", _reverbEngine);
    settings->setValue("gain", _gain);
    settings->setValue("sampleRate", _sampleRate);
    settings->setValue("reverbEnabled", _reverbEnabled);
    settings->setValue("chorusEnabled", _chorusEnabled);
    settings->setValue("ffxivSoundFontMode", _ffxivSoundFontMode);

    // Save SoundFont paths in priority order (first = highest priority)
    QStringList fontPaths = allSoundFontPaths(); // includes disabled
    settings->setValue("soundFontPaths", fontPaths);

    // Save disabled SoundFont paths (merge both runtime and pending sets)
    QSet<QString> allDisabled = _disabledSoundFontPaths;
    allDisabled.unite(_pendingDisabledPaths);
    QStringList disabledPaths(allDisabled.begin(), allDisabled.end());
    settings->setValue("disabledSoundFontPaths", disabledPaths);

    settings->endGroup();
}

void FluidSynthEngine::loadSettings(QSettings *settings) {
    settings->beginGroup("FluidSynth");

    _audioDriverName = settings->value("audioDriver", "").toString();
    _reverbEngine = settings->value("reverbEngine", "fdn").toString();
    _gain = settings->value("gain", 0.5).toDouble();
    _sampleRate = settings->value("sampleRate", 44100.0).toDouble();
    _reverbEnabled = settings->value("reverbEnabled", true).toBool();
    _chorusEnabled = settings->value("chorusEnabled", true).toBool();
    // Read new key, fall back to old key for backward compatibility
    _ffxivSoundFontMode = settings->value("ffxivSoundFontMode",
        settings->value("allChannelsMelodic", false)).toBool();

    QStringList fontPaths = settings->value("soundFontPaths").toStringList();
    QStringList disabledPaths = settings->value("disabledSoundFontPaths").toStringList();

    settings->endGroup();

    // Store paths for deferred loading when engine initializes
    _pendingSoundFontPaths = fontPaths;
    _pendingDisabledPaths = QSet<QString>(disabledPaths.begin(), disabledPaths.end());

    // If we're already initialized, reload SoundFonts immediately
    if (_initialized && !fontPaths.isEmpty()) {
        _disabledSoundFontPaths = _pendingDisabledPaths;
        setSoundFontStack(fontPaths);
        _pendingSoundFontPaths.clear();
        _pendingDisabledPaths.clear();
    }
}

#endif // FLUIDSYNTH_SUPPORT
