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

#ifndef SNAPMATH_H_
#define SNAPMATH_H_

#include <QList>
#include <QPair>
#include <cstdlib>

/**
 * \namespace SnapMath
 *
 * \brief Pure grid-snap math, kept free of GUI/MIDI dependencies so it can be
 * unit-tested in isolation (header-only). Used by EventTool::rasteredX() for
 * the Magnet tool's two snap behaviours.
 */
namespace SnapMath {

/** Default Legacy "magnetic pull" radius in pixels. */
constexpr int kLegacyThresholdPx = 5;

/**
 * \brief Decide whether a pixel X snaps to a grid division.
 * \param x Pixel X to snap.
 * \param divs Visible grid divisions as (pixelX, tick) pairs.
 * \param modern True = snap to the nearest division unconditionally (hard grid,
 *        DAW-style); false = only snap within \a legacyThresholdPx of a division
 *        (the historical magnetic-pull behaviour).
 * \param snappedX Out (optional): the snapped pixel X, set only when returning true.
 * \param tick Out (optional): the snapped division's tick, set only when true.
 * \param legacyThresholdPx Legacy pull radius in pixels.
 * \return True if x snapped to a division; false if there is no snap
 *         (Legacy with nothing within the radius, or an empty division list).
 */
inline bool snapToDiv(int x, const QList<QPair<int, int>> &divs, bool modern,
                      int *snappedX, int *tick,
                      int legacyThresholdPx = kLegacyThresholdPx) {
    int bestX = 0;
    int bestTick = 0;
    int bestDist = -1;
    for (const QPair<int, int> &p : divs) {
        int dist = std::abs(p.first - x);
        if (bestDist < 0 || dist < bestDist) {
            bestDist = dist;
            bestX = p.first;
            bestTick = p.second;
        }
    }
    if (bestDist < 0)
        return false; // no divisions to snap to
    if (!modern && bestDist > legacyThresholdPx)
        return false; // Legacy: nothing close enough
    if (snappedX) *snappedX = bestX;
    if (tick) *tick = bestTick;
    return true;
}

} // namespace SnapMath

#endif // SNAPMATH_H_
