// Unit tests for CollabIdentity — display name + machine UUID derivation
// and persistence (Plan §6.4).
//
// Pure QSettings + QUuid — no MidiFile / network deps. Sandboxes its own
// QSettings via QStandardPaths::setTestModeEnabled(true) so the developer's
// real Collab/identity/* keys are never touched.
//
// Run from a build configured with -DBUILD_TESTING=ON:
//     ctest --test-dir build -V -R CollabIdentity

#include "CollabIdentity.h"

#include <QSettings>
#include <QStandardPaths>
#include <QString>
#include <QUuid>
#include <QtTest/QtTest>

class TestCollabIdentity : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();        // runs before every test method - wipe identity keys
    void cleanupTestCase();

    void displayName_fallsBackToOsUserOrAnonymous();
    void displayName_persistsAcrossCalls();
    void setDisplayName_trimsWhitespace();
    void setDisplayName_emptyResetsToFallback();

    void machineId_generatedLazilyOnFirstCall();
    void machineId_stableAcrossCalls();
    void machineId_isCanonicalUuidWithoutBraces();

    void regenerateMachineId_producesDifferentValue();
    void regenerateMachineId_persistsNewValue();

    void displayLabel_combinesNameAndIdPrefix();
    void displayLabel_idPrefixIsEightHexChars();

private:
    static void wipeIdentityKeys() {
        QSettings settings(QStringLiteral("MidiEditor"), QStringLiteral("NONE"));
        settings.remove(QStringLiteral("Collab/identity/displayName"));
        settings.remove(QStringLiteral("Collab/identity/machineId"));
        settings.sync();
    }
};

void TestCollabIdentity::initTestCase() {
    // Sandbox QSettings so the developer's real config isn't touched.
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("MidiEditor"));
    QCoreApplication::setApplicationName(QStringLiteral("NONE"));
}

void TestCollabIdentity::init() {
    wipeIdentityKeys();
}

void TestCollabIdentity::cleanupTestCase() {
    wipeIdentityKeys();
}

// ---------------------------------------------------------------------
// displayName
// ---------------------------------------------------------------------

// On first call with no key set, displayName falls back to the OS user
// (USERNAME / USER), or to "Anonymous" if neither env var is set.
void TestCollabIdentity::displayName_fallsBackToOsUserOrAnonymous() {
    QString name = CollabIdentity::displayName();
    QVERIFY(!name.isEmpty());
    // Either matches the env var or is the documented fallback string.
    QString osUser = qEnvironmentVariable("USERNAME");
    if (osUser.isEmpty()) osUser = qEnvironmentVariable("USER");
    if (!osUser.isEmpty()) {
        QCOMPARE(name, osUser);
    } else {
        QCOMPARE(name, QStringLiteral("Anonymous"));
    }
}

// Persistence round-trip via QSettings.
void TestCollabIdentity::displayName_persistsAcrossCalls() {
    CollabIdentity::setDisplayName(QStringLiteral("Alice"));
    QCOMPARE(CollabIdentity::displayName(), QStringLiteral("Alice"));
    QCOMPARE(CollabIdentity::displayName(), QStringLiteral("Alice"));
}

// Leading / trailing whitespace must be stripped before persisting so
// stored values stay clean.
void TestCollabIdentity::setDisplayName_trimsWhitespace() {
    CollabIdentity::setDisplayName(QStringLiteral("  Bob  "));
    QCOMPARE(CollabIdentity::displayName(), QStringLiteral("Bob"));
}

// Setting an empty (or whitespace-only) name removes the override and
// returns to the OS-user fallback.
void TestCollabIdentity::setDisplayName_emptyResetsToFallback() {
    CollabIdentity::setDisplayName(QStringLiteral("Carol"));
    QCOMPARE(CollabIdentity::displayName(), QStringLiteral("Carol"));
    CollabIdentity::setDisplayName(QStringLiteral("   "));
    // Whitespace-only is treated as empty after trim.
    QString afterEmpty = CollabIdentity::displayName();
    QVERIFY(afterEmpty != QStringLiteral("Carol"));
    QVERIFY(!afterEmpty.isEmpty());
}

// ---------------------------------------------------------------------
// machineId
// ---------------------------------------------------------------------

void TestCollabIdentity::machineId_generatedLazilyOnFirstCall() {
    QSettings settings(QStringLiteral("MidiEditor"), QStringLiteral("NONE"));
    QVERIFY(settings.value(QStringLiteral("Collab/identity/machineId"))
                    .toString().isEmpty());
    QString id = CollabIdentity::machineId();
    QVERIFY(!id.isEmpty());
    // Now persisted.
    QCOMPARE(settings.value(QStringLiteral("Collab/identity/machineId"))
                     .toString(), id);
}

void TestCollabIdentity::machineId_stableAcrossCalls() {
    QString first  = CollabIdentity::machineId();
    QString second = CollabIdentity::machineId();
    QString third  = CollabIdentity::machineId();
    QCOMPARE(second, first);
    QCOMPARE(third, first);
}

// Implementation uses QUuid::toString(WithoutBraces) — no surrounding {}
// so the value can be embedded in JSON and URLs without escaping.
void TestCollabIdentity::machineId_isCanonicalUuidWithoutBraces() {
    QString id = CollabIdentity::machineId();
    QVERIFY(!id.startsWith(QChar('{')));
    QVERIFY(!id.endsWith(QChar('}')));
    // QUuid::fromString round-trip succeeds for canonical form.
    QUuid parsed = QUuid::fromString(id);
    QVERIFY2(!parsed.isNull(), qPrintable(id));
}

// ---------------------------------------------------------------------
// regenerateMachineId
// ---------------------------------------------------------------------

void TestCollabIdentity::regenerateMachineId_producesDifferentValue() {
    QString before = CollabIdentity::machineId();
    CollabIdentity::regenerateMachineId();
    QString after = CollabIdentity::machineId();
    QVERIFY(!after.isEmpty());
    QVERIFY(after != before);
}

void TestCollabIdentity::regenerateMachineId_persistsNewValue() {
    CollabIdentity::regenerateMachineId();
    QString first = CollabIdentity::machineId();
    QString second = CollabIdentity::machineId();
    QCOMPARE(first, second);
    QSettings settings(QStringLiteral("MidiEditor"), QStringLiteral("NONE"));
    QCOMPARE(settings.value(QStringLiteral("Collab/identity/machineId"))
                     .toString(), first);
}

// ---------------------------------------------------------------------
// displayLabel
// ---------------------------------------------------------------------

// Format: "Name (8-char-prefix)" — e.g. "Alice (a1b2c3d4)".
void TestCollabIdentity::displayLabel_combinesNameAndIdPrefix() {
    CollabIdentity::setDisplayName(QStringLiteral("Alice"));
    QString id = CollabIdentity::machineId();
    QString label = CollabIdentity::displayLabel();
    QVERIFY(label.startsWith(QStringLiteral("Alice (")));
    QVERIFY(label.endsWith(QChar(')')));
    QVERIFY(label.contains(id.left(8)));
}

void TestCollabIdentity::displayLabel_idPrefixIsEightHexChars() {
    QString label = CollabIdentity::displayLabel();
    int open = label.lastIndexOf(QChar('('));
    int close = label.lastIndexOf(QChar(')'));
    QVERIFY(open >= 0 && close > open);
    QString prefix = label.mid(open + 1, close - open - 1);
    QCOMPARE(prefix.size(), 8);
}

QTEST_APPLESS_MAIN(TestCollabIdentity)
#include "test_collab_identity.moc"
