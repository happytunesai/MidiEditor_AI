/*
 * MidiEditor AI - TimeDisplayFormat (Phase 41).
 *
 * Pure formatting + mode helpers for the retro cursor-time display
 * (TimeDisplayWidget). Header-only and free of any QWidget / MidiFile
 * dependency so the logic is unit-testable against Qt6::Core/Test alone
 * - same split rationale as src/collab/SessionMode.h.
 */

#ifndef TIMEDISPLAYFORMAT_H
#define TIMEDISPLAYFORMAT_H

#include <QChar>
#include <QLatin1Char>
#include <QString>
#include <QStringLiteral>

namespace TimeDisplay {

/// The five readouts cycled by a single click, in cycle order.
enum class Mode {
    Position = 0,  ///< cursor time (idle) / player time (playing)
    Length,        ///< total song duration
    Remaining,     ///< length - position, shown with a leading '-'
    Bpm,           ///< tempo at the current position
    Bar,           ///< musical position bar.beat + time signature
};

constexpr int kModeCount = 5;

/// Short tag rendered beside the digits so the active mode is unambiguous.
inline const char *modeTag(Mode m) {
    switch (m) {
    case Mode::Position:  return "POS";
    case Mode::Length:    return "LEN";
    case Mode::Remaining: return "REM";
    case Mode::Bpm:       return "BPM";
    case Mode::Bar:       return "BAR";
    }
    return "POS";
}

/// Advance to the next readout, wrapping around (the click behaviour).
inline Mode nextMode(Mode m) {
    return static_cast<Mode>((static_cast<int>(m) + 1) % kModeCount);
}

/// Clamp a persisted/int mode value back into the valid range.
inline Mode modeFromInt(int v) {
    if (v < 0 || v >= kModeCount) return Mode::Position;
    return static_cast<Mode>(v);
}

/// Adaptive wall-clock string: `MM:SS` below an hour, widening to
/// `H:MM:SS` only once the time reaches an hour - most songs are short,
/// so a 90 s piece reads `01:30`, not `00:01:30`. Negative input clamps
/// to zero.
inline QString formatClock(int ms) {
    if (ms < 0) ms = 0;
    const int totalSec = ms / 1000;
    const int h = totalSec / 3600;
    const int m = (totalSec % 3600) / 60;
    const int s = totalSec % 60;
    if (h > 0) {
        return QStringLiteral("%1:%2:%3")
            .arg(h)
            .arg(m, 2, 10, QLatin1Char('0'))
            .arg(s, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2")
        .arg(m, 2, 10, QLatin1Char('0'))
        .arg(s, 2, 10, QLatin1Char('0'));
}

/// Remaining time = length - position, never negative, prefixed with '-'
/// so it reads like a media player's countdown (e.g. `-01:34`).
inline QString formatRemaining(int lengthMs, int positionMs) {
    int rem = lengthMs - positionMs;
    if (rem < 0) rem = 0;
    return QStringLiteral("-") + formatClock(rem);
}

/// Musical-position string for the BAR readout: `bar.beat num/den`
/// (e.g. `12.3 4/4`). Bars and beats are 1-based for display.
inline QString formatBar(int bar, int beat, int num, int den) {
    return QStringLiteral("%1.%2 %3/%4").arg(bar).arg(beat).arg(num).arg(den);
}

} // namespace TimeDisplay

#endif // TIMEDISPLAYFORMAT_H
