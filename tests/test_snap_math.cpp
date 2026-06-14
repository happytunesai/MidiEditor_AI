/*
 * test_snap_math
 *
 * Unit tests for SnapMath::snapToDiv() — the pure grid-snap math behind the
 * Magnet tool's Modern (hard snap to nearest grid line, DAW-style) vs Legacy
 * (magnetic pull within a few pixels) behaviours. Header-only, no GUI/MIDI deps.
 *
 * The same function drives drawing (NewNoteTool), moving (EventMoveTool) and
 * resizing (SizeChangeTool), since all go through EventTool::rasteredX().
 */

#include <QtTest/QtTest>
#include <QList>
#include <QPair>

#include "../src/tool/SnapMath.h"

class TestSnapMath : public QObject {
    Q_OBJECT

private:
    // Grid lines at pixels 100 / 200 / 300 mapping to ticks 0 / 480 / 960.
    static QList<QPair<int, int>> grid() {
        return {{100, 0}, {200, 480}, {300, 960}};
    }

private slots:

    // Modern snaps to the nearest division even when the pointer is far from it.
    void modern_snapsToNearest() {
        int sx = -1, tick = -1;
        QVERIFY(SnapMath::snapToDiv(170, grid(), /*modern*/ true, &sx, &tick));
        QCOMPARE(sx, 200);
        QCOMPARE(tick, 480);
    }

    void modern_snapsToLowerNeighbour() {
        int sx = -1, tick = -1;
        QVERIFY(SnapMath::snapToDiv(130, grid(), true, &sx, &tick));
        QCOMPARE(sx, 100);
        QCOMPARE(tick, 0);
    }

    // The core of the fix: between grid lines, Modern snaps but Legacy does not.
    void modern_snapsWhereLegacyWouldNot() {
        int sxM = -1, sxL = -1;
        QVERIFY(SnapMath::snapToDiv(160, grid(), true, &sxM, nullptr));   // modern: snaps
        QCOMPARE(sxM, 200);
        QVERIFY(!SnapMath::snapToDiv(160, grid(), false, &sxL, nullptr)); // legacy: no snap
    }

    // Legacy snaps only within the pull radius (default 5px).
    void legacy_snapsWithinThreshold() {
        int sx = -1, tick = -1;
        QVERIFY(SnapMath::snapToDiv(103, grid(), false, &sx, &tick));
        QCOMPARE(sx, 100);
        QCOMPARE(tick, 0);
    }

    void legacy_noSnapBeyondThreshold() {
        int sx = -1, tick = -1;
        QVERIFY(!SnapMath::snapToDiv(150, grid(), false, &sx, &tick));
    }

    // Threshold boundary is inclusive (dist == radius still snaps).
    void legacy_thresholdBoundaryInclusive() {
        QVERIFY(SnapMath::snapToDiv(105, grid(), false, nullptr, nullptr));   // dist 5 -> snap
        QVERIFY(!SnapMath::snapToDiv(106, grid(), false, nullptr, nullptr));  // dist 6 -> no snap
    }

    // Custom legacy threshold is honoured.
    void legacy_customThreshold() {
        QVERIFY(SnapMath::snapToDiv(112, grid(), false, nullptr, nullptr, /*threshold*/ 15)); // dist 12 <= 15
        QVERIFY(!SnapMath::snapToDiv(112, grid(), false, nullptr, nullptr, /*threshold*/ 5)); // dist 12 > 5
    }

    // Empty division list never snaps, in either mode.
    void emptyDivs_neverSnaps() {
        QList<QPair<int, int>> none;
        QVERIFY(!SnapMath::snapToDiv(170, none, true, nullptr, nullptr));
        QVERIFY(!SnapMath::snapToDiv(170, none, false, nullptr, nullptr));
    }

    // Exact hit snaps in both modes.
    void exactHit_snapsInBothModes() {
        int sxM = -1, sxL = -1;
        QVERIFY(SnapMath::snapToDiv(200, grid(), true, &sxM, nullptr));
        QVERIFY(SnapMath::snapToDiv(200, grid(), false, &sxL, nullptr));
        QCOMPARE(sxM, 200);
        QCOMPARE(sxL, 200);
    }

    // Null out-pointers are allowed (just a yes/no query).
    void nullOutPointers_ok() {
        QVERIFY(SnapMath::snapToDiv(170, grid(), true, nullptr, nullptr));
    }
};

QTEST_APPLESS_MAIN(TestSnapMath)
#include "test_snap_math.moc"
