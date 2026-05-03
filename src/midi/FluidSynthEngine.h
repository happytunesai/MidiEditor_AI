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
#include <QElapsedTimer>
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
    quint32 mutedChannelsMask = 0; ///< Bit i set = drop all events on MIDI channel i during export.
                                   ///< Mirrors the live-playback behaviour of `MidiFile::channelMuted()`.
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

    /**
     * \brief Returns the FFXIV SF2 program number for a GM percussion key.
     *
     * Used when an arbitrary GM drum-kit channel (typically CH9) plays
     * many different drum sounds (kick, snare, toms, hats, crashes) and
     * we want to route each individual hit to the closest FFXIV bard
     * percussion preset. Only active while FFXIV SoundFont mode is on.
     *
     * \param gmNote The MIDI key from the NoteOn (35..81 cover GM drum kit)
     * \return Program number (0-127) or -1 if no FFXIV match is appropriate
     */
    static int ffxivDrumProgramForGmNote(int gmNote);

    // === Audio Settings ===

    void setAudioDriver(const QString &driver);
    void setGain(double gain);
    void setSampleRate(double rate);
    void setReverbEngine(const QString &engine);
    void setReverbEnabled(bool enabled);
    void setChorusEnabled(bool enabled);
    void setFfxivSoundFontMode(bool enabled);

    /**
     * \brief Toggles "bard accuracy" playback shaping for FFXIV SoundFont mode.
     *
     * When enabled (default) AND FFXIV SoundFont mode is active, FluidSynthEngine:
     *  - disables FluidSynth reverb and chorus (FF14 samples are dry in-game),
     *  - caps polyphony at 16 voices (matches BMP / LightAmp Siren),
     *  - forces NoteOn velocity to 127 (in-game bards have fixed attack),
     *  - applies a cubic (vol*expr)³ volume curve via CC7/CC11 (matches BMP),
     *  - lengthens too-short NoteOffs to per-instrument minima
     *    (e.g. Harp ≥ 1.13 s, Piano ≥ 1.53 s) so staccatos don't click.
     *
     * No effect while FFXIV SoundFont mode is OFF.
     */
    void setBardAccurateMode(bool enabled);
    bool bardAccurateMode() const;

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
    /**
     * \brief Loads all FluidSynth settings from QSettings.
     */
    void loadSettings(QSettings *settings);

    /**
     * \brief Plays a short Do-Re-Mi-Sol arpeggio (C4 D4 E4 G4) on the
     * given GM program — used by the FFXIV SoundFont Equalizer dialog
     * so the user can audition each instrument while sliding its gain.
     *
     * The preview is routed on a dedicated channel (15) so it does not
     * collide with whatever the live MIDI player is doing on channels
     * 0..14, and goes through the same `sendMidiData()` path as live
     * playback so the FfxivEqualizerService gain factor is applied
     * exactly the same way as during normal playback.
     *
     * \param program GM program number 0..127. For drum-kit preview,
     *                pass any percussion preset number (e.g. 117 BassDrum)
     *                and set \a isDrum=true to also route the test
     *                sequence to GM standard kit keys instead of pitches.
     * \param isDrum  If true, plays kick/snare/hat/crash on CH9 instead
     *                of the C-D-E-G arpeggio on CH15.
     */
    void playPreviewArpeggio(int program, bool isDrum = false);

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

    // === Bard-accuracy helpers (FFXIV SoundFont + bardAccurateMode) ===
    /// Apply runtime synth state for current ffxiv/bard combination.
    void applyBardAccuracySettings();
    /// Map MIDI program number to LightAmp's FFXIV instrument index (1..28).
    /// Returns -1 if program isn't a known FFXIV bard instrument.
    static int bardInstrumentIndexForProgram(int program);
    /// Minimum note length in milliseconds for a given instrument index and MIDI key.
    /// Returns 0 when no minimum applies. \a duration is the natural duration that
    /// should be returned when no register-specific floor applies.
    static qint64 bardMinNoteLengthMs(int instrumentIndex, int midiKey, qint64 duration);
    /// Reset all per-channel/per-key bard state (note-on times, held flags, generations).
    void resetBardNoteState();
    /// Cubic-curve recomputation for one channel (CC7 · CC11)^3, sent as CC7 to synth.
    void applyBardVolumeCurve(int channel);

    /// Per-program loudness trim in centibels (positive = additional
    /// attenuation). Compensates for the FFXIV SoundFont presets being
    /// sampled at uneven levels (e.g. ElectricGuitarClean is noticeably
    /// quieter than Lute / Harp / Piano). Applied as an additive
    /// `GEN_ATTENUATION` per channel in bard mode.
    static int bardProgramAttenuationCb(int program);

    /// Phase 39 (FFXIV-EQ-001): convert an EQ gain factor (0.0..2.0)
    /// to additional `GEN_ATTENUATION` centibels. gain=1.0 → 0 cB,
    /// gain=0.5 → +60 cB (quieter), gain=2.0 → -60 cB (louder).
    /// Returns a sentinel high value (well past audible) for gain<=0
    /// so a muted slot is fully silent.
    static float ffxivEqualizerCb(float gain);

    /// Phase 39: push the combined (bard + EQ) `GEN_ATTENUATION` for
    /// the given channel using the channel's last-seen program. Pass
    /// channel = -1 to refresh all 16 channels. Cheap no-op outside
    /// FFXIV SoundFont mode. Wired to FfxivEqualizerService::mixChanged
    /// in initialize() so live slider edits affect playback instantly.
    void applyFfxivEqualizerAttenuation(int channel = -1);

    /// fluid_player_t playback callback used by exportAudio() so offline
    /// rendering applies the same FFXIV bank-select / program-fallback as
    /// live playback. \a data is the export fluid_synth_t*.
    static int exportPlaybackCallback(void *data, fluid_midi_event_t *event);

    /// Per-export state read by the playback callback. Set in exportAudio()
    /// before the player runs and reset on completion. Only safe for one
    /// concurrent export (which is the only mode the engine supports).
    static quint32 _exportMutedChannelsMask;
    static bool    _exportBardActive;
    // Per-channel "the file already gave us an explicit Program Change".
    // Used by the export callback so the GM-drum-key fallback on CH9
    // doesn't overwrite the program a Snare Drum / Bass Drum track
    // already requested via an injected PC.
    static bool    _exportExplicitPC[16];
    /// Phase 39: per-channel current program — needed so the offline
    /// playback callback can ask FfxivEqualizerService for the right
    /// gain factor on every NoteOn (the GM/FFXIV program isn't
    /// reachable from the fluid_synth_t* alone). Updated on every PC.
    static int     _exportCurrentProgram[16];

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
    bool _bardAccurateMode;

    // === Bard-accuracy runtime state ===
    QElapsedTimer _bardClock;             // time base for note-on timestamps
    int _bardCurrentProgram[16];          // last program number seen per channel
    int _bardCC7[16];                     // last CC7 value per channel (default 127)
    int _bardCC11[16];                    // last CC11 value per channel (default 127)
    qint64 _bardNoteOnMs[16][128];        // ms timestamp of last NoteOn per (ch,key)
    bool _bardNoteHeld[16][128];          // true while a NoteOn for (ch,key) is sounding
    quint32 _bardNoteGen[16][128];        // generation counter to invalidate pending offs

    // Export cancel flag (thread-safe)
    std::atomic<bool> _cancelExport{false};
};

#endif // FLUIDSYNTH_SUPPORT

#endif // FLUIDSYNTHENGINE_H_
