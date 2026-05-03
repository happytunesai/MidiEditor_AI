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
#include <QPointer>
#include <QRegularExpression>
#include <QSettings>
#include <QTimer>
#include <QtMath>
#include <cmath>
#include <cstring>

#include "FfxivEqualizerService.h"

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
      _sampleRate(48000.0),
      _reverbEnabled(true),
      _chorusEnabled(true),
      _ffxivSoundFontMode(false),
      _bardAccurateMode(true) {
    for (int ch = 0; ch < 16; ++ch) {
        _bardCurrentProgram[ch] = 0;
        _bardCC7[ch] = 127;
        _bardCC11[ch] = 127;
        for (int k = 0; k < 128; ++k) {
            _bardNoteOnMs[ch][k] = 0;
            _bardNoteHeld[ch][k] = false;
            _bardNoteGen[ch][k] = 0;
        }
    }
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

    // synth.reverb.engine is not a standard FluidSynth setting in v2.5.x —
    // only attempt if the settings object actually knows about it.
    if (fluid_settings_get_type(_settings, "synth.reverb.engine") != FLUID_NO_TYPE) {
        fluid_settings_setstr(_settings, "synth.reverb.engine",
                              _reverbEngine.toUtf8().constData());
    }

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

    // Start the bard-mode time base
    if (!_bardClock.isValid()) {
        _bardClock.start();
    }
    resetBardNoteState();

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

    // Apply runtime bard-accuracy state (reverb/chorus/polyphony) after
    // SoundFonts are loaded so we override any sf_init defaults.
    applyBardAccuracySettings();

    // Phase 39 (FFXIV-EQ-001): live slider edits in the FFXIV SoundFont
    // Equalizer dialog must affect playback instantly. Wire the
    // service's mixChanged signal to refresh GEN_ATTENUATION on every
    // channel. Use a direct connection because we are on the main thread
    // (the dialog mutates the service from the GUI thread). The connection
    // is set up exactly once (singleton initialize is idempotent above).
    static bool eqConnected = false;
    if (!eqConnected) {
        connect(FfxivEqualizerService::instance(),
                &FfxivEqualizerService::mixChanged,
                this, [this]() { applyFfxivEqualizerAttenuation(-1); });
        eqConnected = true;
    }
    // Push the current EQ gains in case FFXIV mode is already on.
    applyFfxivEqualizerAttenuation(-1);

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

int FluidSynthEngine::ffxivDrumProgramForGmNote(int gmNote) {
    // Note: callers (live MidiOutput, exportPlaybackCallback) gate this
    // on FFXIV mode being active, so we don't re-check _ffxivSoundFontMode
    // here (which would also block the static export-callback path).
    // GM Standard Drum Kit (key 35..81). Map each common drum to the
    // closest FFXIV bard percussion preset in our SoundFont:
    //   117 Bass Drum, 118 Snare Drum, 47 Timpani (toms),
    //   119 Cymbal (hats / cymbals / cowbell), 116 Bongo (hand drums / blocks).
    switch (gmNote) {
    // Kick / bass
    case 35: case 36:
        return 117;
    // Snares + rim
    case 37: case 38: case 40:
        return 118;
    // Toms (LightAmp also routes toms to Timpani)
    case 41: case 43: case 45: case 47: case 48: case 50:
        return 47;
    // Hi-hats + cymbals
    case 42: case 44: case 46:                       // hi-hats
    case 49: case 51: case 52: case 53: case 55:
    case 57: case 59:                                // crashes / rides / splash
        return 119;
    // Hand drums / wood / agogo / cabasa / claves / blocks (percussive, short)
    case 60: case 61: case 62: case 63: case 64:     // hi/lo bongo, conga family
    case 65: case 66: case 67: case 68:              // timbale / agogo
    case 69: case 70: case 71: case 72: case 73:     // cabasa / maracas / whistle
    case 74: case 75: case 76: case 77:              // guiro / claves / wood blocks
    case 78: case 79: case 80: case 81:              // cuica / triangle
        return 116;
    default:
        return -1;
    }
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
                // velocity-0 note-on is a note-off — route through the same path
                bool bardActive = _ffxivSoundFontMode && _bardAccurateMode;
                if (!bardActive || !_bardNoteHeld[channel][key]) {
                    fluid_synth_noteoff(_synth, channel, key);
                    if (bardActive) _bardNoteHeld[channel][key] = false;
                } else {
                    qint64 elapsed = _bardClock.isValid()
                        ? (_bardClock.elapsed() - _bardNoteOnMs[channel][key])
                        : 0;
                    int instIdx = bardInstrumentIndexForProgram(_bardCurrentProgram[channel]);
                    qint64 minLen = bardMinNoteLengthMs(instIdx, key, elapsed);
                    if (instIdx < 0 || elapsed >= minLen) {
                        fluid_synth_noteoff(_synth, channel, key);
                        _bardNoteHeld[channel][key] = false;
                    } else {
                        qint64 remaining = minLen - elapsed;
                        quint32 gen = _bardNoteGen[channel][key];
                        QPointer<FluidSynthEngine> guard(this);
                        QTimer::singleShot(static_cast<int>(remaining), this,
                            [guard, channel, key, gen]() {
                                if (!guard) return;
                                if (!guard->_initialized || !guard->_synth) return;
                                if (guard->_bardNoteGen[channel][key] != gen) return;
                                if (!guard->_bardNoteHeld[channel][key]) return;
                                fluid_synth_noteoff(guard->_synth, channel, key);
                                guard->_bardNoteHeld[channel][key] = false;
                            });
                    }
                }
            } else {
                bool bardActive = _ffxivSoundFontMode && _bardAccurateMode;
                if (bardActive) {
                    // If a previous note for (ch,key) is still held by min-length,
                    // flush it now so the synth retriggers cleanly.
                    if (_bardNoteHeld[channel][key]) {
                        fluid_synth_noteoff(_synth, channel, key);
                        _bardNoteHeld[channel][key] = false;
                    }
                    // Force max velocity; dynamics flow through CC7/CC11 (cubic curve)
                    vel = 127;
                }
                // Phase 39 (FFXIV-EQ-001): per-instrument volume mixer.
                // The user's FFXIV SoundFont Equalizer preset multiplies
                // the NoteOn velocity by a per-program factor (gainFor()
                // already folds in the master gain). Gated on FFXIV mode
                // so non-FFXIV users pay zero overhead. A muted slot
                // returns 0.0f — drop the NoteOn entirely (skip both
                // fluid_synth_noteon and the bard bookkeeping below).
                if (_ffxivSoundFontMode) {
                    int prog = _bardCurrentProgram[channel];
                    bool isDrum = (channel == 9);
                    float g = FfxivEqualizerService::instance()->gainFor(prog, isDrum);
                    if (g <= 0.0f) {
                        // Muted — swallow this NoteOn. The matching
                        // NoteOff still fires later but is harmless
                        // because no voice was started.
                        break;
                    }
                    int scaled = static_cast<int>(vel * g + 0.5f);
                    vel = qBound(1, scaled, 127);
                }
                fluid_synth_noteon(_synth, channel, key, vel);
                if (bardActive) {
                    _bardNoteOnMs[channel][key] = _bardClock.isValid() ? _bardClock.elapsed() : 0;
                    _bardNoteHeld[channel][key] = true;
                    _bardNoteGen[channel][key]++;
                }
            }
        }
        break;

    case 0x80: // Note Off
        if (data.size() >= 3) {
            int key = static_cast<unsigned char>(data[1]);
            bool bardActive = _ffxivSoundFontMode && _bardAccurateMode;
            if (!bardActive || !_bardNoteHeld[channel][key]) {
                fluid_synth_noteoff(_synth, channel, key);
                if (bardActive) _bardNoteHeld[channel][key] = false;
                break;
            }
            qint64 elapsed = _bardClock.isValid()
                ? (_bardClock.elapsed() - _bardNoteOnMs[channel][key])
                : 0;
            int instIdx = bardInstrumentIndexForProgram(_bardCurrentProgram[channel]);
            qint64 minLen = bardMinNoteLengthMs(instIdx, key, elapsed);
            if (instIdx < 0 || elapsed >= minLen) {
                fluid_synth_noteoff(_synth, channel, key);
                _bardNoteHeld[channel][key] = false;
            } else {
                qint64 remaining = minLen - elapsed;
                quint32 gen = _bardNoteGen[channel][key];
                QPointer<FluidSynthEngine> guard(this);
                QTimer::singleShot(static_cast<int>(remaining), this,
                    [guard, channel, key, gen]() {
                        if (!guard) return;
                        if (!guard->_initialized || !guard->_synth) return;
                        if (guard->_bardNoteGen[channel][key] != gen) return;
                        if (!guard->_bardNoteHeld[channel][key]) return;
                        fluid_synth_noteoff(guard->_synth, channel, key);
                        guard->_bardNoteHeld[channel][key] = false;
                    });
            }
        }
        break;

    case 0xC0: // Program Change
        if (data.size() >= 2) {
            int program = static_cast<unsigned char>(data[1]);
            int origProgram = program;
            // In melodic mode, force bank 0 on ALL channels so FFXIV
            // SoundFont presets are always found (they only exist in bank 0)
            if (_ffxivSoundFontMode) {
                fluid_synth_bank_select(_synth, channel, 0);
                // GM-program → FFXIV-SF2 fallback. The FFXIV SoundFont is
                // sparse: any GM program with no preset silently falls back
                // to bank 0 / prog 0 (= Piano), so e.g. an Acoustic Guitar
                // (nylon) track plays as piano. Remap the most common
                // missing slots to the closest FFXIV instrument so old or
                // imported MIDIs (Guitar Pro, MusicXML, …) still sound
                // sensible. Verified against FF14-c3c6-fixed.sf2 phdr
                // chunk on 2026-04-28.
                static const QHash<int, int> ffxivProgramFallback = {
                    {24, 25},  // Acoustic Guitar (nylon) → Lute
                    {26, 27},  // Acoustic Guitar (jazz)  → Clean Guitar
                };
                auto it = ffxivProgramFallback.constFind(program);
                if (it != ffxivProgramFallback.constEnd()) {
                    program = it.value();
                }
            }
            fluid_synth_program_change(_synth, channel, program);
            _bardCurrentProgram[channel] = program;
            // In bard mode, normalise per-preset loudness via additive
            // initialAttenuation (gen 48 = GEN_ATTENUATION, in cB).
            // Phase 39: combined with the EQ user gain via the helper.
            if (_ffxivSoundFontMode) {
                applyFfxivEqualizerAttenuation(channel);
            }
            if (_ffxivSoundFontMode && program != origProgram) {
                qDebug() << "FluidSynth: Program change ch" << channel
                         << "prog" << origProgram << "→" << program
                         << "(FFXIV fallback)";
            } else {
                qDebug() << "FluidSynth: Program change ch" << channel
                         << "prog" << program
                         << ((_ffxivSoundFontMode) ? "(FFXIV mode/bank0)" : "");
            }
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
            // In bard accuracy mode, reshape volume (CC7) and expression (CC11)
            // through a cubic perceptual curve and send the result on CC7 only.
            if (_ffxivSoundFontMode && _bardAccurateMode && (ctrl == 7 || ctrl == 11)) {
                if (ctrl == 7)  _bardCC7[channel]  = value;
                else            _bardCC11[channel] = value;
                applyBardVolumeCurve(channel);
                break;
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
                          _reverbEngine.toUtf8().constData());  // may fail silently on v2.5.x

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

    // Match the live-playback voicing in offline render so the MP3/WAV
    // sounds the same as what the user just heard. When FFXIV+bard mode
    // is active we run the export synth dry, capped to 16 voices; the
    // cubic CC7·CC11 curve and per-instrument min-note-length still
    // happen at the live-playback layer (live MidiOutput) — for offline
    // export we only need to fix what fluid_player_t would otherwise do
    // wrong: bank select must stay 0 and program changes must honour
    // our FFXIV fallback table (24→25, 26→27).
    if (_ffxivSoundFontMode && _bardAccurateMode) {
        fluid_synth_set_reverb_on(expSynth, 0);
        fluid_synth_set_chorus_on(expSynth, 0);
        fluid_synth_set_polyphony(expSynth, 16);
        // Seed per-program loudness trim for the default program 0
        // on every channel so tracks that never send a PC still get
        // the same attenuation as live playback.
        for (int ch = 0; ch < 16; ++ch) {
            fluid_synth_set_gen(expSynth, ch, GEN_ATTENUATION,
                                static_cast<float>(bardProgramAttenuationCb(0)));
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

    // Install a playback callback when FFXIV mode is active so program
    // changes get the same fallback remap and bank-select forcing that
    // live playback applies via sendMidiData(). Without this the
    // sparse FFXIV SoundFont silently falls back to bank 0 / preset 0
    // (= Piano) for any GM program it doesn't ship — e.g. Acoustic
    // Guitar (nylon, prog 24) plays as piano in the exported MP3 even
    // though it sounded right during live playback.
    // The callback also drops events for muted channels (mirroring
    // MidiFile::channelMuted()) and applies the per-program loudness
    // trim (bardProgramAttenuationCb) when bard mode is active.
    _exportMutedChannelsMask = options.mutedChannelsMask;
    _exportBardActive = (_ffxivSoundFontMode && _bardAccurateMode);
    for (int ch = 0; ch < 16; ++ch) _exportExplicitPC[ch] = false;
    for (int ch = 0; ch < 16; ++ch) _exportCurrentProgram[ch] = 0;
    if (_ffxivSoundFontMode || _exportMutedChannelsMask != 0) {
        fluid_player_set_playback_callback(player, &FluidSynthEngine::exportPlaybackCallback, expSynth);
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

    // Clean up temporary MIDI file if requested
    if (options.deleteMidiFileAfterExport && !options.midiFilePath.isEmpty()) {
        QFile::remove(options.midiFilePath);
    }

    emit exportProgress(100);
    emit exportFinished(true, options.outputFilePath);
}

void FluidSynthEngine::cancelExport() {
    _cancelExport.store(true);
}

// Offline-render playback callback. Mirrors the FFXIV remapping that
// sendMidiData() does for live playback so MP3/WAV exports voice the
// same instruments as the user just heard. Forwards everything else
// untouched to the default handler.
int FluidSynthEngine::exportPlaybackCallback(void *data, fluid_midi_event_t *event) {
    fluid_synth_t *synth = static_cast<fluid_synth_t *>(data);
    if (!synth || !event) return FLUID_OK;

    int type = fluid_midi_event_get_type(event);
    int channel = fluid_midi_event_get_channel(event);

    // Drop all channel-bound events on muted channels so exporting with
    // some channels muted produces only the audible voices, just like
    // playback would. We still let meta / sysex events through (they
    // typically don't carry a meaningful channel value here).
    const bool isChannelMsg = (type == 0x80 || type == 0x90 || type == 0xA0 ||
                               type == 0xB0 || type == 0xC0 || type == 0xD0 ||
                               type == 0xE0);
    if (isChannelMsg && channel >= 0 && channel < 16 &&
        (_exportMutedChannelsMask & (1u << channel)) != 0) {
        return FLUID_OK;
    }

    // Per-note drum program injection on CH9 — mirrors the live
    // MidiOutput::sendCommand path. The standard GM convention puts
    // every percussion hit on channel 9 with the *MIDI key* selecting
    // which drum sound plays. The FFXIV SoundFont instead exposes
    // each percussion sound as its own program, so we have to switch
    // program right before each NoteOn based on the GM key. Without
    // this the player keeps whatever program was last set on CH9
    // (often 0 = Piano), which is exactly the "snare exports as
    // piano" bug the user reported.
    if (_exportBardActive && channel == 9 && type == 0x90) {
        int vel = fluid_midi_event_get_velocity(event);
        int key = fluid_midi_event_get_key(event);
        if (vel > 0) {
            // Only run the GM-drum-key fallback when the file did NOT
            // already inject an explicit PC for this channel. With
            // injected PCs (Snare/Bass-Drum/Cymbal tracks recognised by
            // name), the program is already correct — falling back
            // here would clobber it (e.g. Snare key 60 → Bongo).
            int activeProgram = _exportCurrentProgram[channel];
            if (!_exportExplicitPC[channel]) {
                int prog = ffxivDrumProgramForGmNote(key);
                if (prog >= 0) {
                    fluid_synth_bank_select(synth, channel, 0);
                    fluid_synth_program_change(synth, channel, prog);
                    fluid_synth_set_gen(synth, channel, GEN_ATTENUATION,
                                        static_cast<float>(bardProgramAttenuationCb(prog)));
                    activeProgram = prog;
                    _exportCurrentProgram[channel] = prog;
                }
            }
            // Phase 39 (FFXIV-EQ-001): apply per-program equalizer gain
            // so offline renders match live playback. A muted slot
            // returns 0.0 — swallow the NoteOn entirely.
            float g = FfxivEqualizerService::instance()->gainFor(activeProgram, /*isDrum=*/true);
            if (g <= 0.0f) {
                return FLUID_OK;
            }
            int scaled = static_cast<int>(vel * g + 0.5f);
            vel = qBound(1, scaled, 127);
            // Trigger the note directly (mirrors live `fluid_synth_noteon`)
            // instead of forwarding via handle_midi_event, which can
            // re-interpret the channel as a drum channel and silently
            // route the event into a non-existent percussion bank.
            fluid_synth_noteon(synth, channel, key, vel);
        } else {
            fluid_synth_noteoff(synth, channel, key);
        }
        return FLUID_OK;
    }
    if (_exportBardActive && channel == 9 && type == 0x80) {
        int key = fluid_midi_event_get_key(event);
        fluid_synth_noteoff(synth, channel, key);
        return FLUID_OK;
    }

    // Bank select (CC#0 MSB or CC#32 LSB) → force 0; FFXIV SoundFont
    // only ships bank 0 presets, any other selection silently falls
    // back to a wrong preset.
    if (type == 0xB0) {
        int ctrl = fluid_midi_event_get_control(event);
        if (ctrl == 0 || ctrl == 32) {
            fluid_midi_event_set_value(event, 0);
        }
        return fluid_synth_handle_midi_event(synth, event);
    }

    // Program change → apply same fallback table as live playback.
    if (type == 0xC0) {
        int program = fluid_midi_event_get_program(event);
        // Keep this list in lockstep with sendMidiData()'s
        // ffxivProgramFallback hash. Verified against
        // FF14-c3c6-fixed.sf2 phdr chunk.
        switch (program) {
        case 24: program = 25; break; // Acoustic Guitar (nylon) → Lute
        case 26: program = 27; break; // Acoustic Guitar (jazz)  → Clean Guitar
        default: break;
        }
        // Force bank 0 first so the (possibly remapped) program
        // resolves into the FFXIV SoundFont rather than whatever
        // bank the MIDI file requested earlier.
        fluid_synth_bank_select(synth, channel, 0);
        fluid_synth_program_change(synth, channel, program);
        // Mirror the live per-program loudness trim so muted-mix and
        // exported mix have the same balance across instruments.
        if (_exportBardActive) {
            fluid_synth_set_gen(synth, channel, GEN_ATTENUATION,
                                static_cast<float>(bardProgramAttenuationCb(program)));
        }
        // Remember that the file gave us an explicit program for this
        // channel so the CH9 GM-key fallback above doesn't overwrite
        // it on the next NoteOn.
        if (channel >= 0 && channel < 16) {
            _exportExplicitPC[channel] = true;
            _exportCurrentProgram[channel] = program;
        }
        return FLUID_OK;
    }

    // Phase 39 (FFXIV-EQ-001): equalizer gain for non-drum NoteOns in
    // the offline render. Mirrors the live-playback hook in
    // sendMidiData() so exported MP3/WAV matches what the user heard.
    // We trap NoteOns here (instead of letting them fall through to
    // handle_midi_event below) so we can pre-scale the velocity by the
    // active preset's per-program factor. Muted slots drop the note.
    if (_exportBardActive && type == 0x90 && channel >= 0 && channel < 16) {
        int vel = fluid_midi_event_get_velocity(event);
        int key = fluid_midi_event_get_key(event);
        if (vel > 0) {
            int activeProgram = _exportCurrentProgram[channel];
            float g = FfxivEqualizerService::instance()->gainFor(activeProgram, /*isDrum=*/false);
            if (g <= 0.0f) {
                return FLUID_OK;
            }
            int scaled = static_cast<int>(vel * g + 0.5f);
            vel = qBound(1, scaled, 127);
            fluid_synth_noteon(synth, channel, key, vel);
        } else {
            fluid_synth_noteoff(synth, channel, key);
        }
        return FLUID_OK;
    }

    return fluid_synth_handle_midi_event(synth, event);
}

// Per-export state — only one offline render runs at a time.
quint32 FluidSynthEngine::_exportMutedChannelsMask = 0;
bool    FluidSynthEngine::_exportBardActive = false;
bool    FluidSynthEngine::_exportExplicitPC[16] = {false};
int     FluidSynthEngine::_exportCurrentProgram[16] = {0};

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
    bool changed = (_ffxivSoundFontMode != enabled);
    _ffxivSoundFontMode = enabled;
    applyChannelMode();
    applyBardAccuracySettings();
    if (changed) {
        emit ffxivSoundFontModeChanged(enabled);
    }
}

void FluidSynthEngine::setBardAccurateMode(bool enabled) {
    if (_bardAccurateMode == enabled) {
        return;
    }
    _bardAccurateMode = enabled;
    applyBardAccuracySettings();
    qDebug() << "FluidSynth: Bard accurate mode" << (enabled ? "ON" : "OFF");
}

bool FluidSynthEngine::bardAccurateMode() const {
    return _bardAccurateMode;
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

// ============================================================================
// Bard-accuracy helpers (FFXIV SoundFont + bardAccurateMode)
//
// These shape playback so it sounds like the in-game FFXIV bard performance
// (and like LightAmp's "Song Preview"):
//   * Reverb + chorus disabled (FFXIV samples are dry in-game).
//   * Polyphony capped at 16 voices, voice-stealing handled by FluidSynth.
//   * Velocity forced to 127, dynamics carried by CC7 / CC11.
//   * (CC7 * CC11)^3 cubic curve approximates perceptual loudness.
//   * Per-instrument minimum NoteOff length stretches staccatos to the
//     natural sustain of the FFXIV samples (no clicks on short Harp/Piano).
//
// Numbers ported verbatim from LightAmp:
//   BardMusicPlayer.Siren/Utils/Utils.cs MinimumLength()
// ============================================================================

void FluidSynthEngine::applyBardAccuracySettings() {
    if (!_initialized || !_synth) {
        return;
    }
    bool active = _ffxivSoundFontMode && _bardAccurateMode;

    // Reverb / chorus: forced off in bard mode, otherwise honour user setting
    fluid_synth_set_reverb_on(_synth, active ? 0 : (_reverbEnabled ? 1 : 0));
    fluid_synth_set_chorus_on(_synth, active ? 0 : (_chorusEnabled ? 1 : 0));

    // Polyphony cap: 16 voices in bard mode, otherwise FluidSynth default (256)
    fluid_synth_set_polyphony(_synth, active ? 16 : 256);

    // Reset cubic-curve state so we don't keep stale values when toggling
    if (active) {
        for (int ch = 0; ch < 16; ++ch) {
            _bardCC7[ch] = 127;
            _bardCC11[ch] = 127;
            // push neutral CC11 so any prior expression doesn't bleed through
            fluid_synth_cc(_synth, ch, 11, 127);
        }
        // Phase 39: push combined bard+EQ attenuation in one pass.
        applyFfxivEqualizerAttenuation(-1);
    } else {
        // Leaving bard mode: clear our additive attenuation so non-bard
        // playback is back to the SoundFont's baked level. If FFXIV mode
        // is still on, re-apply just the EQ contribution.
        for (int ch = 0; ch < 16; ++ch) {
            fluid_synth_set_gen(_synth, ch, GEN_ATTENUATION, 0.0f);
        }
        if (_ffxivSoundFontMode) {
            applyFfxivEqualizerAttenuation(-1);
        }
    }

    qDebug() << "FluidSynth: Bard accuracy" << (active ? "ON" : "OFF")
             << "(ffxiv=" << _ffxivSoundFontMode
             << "bardAccurate=" << _bardAccurateMode << ")";
}

int FluidSynthEngine::bardInstrumentIndexForProgram(int program) {
    // Maps GM/FFXIV program numbers to LightAmp's 1..28 instrument index.
    // Includes both LightAmp's reference programs and the program numbers our
    // FFXIV SoundFont actually uses (drum SnareDrum=118, Cymbal=119, etc).
    switch (program) {
    case 46: return 1;   // Harp
    case 0:  return 2;   // Piano
    case 24: case 25: return 3;  // Lute (24 = nylon → remapped to 25 in FFXIV mode)
    case 45: return 4;   // Fiddle
    case 73: return 5;   // Flute
    case 68: return 6;   // Oboe
    case 71: return 7;   // Clarinet
    case 72: return 8;   // Fife
    case 75: return 9;   // Panpipes
    case 47: return 10;  // Timpani
    case 116: return 11; // Bongo
    case 117: return 12; // Bass Drum
    case 115: case 118: return 13; // Snare Drum (LA=115, our SF2=118)
    case 119: case 127: return 14; // Cymbal (LA=127, our SF2=119)
    case 56: return 15;  // Trumpet
    case 57: return 16;  // Trombone
    case 58: return 17;  // Tuba
    case 60: return 18;  // Horn
    case 65: return 19;  // Saxophone
    case 40: return 20;  // Violin
    case 41: return 21;  // Viola
    case 42: return 22;  // Cello
    case 43: return 23;  // Double Bass
    case 29: return 24;  // ElectricGuitar Overdriven
    case 27: return 25;  // ElectricGuitar Clean (also our 26→27 fallback target)
    case 28: return 26;  // ElectricGuitar Muted
    case 30: return 27;  // ElectricGuitar PowerChords
    case 31: return 28;  // ElectricGuitar Special
    default: return -1;
    }
}

qint64 FluidSynthEngine::bardMinNoteLengthMs(int instrumentIndex, int midiKey, qint64 duration) {
    // LightAmp's MinimumLength() uses (midiKey - 48) as the register offset.
    int n = midiKey - 48;
    if (n < 0) n = 0;
    switch (instrumentIndex) {
    case 1: // Harp
        if (n <= 19) return 1338;
        if (n <= 28) return 1334;
        return 1136;
    case 2: // Piano
        if (n <= 25) return 1531;
        if (n <= 28) return 1332;
        return 1531;
    case 3: // Lute
        if (n <= 14) return 1728;
        if (n <= 28) return 1727;
        return 1528;
    case 4: // Fiddle
        return 634;
    case 5: case 6: case 7: case 8: case 9: // Flute / Oboe / Clarinet / Fife / Panpipes
        return duration > 4500 ? 4500 : (duration < 500 ? 500 : duration);
    case 10: // Timpani
        if (n <= 15) return 1193;
        if (n <= 23) return 1355;
        return 1309;
    case 11: // Bongo
        if (n <= 7)  return 720;
        if (n <= 21) return 544;
        return 275;
    case 12: // Bass Drum
        if (n <= 6)  return 448;
        if (n <= 11) return 335;
        if (n <= 23) return 343;
        return 254;
    case 13: return 260; // Snare Drum
    case 14: return 700; // Cymbal
    case 15: case 16: case 17: case 18: case 19: // Brass
    case 20: case 21: case 22: case 23: // Strings
    case 24: case 25: case 27:          // E-Guitar Overdriven/Clean/PowerChords
        return duration > 4500 ? 4500 : (duration < 300 ? 300 : duration);
    case 26: // ElectricGuitar Muted
        if (n <= 18) return 186;
        if (n <= 21) return 158;
        return 174;
    case 28: // ElectricGuitar Special
        return 1500;
    default:
        return 0;
    }
}

void FluidSynthEngine::resetBardNoteState() {
    for (int ch = 0; ch < 16; ++ch) {
        _bardCurrentProgram[ch] = 0;
        _bardCC7[ch] = 127;
        _bardCC11[ch] = 127;
        for (int k = 0; k < 128; ++k) {
            _bardNoteOnMs[ch][k] = 0;
            _bardNoteHeld[ch][k] = false;
            // Bump generation so any pending QTimer NoteOff lambdas drop their work.
            _bardNoteGen[ch][k]++;
        }
    }
}

void FluidSynthEngine::applyBardVolumeCurve(int channel) {
    if (!_initialized || !_synth) return;
    if (channel < 0 || channel >= 16) return;
    double v = (_bardCC7[channel] / 127.0) * (_bardCC11[channel] / 127.0);
    v = v * v * v;
    int cubed = qBound(0, int(std::lround(v * 127.0)), 127);
    fluid_synth_cc(_synth, channel, 7, cubed);
    // Push a neutral expression so FluidSynth doesn't multiply the curve again.
    fluid_synth_cc(_synth, channel, 11, 127);
}

int FluidSynthEngine::bardProgramAttenuationCb(int program) {
    // Centibels of additional attenuation (positive = quieter).
    // Reference (0 cB) is the quietest preset the user noticed:
    // ElectricGuitarClean (program 27). All other bard presets are
    // pulled down so the perceived loudness matches.
    // +20 cB ≈ -2 dB ≈ 25 % perceived loudness drop.
    switch (program) {
    case 27: return 0;   // ElectricGuitarClean (reference)
    case 28: return 0;   // ElectricGuitarSpecial / PowerChords
    case 29: case 30:    // Overdriven / Distortion guitar slot
    case 31:             // Muted electric
        return 0;
    // Loud/bright FFXIV bard presets — attenuate to match clean guitar
    case 0:  case 1:  case 2:  case 3:                  // Piano family
    case 24: case 25:                                   // Harp / Lute
    case 26:                                            // Acoustic guitar fallback
    case 46: case 47:                                   // Harp / Timpani
        return 20;                                      // ≈ -2 dB
    default:
        // Slight pull-down for everything else so the mix stays balanced.
        return 12;                                      // ≈ -1.2 dB
    }
}

// ---------------------------------------------------------------------------
// Phase 39 (FFXIV-EQ-001) — Equalizer attenuation helpers
// ---------------------------------------------------------------------------
float FluidSynthEngine::ffxivEqualizerCb(float gain) {
    if (gain <= 0.0f) {
        // Muted — push to a value well past audible. SF spec caps at
        // 1440 cB (= 144 dB), but FluidSynth clamps internally too.
        return 1440.0f;
    }
    // Amplitude-domain dB: 20 * log10(gain). Centibels = 1/10 dB.
    // Sign: positive cB = MORE attenuation (quieter), so flip sign.
    return static_cast<float>(-200.0 * std::log10(static_cast<double>(gain)));
}

void FluidSynthEngine::applyFfxivEqualizerAttenuation(int channel) {
    if (!_initialized || !_synth) return;
    // Outside FFXIV mode we leave the SoundFont's baked attenuation alone.
    if (!_ffxivSoundFontMode) return;

    auto applyOne = [this](int ch) {
        int prog = _bardCurrentProgram[ch];
        bool isDrum = (ch == 9);
        // Bard-normalisation atten only contributes when bard mode is on,
        // matching the original behaviour in applyBardAccuracySettings().
        float bardCb = _bardAccurateMode
                           ? static_cast<float>(bardProgramAttenuationCb(prog))
                           : 0.0f;
        float eqCb = ffxivEqualizerCb(
            FfxivEqualizerService::instance()->gainFor(prog, isDrum));
        fluid_synth_set_gen(_synth, ch, GEN_ATTENUATION, bardCb + eqCb);
    };

    if (channel < 0) {
        for (int ch = 0; ch < 16; ++ch) applyOne(ch);
    } else if (channel >= 0 && channel < 16) {
        applyOne(channel);
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
    settings->setValue("bardAccurateMode", _bardAccurateMode);

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
    _sampleRate = settings->value("sampleRate", 48000.0).toDouble();
    _reverbEnabled = settings->value("reverbEnabled", true).toBool();
    _chorusEnabled = settings->value("chorusEnabled", true).toBool();
    // Read new key, fall back to old key for backward compatibility
    _ffxivSoundFontMode = settings->value("ffxivSoundFontMode",
        settings->value("allChannelsMelodic", false)).toBool();
    _bardAccurateMode = settings->value("bardAccurateMode", true).toBool();

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

// ============================================================================
// Phase 39: Equalizer dialog test-tone preview.
// ============================================================================
void FluidSynthEngine::playPreviewArpeggio(int program, bool isDrum) {
    if (!_initialized || !_synth) return;

    // Use a dedicated channel that the live MIDI player isn't likely to
    // touch. CH9 for drum previews (GM percussion convention), CH15 for
    // melodic so we don't fight the channel-15 metadata channel that
    // some files use \u2014 we are an interactive auditioner, not a
    // long-running track.
    const int channel = isDrum ? 9 : 15;

    // FFXIV SoundFont only has bank 0; force-select to avoid surprises
    // if a previously-loaded MIDI moved this channel to another bank.
    fluid_synth_bank_select(_synth, channel, 0);
    fluid_synth_program_change(_synth, channel, program);
    // Track the current program on the preview channel so the
    // mixChanged-driven applyFfxivEqualizerAttenuation() refresh can
    // re-push the correct gain while the arpeggio is still playing.
    _bardCurrentProgram[channel] = program;

    // Phase 39 (FFXIV-EQ-001): the preview is the user's auditioning
    // tool — the EQ MUST always affect it, even if FFXIV mode happens
    // to be off when the dialog is open. Push GEN_ATTENUATION directly
    // from the EQ gain so the slider's effect is immediately audible.
    {
        float g = FfxivEqualizerService::instance()->gainFor(program, isDrum);
        fluid_synth_set_gen(_synth, channel, GEN_ATTENUATION,
                            ffxivEqualizerCb(g));
    }

    // For melodic instruments: C4-D4-E4-G4 (Do-Re-Mi-Sol) at ~250 ms
    // per note. For drums: kick / snare / hat / crash so the user
    // hears all four percussion presets at a glance.
    QList<int> keys;
    if (isDrum) {
        keys << 36 << 38 << 42 << 49; // GM kick, snare, closed hat, crash
    } else {
        keys << 60 << 62 << 64 << 67; // C, D, E, G (octave 4)
    }

    const int noteOnSpacingMs = 250;
    const int noteHoldMs      = 220;
    QPointer<FluidSynthEngine> self(this);
    for (int i = 0; i < keys.size(); ++i) {
        int key = keys[i];
        int onDelay  = i * noteOnSpacingMs;
        int offDelay = onDelay + noteHoldMs;
        QTimer::singleShot(onDelay, this, [self, channel, key]() {
            if (!self || !self->_synth) return;
            // Phase 39 (FFXIV-EQ-001): GEN_ATTENUATION is already pushed
            // for this channel above (and refreshed on every slider edit
            // via the mixChanged hook). Here we only need to swallow the
            // NoteOn for muted slots, since GEN_ATTENUATION clamps to a
            // finite value and a muted preset must be truly silent.
            int currentProg = 0;
            fluid_synth_get_program(self->_synth, channel,
                                    nullptr, nullptr, &currentProg);
            float g = FfxivEqualizerService::instance()->gainFor(
                currentProg, /*isDrum=*/(channel == 9));
            if (g <= 0.0f) return; // muted — silent preview
            // A solid mid-velocity gives the SoundFont a chance to use
            // its medium-loudness layer; the GEN_ATTENUATION applied
            // above provides the actual gain trim.
            fluid_synth_noteon(self->_synth, channel, key, 110);
        });
        QTimer::singleShot(offDelay, this, [self, channel, key]() {
            if (!self || !self->_synth) return;
            fluid_synth_noteoff(self->_synth, channel, key);
        });
    }
}

#endif // FLUIDSYNTH_SUPPORT
