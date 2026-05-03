/*
 * MidiEditor AI — FFXIV SoundFont Equalizer Service (Phase 39)
 *
 * Singleton that owns the per-instrument volume preset used by the
 * FluidSynth NoteOn path (live playback) and the offline export
 * playback callback. Hooks are gated on FFXIV SoundFont Mode being
 * active, so non-FFXIV users pay zero overhead.
 *
 * Persistence: presets live under
 *   QSettings("MidiEditor", "NONE")/FFXIV/equalizerPresets/<name>
 * with the active preset name under
 *   QSettings(...)/FFXIV/equalizerActivePreset
 *
 * The built-in "FFXIV Default" preset is always available and
 * cannot be overwritten or deleted; user copies are editable.
 */

#ifndef FFXIVEQUALIZERSERVICE_H_
#define FFXIVEQUALIZERSERVICE_H_

#include <QObject>
#include <QHash>
#include <QString>
#include <QStringList>

class QSettings;

/**
 * \class FfxivEqualizerService
 *
 * Per-program volume mixer. `gainFor(program, isDrum)` returns a
 * float in [0.0, 2.0] (1.0 = unity). A muted slot returns 0.0 so the
 * caller can short-circuit the NoteOn entirely. The service has no
 * dependency on FluidSynth itself — the engine just queries it.
 */
class FfxivEqualizerService : public QObject {
    Q_OBJECT

public:
    /// Synthetic "program" key used for the GM Standard Drum Kit
    /// (CH9 in non-FFXIV mode, where individual hits never fire a
    /// Program Change). Sits outside the 0..127 GM space on purpose.
    static constexpr int kDrumKitProgram = 200;

    /// Per-program mixer state. Public so the dialog can render rows
    /// from a snapshot without piercing private state.
    struct Slot {
        float gain = 1.0f;
        bool muted = false;
    };

    /// Ordered list of (FFXIV instrument display name, GM program) pairs
    /// shown in the dialog. Drum kit lives at index 0 with kDrumKitProgram.
    static const QList<QPair<QString, int>> &knownInstruments();

    static FfxivEqualizerService *instance();

    // === Live mixer query (called from real-time NoteOn path) ===

    /// Gain factor in [0.0, 2.0] for `program`. `isDrum` routes the
    /// query to the synthetic drum-kit slot when the caller doesn't
    /// know an explicit FFXIV percussion program. Always returns
    /// 1.0 (no-op) before initialize() runs or when the slot is unknown.
    float gainFor(int program, bool isDrum = false) const;

    /// True if the program (or drum kit) is currently muted.
    bool isMuted(int program, bool isDrum = false) const;

    /// Master gain on top of the per-program factor. [0.0, 2.0]
    float masterGain() const { return _masterGain; }

    // === Mutating API (used by the dialog) ===

    void setProgramGain(int program, float gain);
    void setProgramMuted(int program, bool muted);
    void setMasterGain(float gain);

    /// Reset every program slot to the active preset's stored value.
    void revertToActivePreset();

    /// Reset every program slot to the built-in "FFXIV Default".
    void resetToBuiltinDefault();

    // === Preset management (persisted in QSettings) ===

    /// Stable identifier for the built-in preset.
    static const QString &builtinPresetName();

    QString activePresetName() const { return _activePresetName; }
    QStringList allPresetNames() const;

    /// Switch the active preset. Loads its values into the live mixer.
    void setActivePreset(const QString &name);

    /// Persist the live mixer state under `name`. Refuses to overwrite
    /// the built-in preset name. Emits presetSaved.
    bool savePresetAs(const QString &name);

    /// Delete a user preset by name. The built-in preset cannot be
    /// deleted; if `name` is the active preset, the active preset
    /// falls back to the built-in default.
    bool deletePreset(const QString &name);

    // === Persistence ===

    void loadFromSettings(QSettings *settings);
    void saveToSettings(QSettings *settings) const;

    /// Convenience: load using the global QSettings singleton.
    void initialize();

    /// Snapshot of the live mixer state — used by the dialog to seed
    /// row controls without touching private members.
    QHash<int, Slot> currentSlotsSnapshot() const;

signals:
    /// Emitted whenever any per-program slot or master gain changes
    /// so live previews / FluidSynth hooks can react.
    void mixChanged();

    /// Emitted after savePresetAs / deletePreset / setActivePreset.
    void presetsChanged();

private:
    FfxivEqualizerService();
    Q_DISABLE_COPY(FfxivEqualizerService)

    void applyPresetData(const QHash<int, Slot> &data, float master);

    QHash<int, Slot> _slots;     ///< program → gain/mute
    float            _masterGain = 1.0f;
    QString          _activePresetName;
};

#endif // FFXIVEQUALIZERSERVICE_H_
