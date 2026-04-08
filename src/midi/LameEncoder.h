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

#ifndef LAMEENCODER_H
#define LAMEENCODER_H

#if defined(FLUIDSYNTH_SUPPORT) && defined(LAME_SUPPORT)

#include <QString>
#include <functional>
#include <atomic>

/**
 * \class LameEncoder
 * \brief Encodes WAV files to MP3 using the built-in LAME library.
 *
 * LAME is compiled directly into the application — no external DLL or exe needed.
 */
class LameEncoder {
public:
    /** Always returns true (LAME is compiled in). */
    static bool isAvailable();

    /**
     * \brief Encode a WAV file to MP3.
     *
     * Blocking call — run from a worker thread.
     *
     * \param wavPath   Input WAV file path.
     * \param mp3Path   Output MP3 file path.
     * \param bitrate   CBR bitrate in kbps (128, 192, 256, 320).
     * \param progress  Optional callback receiving percent 0–100.
     * \param cancelFlag Optional atomic flag; when set to true, encoding aborts.
     * \return True on success.
     */
    static bool encode(const QString &wavPath, const QString &mp3Path,
                       int bitrate = 192,
                       std::function<void(int percent)> progress = nullptr,
                       std::atomic<bool> *cancelFlag = nullptr);
};

#endif // FLUIDSYNTH_SUPPORT && LAME_SUPPORT
#endif // LAMEENCODER_H
