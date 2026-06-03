/*
 * MidiEditor AI - C64 SoundFont Mode orchestration (Phase 42.2).
 *
 * Mirrors FfxivSoundFontHelper: toggling C64 SoundFont Mode also manages the
 * FluidSynth SoundFont stack so the mode behaves like FFXIV mode.
 *
 *  - On enable: snapshot the currently active (non-C64) SoundFonts, then
 *    isolate a C64 SoundFont (enable it, disable the rest). If none is loaded,
 *    tell the user to load one (we don't host a redistributable C64 SF -
 *    CC BY-NC).
 *  - On disable: restore the snapshot so playback falls back to the previous
 *    (e.g. General MIDI) SoundFont, exactly like turning FFXIV mode off.
 */

#ifndef C64SOUNDFONTHELPER_H
#define C64SOUNDFONTHELPER_H

#include <QString>

class QWidget;

namespace C64SoundFontHelper {

/// True if the SoundFont basename looks like a Commodore 64 SF.
bool isC64SoundFont(const QString &path);

/// Absolute path of the first C64 SoundFont in <appDir>/soundfonts/, or empty.
QString findLocalC64SoundFont();

/// Enable C64 SoundFont Mode (isolate a C64 SF + set the engine flag).
/// Returns false (and does not enable) when no C64 SoundFont is available.
bool requestEnable(QWidget *parent);

/// Disable C64 SoundFont Mode and restore the previous SoundFont selection.
void requestDisable(QWidget *parent);

/// Normalise the FluidSynth SoundFont to a sane default when NO special mode
/// (C64 SoundFont / FFXIV) is active - called on file load. If a C64 or FFXIV
/// SoundFont is left enabled but its mode is off (e.g. you dragged in a Guitar
/// Pro file while a SID was loaded), disable it and fall back to a General MIDI
/// SoundFont, or to the Microsoft GS Wavetable Synth if no GM SoundFont is
/// loaded. Leaves a plain GM / custom-SoundFont setup untouched, and switches
/// the MIDI output only after stopping playback (BUG-C64-DEFAULTSF).
void normalizeDefaultSoundFont(QWidget *parent);

} // namespace C64SoundFontHelper

#endif // C64SOUNDFONTHELPER_H
