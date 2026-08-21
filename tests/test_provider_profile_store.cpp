/*
 * test_provider_profile_store (Phase 50, v2.3)
 *
 * Pins ProviderProfileStore: named AI endpoint configurations. Contract to
 * cover (see roadmap Phase 50):
 *   1. Store round-trip: save / list / load / rename via save+remove / remove.
 *   2. apply() sets the four active settings keys (provider, base URL, key,
 *      model) and keeps the per-provider key memory consistent.
 *   3. Switching provider and back must not clobber a stored profile.
 *   4. A file preset referencing a missing profile falls back cleanly
 *      (tested at the store level: load() with ok=false).
 *
 * Uses the AppPaths test seam - the developer's real settings scope must
 * never be touched (TESTWIPE class).
 */

#include <QtTest/QtTest>
#include <QObject>
#include <QSettings>

#include "../src/ai/ProviderProfileStore.h"
#include "../src/AppPaths.h"

class TestProviderProfileStore : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        AppPaths::setSettingsScopeForTests(QStringLiteral("MidiEditorTest"),
                                           QStringLiteral("ProviderProfileStoreTest"));
    }

    void cleanupTestCase() {
        AppPaths::settings()->clear();
        AppPaths::setSettingsScopeForTests(QString(), QString());
    }

    // Skeleton case - replaced by the Phase 50 implementation's tests.
    void skeletonStoreIsEmpty() {
        QVERIFY(ProviderProfileStore::profileNames().isEmpty());
        bool ok = true;
        ProviderProfileStore::load(QStringLiteral("nope"), &ok);
        QVERIFY(!ok);
    }
};

QTEST_APPLESS_MAIN(TestProviderProfileStore)
#include "test_provider_profile_store.moc"
