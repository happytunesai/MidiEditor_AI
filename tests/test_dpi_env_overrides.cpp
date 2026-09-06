/*
 * test_dpi_env_overrides
 *
 * SP-04 (external system/performance review, 2026-09-06): the QT_* scaling
 * overrides main() sets from the Performance page are inherited by an in-app
 * restart, so a switched-off option stayed in force and an externally
 * configured value was lost. DpiEnvOverrides records the inherited state and
 * the next instance restores it. Pinned here:
 *   - an inherited value is put back, an inherited "unset" is unset again
 *   - a repeated override keeps the FIRST backup (never our own value)
 *   - several overrides are listed and restored together
 *   - restoring twice, or without any record, changes nothing
 *   - the relaunch sequence: override, restore in the "child", override again
 * The variables under test are private names, never the real QT_* ones, so
 * the test can never influence the Qt runtime it runs in.
 */

#include <QtTest/QtTest>
#include <QByteArray>
#include <QByteArrayList>

#include "../src/DpiEnvOverrides.h"

namespace {

const char *const kVarA = "MIDIEDITOR_TEST_ENV_A";
const char *const kVarB = "MIDIEDITOR_TEST_ENV_B";

QByteArray backupOf(const char *name)
{
    return QByteArray(DpiEnvOverrides::kBackupPrefix) + name;
}

void clearAll()
{
    for (const char *name : {kVarA, kVarB}) {
        qunsetenv(name);
        qunsetenv(backupOf(name).constData());
    }
    qunsetenv(DpiEnvOverrides::kMarkerVariable);
}

} // namespace

class TestDpiEnvOverrides : public QObject {
    Q_OBJECT

private slots:
    void init() { clearAll(); }
    void cleanup() { clearAll(); }

    void inheritedValueIsRestored()
    {
        qputenv(kVarA, "external");
        DpiEnvOverrides::set(kVarA, "1.0");
        QCOMPARE(qgetenv(kVarA), QByteArray("1.0"));
        QCOMPARE(qgetenv(backupOf(kVarA).constData()), QByteArray("1:external"));
        QVERIFY(DpiEnvOverrides::recordedNames().contains(QByteArray(kVarA)));

        QCOMPARE(DpiEnvOverrides::restoreInherited(), 1);
        QCOMPARE(qgetenv(kVarA), QByteArray("external"));
        QVERIFY(!qEnvironmentVariableIsSet(backupOf(kVarA).constData()));
        QVERIFY(!qEnvironmentVariableIsSet(DpiEnvOverrides::kMarkerVariable));
    }

    void inheritedUnsetIsUnsetAgain()
    {
        QVERIFY(!qEnvironmentVariableIsSet(kVarA));
        DpiEnvOverrides::set(kVarA, "0");
        QCOMPARE(qgetenv(kVarA), QByteArray("0"));
        QCOMPARE(qgetenv(backupOf(kVarA).constData()), QByteArray("0"));

        QCOMPARE(DpiEnvOverrides::restoreInherited(), 1);
        QVERIFY(!qEnvironmentVariableIsSet(kVarA));
        QVERIFY(!qEnvironmentVariableIsSet(backupOf(kVarA).constData()));
    }

    void repeatedOverrideKeepsFirstBackup()
    {
        qputenv(kVarA, "external");
        DpiEnvOverrides::set(kVarA, "1.0");
        DpiEnvOverrides::set(kVarA, "2.0");
        QCOMPARE(qgetenv(kVarA), QByteArray("2.0"));
        QCOMPARE(qgetenv(backupOf(kVarA).constData()), QByteArray("1:external"));
        QCOMPARE(DpiEnvOverrides::recordedNames().size(), 1);

        QCOMPARE(DpiEnvOverrides::restoreInherited(), 1);
        QCOMPARE(qgetenv(kVarA), QByteArray("external"));
    }

    void severalOverridesRestoredTogether()
    {
        qputenv(kVarA, "ext-a");
        DpiEnvOverrides::set(kVarA, "1.0");
        DpiEnvOverrides::set(kVarB, "96");
        const QByteArrayList names = DpiEnvOverrides::recordedNames();
        QCOMPARE(names.size(), 2);
        QVERIFY(names.contains(QByteArray(kVarA)));
        QVERIFY(names.contains(QByteArray(kVarB)));

        QCOMPARE(DpiEnvOverrides::restoreInherited(), 2);
        QCOMPARE(qgetenv(kVarA), QByteArray("ext-a"));
        QVERIFY(!qEnvironmentVariableIsSet(kVarB));
        QVERIFY(DpiEnvOverrides::recordedNames().isEmpty());
    }

    void restoreWithoutRecordIsNoop()
    {
        qputenv(kVarA, "external");
        QCOMPARE(DpiEnvOverrides::restoreInherited(), 0);
        QCOMPARE(qgetenv(kVarA), QByteArray("external"));
        // Twice in a row: the second call finds nothing recorded either.
        DpiEnvOverrides::set(kVarA, "1.0");
        QCOMPARE(DpiEnvOverrides::restoreInherited(), 1);
        QCOMPARE(DpiEnvOverrides::restoreInherited(), 0);
        QCOMPARE(qgetenv(kVarA), QByteArray("external"));
    }

    void relaunchScenario_switchedOffOptionIsReallyOff()
    {
        // Instance 1: option on -> override. Instance 2 (the in-app restart)
        // inherits the environment, restores first, and its own "off" branch
        // sets nothing: the variable must be back to the external state.
        qputenv(kVarA, "external");
        DpiEnvOverrides::set(kVarA, "0");            // instance 1, option on
        QCOMPARE(qgetenv(kVarA), QByteArray("0"));
        DpiEnvOverrides::restoreInherited();          // instance 2 startup
        QCOMPARE(qgetenv(kVarA), QByteArray("external"));
        // Instance 2 with the option still on overrides again and keeps the
        // external value in its backup for instance 3.
        DpiEnvOverrides::set(kVarA, "0");
        QCOMPARE(qgetenv(backupOf(kVarA).constData()), QByteArray("1:external"));
        QCOMPARE(DpiEnvOverrides::restoreInherited(), 1);
        QCOMPARE(qgetenv(kVarA), QByteArray("external"));
    }
};

QTEST_GUILESS_MAIN(TestDpiEnvOverrides)
#include "test_dpi_env_overrides.moc"
