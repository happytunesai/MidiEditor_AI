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

using Profile = ProviderProfileStore::Profile;

namespace {

Profile makeProfile(const QString &name, const QString &provider,
                    const QString &url, const QString &model)
{
    Profile p;
    p.name = name;
    p.provider = provider;
    p.baseUrl = url;
    p.model = model;
    return p;
}

// The provider switch as both wiring sites implement it (AiSettingsWidget
// onProviderChanged / MidiPilotWidget onProviderComboChanged): remember the
// active key under the OLD provider, then load the NEW provider's key.
void simulateProviderSwitch(const QString &oldProvider, const QString &newProvider)
{
    auto s = AppPaths::settings();
    const QString activeKey = s->value(QStringLiteral("AI/api_key")).toString();
    if (!activeKey.isEmpty())
        s->setValue(QStringLiteral("AI/api_key/%1").arg(oldProvider), activeKey);
    s->setValue(QStringLiteral("AI/provider"), newProvider);
    s->setValue(QStringLiteral("AI/api_key"),
                s->value(QStringLiteral("AI/api_key/%1").arg(newProvider)).toString());
}

} // namespace

class TestProviderProfileStore : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        AppPaths::setSettingsScopeForTests(QStringLiteral("MidiEditorTest"),
                                           QStringLiteral("ProviderProfileStoreTest"));
    }

    void init() {
        // Every case starts on an empty store - the cases below assert on
        // exact list contents.
        AppPaths::settings()->clear();
    }

    void cleanupTestCase() {
        AppPaths::settings()->clear();
        AppPaths::setSettingsScopeForTests(QString(), QString());
    }

    // --- 1. round-trip ----------------------------------------------------

    void emptyStoreListsNothing() {
        QVERIFY(ProviderProfileStore::profileNames().isEmpty());
        QVERIFY(!ProviderProfileStore::exists(QStringLiteral("nope")));
    }

    void saveListLoadRemove() {
        QVERIFY(ProviderProfileStore::save(
            makeProfile(QStringLiteral("HF Router"), QStringLiteral("custom"),
                        QStringLiteral("https://router.example/v1"),
                        QStringLiteral("some/model")),
            QStringLiteral("hf-token")));
        QVERIFY(ProviderProfileStore::save(
            makeProfile(QStringLiteral("Local server"), QStringLiteral("custom"),
                        QStringLiteral("http://localhost:8080/v1"),
                        QStringLiteral("local-model")),
            QString()));

        // Sorted, both present.
        QCOMPARE(ProviderProfileStore::profileNames(),
                 (QStringList{QStringLiteral("HF Router"),
                              QStringLiteral("Local server")}));
        QVERIFY(ProviderProfileStore::exists(QStringLiteral("HF Router")));

        bool ok = false;
        const Profile p = ProviderProfileStore::load(QStringLiteral("HF Router"), &ok);
        QVERIFY(ok);
        QCOMPARE(p.name, QStringLiteral("HF Router"));
        QCOMPARE(p.provider, QStringLiteral("custom"));
        QCOMPARE(p.baseUrl, QStringLiteral("https://router.example/v1"));
        QCOMPARE(p.model, QStringLiteral("some/model"));
        QCOMPARE(ProviderProfileStore::apiKeyFor(QStringLiteral("HF Router")),
                 QStringLiteral("hf-token"));
        // A keyless (local) profile round-trips as such.
        QVERIFY(ProviderProfileStore::apiKeyFor(QStringLiteral("Local server")).isEmpty());

        QVERIFY(ProviderProfileStore::remove(QStringLiteral("HF Router")));
        QCOMPARE(ProviderProfileStore::profileNames(),
                 (QStringList{QStringLiteral("Local server")}));
        // Removing takes the key with it, and a second remove is a no-op.
        QVERIFY(ProviderProfileStore::apiKeyFor(QStringLiteral("HF Router")).isEmpty());
        QVERIFY(!ProviderProfileStore::remove(QStringLiteral("HF Router")));
    }

    void saveOverwritesInPlace() {
        QVERIFY(ProviderProfileStore::save(
            makeProfile(QStringLiteral("Work"), QStringLiteral("openai"),
                        QStringLiteral("https://api.openai.com/v1"),
                        QStringLiteral("gpt-4o")),
            QStringLiteral("key-one")));
        QVERIFY(ProviderProfileStore::save(
            makeProfile(QStringLiteral("Work"), QStringLiteral("openrouter"),
                        QStringLiteral("https://openrouter.ai/api/v1"),
                        QStringLiteral("openai/gpt-4.1")),
            QStringLiteral("key-two")));

        QCOMPARE(ProviderProfileStore::profileNames().size(), 1);
        bool ok = false;
        const Profile p = ProviderProfileStore::load(QStringLiteral("Work"), &ok);
        QVERIFY(ok);
        QCOMPARE(p.provider, QStringLiteral("openrouter"));
        QCOMPARE(p.model, QStringLiteral("openai/gpt-4.1"));
        QCOMPARE(ProviderProfileStore::apiKeyFor(QStringLiteral("Work")),
                 QStringLiteral("key-two"));
    }

    void renameIsSaveUnderNewNamePlusRemove() {
        QVERIFY(ProviderProfileStore::save(
            makeProfile(QStringLiteral("Old"), QStringLiteral("custom"),
                        QStringLiteral("http://host/v1"), QStringLiteral("m")),
            QStringLiteral("k")));
        bool ok = false;
        Profile p = ProviderProfileStore::load(QStringLiteral("Old"), &ok);
        QVERIFY(ok);
        p.name = QStringLiteral("New");
        QVERIFY(ProviderProfileStore::save(
            p, ProviderProfileStore::apiKeyFor(QStringLiteral("Old"))));
        QVERIFY(ProviderProfileStore::remove(QStringLiteral("Old")));

        QCOMPARE(ProviderProfileStore::profileNames(),
                 (QStringList{QStringLiteral("New")}));
        QCOMPARE(ProviderProfileStore::apiKeyFor(QStringLiteral("New")),
                 QStringLiteral("k"));
    }

    // --- 2. name sanitization --------------------------------------------

    void nameSanitizationSurvivesSeparatorsAndWhitespace() {
        // '/' is the QSettings group separator: a raw name would fan out into
        // nested groups and could never be listed or found again.
        const QString tricky = QStringLiteral("  HF / local\tserver  ");
        const QString expected = QStringLiteral("HF / local server");
        QCOMPARE(ProviderProfileStore::normalizeName(tricky), expected);
        QVERIFY(!ProviderProfileStore::encodeName(tricky).contains(QLatin1Char('/')));
        QCOMPARE(ProviderProfileStore::decodeName(
                     ProviderProfileStore::encodeName(tricky)), expected);

        QVERIFY(ProviderProfileStore::save(
            makeProfile(tricky, QStringLiteral("custom"),
                        QStringLiteral("http://host/v1"), QString()),
            QStringLiteral("k")));
        // Listed and loadable under the normalized name, exactly once.
        QCOMPARE(ProviderProfileStore::profileNames(), (QStringList{expected}));
        bool ok = false;
        const Profile p = ProviderProfileStore::load(tricky, &ok);
        QVERIFY(ok);
        QCOMPARE(p.name, expected);
        QVERIFY(ProviderProfileStore::exists(expected));
        QVERIFY(ProviderProfileStore::remove(expected));
        QVERIFY(ProviderProfileStore::profileNames().isEmpty());
    }

    void unusableNamesAreRejected() {
        QVERIFY(!ProviderProfileStore::save(
            makeProfile(QStringLiteral("   "), QStringLiteral("custom"),
                        QStringLiteral("http://host/v1"), QString()),
            QString()));
        // A profile without a provider is not a profile.
        QVERIFY(!ProviderProfileStore::save(
            makeProfile(QStringLiteral("No provider"), QString(),
                        QStringLiteral("http://host/v1"), QString()),
            QString()));
        QVERIFY(ProviderProfileStore::profileNames().isEmpty());

        // Over-long names are capped, not rejected.
        const QString longName(200, QLatin1Char('x'));
        QCOMPARE(ProviderProfileStore::normalizeName(longName).size(),
                 ProviderProfileStore::maxNameLength());
    }

    // --- 3. apply() semantics --------------------------------------------

    void applySetsTheFourActiveKeysAndProviderMemory() {
        auto s = AppPaths::settings();
        s->setValue(QStringLiteral("AI/provider"), QStringLiteral("openai"));
        s->setValue(QStringLiteral("AI/api_base_url"),
                    QStringLiteral("https://api.openai.com/v1"));
        s->setValue(QStringLiteral("AI/api_key"), QStringLiteral("openai-key"));
        s->setValue(QStringLiteral("AI/api_key/openai"), QStringLiteral("openai-key"));
        s->setValue(QStringLiteral("AI/model"), QStringLiteral("gpt-4o"));

        QVERIFY(ProviderProfileStore::save(
            makeProfile(QStringLiteral("HF Router"), QStringLiteral("custom"),
                        QStringLiteral("https://router.example/v1"),
                        QStringLiteral("some/model")),
            QStringLiteral("hf-token")));

        QString error = QStringLiteral("untouched");
        QVERIFY(ProviderProfileStore::apply(QStringLiteral("HF Router"), &error));
        QCOMPARE(error, QStringLiteral("untouched"));

        QCOMPARE(s->value(QStringLiteral("AI/provider")).toString(),
                 QStringLiteral("custom"));
        QCOMPARE(s->value(QStringLiteral("AI/api_base_url")).toString(),
                 QStringLiteral("https://router.example/v1"));
        QCOMPARE(s->value(QStringLiteral("AI/api_key")).toString(),
                 QStringLiteral("hf-token"));
        QCOMPARE(s->value(QStringLiteral("AI/model")).toString(),
                 QStringLiteral("some/model"));
        // Per-provider memory follows, so a later switch to "custom" finds it.
        QCOMPARE(s->value(QStringLiteral("AI/api_key/custom")).toString(),
                 QStringLiteral("hf-token"));
        // The other provider's remembered key is untouched.
        QCOMPARE(s->value(QStringLiteral("AI/api_key/openai")).toString(),
                 QStringLiteral("openai-key"));
        QCOMPARE(ProviderProfileStore::activeProfileName(),
                 QStringLiteral("HF Router"));
    }

    void applyOfAKeylessProfileKeepsTheRememberedKey() {
        auto s = AppPaths::settings();
        s->setValue(QStringLiteral("AI/api_key/custom"), QStringLiteral("hf-token"));

        QVERIFY(ProviderProfileStore::save(
            makeProfile(QStringLiteral("Local"), QStringLiteral("custom"),
                        QStringLiteral("http://localhost:8080/v1"), QString()),
            QString()));
        QVERIFY(ProviderProfileStore::apply(QStringLiteral("Local")));

        QVERIFY(s->value(QStringLiteral("AI/api_key")).toString().isEmpty());
        // A local endpoint without a key must not erase the cloud key memory.
        QCOMPARE(s->value(QStringLiteral("AI/api_key/custom")).toString(),
                 QStringLiteral("hf-token"));
    }

    void applyLeavesTheModelAloneWhenTheProfileHasNone() {
        auto s = AppPaths::settings();
        s->setValue(QStringLiteral("AI/model"), QStringLiteral("gpt-4o"));
        QVERIFY(ProviderProfileStore::save(
            makeProfile(QStringLiteral("Endpoint only"), QStringLiteral("custom"),
                        QStringLiteral("http://host/v1"), QString()),
            QStringLiteral("k")));
        QVERIFY(ProviderProfileStore::apply(QStringLiteral("Endpoint only")));
        QCOMPARE(s->value(QStringLiteral("AI/model")).toString(),
                 QStringLiteral("gpt-4o"));
    }

    // --- 4. interplay with the per-provider key memory --------------------

    void providerSwitchAndBackDoesNotClobberTheProfile() {
        auto s = AppPaths::settings();
        s->setValue(QStringLiteral("AI/api_key/openai"), QStringLiteral("openai-key"));

        QVERIFY(ProviderProfileStore::save(
            makeProfile(QStringLiteral("HF Router"), QStringLiteral("custom"),
                        QStringLiteral("https://router.example/v1"),
                        QStringLiteral("some/model")),
            QStringLiteral("hf-token")));
        QVERIFY(ProviderProfileStore::apply(QStringLiteral("HF Router")));

        // Away to OpenAI and back to custom, exactly as the two footers do it.
        simulateProviderSwitch(QStringLiteral("custom"), QStringLiteral("openai"));
        QCOMPARE(s->value(QStringLiteral("AI/api_key")).toString(),
                 QStringLiteral("openai-key"));
        // Ad-hoc now: the active settings no longer match any profile.
        QVERIFY(ProviderProfileStore::activeProfileName().isEmpty());

        simulateProviderSwitch(QStringLiteral("openai"), QStringLiteral("custom"));

        // The stored profile is untouched by all of that.
        bool ok = false;
        const Profile p = ProviderProfileStore::load(QStringLiteral("HF Router"), &ok);
        QVERIFY(ok);
        QCOMPARE(p.baseUrl, QStringLiteral("https://router.example/v1"));
        QCOMPARE(p.model, QStringLiteral("some/model"));
        QCOMPARE(ProviderProfileStore::apiKeyFor(QStringLiteral("HF Router")),
                 QStringLiteral("hf-token"));

        // ... and re-applying restores the whole endpoint in one step.
        QVERIFY(ProviderProfileStore::apply(QStringLiteral("HF Router")));
        QCOMPARE(s->value(QStringLiteral("AI/api_base_url")).toString(),
                 QStringLiteral("https://router.example/v1"));
        QCOMPARE(s->value(QStringLiteral("AI/api_key")).toString(),
                 QStringLiteral("hf-token"));
        QCOMPARE(ProviderProfileStore::activeProfileName(),
                 QStringLiteral("HF Router"));
    }

    void twoCustomEndpointsCoexist() {
        QVERIFY(ProviderProfileStore::save(
            makeProfile(QStringLiteral("HF Router"), QStringLiteral("custom"),
                        QStringLiteral("https://router.example/v1"),
                        QStringLiteral("a/model")),
            QStringLiteral("hf-token")));
        QVERIFY(ProviderProfileStore::save(
            makeProfile(QStringLiteral("Local llama"), QStringLiteral("custom"),
                        QStringLiteral("http://localhost:8080/v1"),
                        QStringLiteral("b-model")),
            QStringLiteral("local-token")));

        QVERIFY(ProviderProfileStore::apply(QStringLiteral("HF Router")));
        QCOMPARE(ProviderProfileStore::activeProfileName(),
                 QStringLiteral("HF Router"));
        QVERIFY(ProviderProfileStore::apply(QStringLiteral("Local llama")));
        QCOMPARE(ProviderProfileStore::activeProfileName(),
                 QStringLiteral("Local llama"));
        // Both endpoints survive the switching - the single custom slot did not.
        QCOMPARE(ProviderProfileStore::apiKeyFor(QStringLiteral("HF Router")),
                 QStringLiteral("hf-token"));
        QCOMPARE(ProviderProfileStore::apiKeyFor(QStringLiteral("Local llama")),
                 QStringLiteral("local-token"));
    }

    // --- 5. the preset fallback contract ----------------------------------

    void missingProfileReportsNotOkAndDoesNotTouchSettings() {
        auto s = AppPaths::settings();
        s->setValue(QStringLiteral("AI/provider"), QStringLiteral("openai"));
        s->setValue(QStringLiteral("AI/api_key"), QStringLiteral("openai-key"));

        bool ok = true;
        ProviderProfileStore::load(QStringLiteral("Not on this machine"), &ok);
        QVERIFY(!ok);

        QString error;
        QVERIFY(!ProviderProfileStore::apply(QStringLiteral("Not on this machine"),
                                             &error));
        QVERIFY(!error.isEmpty());
        // The preset falls back to its own provider/model - nothing was changed.
        QCOMPARE(s->value(QStringLiteral("AI/provider")).toString(),
                 QStringLiteral("openai"));
        QCOMPARE(s->value(QStringLiteral("AI/api_key")).toString(),
                 QStringLiteral("openai-key"));
    }

    // --- 6. model-list / favourites scope ---------------------------------
    //
    // Favourites (AI/favorites/<scope>) and the cached model list hang off the
    // ENDPOINT, so two custom profiles never share a model list. Only "custom"
    // is split - the built-in providers keep one scope each.

    void modelScopeIsThePlainProviderForBuiltInProviders() {
        QCOMPARE(ProviderProfileStore::modelScopeId(
                     QStringLiteral("openai"),
                     QStringLiteral("https://api.openai.com/v1"),
                     QStringLiteral("openai-key")),
                 QStringLiteral("openai"));

        // A stored OpenAI profile shares the provider scope on purpose: same
        // provider, same catalogue.
        QVERIFY(ProviderProfileStore::save(
            makeProfile(QStringLiteral("Work"), QStringLiteral("openai"),
                        QStringLiteral("https://api.openai.com/v1"),
                        QStringLiteral("gpt-4o")),
            QStringLiteral("openai-key")));
        QCOMPARE(ProviderProfileStore::modelScopeId(
                     QStringLiteral("openai"),
                     QStringLiteral("https://api.openai.com/v1"),
                     QStringLiteral("openai-key")),
                 QStringLiteral("openai"));
        QCOMPARE(ProviderProfileStore::modelScopeIdForProfile(QStringLiteral("Work")),
                 QStringLiteral("openai"));
    }

    void modelScopeStaysAdHocCustomWithoutAMatchingProfile() {
        // Nothing stored at all - the pre-profile scope, so existing
        // AI/favorites/custom entries keep working untouched.
        QCOMPARE(ProviderProfileStore::modelScopeId(
                     QStringLiteral("custom"),
                     QStringLiteral("https://router.example/v1"),
                     QStringLiteral("hf-token")),
                 QStringLiteral("custom"));

        QVERIFY(ProviderProfileStore::save(
            makeProfile(QStringLiteral("HF Router"), QStringLiteral("custom"),
                        QStringLiteral("https://router.example/v1"),
                        QStringLiteral("a/model")),
            QStringLiteral("hf-token")));

        // A different key or a different URL is a different endpoint.
        QCOMPARE(ProviderProfileStore::modelScopeId(
                     QStringLiteral("custom"),
                     QStringLiteral("https://router.example/v1"),
                     QStringLiteral("other-token")),
                 QStringLiteral("custom"));
        QCOMPARE(ProviderProfileStore::modelScopeId(
                     QStringLiteral("custom"),
                     QStringLiteral("https://elsewhere.example/v1"),
                     QStringLiteral("hf-token")),
                 QStringLiteral("custom"));
        // An unknown profile has no scope.
        QVERIFY(ProviderProfileStore::modelScopeIdForProfile(
                    QStringLiteral("Not here")).isEmpty());
    }

    void modelScopeFollowsTheActiveCustomProfile() {
        QVERIFY(ProviderProfileStore::save(
            makeProfile(QStringLiteral("HF"), QStringLiteral("custom"),
                        QStringLiteral("https://router.example/v1"),
                        QStringLiteral("a/model")),
            QStringLiteral("hf-token")));
        QVERIFY(ProviderProfileStore::save(
            makeProfile(QStringLiteral("Local llama"), QStringLiteral("custom"),
                        QStringLiteral("http://localhost:8080/v1"),
                        QStringLiteral("b-model")),
            QStringLiteral("local-token")));

        const QString hfScope =
            ProviderProfileStore::modelScopeIdForProfile(QStringLiteral("HF"));
        const QString localScope =
            ProviderProfileStore::modelScopeIdForProfile(QStringLiteral("Local llama"));
        QCOMPARE(hfScope, QStringLiteral("custom:profile:HF"));
        QCOMPARE(localScope, QStringLiteral("custom:profile:Local llama"));
        QVERIFY(hfScope != localScope);

        QVERIFY(ProviderProfileStore::apply(QStringLiteral("HF")));
        QCOMPARE(ProviderProfileStore::activeModelScopeId(), hfScope);
        QVERIFY(ProviderProfileStore::apply(QStringLiteral("Local llama")));
        QCOMPARE(ProviderProfileStore::activeModelScopeId(), localScope);

        // Back to a built-in provider: the provider alone decides the scope.
        simulateProviderSwitch(QStringLiteral("custom"), QStringLiteral("openai"));
        QCOMPARE(ProviderProfileStore::activeModelScopeId(),
                 QStringLiteral("openai"));
    }

    void modelScopeIgnoresTheSelectedModel() {
        // The scope is the endpoint. Picking another model must not move the
        // user to a different favourites bucket mid-edit, even though the
        // profile *name* honestly falls back to ad-hoc.
        QVERIFY(ProviderProfileStore::save(
            makeProfile(QStringLiteral("HF"), QStringLiteral("custom"),
                        QStringLiteral("https://router.example/v1"),
                        QStringLiteral("a/model")),
            QStringLiteral("hf-token")));
        QVERIFY(ProviderProfileStore::apply(QStringLiteral("HF")));

        AppPaths::settings()->setValue(QStringLiteral("AI/model"),
                                       QStringLiteral("another/model"));
        QVERIFY(ProviderProfileStore::activeProfileName().isEmpty());
        QCOMPARE(ProviderProfileStore::activeModelScopeId(),
                 QStringLiteral("custom:profile:HF"));
    }

    void modelScopeOfAnEncodedNameCarriesNoGroupSeparator() {
        // '/' would fan the favourites key out into nested settings groups.
        QVERIFY(ProviderProfileStore::save(
            makeProfile(QStringLiteral("HF / local"), QStringLiteral("custom"),
                        QStringLiteral("http://host/v1"), QString()),
            QStringLiteral("k")));
        const QString scope =
            ProviderProfileStore::modelScopeIdForProfile(QStringLiteral("HF / local"));
        QCOMPARE(scope, QStringLiteral("custom:profile:HF %2F local"));
        QVERIFY(!scope.contains(QLatin1Char('/')));
    }

    void nameMatchingReportsAdHocConfigurations() {
        QVERIFY(ProviderProfileStore::save(
            makeProfile(QStringLiteral("HF Router"), QStringLiteral("custom"),
                        QStringLiteral("https://router.example/v1"),
                        QStringLiteral("a/model")),
            QStringLiteral("hf-token")));

        QCOMPARE(ProviderProfileStore::nameMatching(
                     QStringLiteral("custom"),
                     QStringLiteral("https://router.example/v1"),
                     QStringLiteral("a/model"), QStringLiteral("hf-token")),
                 QStringLiteral("HF Router"));
        // Any edited field means ad-hoc.
        QVERIFY(ProviderProfileStore::nameMatching(
                    QStringLiteral("custom"),
                    QStringLiteral("https://router.example/v1"),
                    QStringLiteral("a/model"), QStringLiteral("other-token"))
                    .isEmpty());
        QVERIFY(ProviderProfileStore::nameMatching(
                    QStringLiteral("custom"),
                    QStringLiteral("https://other.example/v1"),
                    QStringLiteral("a/model"), QStringLiteral("hf-token"))
                    .isEmpty());
        QVERIFY(ProviderProfileStore::nameMatching(
                    QStringLiteral("openai"),
                    QStringLiteral("https://router.example/v1"),
                    QStringLiteral("a/model"), QStringLiteral("hf-token"))
                    .isEmpty());
    }
};

QTEST_APPLESS_MAIN(TestProviderProfileStore)
#include "test_provider_profile_store.moc"
