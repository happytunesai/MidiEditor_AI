#ifndef TRACKPOSITIONFOLLOW_H
#define TRACKPOSITIONFOLLOW_H

/**
 * \file gui/TrackPositionFollow.h
 *
 * \brief Keeps a track chosen by position on its track when the order changes
 *        (pure, unit-tested).
 *
 * The edit track ("Add new events to") and the paste target are stored as
 * positions. After a reorder - drag, Move Up/Down, Clone, the split tools, a
 * MidiPilot/MCP track change, their undo - a position has to follow its
 * TRACK, or new notes go into whatever track moved into the old place
 * (track-order review TR-01). The track list as it was when the position was
 * chosen tells which track that was, whoever set the position.
 */
#include <QList>

namespace TrackPositionFollow {

/**
 * \param before   the track list the position was chosen in
 * \param after    the track list now
 * \param position the chosen position (negative values are modes, not tracks)
 * \return the new position of the same track; \a position unchanged when that
 *         track is gone or \a before belongs to another document
 */
template <typename T>
int follow(const QList<T *> &before, const QList<T *> &after, int position) {
    if (position < 0 || position >= before.size()) {
        return position;
    }
    const int now = after.indexOf(before.at(position));
    return now >= 0 ? now : position;
}

} // namespace TrackPositionFollow

#endif // TRACKPOSITIONFOLLOW_H
