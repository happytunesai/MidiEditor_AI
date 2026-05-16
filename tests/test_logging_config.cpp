// Unit tests for LoggingConfig — coarse log-level dropdown + per-category
// overrides + collab-verbose flag, persisted via QSettings (Phase 9.6f).
//
// LoggingConfig is pure Qt (QLoggingCategory + QSettings) with no MidiFile /
// MidiEvent / network deps. The test sandboxes QSettings via
// QStandardPaths::setTestModeEnabled(true) so the developer's real
// Logging/* and Collab/verboseLogging keys are never touched.
//
// Run from a build configured with -DBUILD_TESTING=ON:
//     ctest --test-dir build -V -R LoggingConfig

#include "LoggingConfig.h"

#include <QCoreApplication>
#include <QSettings>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QtTest/QtTest>

class TestLoggingConfig : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();         // wipes Logging/* + Collab/verboseLogging before each
    void cleanupTestCase();

    // buildFilterRules — coarse level mapping
    void buildFilterRules_offSilencesEverySeverity();
    void buildFilterRules_errorsSilencesDebugInfoWarning();
    void buildFilterRules_warningsSilencesOnlyDebugAndInfo();
    void buildFilterRules_infoSilencesOnlyDebug();
    void buildFilterRules_debugEnablesEverything();

    // buildFilterRules — overrides + ordering
    void buildFilterRules_perCategoryAppendedAfterGlobalSoItOverrides();
    void buildFilterRules_emptyOverridesAreNotAppendedAsBlankLines();
    void buildFilterRules_whitespaceOnlyOverridesAreTreatedAsEmpty();
    void buildFilterRules_collabVerboseAppendedLastOnlyWhenFlagSet();

    // loadLevel — boundary + corruption guards
    void loadLevel_defaultsToWarningsWhenUnset();
    void loadLevel_outOfRangeValuesClampedToDefault();
    void loadLevel_roundTripsValidValues();

    // loadPerCategory
    void loadPerCategory_defaultsToEmptyString();

    // applyAndPersist
    void applyAndPersist_writesLevelAndPerCategory();
    void applyAndPersist_emptyPerCategoryRemovesTheKey();
    void applyAndPersist_whitespacePerCategoryRemovesTheKey();

    // setCollabVerbose
    void setCollabVerbose_persistsTrueAndClearsOnFalse();
    void loadCollabVerbose_defaultsToFalse();

private:
    static void wipeKeys() {
        QSettings s(QStringLiteral("MidiEditor"), QStringLiteral("NONE"));
        s.remove(QStringLiteral("Logging/level"));
        s.remove(QStringLiteral("Logging/perCategory"));
        s.remove(QStringLiteral("Collab/verboseLogging"));
        s.sync();
    }
};

void TestLoggingConfig::initTestCase() {
    // Sandbox QSettings so the developer's real config isn't touched.
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("MidiEditor"));
    QCoreApplication::setApplicationName(QStringLiteral("NONE"));
}

void TestLoggingConfig::init() {
    wipeKeys();
}

void TestLoggingConfig::cleanupTestCase() {
    wipeKeys();
}

// ---------------------------------------------------------------------
// buildFilterRules — coarse level mapping
// ---------------------------------------------------------------------

// Off: every severity globally suppressed (debug + info + warning + critical).
void TestLoggingConfig::buildFilterRules_offSilencesEverySeverity() {
    QString rules = LoggingConfig::buildFilterRules(LoggingConfig::Level::Off, QString());
    QVERIFY(rules.contains(QStringLiteral("*.debug=false")));
    QVERIFY(rules.contains(QStringLiteral("*.info=false")));
    QVERIFY(rules.contains(QStringLiteral("*.warning=false")));
    QVERIFY(rules.contains(QStringLiteral("*.critical=false")));
}

// Errors: debug/info/warning suppressed; critical remains at the Qt default.
void TestLoggingConfig::buildFilterRules_errorsSilencesDebugInfoWarning() {
    QString rules = LoggingConfig::buildFilterRules(LoggingConfig::Level::Errors, QString());
    QVERIFY(rules.contains(QStringLiteral("*.debug=false")));
    QVERIFY(rules.contains(QStringLiteral("*.info=false")));
    QVERIFY(rules.contains(QStringLiteral("*.warning=false")));
    QVERIFY(!rules.contains(QStringLiteral("*.critical=false")));
}

// Warnings (the Qt default): only debug + info suppressed.
void TestLoggingConfig::buildFilterRules_warningsSilencesOnlyDebugAndInfo() {
    QString rules = LoggingConfig::buildFilterRules(LoggingConfig::Level::Warnings, QString());
    QVERIFY(rules.contains(QStringLiteral("*.debug=false")));
    QVERIFY(rules.contains(QStringLiteral("*.info=false")));
    QVERIFY(!rules.contains(QStringLiteral("*.warning=false")));
}

// Info: only debug suppressed (info / warning / critical all show).
void TestLoggingConfig::buildFilterRules_infoSilencesOnlyDebug() {
    QString rules = LoggingConfig::buildFilterRules(LoggingConfig::Level::Info, QString());
    QVERIFY(rules.contains(QStringLiteral("*.debug=false")));
    QVERIFY(!rules.contains(QStringLiteral("*.info=false")));
    QVERIFY(!rules.contains(QStringLiteral("*.warning=false")));
}

// Debug: emits the explicit "*=true" enable-all rule (every category, every
// severity). This is the only level that uses positive enabling rather than
// per-severity disables.
void TestLoggingConfig::buildFilterRules_debugEnablesEverything() {
    QString rules = LoggingConfig::buildFilterRules(LoggingConfig::Level::Debug, QString());
    QVERIFY(rules.contains(QStringLiteral("*=true")));
    QVERIFY(!rules.contains(QStringLiteral("*.debug=false")));
}

// ---------------------------------------------------------------------
// buildFilterRules — per-category overrides + ordering
// ---------------------------------------------------------------------

// Per-category overrides must be appended AFTER the global level rules so
// that Qt's last-wins evaluation lets a user re-enable a specific category
// even when the global level is Warnings/Errors. Verified by line position.
void TestLoggingConfig::buildFilterRules_perCategoryAppendedAfterGlobalSoItOverrides() {
    QString rules = LoggingConfig::buildFilterRules(
        LoggingConfig::Level::Warnings,
        QStringLiteral("midieditor.collab.lan.debug=true"));
    QStringList lines = rules.split(QChar('\n'));
    int globalIdx = -1;
    int overrideIdx = -1;
    for (int i = 0; i < lines.size(); ++i) {
        if (lines.at(i) == QStringLiteral("*.debug=false")) globalIdx = i;
        if (lines.at(i).contains(QStringLiteral("midieditor.collab.lan.debug=true")))
            overrideIdx = i;
    }
    QVERIFY2(globalIdx >= 0 && overrideIdx >= 0,
             qPrintable(QStringLiteral("rules=\n%1").arg(rules)));
    QVERIFY2(overrideIdx > globalIdx,
             qPrintable(QStringLiteral("override must come after global; got %1 < %2")
                            .arg(overrideIdx).arg(globalIdx)));
}

// Empty override string must NOT introduce a stray blank line. Important
// because QLoggingCategory tolerates blank lines but other downstream
// consumers (e.g. settings UI preview) split on \n and would show empties.
void TestLoggingConfig::buildFilterRules_emptyOverridesAreNotAppendedAsBlankLines() {
    QString rules = LoggingConfig::buildFilterRules(LoggingConfig::Level::Warnings, QString());
    QStringList lines = rules.split(QChar('\n'));
    for (const QString &line : lines) {
        QVERIFY2(!line.isEmpty(),
                 qPrintable(QStringLiteral("unexpected empty line in rules=\n%1").arg(rules)));
    }
}

// Whitespace-only override (e.g. user typed nothing but a few spaces) must
// not be appended to the rules either.
void TestLoggingConfig::buildFilterRules_whitespaceOnlyOverridesAreTreatedAsEmpty() {
    QString rules = LoggingConfig::buildFilterRules(LoggingConfig::Level::Warnings,
                                                    QStringLiteral("   \n  \t"));
    // The override should NOT appear (trimmed view is empty)
    QVERIFY(!rules.contains(QStringLiteral("   \n  \t")));
    // And the rule set still has the expected global lines
    QVERIFY(rules.contains(QStringLiteral("*.debug=false")));
    QVERIFY(rules.contains(QStringLiteral("*.info=false")));
}

// Collab-verbose flag layered last so it overrides global level for collab
// categories specifically. Off by default; flips on when the flag is true.
void TestLoggingConfig::buildFilterRules_collabVerboseAppendedLastOnlyWhenFlagSet() {
    QString offRules = LoggingConfig::buildFilterRules(LoggingConfig::Level::Warnings, QString());
    QVERIFY(!offRules.contains(QStringLiteral("midieditor.collab.*=true")));

    LoggingConfig::setCollabVerbose(true);
    QString onRules = LoggingConfig::buildFilterRules(LoggingConfig::Level::Warnings, QString());
    QVERIFY(onRules.contains(QStringLiteral("midieditor.collab.*=true")));

    // And the collab-verbose rule must be the LAST line (last-wins ordering)
    QStringList lines = onRules.split(QChar('\n'));
    QVERIFY(!lines.isEmpty());
    QCOMPARE(lines.last(), QStringLiteral("midieditor.collab.*=true"));

    // Cleanup so subsequent tests aren't perturbed
    LoggingConfig::setCollabVerbose(false);
    QString afterRules = LoggingConfig::buildFilterRules(LoggingConfig::Level::Warnings, QString());
    QVERIFY(!afterRules.contains(QStringLiteral("midieditor.collab.*=true")));
}

// ---------------------------------------------------------------------
// loadLevel — boundary + corruption guards
// ---------------------------------------------------------------------

void TestLoggingConfig::loadLevel_defaultsToWarningsWhenUnset() {
    QCOMPARE(static_cast<int>(LoggingConfig::loadLevel()),
             static_cast<int>(LoggingConfig::Level::Warnings));
}

// Out-of-range int in the QSettings file (e.g. user edited the .ini or a
// version downgrade left a higher value) must NOT crash or honour the
// invalid value — clamp to Warnings instead.
void TestLoggingConfig::loadLevel_outOfRangeValuesClampedToDefault() {
    QSettings s(QStringLiteral("MidiEditor"), QStringLiteral("NONE"));
    s.setValue(QStringLiteral("Logging/level"), 99);
    s.sync();
    QCOMPARE(static_cast<int>(LoggingConfig::loadLevel()),
             static_cast<int>(LoggingConfig::Level::Warnings));

    s.setValue(QStringLiteral("Logging/level"), -5);
    s.sync();
    QCOMPARE(static_cast<int>(LoggingConfig::loadLevel()),
             static_cast<int>(LoggingConfig::Level::Warnings));
}

void TestLoggingConfig::loadLevel_roundTripsValidValues() {
    for (int i = 0; i <= 4; ++i) {
        LoggingConfig::applyAndPersist(static_cast<LoggingConfig::Level>(i), QString());
        QCOMPARE(static_cast<int>(LoggingConfig::loadLevel()), i);
    }
}

// ---------------------------------------------------------------------
// loadPerCategory
// ---------------------------------------------------------------------

void TestLoggingConfig::loadPerCategory_defaultsToEmptyString() {
    QCOMPARE(LoggingConfig::loadPerCategory(), QString());
}

// ---------------------------------------------------------------------
// applyAndPersist
// ---------------------------------------------------------------------

void TestLoggingConfig::applyAndPersist_writesLevelAndPerCategory() {
    LoggingConfig::applyAndPersist(LoggingConfig::Level::Info,
                                   QStringLiteral("midieditor.collab.lan.debug=true"));
    QCOMPARE(static_cast<int>(LoggingConfig::loadLevel()),
             static_cast<int>(LoggingConfig::Level::Info));
    QCOMPARE(LoggingConfig::loadPerCategory(),
             QStringLiteral("midieditor.collab.lan.debug=true"));
}

// Empty per-category string removes the key so later loadPerCategory()
// returns the documented default (empty) — not a stale value.
void TestLoggingConfig::applyAndPersist_emptyPerCategoryRemovesTheKey() {
    LoggingConfig::applyAndPersist(LoggingConfig::Level::Info,
                                   QStringLiteral("midieditor.collab.lan.debug=true"));
    QVERIFY(!LoggingConfig::loadPerCategory().isEmpty());
    LoggingConfig::applyAndPersist(LoggingConfig::Level::Warnings, QString());
    QCOMPARE(LoggingConfig::loadPerCategory(), QString());

    // And the actual key is removed (not just blanked) — important for
    // tools that scan the .ini for known keys.
    QSettings s(QStringLiteral("MidiEditor"), QStringLiteral("NONE"));
    QVERIFY(!s.contains(QStringLiteral("Logging/perCategory")));
}

// Whitespace-only per-category is treated identically to empty — the key
// is removed, not stored verbatim.
void TestLoggingConfig::applyAndPersist_whitespacePerCategoryRemovesTheKey() {
    LoggingConfig::applyAndPersist(LoggingConfig::Level::Info,
                                   QStringLiteral("midieditor.collab.lan.debug=true"));
    LoggingConfig::applyAndPersist(LoggingConfig::Level::Warnings,
                                   QStringLiteral("   \n\t  "));
    QCOMPARE(LoggingConfig::loadPerCategory(), QString());
    QSettings s(QStringLiteral("MidiEditor"), QStringLiteral("NONE"));
    QVERIFY(!s.contains(QStringLiteral("Logging/perCategory")));
}

// ---------------------------------------------------------------------
// setCollabVerbose / loadCollabVerbose
// ---------------------------------------------------------------------

void TestLoggingConfig::setCollabVerbose_persistsTrueAndClearsOnFalse() {
    QVERIFY(!LoggingConfig::loadCollabVerbose());
    LoggingConfig::setCollabVerbose(true);
    QVERIFY(LoggingConfig::loadCollabVerbose());

    // The key should exist now
    QSettings s(QStringLiteral("MidiEditor"), QStringLiteral("NONE"));
    QVERIFY(s.contains(QStringLiteral("Collab/verboseLogging")));

    // Turning it off removes the key (not just sets it false) so default
    // detection (key-absent => false) works after a user toggle-pair.
    LoggingConfig::setCollabVerbose(false);
    QVERIFY(!LoggingConfig::loadCollabVerbose());
    QSettings s2(QStringLiteral("MidiEditor"), QStringLiteral("NONE"));
    QVERIFY(!s2.contains(QStringLiteral("Collab/verboseLogging")));
}

void TestLoggingConfig::loadCollabVerbose_defaultsToFalse() {
    QVERIFY(!LoggingConfig::loadCollabVerbose());
}

QTEST_APPLESS_MAIN(TestLoggingConfig)
#include "test_logging_config.moc"
