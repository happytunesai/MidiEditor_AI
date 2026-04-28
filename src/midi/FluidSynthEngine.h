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

#ifndef FLUIDSYNTHENGINE_H_
#define FLUIDSYNTHENGINE_H_

#ifdef FLUIDSYNTH_SUPPORT

#include <QObject>
#include <QList>
#include <QPair>
#include <QSet>
#include <QString>
#include <QStringList>

#include <atomic>
#include <fluidsynth.h>

class QSettings;

/**
 * \struct ExportOptions
 * \brief Configuration for audio export (MIDI → WAV/FLAC/OGG).
 */
struct ExportOptions {
    QString midiFilePath;       ///< Source MIDI file (usually a temp file from workspace)
    QString outputFilePath;     ///< Destination audio file path
    QString fileType;           ///< FluidSynth audio.file.type: "wav", "flac", "oga", "aiff", "raw", or "mp3"
    QString sampleFormat;       ///< FluidSynth audio.file.format: "s16", "s24", "s32", "float"
    double sampleRate = 44100.0;///< Sample rate in Hz
    double encodingQuality = 0.5; ///< 0.0–1.0 for lossy formats (OGG Vorbis)
    bool includeReverbTail = true; ///< Render extra ~2s after last note for reverb decay
    int startTick = -1;         ///< First tick to include (-1 = from beginning)
    int endTick = -1;           ///< Last tick to include (-1 = until end)
    int mp3Bitrate = 192;       ///< MP3 CBR bitrate in kbps (128, 192, 256, 320)
    bool deleteMidiFileAfterExport = false; ///< Delete midiFilePath after export (for temp files)
};

/**
 * \class FluidSynthEngine
 *
 * \brief Singleton managing the FluidSynth software synthesizer lifecycle.
 *
 * FluidSynthEngine provides a built-in MIDI synthesizer using FluidSynth,
 * eliminating the need for an external softsynth. It manages:
 *
 * - **Synthesizer lifecycle**: Creation and teardown of FluidSynth instances
 * - **SoundFont management**: Loading, unloading, and priority-based stacking
 * - **MIDI routing**: Parsing raw MIDI bytes and dispatching to FluidSynth
 * - **Audio configuration**: Driver, gain, sample rate, reverb, chorus
 * - **Settings persistence**: Save/restore configuration via QSettings
 *
 * SoundFont Stacking:
 * FluidSynth uses a stack-based priority system. The last-loaded SoundFont
 * has highest priority (checked first for instrument presets). This engine
 * exposes that as a reorderable list where top = highest priority.
 */
class FluidSynthEngine : public QObject {
    Q_OBJECT

public:
    /**
     * \brief Returns the singleton instance.
     */
    static FluidSynthEngine* instance();

    // === Lifecycle ===

    /**
     * \brief Initializes the synthesizer, settings, and audio driver.
     * \return True if initialization was successful
     */
    bool initialize();

    /**
     * \brief Shuts down the audio driver, synth, and settings.
     */
    void shutdown();

    /**
     * \brief Returns true if the engine is initialized and ready.
     */
    bool isInitialized() const;

    // === SoundFont Management ===

    /**
     * \brief Loads a SoundFont file.
     * \param path Absolute path to an SF2 or SF3 file
     * \return FluidSynth sfont_id on success, -1 on error
     */
    int loadSoundFont(const QString &path);

    /**
     * \brief Unloads a SoundFont by its ID.
     * \param sfontId The FluidSynth sfont_id returned by loadSoundFont
     * \return True if unloaded successfully
     */
    bool unloadSoundFont(int sfontId);

    /**
     * \brief Removes a SoundFont by path (works for both enabled and disabled fonts).
     */
    void removeSoundFontByPath(const QString &path);

    /**
     * \brief Unloads all loaded SoundFonts.
     */
    void unloadAllSoundFonts();

    /**
     * \brief Returns the currently loaded SoundFonts in priority order.
     * \return List of (sfont_id, file_path) pairs, highest priority first
     */
    QList<QPair<int, QString>> loadedSoundFonts() const;

    /**
     * \brief Replaces the entire SoundFont stack with the given paths.
     *
     * Unloads all current SoundFonts and reloads in the given order.
     * The last item in the list will have the highest priority in FluidSynth
     * (loaded last). The UI presents top = highest priority, so this method
     * loads items in reverse order. Disabled SoundFonts are tracked but not loaded.
     *
     * \param paths List of SF2/SF3 file paths, top = highest priority
     */
    void setSoundFontStack(const QStringList &paths);

    /**
     * \brief Returns ALL SoundFont paths (enabled + disabled) in priority order.
     * Top = highest priority.
     */
    QStringList allSoundFontPaths() const;

    /**
     * \brief Enable or disable a SoundFont without removing it from the stack.
     * Disabled SoundFonts are not loaded into FluidSynth but remain in the list.
     */
    void setSoundFontEnabled(const QString &path, bool enabled);

    /**
     * \brief Returns true if the SoundFont at the given path is enabled.
     */
    bool isSoundFontEnabled(const QString &path) const;

    /**
     * \brief Adds SoundFont paths to the pending list (used before initialization).
     * These will be loaded when the engine initializes.
     */
    void addPendingSoundFontPaths(const QStringList &paths);

    // === MIDI Message Routing ===

    /**
     * \brief Routes raw MIDI bytes to the FluidSynth synthesizer.
     * \param data Raw MIDI message bytes
     */
    void sendMidiData(const QByteArray &data);

    // === Audio Export ===

    /**
     * \brief Renders a MIDI file to an audio file using the current SoundFont stack.
     *
     * Creates a separate FluidSynth instance (does not affect live playback).
     * Runs synchronously — call from a worker thread (e.g. QThreadPool).
     * Emits exportProgress(), exportFinished(), or exportCancelled().
     *
     * \param options Export configuration (format, quality, paths, etc.)
     */
    void exportAudio(const ExportOptions &options);

    /**
     * \brief Cancels an in-progress export.
     * Thread-safe — can be called from any thread.
     */
    void cancelExport();

    /**
     * \brief Returns a pointer to the cancel flag (for passing to blocking encoders).
     */
    std::atomic<bool>* cancelExportFlag() { return &_cancelExport; }

    /**
     * \brief Returns the FFXIV SF2 program number for a percussion track name.
     *
     * Maps track names like "Snare Drum", "Bass Drum+1" etc. to the correct
     * Bank 0 preset in the FFXIV SoundFont. Only active when All Channels
     * Melodic mode is enabled.
     *
     * \param trackName The MIDI track name (may include octave suffix)
     * \return Program number (0-127) or -1 if not a known drum instrument
     */
    int drumProgramForTrackName(const QString &trackName) const;

    // === Audio Settings ===

    void setAudioDriver(const QString &driver);
    void setGain(double gain);
    void setSampleRate(double rate);
    void setReverbEngine(const QString &engine);
    void setReverbEnabled(bool enabled);
    void setChorusEnabled(bool enabled);
    void setFfxivSoundFontMode(bool enabled);

    bool ffxivSoundFontMode() const;
    QString audioDriver() const;
    double gain() const;
    double sampleRate() const;
    QString reverbEngine() const;
    bool reverbEnabled() const;
    bool chorusEnabled() const;
    QStringList availableAudioDrivers() const;

    /**
     * \brief Returns a human-readable display name for an audio driver.
     */
    static QString audioDriverDisplayName(const QString &driver);

    /**
     * \brief Checks if a SoundFont with the same path is already loaded.
     */
    bool isSoundFontLoaded(const QString &path) const;

    // === Persistence ===

    /**
     * \brief Saves all FluidSynth settings to QSettings.
     */
    void saveSettings(QSettings *settings);

    /**
     * \brief Loads all FluidSynth settings from QSettings.
     */
    void loadSettings(QSettings *settings);

signals:
    void soundFontLoaded(int sfontId, const QString &path);
    void soundFontUnloaded(int sfontId);
    void initializationFailed(const QString &error);
    void engineRestarted();
    void exportProgress(int percent);
    void exportFinished(bool success, const QString &outputPath);
    void exportCancelled();
    void ffxivSoundFontModeChanged(bool enabled);

private:
    FluidSynthEngine();
    ~FluidSynthEngine();

    // Non-copyable
    FluidSynthEngine(const FluidSynthEngine &) = delete;
    FluidSynthEngine &operator=(const FluidSynthEngine &) = delete;

    void applyChannelMode();

    fluid_settings_t *_settings;
    fluid_synth_t *_synth;
    fluid_audio_driver_t *_audioDriver;
    bool _initialized;

    // SoundFont tracking: sfont_id → file path (in load order, last = highest priority)
    QList<QPair<int, QString>> _loadedFonts;

    // Complete SoundFont stack (enabled + disabled, in priority order: last = highest)
    QStringList _soundFontStack;

    // Paths of disabled SoundFonts (tracked but not loaded into synth)
    QSet<QString> _disabledSoundFontPaths;

    // Deferred SoundFont paths loaded from settings before engine init
    QStringList _pendingSoundFontPaths;
    QSet<QString> _pendingDisabledPaths;

    // Cached settings values
    QString _audioDriverName;
    QString _reverbEngine;
    double _gain;
    double _sampleRate;
    bool _reverbEnabled;
    bool _chorusEnabled;
    bool _ffxivSoundFontMode;

    // Export cancel flag (thread-safe)
    std::atomic<bool> _cancelExport{false};
};

#endif // FLUIDSYNTH_SUPPORT

#endif // FLUIDSYNTHENGINE_H_
