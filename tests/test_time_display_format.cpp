/*
 * MidiEditor AI - Unit test for the TimeDisplayWidget format/mode helpers
 * (Phase 41).
 *
 * Tests the pure logic in src/gui/TimeDisplayFormat.h:
 *   - formatClock   (adaptive MM:SS / H:MM:SS, negative clamp)
 *   - formatRemaining (leading '-', never negative)
 *   - formatBar     (bar.beat num/den shape)
 *   - modeTag / nextMode / modeFromInt (cycle + persistence helpers)
 *
 * Header-only module -> the executable links nothing but Qt::Core/Test.
 */

#include <QString>
#include <QtTest/QtTest>

#include "../src/gui/TimeDisplayFormat.h"

using namespace TimeDisplay;

class TimeDisplayFormatTest : public QObject {
    Q_OBJECT
private slots:
    // ---- formatClock: adaptive width --------------------------------

    void clock_zero() {
        QCOMPARE(formatClock(0), QStringLiteral("00:00"));
    }

    void clock_subMinute() {
        QCOMPARE(formatClock(59 * 1000), QStringLiteral("00:59"));
    }

    void clock_minuteBoundary() {
        QCOMPARE(formatClock(60 * 1000), QStringLiteral("01:00"));
        QCOMPARE(formatClock(90 * 1000), QStringLiteral("01:30"));
    }

    void clock_subHour_staysMMSS() {
        // 59:59 - still under an hour, so no hours field.
        QCOMPARE(formatClock(3599 * 1000), QStringLiteral("59:59"));
    }

    void clock_hourWidensFormat() {
        QCOMPARE(formatClock(3600 * 1000), QStringLiteral("1:00:00"));
        QCOMPARE(formatClock(3661 * 1000), QStringLiteral("1:01:01"));
    }

    void clock_negativeClampsToZero() {
        QCOMPARE(formatClock(-5000), QStringLiteral("00:00"));
    }

    void clock_truncatesSubSecond() {
        // 1500 ms -> 1 s (integer division, no rounding up).
        QCOMPARE(formatClock(1500), QStringLiteral("00:01"));
    }

    // ---- formatRemaining --------------------------------------------

    void remaining_basic() {
        // length 3:20, pos 1:20 -> 2:00 left.
        QCOMPARE(formatRemaining(200 * 1000, 80 * 1000),
                 QStringLiteral("-02:00"));
    }

    void remaining_atEndIsZero() {
        QCOMPARE(formatRemaining(120 * 1000, 120 * 1000),
                 QStringLiteral("-00:00"));
    }

    void remaining_pastEndClampsToZero() {
        // Player overran the cached length - never show a positive count-up.
        QCOMPARE(formatRemaining(120 * 1000, 130 * 1000),
                 QStringLiteral("-00:00"));
    }

    // ---- formatBar ---------------------------------------------------

    void bar_shape() {
        QCOMPARE(formatBar(12, 3, 4, 4), QStringLiteral("12.3 4/4"));
        QCOMPARE(formatBar(1, 1, 6, 8), QStringLiteral("1.1 6/8"));
    }

    // ---- mode helpers ------------------------------------------------

    void modeTag_all() {
        QCOMPARE(QString::fromLatin1(modeTag(Mode::Position)), QStringLiteral("POS"));
        QCOMPARE(QString::fromLatin1(modeTag(Mode::Length)),   QStringLiteral("LEN"));
        QCOMPARE(QString::fromLatin1(modeTag(Mode::Remaining)),QStringLiteral("REM"));
        QCOMPARE(QString::fromLatin1(modeTag(Mode::Bpm)),      QStringLiteral("BPM"));
        QCOMPARE(QString::fromLatin1(modeTag(Mode::Bar)),      QStringLiteral("BAR"));
    }

    void nextMode_cyclesAndWraps() {
        QCOMPARE(nextMode(Mode::Position),  Mode::Length);
        QCOMPARE(nextMode(Mode::Length),    Mode::Remaining);
        QCOMPARE(nextMode(Mode::Remaining), Mode::Bpm);
        QCOMPARE(nextMode(Mode::Bpm),       Mode::Bar);
        QCOMPARE(nextMode(Mode::Bar),       Mode::Position); // wrap
    }

    void modeFromInt_clampsAndMaps() {
        QCOMPARE(modeFromInt(0), Mode::Position);
        QCOMPARE(modeFromInt(4), Mode::Bar);
        QCOMPARE(modeFromInt(-1), Mode::Position);  // out of range -> default
        QCOMPARE(modeFromInt(99), Mode::Position);  // out of range -> default
    }
};

QTEST_MAIN(TimeDisplayFormatTest)
#include "test_time_display_format.moc"
