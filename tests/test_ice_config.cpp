// Unit tests for IceConfig — STUN/TURN URI list I/O via QSettings (Plan
// §11.10, Phase 9.6).
//
// Pure Qt - no libdatachannel symbols referenced from IceConfig.cpp, so
// the test executable compiles only the single .cpp with the
// MIDIEDITOR_WEBRTC_ENABLED define (since the IceConfig header gates its
// declarations on that flag).
//
// Sandboxes QSettings via QStandardPaths::setTestModeEnabled(true) so the
// developer's real Collab/lan/iceServers key is never touched.
//
// Run from a build configured with -DBUILD_TESTING=ON:
//     ctest --test-dir build -V -R IceConfig

#include "IceConfig.h"

#include <QSettings>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QtTest/QtTest>

class TestIceConfig : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanupTestCase();

    void googleDefaults_isNonEmptyStunList();
    void googleDefaults_coversMultiplePorts();
    void load_unsetReturnsDefaults();
    void load_emptyValueReturnsDefaults();
    void load_whitespaceOnlyValueReturnsDefaults();
    void save_persistsViaQSettings();
    void roundTrip_singleEntry();
    void roundTrip_multiEntry();
    void load_skipsBlankLinesAndComments();
    void load_returnsDefaultsWhenAllLinesAreCommentsOrBlank();
    void save_emptyListEffectivelyRevertsToDefaults();

private:
    static void wipeIceKey() {
        QSettings s(QStringLiteral("MidiEditor"), QStringLiteral("NONE"));
        s.remove(QStringLiteral("Collab/lan/iceServers"));
        s.sync();
    }
};

void TestIceConfig::initTestCase() {
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("MidiEditor"));
    QCoreApplication::setApplicationName(QStringLiteral("NONE"));
}

void TestIceConfig::init() {
    wipeIceKey();
}

void TestIceConfig::cleanupTestCase() {
    wipeIceKey();
}

// ---------------------------------------------------------------------
// googleDefaults
// ---------------------------------------------------------------------

void TestIceConfig::googleDefaults_isNonEmptyStunList() {
    QStringList defs = IceConfig::googleDefaults();
    QVERIFY(!defs.isEmpty());
    for (const QString &uri : defs) {
        QVERIFY2(uri.startsWith(QStringLiteral("stun:")),
                 qPrintable(uri));
    }
}

// Defaults cover both 3478/19302 and 5349 ports per §11.10 so at least
// one endpoint is reachable from networks blocking specific ports.
void TestIceConfig::googleDefaults_coversMultiplePorts() {
    QStringList defs = IceConfig::googleDefaults();
    bool has5349 = false, hasOther = false;
    for (const QString &uri : defs) {
        if (uri.endsWith(QStringLiteral(":5349"))) has5349 = true;
        else if (uri.contains(QStringLiteral(":19302")) ||
                 uri.contains(QStringLiteral(":3478")))
            hasOther = true;
    }
    QVERIFY2(has5349, "Defaults must include :5349 entries");
    QVERIFY2(hasOther, "Defaults must include :3478 or :19302 entries");
}

// ---------------------------------------------------------------------
// load()
// ---------------------------------------------------------------------

void TestIceConfig::load_unsetReturnsDefaults() {
    QStringList got = IceConfig::load();
    QCOMPARE(got, IceConfig::googleDefaults());
}

void TestIceConfig::load_emptyValueReturnsDefaults() {
    QSettings s(QStringLiteral("MidiEditor"), QStringLiteral("NONE"));
    s.setValue(QStringLiteral("Collab/lan/iceServers"), QString());
    s.sync();
    QCOMPARE(IceConfig::load(), IceConfig::googleDefaults());
}

void TestIceConfig::load_whitespaceOnlyValueReturnsDefaults() {
    QSettings s(QStringLiteral("MidiEditor"), QStringLiteral("NONE"));
    s.setValue(QStringLiteral("Collab/lan/iceServers"),
               QStringLiteral("   \n\t  \n  "));
    s.sync();
    QCOMPARE(IceConfig::load(), IceConfig::googleDefaults());
}

// ---------------------------------------------------------------------
// save()
// ---------------------------------------------------------------------

void TestIceConfig::save_persistsViaQSettings() {
    IceConfig::save({QStringLiteral("stun:my.stun.example:3478"),
                     QStringLiteral("turn:user:pw@turn.example:5349")});
    QSettings s(QStringLiteral("MidiEditor"), QStringLiteral("NONE"));
    QString blob = s.value(QStringLiteral("Collab/lan/iceServers")).toString();
    QVERIFY(blob.contains(QStringLiteral("stun:my.stun.example:3478")));
    QVERIFY(blob.contains(QStringLiteral("turn:user:pw@turn.example:5349")));
}

// ---------------------------------------------------------------------
// Round-trip save() -> load()
// ---------------------------------------------------------------------

void TestIceConfig::roundTrip_singleEntry() {
    QStringList in = {QStringLiteral("stun:custom.example:3478")};
    IceConfig::save(in);
    QCOMPARE(IceConfig::load(), in);
}

void TestIceConfig::roundTrip_multiEntry() {
    QStringList in = {QStringLiteral("stun:a.example:3478"),
                      QStringLiteral("stun:b.example:5349"),
                      QStringLiteral("turn:u:p@c.example:3478")};
    IceConfig::save(in);
    QCOMPARE(IceConfig::load(), in);
}

// ---------------------------------------------------------------------
// load() text parsing
// ---------------------------------------------------------------------

// Blank lines and lines starting with '#' are dropped; payload lines are
// preserved verbatim in order.
void TestIceConfig::load_skipsBlankLinesAndComments() {
    QSettings s(QStringLiteral("MidiEditor"), QStringLiteral("NONE"));
    QString blob =
        QStringLiteral("# top-of-file comment\n"
                       "stun:a.example:3478\n"
                       "\n"
                       "   \n"
                       "  # indented comment\n"
                       "stun:b.example:5349\n"
                       "# trailing comment\n");
    s.setValue(QStringLiteral("Collab/lan/iceServers"), blob);
    s.sync();

    QStringList got = IceConfig::load();
    QStringList expected = {QStringLiteral("stun:a.example:3478"),
                            QStringLiteral("stun:b.example:5349")};
    QCOMPARE(got, expected);
}

void TestIceConfig::load_returnsDefaultsWhenAllLinesAreCommentsOrBlank() {
    QSettings s(QStringLiteral("MidiEditor"), QStringLiteral("NONE"));
    s.setValue(QStringLiteral("Collab/lan/iceServers"),
               QStringLiteral("# everything is a comment\n"
                              "\n"
                              "   \n"
                              "# another\n"));
    s.sync();
    QCOMPARE(IceConfig::load(), IceConfig::googleDefaults());
}

// Documented behaviour: saving an empty list writes an empty blob, which
// load() then falls back from to googleDefaults().
void TestIceConfig::save_emptyListEffectivelyRevertsToDefaults() {
    IceConfig::save({QStringLiteral("stun:tmp.example:3478")});
    QCOMPARE(IceConfig::load(),
             QStringList{QStringLiteral("stun:tmp.example:3478")});
    IceConfig::save(QStringList());
    QCOMPARE(IceConfig::load(), IceConfig::googleDefaults());
}

QTEST_APPLESS_MAIN(TestIceConfig)
#include "test_ice_config.moc"
