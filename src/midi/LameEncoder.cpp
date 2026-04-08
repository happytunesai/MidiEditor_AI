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

#if defined(FLUIDSYNTH_SUPPORT) && defined(LAME_SUPPORT)

#include "LameEncoder.h"

#include <QFile>

#include <lame.h>

// ============================================================================
// Availability
// ============================================================================

bool LameEncoder::isAvailable() {
    return true; // Always available â€” LAME is compiled in
}

// ============================================================================
// Encode WAV â†’ MP3
// ============================================================================

bool LameEncoder::encode(const QString &wavPath, const QString &mp3Path,
                         int bitrate, std::function<void(int)> progress,
                         std::atomic<bool> *cancelFlag) {

    // Open WAV file
    QFile wav(wavPath);
    if (!wav.open(QIODevice::ReadOnly)) return false;

    // Read and skip past WAV header to find the "data" chunk
    if (wav.size() < 44) { wav.close(); return false; }

    // Parse minimal WAV header (RIFF PCM)
    QByteArray header = wav.read(12); // "RIFF" + size + "WAVE"
    if (header.size() < 12 || header.left(4) != "RIFF" || header.mid(8, 4) != "WAVE") {
        wav.close();
        return false;
    }

    int channels = 2;
    int sampleRate = 44100;
    int bitsPerSample = 16;
    qint64 dataSize = 0;

    // Walk chunks to find "fmt " and "data"
    while (!wav.atEnd()) {
        QByteArray chunkId = wav.read(4);
        if (chunkId.size() < 4) break;
        QByteArray szBytes = wav.read(4);
        if (szBytes.size() < 4) break;

        quint32 chunkSize =
            static_cast<quint32>(static_cast<unsigned char>(szBytes[0])) |
            (static_cast<quint32>(static_cast<unsigned char>(szBytes[1])) << 8) |
            (static_cast<quint32>(static_cast<unsigned char>(szBytes[2])) << 16) |
            (static_cast<quint32>(static_cast<unsigned char>(szBytes[3])) << 24);

        if (chunkId == "fmt ") {
            QByteArray fmt = wav.read(chunkSize);
            if (fmt.size() >= 16) {
                channels = static_cast<unsigned char>(fmt[2]) | (static_cast<unsigned char>(fmt[3]) << 8);
                sampleRate = static_cast<unsigned char>(fmt[4]) |
                             (static_cast<unsigned char>(fmt[5]) << 8) |
                             (static_cast<unsigned char>(fmt[6]) << 16) |
                             (static_cast<unsigned char>(fmt[7]) << 24);
                bitsPerSample = static_cast<unsigned char>(fmt[14]) | (static_cast<unsigned char>(fmt[15]) << 8);
            }
        } else if (chunkId == "data") {
            dataSize = chunkSize;
            break; // File position is now at start of PCM data
        } else {
            wav.skip(chunkSize);
        }
    }

    if (dataSize == 0 || channels < 1 || channels > 2 || bitsPerSample != 16) {
        wav.close();
        return false;
    }

    // Initialize LAME encoder
    lame_global_flags *gfp = lame_init();
    if (!gfp) { wav.close(); return false; }

    lame_set_in_samplerate(gfp, sampleRate);
    lame_set_num_channels(gfp, channels);
    lame_set_brate(gfp, bitrate);
    lame_set_mode(gfp, channels == 2 ? JOINT_STEREO : MONO);
    lame_set_quality(gfp, 2); // 2 = high quality, good speed
    lame_set_bWriteVbrTag(gfp, 0); // No VBR tag for CBR

    if (lame_init_params(gfp) < 0) {
        lame_close(gfp);
        wav.close();
        return false;
    }

    // Open output MP3 file
    QFile mp3(mp3Path);
    if (!mp3.open(QIODevice::WriteOnly)) {
        lame_close(gfp);
        wav.close();
        return false;
    }

    // Encode in chunks
    const int SAMPLES_PER_READ = 8192;
    const int pcmBufSize = SAMPLES_PER_READ * channels * 2; // 16-bit PCM
    const int mp3BufSize = static_cast<int>(1.25 * SAMPLES_PER_READ + 7200);
    QByteArray pcmBuf(pcmBufSize, 0);
    QByteArray mp3Buf(mp3BufSize, 0);

    qint64 totalRead = 0;
    int lastPercent = -1;
    bool encodeError = false;
    bool cancelled = false;

    while (totalRead < dataSize) {
        // Check cancellation
        if (cancelFlag && cancelFlag->load()) {
            cancelled = true;
            break;
        }

        qint64 toRead = qMin(static_cast<qint64>(pcmBufSize), dataSize - totalRead);
        qint64 bytesRead = wav.read(pcmBuf.data(), toRead);
        if (bytesRead <= 0) break;
        totalRead += bytesRead;

        int samplesRead = static_cast<int>(bytesRead / (channels * 2));
        int mp3Bytes = lame_encode_buffer_interleaved(
            gfp,
            reinterpret_cast<short int *>(pcmBuf.data()),
            samplesRead,
            reinterpret_cast<unsigned char *>(mp3Buf.data()),
            mp3BufSize);

        if (mp3Bytes > 0) {
            mp3.write(mp3Buf.data(), mp3Bytes);
        } else if (mp3Bytes < 0) {
            encodeError = true;
            break;
        }

        if (progress && dataSize > 0) {
            int pct = static_cast<int>(100 * totalRead / dataSize);
            if (pct != lastPercent) {
                lastPercent = pct;
                progress(pct);
            }
        }
    }

    // Flush remaining MP3 data
    if (!encodeError && !cancelled) {
        int flushBytes = lame_encode_flush(
            gfp,
            reinterpret_cast<unsigned char *>(mp3Buf.data()),
            mp3BufSize);
        if (flushBytes > 0) {
            mp3.write(mp3Buf.data(), flushBytes);
        }
    }

    lame_close(gfp);
    mp3.close();
    wav.close();

    if (encodeError || cancelled) {
        QFile::remove(mp3Path);
        return false;
    }

    if (progress) progress(100);
    return true;
}

#endif // FLUIDSYNTH_SUPPORT && LAME_SUPPORT

