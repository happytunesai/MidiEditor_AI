/*
 * MidiEditor AI — FFXIV SoundFont Equalizer Service (Phase 39)
 * Implementation. See FfxivEqualizerService.h for design notes.
 */

#include "FfxivEqualizerService.h"

#include <QSettings>
#include <QStringList>
#include <QtGlobal>
#include <algorithm>

// ---------------------------------------------------------------------------
// All preset persistence MUST use the same QSettings scope as the rest of
// the application (see InstrumentChooser, MidiPilotWidget, ToolDefinitions,
// Appearance, …). The app does NOT call QCoreApplication::setOrganizationName
// — using the default `QSettings settings;` constructor lands writes in an
// unspecified location on Windows and silently desyncs from the rest of the
// codebase. Centralise the scope here so every read and write agrees.
namespace {
inline QSettings ffxivEqSettings() {
    return QSettings(QStringLiteral("MidiEditor"), QStringLiteral("NONE"));
}
}

// ---------------------------------------------------------------------------
// Built-in instrument table — drives the dialog row order *and* the
// FFXIV Default preset. Keep in lockstep with FFXIVChannelFixer.cpp's
// programNumber() map (canonical FFXIV SF preset numbers).
// ---------------------------------------------------------------------------
const QList<QPair<QString, int>> &FfxivEqualizerService::knownInstruments() {
    static const QList<QPair<QString, int>> kList = {
        // Melodic
        {QStringLiteral("Piano"),                 0},
        {QStringLiteral("Lute"),                  25},
        {QStringLiteral("Harp"),                  46},
        {QStringLiteral("Fiddle"),                45},
        {QStringLiteral("Fife"),                  72},
        {QStringLiteral("Flute"),                 73},
        {QStringLiteral("Oboe"),                  68},
        {QStringLiteral("Panpipes"),              75},
        {QStringLiteral("Clarinet"),              71},
        {QStringLiteral("Trumpet"),               56},
        {QStringLiteral("Saxophone"),             65},
        {QStringLiteral("Trombone"),              57},
        {QStringLiteral("Horn"),                  60},
        {QStringLiteral("Tuba"),                  58},
        {QStringLiteral("Violin"),                40},
        {QStringLiteral("Viola"),                 41},
        {QStringLiteral("Cello"),                 42},
        {QStringLiteral("Double Bass"),           43},
        // Percussion (named-track route)
        {QStringLiteral("Timpani"),               47},
        {QStringLiteral("Bongo"),                 116},
        {QStringLiteral("Bass Drum"),             117},
        {QStringLiteral("Snare Drum"),            118},
        {QStringLiteral("Cymbal"),                119},
        // Guitar variants
        {QStringLiteral("E-Guitar Clean"),        27},
        {QStringLiteral("E-Guitar Muted"),        28},
        {QStringLiteral("E-Guitar Overdriven"),   29},
        {QStringLiteral("E-Guitar Power Chords"), 30},
        {QStringLiteral("E-Guitar Special"),      31},
    };
    return kList;
}

// FFXIV Default — curated trims to balance the bard SoundFont in-game.
// Drums slightly tamed, brass nudged down, melodic stays at unity.
static QHash<int, FfxivEqualizerService::Slot> builtinDefaultSlots() {
    using Slot = FfxivEqualizerService::Slot;
    QHash<int, Slot> m;
    auto put = [&](int prog, float g, bool muted = false) {
        Slot s; s.gain = g; s.muted = muted; m.insert(prog, s);
    };
    // Melodic — Lute/Harp/Piano stay at unity (reference tracks).
    put(0,   1.00f); // Piano
    put(25,  1.00f); // Lute
    put(46,  1.00f); // Harp
    put(45,  0.95f); // Fiddle
    put(72,  0.95f); // Fife
    put(73,  0.95f); // Flute
    put(68,  0.92f); // Oboe
    put(75,  0.95f); // Panpipes
    put(71,  0.95f); // Clarinet
    put(56,  0.85f); // Trumpet
    put(65,  0.92f); // Saxophone
    put(57,  0.88f); // Trombone
    put(60,  0.90f); // Horn
    put(58,  0.85f); // Tuba
    put(40,  1.00f); // Violin
    put(41,  1.00f); // Viola
    put(42,  1.00f); // Cello
    put(43,  1.00f); // Double Bass
    // Per-instrument percussion presets — slightly tamed
    put(47,  0.85f); // Timpani
    put(116, 0.80f); // Bongo
    put(117, 0.75f); // Bass Drum
    put(118, 0.75f); // Snare Drum
    put(119, 0.75f); // Cymbal
    // Guitar
    put(27,  1.00f); // Clean
    put(28,  1.00f); // Muted
    put(29,  0.95f); // Overdriven (already loud)
    put(30,  0.95f); // Power Chords
    put(31,  1.00f); // Special
    return m;
}

const QString &FfxivEqualizerService::builtinPresetName() {
    static const QString kName = QStringLiteral("FFXIV Default");
    return kName;
}

// ---------------------------------------------------------------------------
// Singleton
// ---------------------------------------------------------------------------
FfxivEqualizerService *FfxivEqualizerService::instance() {
    static FfxivEqualizerService inst;
    return &inst;
}

FfxivEqualizerService::FfxivEqualizerService() {
    // Seed with built-in defaults so callers get sensible answers
    // even before initialize() runs (e.g. unit tests, headless mode).
    applyPresetData(builtinDefaultSlots(), 1.0f);
    _activePresetName = builtinPresetName();
}

// ---------------------------------------------------------------------------
// Live mixer query
// ---------------------------------------------------------------------------
float FfxivEqualizerService::gainFor(int program, bool isDrum) const {
    int key = isDrum ? kDrumKitProgram : program;
    auto it = _slots.constFind(key);
    if (it == _slots.constEnd()) {
        // Unknown program → treat as unity * master (safe fallback).
        return _masterGain;
    }
    if (it->muted) return 0.0f;
    return it->gain * _masterGain;
}

bool FfxivEqualizerService::isMuted(int program, bool isDrum) const {
    int key = isDrum ? kDrumKitProgram : program;
    auto it = _slots.constFind(key);
    return (it != _slots.constEnd()) && it->muted;
}

// ---------------------------------------------------------------------------
// Mutating API
// ---------------------------------------------------------------------------
void FfxivEqualizerService::setProgramGain(int program, float gain) {
    gain = qBound(0.0f, gain, 2.0f);
    Slot &s = _slots[program];
    if (qFuzzyCompare(s.gain, gain)) return;
    s.gain = gain;
    emit mixChanged();
}

void FfxivEqualizerService::setProgramMuted(int program, bool muted) {
    Slot &s = _slots[program];
    if (s.muted == muted) return;
    s.muted = muted;
    emit mixChanged();
}

void FfxivEqualizerService::setMasterGain(float gain) {
    gain = qBound(0.0f, gain, 2.0f);
    if (qFuzzyCompare(_masterGain, gain)) return;
    _masterGain = gain;
    emit mixChanged();
}

void FfxivEqualizerService::revertToActivePreset() {
    QString prev = _activePresetName;
    setActivePreset(prev);
}

void FfxivEqualizerService::resetToBuiltinDefault() {
    applyPresetData(builtinDefaultSlots(), 1.0f);
    emit mixChanged();
}

void FfxivEqualizerService::applyPresetData(const QHash<int, Slot> &data, float master) {
    _slots = data;
    _masterGain = qBound(0.0f, master, 2.0f);
}

QHash<int, FfxivEqualizerService::Slot>
FfxivEqualizerService::currentSlotsSnapshot() const {
    return _slots;
}

// ---------------------------------------------------------------------------
// Preset management
// ---------------------------------------------------------------------------
QStringList FfxivEqualizerService::allPresetNames() const {
    QSettings settings = ffxivEqSettings();
    // Authoritative: explicit QStringList index. Avoids the QSettings
    // childGroups() visibility quirk where path-style setValue() writes
    // do not always expose intermediate components as child groups.
    QStringList names =
        settings.value(QStringLiteral("FFXIV/equalizerPresetIndex"))
                .toStringList();
    // Migration / safety net: fold in any presets discovered via the
    // child-group scan (covers presets written by older builds).
    settings.beginGroup(QStringLiteral("FFXIV/equalizerPresets"));
    const QStringList groups = settings.childGroups();
    settings.endGroup();
    for (const QString &g : groups) {
        if (g != builtinPresetName() && !names.contains(g))
            names.append(g);
    }
    if (!names.contains(builtinPresetName())) {
        names.prepend(builtinPresetName());
    }
    std::sort(names.begin(), names.end(), [](const QString &a, const QString &b) {
        if (a == builtinPresetName()) return true;
        if (b == builtinPresetName()) return false;
        return a.localeAwareCompare(b) < 0;
    });
    return names;
}

void FfxivEqualizerService::setActivePreset(const QString &name) {
    QSettings settings = ffxivEqSettings();
    if (name == builtinPresetName()) {
        applyPresetData(builtinDefaultSlots(), 1.0f);
    } else {
        const QString base = QStringLiteral("FFXIV/equalizerPresets/") + name;
        // Use the explicit index list as the authoritative existence check
        // (mirrors allPresetNames()) and fall back to the path probe.
        // The previous childGroups("FFXIV") check could spuriously fail on
        // backends where intermediate path components aren't enumerated,
        // causing a freshly-saved preset to be silently reverted to builtin.
        const QStringList index =
            settings.value(QStringLiteral("FFXIV/equalizerPresetIndex"))
                    .toStringList();
        const bool inIndex = index.contains(name);
        const bool hasMaster =
            !settings.value(base + QStringLiteral("/master")).isNull();
        if (!inIndex && !hasMaster) {
            // Unknown preset — fall back to built-in default.
            applyPresetData(builtinDefaultSlots(), 1.0f);
            _activePresetName = builtinPresetName();
            settings.setValue(QStringLiteral("FFXIV/equalizerActivePreset"),
                              _activePresetName);
            emit mixChanged();
            emit presetsChanged();
            return;
        }
        QHash<int, Slot> loaded;
        float master = settings.value(base + QStringLiteral("/master"), 1.0).toFloat();
        settings.beginGroup(base + QStringLiteral("/programs"));
        const QStringList keys = settings.childKeys();
        for (const QString &k : keys) {
            QStringList v = settings.value(k).toStringList();
            // Encoded as ["gain", "muted"] strings to keep the QSettings
            // INI human-readable.
            if (v.size() != 2) continue;
            Slot s;
            s.gain  = v[0].toFloat();
            s.muted = (v[1] == QLatin1String("1"));
            loaded.insert(k.toInt(), s);
        }
        settings.endGroup();
        if (loaded.isEmpty()) {
            // Empty group → fall back to built-in default.
            applyPresetData(builtinDefaultSlots(), 1.0f);
        } else {
            applyPresetData(loaded, master);
        }
    }
    _activePresetName = name;
    settings.setValue(QStringLiteral("FFXIV/equalizerActivePreset"),
                      _activePresetName);
    emit mixChanged();
    emit presetsChanged();
}

bool FfxivEqualizerService::savePresetAs(const QString &name) {
    if (name.isEmpty()) return false;
    if (name == builtinPresetName()) return false;
    QSettings settings = ffxivEqSettings();
    // Use explicit beginGroup so Qt registers the preset as a proper child
    // group, making it visible via childGroups() in allPresetNames().
    // setValue("a/b/c") writes correctly but does not reliably expose
    // intermediate path components to childGroups() on all backends.
    settings.beginGroup(QStringLiteral("FFXIV/equalizerPresets"));
    settings.beginGroup(name);
    settings.setValue(QStringLiteral("master"), static_cast<double>(_masterGain));
    settings.beginGroup(QStringLiteral("programs"));
    settings.remove(QString());  // wipe stale keys
    for (auto it = _slots.constBegin(); it != _slots.constEnd(); ++it) {
        QStringList v;
        v << QString::number(it->gain, 'f', 4)
          << (it->muted ? QStringLiteral("1") : QStringLiteral("0"));
        settings.setValue(QString::number(it.key()), v);
    }
    settings.endGroup(); // programs
    settings.endGroup(); // name
    settings.endGroup(); // FFXIV/equalizerPresets
    // Maintain an explicit name index — the source of truth for
    // allPresetNames(). childGroups() is unreliable across QSettings
    // backends/versions for groups created via setValue("a/b/c").
    QStringList index =
        settings.value(QStringLiteral("FFXIV/equalizerPresetIndex"))
                .toStringList();
    if (!index.contains(name)) {
        index.append(name);
        settings.setValue(QStringLiteral("FFXIV/equalizerPresetIndex"), index);
    }
    _activePresetName = name;
    settings.setValue(QStringLiteral("FFXIV/equalizerActivePreset"),
                      _activePresetName);
    settings.sync();  // ensure subsequent QSettings reads see the write
    emit presetsChanged();
    return true;
}

bool FfxivEqualizerService::deletePreset(const QString &name) {
    if (name == builtinPresetName()) return false;
    QSettings settings = ffxivEqSettings();
    QStringList index =
        settings.value(QStringLiteral("FFXIV/equalizerPresetIndex"))
                .toStringList();
    settings.beginGroup(QStringLiteral("FFXIV/equalizerPresets"));
    const bool inGroups = settings.childGroups().contains(name);
    const bool inIndex  = index.contains(name);
    if (!inGroups && !inIndex) {
        settings.endGroup();
        return false;
    }
    settings.remove(name);
    settings.endGroup();
    if (inIndex) {
        index.removeAll(name);
        settings.setValue(QStringLiteral("FFXIV/equalizerPresetIndex"), index);
    }
    settings.sync();
    if (_activePresetName == name) {
        setActivePreset(builtinPresetName());
    } else {
        emit presetsChanged();
    }
    return true;
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------
void FfxivEqualizerService::loadFromSettings(QSettings *settings) {
    if (!settings) return;
    const QString active = settings->value(
        QStringLiteral("FFXIV/equalizerActivePreset"),
        builtinPresetName()).toString();
    setActivePreset(active);
}

void FfxivEqualizerService::saveToSettings(QSettings *settings) const {
    if (!settings) return;
    settings->setValue(QStringLiteral("FFXIV/equalizerActivePreset"),
                       _activePresetName);
}

void FfxivEqualizerService::initialize() {
    QSettings s = ffxivEqSettings();
    loadFromSettings(&s);
}
