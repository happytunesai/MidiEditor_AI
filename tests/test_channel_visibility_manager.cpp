/*
 * test_channel_visibility_manager
 *
 * Unit tests for src/gui/ChannelVisibilityManager — the in-memory
 * corruption-proof channel visibility singleton.
 *
 * NOTE — divergence from the roadmap
 * ----------------------------------
 * Planning/06_TEST_CASES.md §2.7 originally suggested testing this module
 * via a "QSettings::IniFormat round-trip". The current production source
 * (src/gui/ChannelVisibilityManager.{h,cpp} as of v1.4.x) has *no* QSettings
 * persistence — state lives entirely in a `bool channelVisibility[19]`
 * array on the singleton and is reset on every process start. We therefore
 * test the actual public surface: defaults, the channel-16 inheritance
 * rule for indices > 16, bounds clamping, and resetAllVisible. Roadmap
 * entry updated accordingly.
 */

#include <QtTest/QtTest>
#include <QObject>

#include "../src/gui/ChannelVisibilityManager.h"

class TestChannelVisibilityManager : public QObject {
    Q_OBJECT

private slots:

    // Each test starts from a known all-visible state because the manager
    // is a process-wide singleton.
    void init() {
        ChannelVisibilityManager::instance().resetAllVisible();
    }

    // -----------------------------------------------------------------
    void instance_returnsSameSingletonOnRepeatedCalls() {
        ChannelVisibilityManager &a = ChannelVisibilityManager::instance();
        ChannelVisibilityManager &b = ChannelVisibilityManager::instance();
        QCOMPARE(&a, &b);
    }

    // -----------------------------------------------------------------
    void defaults_afterReset_allChannelsVisible() {
        ChannelVisibilityManager &m = ChannelVisibilityManager::instance();
        for (int ch = 0; ch < 19; ++ch) {
            QVERIFY2(m.isChannelVisible(ch),
                     qPrintable(QStringLiteral("Channel %1 should default to visible").arg(ch)));
        }
    }

    // -----------------------------------------------------------------
    void setChannelVisible_togglesIndividualStandardChannel() {
        ChannelVisibilityManager &m = ChannelVisibilityManager::instance();

        m.setChannelVisible(5, false);
        QCOMPARE(m.isChannelVisible(5), false);

        // Other channels in the standard range are unaffected.
        QCOMPARE(m.isChannelVisible(0), true);
        QCOMPARE(m.isChannelVisible(15), true);

        m.setChannelVisible(5, true);
        QCOMPARE(m.isChannelVisible(5), true);
    }

    // -----------------------------------------------------------------
    void isChannelVisible_outOfRangeIndex_returnsTrueByDefault() {
        ChannelVisibilityManager &m = ChannelVisibilityManager::instance();
        QCOMPARE(m.isChannelVisible(-1), true);
        QCOMPARE(m.isChannelVisible(-100), true);
        QCOMPARE(m.isChannelVisible(19), true);
        QCOMPARE(m.isChannelVisible(1000), true);
    }

    // -----------------------------------------------------------------
    void setChannelVisible_outOfRangeIndex_isIgnoredAndDoesNotCorruptNeighbours() {
        ChannelVisibilityManager &m = ChannelVisibilityManager::instance();

        m.setChannelVisible(-1, false);
        m.setChannelVisible(19, false);
        m.setChannelVisible(1000, false);

        // No in-range channel must have been flipped.
        for (int ch = 0; ch < 19; ++ch) {
            QVERIFY2(m.isChannelVisible(ch),
                     qPrintable(QStringLiteral("Channel %1 must stay visible after invalid sets").arg(ch)));
        }
    }

    // -----------------------------------------------------------------
    void isChannelVisible_indicesAbove16_inheritFromChannel16() {
        ChannelVisibilityManager &m = ChannelVisibilityManager::instance();

        // Hide the meta channel; channels 17 and 18 must follow.
        m.setChannelVisible(16, false);
        QCOMPARE(m.isChannelVisible(16), false);
        QCOMPARE(m.isChannelVisible(17), false);
        QCOMPARE(m.isChannelVisible(18), false);

        // Their own stored flags are not consulted while ch16 is hidden:
        // explicitly setting ch17 should NOT make it visible while 16 is off.
        m.setChannelVisible(17, true);
        QCOMPARE(m.isChannelVisible(17), false);

        // Re-enabling ch16 brings 17/18 back regardless of their stored flags.
        m.setChannelVisible(16, true);
        QCOMPARE(m.isChannelVisible(17), true);
        QCOMPARE(m.isChannelVisible(18), true);
    }

    // -----------------------------------------------------------------
    void isChannelVisible_standardChannelsBelow16_areIndependentOfChannel16() {
        ChannelVisibilityManager &m = ChannelVisibilityManager::instance();
        m.setChannelVisible(16, false);
        // Channels 0..15 must NOT be affected by ch16 hiding.
        for (int ch = 0; ch < 16; ++ch) {
            QVERIFY2(m.isChannelVisible(ch),
                     qPrintable(QStringLiteral("Channel %1 must stay visible when ch16 is hidden").arg(ch)));
        }
    }

    // -----------------------------------------------------------------
    void resetAllVisible_afterPartialMutation_restoresFullVisibility() {
        ChannelVisibilityManager &m = ChannelVisibilityManager::instance();

        m.setChannelVisible(0, false);
        m.setChannelVisible(7, false);
        m.setChannelVisible(15, false);
        m.setChannelVisible(16, false);
        QCOMPARE(m.isChannelVisible(0), false);
        QCOMPARE(m.isChannelVisible(7), false);
        QCOMPARE(m.isChannelVisible(15), false);
        QCOMPARE(m.isChannelVisible(16), false);

        m.resetAllVisible();
        for (int ch = 0; ch < 19; ++ch) {
            QVERIFY2(m.isChannelVisible(ch),
                     qPrintable(QStringLiteral("Channel %1 must be visible after reset").arg(ch)));
        }
    }

    // -----------------------------------------------------------------
    void singletonState_persistsBetweenAccessesWithinTheSameTest() {
        // Sanity: the singleton is not silently re-initialised on each
        // instance() call (that would defeat the purpose of the manager).
        ChannelVisibilityManager::instance().setChannelVisible(3, false);
        QCOMPARE(ChannelVisibilityManager::instance().isChannelVisible(3), false);
    }
};

QTEST_APPLESS_MAIN(TestChannelVisibilityManager)
#include "test_channel_visibility_manager.moc"
