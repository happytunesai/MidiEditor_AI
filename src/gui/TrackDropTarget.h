#ifndef TRACKDROPTARGET_H
#define TRACKDROPTARGET_H

/**
 * \file gui/TrackDropTarget.h
 *
 * \brief Where a track dragged in the track list ends up (pure, unit-tested).
 *
 * The list draws its drop indicator above or below the row under the cursor,
 * or - below the last row - on the empty viewport. The dragged track has to
 * land exactly there: taking the row under the cursor as the target put the
 * track one slot away from the line in half of the drops, and a drop right
 * below the first track did nothing (track-order review TR-02).
 */
namespace TrackDropTarget {

/** Where the list's drop indicator points (mirrors QAbstractItemView). */
enum Indicator { OnItem, AboveItem, BelowItem, OnViewport };

/**
 * \param from      current position of the dragged track
 * \param row       row under the cursor (ignored for OnViewport)
 * \param indicator where the drop indicator is drawn
 * \param count     number of tracks
 * \return the track's final position, or -1 when the drop moves nothing
 */
inline int finalIndex(int from, int row, Indicator indicator, int count) {
    if (count <= 0 || from < 0 || from >= count) {
        return -1;
    }
    int insertBefore; // gap in the list BEFORE the dragged track is taken out
    switch (indicator) {
    case OnViewport:
        insertBefore = count;
        break;
    case BelowItem:
        insertBefore = row + 1;
        break;
    case AboveItem:
    case OnItem:
    default:
        insertBefore = row;
        break;
    }
    if (insertBefore < 0 || insertBefore > count) {
        return -1;
    }
    // Taking the track out closes its own gap: a gap behind it moves up by one.
    const int target = (insertBefore > from) ? insertBefore - 1 : insertBefore;
    return (target == from) ? -1 : target;
}

} // namespace TrackDropTarget

#endif // TRACKDROPTARGET_H
