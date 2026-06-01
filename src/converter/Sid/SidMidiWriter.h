/*
 * MidiEditor AI - SID notes -> Standard MIDI File bytes (Phase 42.1).
 *
 * Turns the reconstructed SID notes into a format-1 SMF byte stream that the
 * editor's MidiFile loader can parse. One conductor track (tempo / name /
 * time signature) + three voice tracks (one per SID voice, on MIDI channels
 * 0-2). Frame-accurate timing is preserved: PPQ 600 @ 120 BPM gives an
 * integer ticks-per-frame for both PAL (24) and NTSC (20), so playback runs
 * at the original real-time speed.
 */

#ifndef SID_SIDMIDIWRITER_H
#define SID_SIDMIDIWRITER_H

#include <vector>

#include <QByteArray>
#include <QString>

#include "SidCapture.h"
#include "SidReconstruct.h"

namespace sid {

/// Encode \a notes (with timing/clock from \a cap) into format-1 SMF bytes.
/// Returns an empty array when there is nothing to write.
QByteArray writeSidNotesToSmf(const std::vector<SidNote> &notes,
                              const CaptureResult &cap,
                              const QString &title);

} // namespace sid

#endif // SID_SIDMIDIWRITER_H
