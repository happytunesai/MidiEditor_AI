/*
 * MidiEditor
 * Copyright (C) 2010  Markus Schwenk
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef FFXIVSOUNDFONTHELPER_H
#define FFXIVSOUNDFONTHELPER_H

#include <QString>
#include <QStringList>

class QWidget;

/**
 * \namespace FfxivSoundFontHelper
 *
 * Orchestration around toggling FFXIV SoundFont Mode:
 *
 * - On enable: snapshot the currently active (non-FFXIV) SoundFont selection,
 *   then enable a FFXIV SoundFont. If no FFXIV SoundFont is present locally,
 *   prompt the user to download it via the existing DownloadSoundFontDialog.
 *
 * - On disable: restore the snapshotted selection. If no snapshot exists or
 *   no SoundFont is left enabled, fall back to the first non-FFXIV SoundFont
 *   in the stack; if the stack is empty too, switch the active MIDI output
 *   to the system's "Microsoft GS Wavetable Synth" port.
 */
namespace FfxivSoundFontHelper {

/// Returns true if the given SoundFont basename looks like a FFXIV bard SF.
bool isFfxivSoundFont(const QString &path);

/// Returns the absolute path to the first FFXIV SoundFont file found in
/// <appDir>/soundfonts/, or an empty string when none is present.
QString findLocalFfxivSoundFont();

/// Interactively enable FFXIV SoundFont Mode (see namespace comment).
/// Returns true on success, false if the user cancelled the download.
bool requestEnable(QWidget *parent);

/// Interactively disable FFXIV SoundFont Mode (see namespace comment).
void requestDisable(QWidget *parent);

} // namespace FfxivSoundFontHelper

#endif // FFXIVSOUNDFONTHELPER_H
