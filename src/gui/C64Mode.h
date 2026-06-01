/*
 * MidiEditor AI - C64 playback-engine mode helper (Phase 42.3, 1.8.0).
 *
 * Single source of truth for the C64 engine choice ("soundfont" vs "emulation").
 * Everything that reads or changes the mode goes through here so the Settings
 * radios, the retro toolbar switch (C64ModeSwitchWidget), the first-use picker
 * and the C64 logo button stay in sync:
 *   - current()/isEmulation()  : read the persisted choice
 *   - setMode()                : persist + hand the active engine over + notify
 *   - ensureChosen()           : show the one-time first-use picker if needed
 *   - prefetchSoundFont()      : background-download the C64 .sf2 (once)
 *   - Notifier::modeChanged()  : repaint signal for the toolbar widgets
 */
#ifndef C64MODE_H
#define C64MODE_H

#include <QObject>
#include <QString>

#include <functional>

class QWidget;

namespace C64Mode {

/// Tiny app-lifetime QObject whose modeChanged() lets toolbar widgets repaint
/// when the engine choice is changed from anywhere (switch / settings / dialog).
class Notifier : public QObject {
    Q_OBJECT
public:
    static Notifier *instance();
    void notifyChanged() { emit modeChanged(); }
signals:
    void modeChanged();
private:
    explicit Notifier(QObject *parent = nullptr) : QObject(parent) {}
};

/// Persisted engine choice: "soundfont" (default) or "emulation".
QString current();
inline bool isEmulation() { return current() == QStringLiteral("emulation"); }

/// Whether the Emulation engine can run right now: it plays the *original* .sid
/// through libsidplayfp, so it needs a .sid to be the open file (SoundFont mode
/// works on any MIDI). False for a plain MIDI - including a SID that was saved
/// as .mid and reopened, since the .sid bytes are gone. The toolbar switch /
/// C64 button / settings radio gate Emulation on this.
bool emulationAvailable();

/// Emulation is both chosen AND possible right now. When the persisted choice is
/// "emulation" but no .sid is open, the *effective* engine is SoundFont.
inline bool isEmulationActive() { return isEmulation() && emulationAvailable(); }

/// Whether the user has made a deliberate first-use choice yet.
bool isChosen();

/// Persist \a mode, hand the active engine over (mirrors the Settings radios),
/// and emit Notifier::modeChanged(). \a parent parents any helper dialogs.
void setMode(const QString &mode, QWidget *parent);

/// If the user hasn't chosen yet, show the one-time SoundFont/Emulation picker
/// (announces the ~11 MB background download), persist the choice + the
/// "chosen" flag, and start prefetchSoundFont(). Returns the active mode.
QString ensureChosen(QWidget *parent);

/// Silently download the C64 SoundFont into the soundfonts/ folder if it isn't
/// installed yet, so a later switch to SoundFont mode is instant. Runs at most
/// once per session; fails quietly (the normal download dialog remains a
/// fallback when SoundFont mode is actually activated).
void prefetchSoundFont();

/// MainWindow registers a hook that fully stops the transport (MIDI player +
/// SID) the same way the Stop button does. The engine handover invokes it
/// FIRST so the FluidSynth synth / MIDI output port can't be torn down while
/// the player thread is still sending events to them - that race is a
/// use-after-free crash (0xc0000005, see BUG-C64-CRASH). No-op if unset.
void setStopPlaybackHook(std::function<void()> hook);

/// Run the registered stop-playback hook (if any) before reconfiguring the
/// audio backend. Safe to call when nothing is playing - the hook guards on
/// the transport state itself.
void stopPlaybackForEngineChange();

} // namespace C64Mode

#endif // C64MODE_H
