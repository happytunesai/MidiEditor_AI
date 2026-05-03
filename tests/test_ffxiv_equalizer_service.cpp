/*
 * MidiEditor AI — FfxivEqualizerService unit tests (Phase 39)
 *
 * Verifies pure mixer behaviour without going near FluidSynth.
 * QSettings is redirected to an INI scope under the temp dir so
 * preset CRUD doesn't leak into the user's real config.
 */

#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include "../src/midi/FfxivEqualizerService.h"

class TestFfxivEqualizerService : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName("MidiEditorTest");
        QCoreApplication::setApplicationName("EqualizerTest");
        // Reset to known state for each run (test mode uses a temp dir).
        // The service uses an explicit QSettings("MidiEditor","NONE") scope
        // to stay consistent with the rest of the app, so clear that too.
        QSettings().clear();
        QSettings(QStringLiteral("MidiEditor"), QStringLiteral("NONE")).clear();
        FfxivEqualizerService::instance()->resetToBuiltinDefault();
    }

    void cleanup() {
        // Reset between tests so they don't see each other's mutations.
        FfxivEqualizerService::instance()->resetToBuiltinDefault();
        FfxivEqualizerService::instance()->setMasterGain(1.0f);
    }

    void unknownProgramReturnsMasterGain() {
        auto *svc = FfxivEqualizerService::instance();
        // GM electric piano = 4. Not in the FFXIV table — should fall
        // back to master (1.0) so unknown programs still play.
        QVERIFY(qFuzzyCompare(svc->gainFor(4, false), 1.0f));
        svc->setMasterGain(0.5f);
        QVERIFY(qFuzzyCompare(svc->gainFor(4, false), 0.5f));
    }

    void builtinDefaultsHavePiano() {
        auto *svc = FfxivEqualizerService::instance();
        // Piano (program 0) should be unity in the built-in default.
        QCOMPARE(svc->gainFor(0, false), 1.0f);
        // isDrum=true routes to kDrumKitProgram; no slot registered for it
        // in the FFXIV equalizer (FFXIV percussion uses named programs),
        // so the fallback is master gain (1.0 = no-op).
        QCOMPARE(svc->gainFor(0, true), 1.0f);
    }

    void setProgramGainEmitsMixChanged() {
        auto *svc = FfxivEqualizerService::instance();
        QSignalSpy spy(svc, &FfxivEqualizerService::mixChanged);
        svc->setProgramGain(25, 1.50f);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(svc->gainFor(25, false), 1.50f);
    }

    void mutedSlotReturnsZero() {
        auto *svc = FfxivEqualizerService::instance();
        svc->setProgramMuted(40, true);
        QCOMPARE(svc->gainFor(40, false), 0.0f);
        QVERIFY(svc->isMuted(40));
        svc->setProgramMuted(40, false);
        QVERIFY(svc->gainFor(40, false) > 0.0f);
        QVERIFY(!svc->isMuted(40));
    }

    void masterGainScalesAllPrograms() {
        auto *svc = FfxivEqualizerService::instance();
        svc->setProgramGain(25, 1.0f);  // Lute @ unity
        svc->setMasterGain(0.5f);
        QCOMPARE(svc->gainFor(25, false), 0.5f);
        svc->setMasterGain(2.0f);
        QCOMPARE(svc->gainFor(25, false), 2.0f);
    }

    void savePresetRoundtripPersistsValues() {
        auto *svc = FfxivEqualizerService::instance();
        svc->setProgramGain(25, 1.75f);
        svc->setProgramMuted(40, true);
        svc->setMasterGain(0.8f);

        QVERIFY(svc->savePresetAs("MyTestPreset"));

        // Mutate live state, then reload preset -> values restored.
        svc->setProgramGain(25, 0.10f);
        svc->setProgramMuted(40, false);
        svc->setMasterGain(1.0f);

        svc->setActivePreset("MyTestPreset");
        QCOMPARE(svc->gainFor(25, false), 1.75f * 0.8f);
        QVERIFY(svc->isMuted(40));
        QCOMPARE(svc->masterGain(), 0.8f);
    }

    void deletingActivePresetFallsBackToBuiltin() {
        auto *svc = FfxivEqualizerService::instance();
        svc->setProgramGain(25, 0.20f);
        QVERIFY(svc->savePresetAs("DeleteMe"));
        QCOMPARE(svc->activePresetName(), QString("DeleteMe"));
        QVERIFY(svc->deletePreset("DeleteMe"));
        QCOMPARE(svc->activePresetName(),
                 FfxivEqualizerService::builtinPresetName());
        // Built-in default for Lute is 1.0 (master 1.0).
        QCOMPARE(svc->gainFor(25, false), 1.0f);
    }

    void cannotDeleteBuiltinPreset() {
        auto *svc = FfxivEqualizerService::instance();
        QVERIFY(!svc->deletePreset(FfxivEqualizerService::builtinPresetName()));
    }

    void cannotSaveOverBuiltinPreset() {
        auto *svc = FfxivEqualizerService::instance();
        QVERIFY(!svc->savePresetAs(FfxivEqualizerService::builtinPresetName()));
    }

    void allPresetNamesAlwaysIncludesBuiltin() {
        auto *svc = FfxivEqualizerService::instance();
        QStringList names = svc->allPresetNames();
        QVERIFY(names.contains(FfxivEqualizerService::builtinPresetName()));
        QCOMPARE(names.first(), FfxivEqualizerService::builtinPresetName());
    }

    void savedPresetAppearsInAllPresetNames() {
        // Regression for the savePresetAs / childGroups() visibility bug:
        // setValue("a/b/c") does not reliably expose intermediate path
        // components to childGroups() — explicit beginGroup() is required.
        auto *svc = FfxivEqualizerService::instance();
        QVERIFY(svc->savePresetAs("VisibilityTest"));
        QStringList names = svc->allPresetNames();
        QVERIFY2(names.contains(QStringLiteral("VisibilityTest")),
                 "Saved preset must appear in allPresetNames() immediately after savePresetAs()");
        // Clean up
        svc->deletePreset("VisibilityTest");
    }

    void saveAsBecomesActiveAndSurvivesReselect() {
        // Regression for B-FFXIV-EQ-002: after Save As..., the new preset
        // must (a) become the active preset, (b) appear in allPresetNames(),
        // and (c) survive a re-select via setActivePreset() without being
        // silently reverted to the built-in default by a fragile guard.
        auto *svc = FfxivEqualizerService::instance();
        svc->setProgramGain(25, 1.42f);
        QVERIFY(svc->savePresetAs("UserFlowTest"));
        QCOMPARE(svc->activePresetName(), QString("UserFlowTest"));
        QVERIFY(svc->allPresetNames().contains(QStringLiteral("UserFlowTest")));

        // Switch away then back — must not silently fall back to builtin.
        svc->setActivePreset(FfxivEqualizerService::builtinPresetName());
        QCOMPARE(svc->activePresetName(),
                 FfxivEqualizerService::builtinPresetName());
        svc->setActivePreset("UserFlowTest");
        QCOMPARE(svc->activePresetName(), QString("UserFlowTest"));
        QCOMPARE(svc->gainFor(25, false), 1.42f);

        svc->deletePreset("UserFlowTest");
    }

    void presetsChangedSignalFiresOnSaveAs() {
        // Dialog wires presetsChanged -> refreshPresetCombo so the dropdown
        // updates even if the explicit refresh path is missed. Lock that in.
        auto *svc = FfxivEqualizerService::instance();
        QSignalSpy spy(svc, &FfxivEqualizerService::presetsChanged);
        QVERIFY(svc->savePresetAs("SignalTest"));
        QVERIFY(spy.count() >= 1);
        svc->deletePreset("SignalTest");
    }

    void knownInstrumentsStartsWithPiano() {
        const auto &table = FfxivEqualizerService::knownInstruments();
        QVERIFY(!table.isEmpty());
        QCOMPARE(table.first().first, QStringLiteral("Piano"));
        QCOMPARE(table.first().second, 0);
    }
};

QTEST_MAIN(TestFfxivEqualizerService)
#include "test_ffxiv_equalizer_service.moc"
