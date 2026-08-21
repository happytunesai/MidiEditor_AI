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
 * v2.3 review additions:
 *   5. remove() takes the profile's ENDPOINT-scoped state with it (favourites
 *      + cached model list), and never touches the provider-wide scope that
 *      non-custom profiles share.
 *   6. A name whose encoding would overrun the settings backend's key-name
 *      limit still saves, lists and loads - and save() reports the truth.
 *   7. nameMatchingEndpoint() lets a live selection outrank the stored hint
 *      when two profiles describe one endpoint.
 *
 * Uses the AppPaths test seam - the developer's real settings scope must
 * never be touched (TESTWIPE class). The model-list cache is a file under
 * QStandardPaths, so the cases below run with test mode enabled.
 */

#include <QtTest/QtTest>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QSettings>
#include <QStandardPaths>

#include "../src/ai/ModelFavorites.h"
#include "../src/ai/ModelListCache.h"
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

// One cached model entry in the shape ModelListCache stores.
QJsonArray oneModel(const QString &id)
{
    QJsonObject m;
    m.insert(QStringLiteral("id"), id);
    m.insert(QStringLiteral("contextWindow"), 8192);
    QJsonArray arr;
    arr.append(m);
    return arr;
}

} // namespace

class TestProviderProfileStore : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        AppPaths::setSettingsScopeForTests(QStringLiteral("MidiEditorTest"),
                                           QStringLiteral("ProviderProfileStoreTest"));
        // ModelListCache is a file under QStandardPaths, not a settings key:
        // test mode keeps it out of the developer's application data.
        QStandardPaths::setTestModeEnabled(true);
    }

    void init() {
        // Every case starts on an empty store - the cases below assert on
        // exact list contents.
        AppPaths::settings()->clear();
        QFile::remove(ModelListCache::cacheFilePath());
    }

    void cleanupTestCase() {
        AppPaths::settings()->clear();
        QFile::remove(ModelListCache::cacheFilePath());
        QStandardPaths::setTestModeEnabled(false);
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

    // --- 7. what the provider DROPDOWNS rely on ---------------------------
    //
    // Both wiring sites (MidiPilotWidget footer, AiSettingsWidget page) list
    // stored CUSTOM profiles as first-class entries in the provider combo. The
    // rules below are the store-level half of that contract; the widgets only
    // add item data and blockSignals on top.

    void onlyCustomProfilesBecomeProviderComboEntries() {
        QVERIFY(ProviderProfileStore::save(
            makeProfile(QStringLiteral("HF"), QStringLiteral("custom"),
                        QStringLiteral("https://router.example/v1"),
                        QStringLiteral("a/model")),
            QStringLiteral("hf-token")));
        QVERIFY(ProviderProfileStore::save(
            makeProfile(QStringLiteral("Work"), QStringLiteral("openai"),
                        QStringLiteral("https://api.openai.com/v1"),
                        QStringLiteral("gpt-4o")),
            QStringLiteral("openai-key")));

        // The combos filter on the stored provider: a built-in provider's
        // profile would be a second "OpenAI" line for the same endpoint.
        QStringList entries;
        for (const QString &n : ProviderProfileStore::profileNames()) {
            bool ok = false;
            const Profile p = ProviderProfileStore::load(n, &ok);
            if (ok && p.provider == QStringLiteral("custom"))
                entries.append(p.name);
        }
        QCOMPARE(entries, (QStringList{QStringLiteral("HF")}));
    }

    void aProfileEntryResolvesToTheCustomProviderNeverToItsName() {
        // The combo item carries the NAME, but everything downstream must see
        // provider "custom" - a display name in AI/provider would reach the
        // client and the request builder.
        QVERIFY(ProviderProfileStore::save(
            makeProfile(QStringLiteral("HF"), QStringLiteral("custom"),
                        QStringLiteral("https://router.example/v1"),
                        QStringLiteral("a/model")),
            QStringLiteral("hf-token")));
        QVERIFY(ProviderProfileStore::apply(QStringLiteral("HF")));

        auto s = AppPaths::settings();
        QCOMPARE(s->value(QStringLiteral("AI/provider")).toString(),
                 QStringLiteral("custom"));
        QCOMPARE(s->value(QStringLiteral("AI/api_base_url")).toString(),
                 QStringLiteral("https://router.example/v1"));
        QCOMPARE(s->value(QStringLiteral("AI/model")).toString(),
                 QStringLiteral("a/model"));
    }

    void theProviderComboKeepsTheProfileAfterAModelChange() {
        // Display rule: the provider combo is ENDPOINT-based, so picking
        // another model keeps showing "HF" while the profile picker next to it
        // honestly falls back to "(No profile)".
        QVERIFY(ProviderProfileStore::save(
            makeProfile(QStringLiteral("HF"), QStringLiteral("custom"),
                        QStringLiteral("https://router.example/v1"),
                        QStringLiteral("a/model")),
            QStringLiteral("hf-token")));
        QVERIFY(ProviderProfileStore::apply(QStringLiteral("HF")));

        auto s = AppPaths::settings();
        s->setValue(QStringLiteral("AI/model"), QStringLiteral("another/model"));

        QVERIFY(ProviderProfileStore::activeProfileName().isEmpty());
        QCOMPARE(ProviderProfileStore::nameMatchingEndpoint(
                     s->value(QStringLiteral("AI/provider")).toString(),
                     s->value(QStringLiteral("AI/api_base_url")).toString(),
                     s->value(QStringLiteral("AI/api_key")).toString()),
                 QStringLiteral("HF"));

        // A different endpoint is a different entry - not "HF" with a caveat.
        QVERIFY(ProviderProfileStore::nameMatchingEndpoint(
                    QStringLiteral("custom"),
                    QStringLiteral("https://elsewhere.example/v1"),
                    QStringLiteral("hf-token")).isEmpty());
    }

    void savingTheLiveEndpointAsAProfileMakesItTheActiveOne() {
        // "Save as..." must mark the new profile active, so the scoped
        // favourites and model list take effect without re-applying it. Two
        // profiles describing the SAME endpoint make the difference visible:
        // without the hint the alphabetically first one would win.
        auto s = AppPaths::settings();
        s->setValue(QStringLiteral("AI/provider"), QStringLiteral("custom"));
        s->setValue(QStringLiteral("AI/api_base_url"),
                    QStringLiteral("https://router.example/v1"));
        s->setValue(QStringLiteral("AI/api_key"), QStringLiteral("hf-token"));
        s->setValue(QStringLiteral("AI/model"), QStringLiteral("a/model"));

        // Ad-hoc so far: no profile, shared "custom" scope.
        QVERIFY(ProviderProfileStore::activeProfileName().isEmpty());
        QCOMPARE(ProviderProfileStore::activeModelScopeId(),
                 QStringLiteral("custom"));

        QVERIFY(ProviderProfileStore::save(
            makeProfile(QStringLiteral("AAA earlier"), QStringLiteral("custom"),
                        QStringLiteral("https://router.example/v1"),
                        QStringLiteral("a/model")),
            QStringLiteral("hf-token")));
        QVERIFY(ProviderProfileStore::save(
            makeProfile(QStringLiteral("ZZZ just saved"), QStringLiteral("custom"),
                        QStringLiteral("https://router.example/v1"),
                        QStringLiteral("a/model")),
            QStringLiteral("hf-token")));
        ProviderProfileStore::setActiveProfileHint(QStringLiteral("ZZZ just saved"));

        QCOMPARE(ProviderProfileStore::activeProfileName(),
                 QStringLiteral("ZZZ just saved"));
        QCOMPARE(ProviderProfileStore::activeModelScopeId(),
                 QStringLiteral("custom:profile:ZZZ just saved"));
    }

    void aKeylessLocalProfileKeepsItsOwnScope() {
        // A keyless local endpoint (llama.cpp, LM Studio) is a first-class
        // Custom profile, so it must own its favourites and model list exactly
        // like a keyed one. The match survives the empty key because
        // matchesEndpoint() compares apiKeyFor(name) with the ACTIVE key and
        // apply() always writes AI/api_key - including the empty value. Only
        // the per-provider memory refresh is skipped for a keyless profile.
        auto s = AppPaths::settings();
        s->setValue(QStringLiteral("AI/api_key/custom"), QStringLiteral("hf-token"));

        QVERIFY(ProviderProfileStore::save(
            makeProfile(QStringLiteral("Local llama"), QStringLiteral("custom"),
                        QStringLiteral("http://localhost:8080/v1"),
                        QStringLiteral("b-model")),
            QString()));
        QVERIFY(ProviderProfileStore::save(
            makeProfile(QStringLiteral("HF"), QStringLiteral("custom"),
                        QStringLiteral("https://router.example/v1"),
                        QStringLiteral("a/model")),
            QStringLiteral("hf-token")));

        QVERIFY(ProviderProfileStore::apply(QStringLiteral("Local llama")));
        QVERIFY(s->value(QStringLiteral("AI/api_key")).toString().isEmpty());
        QCOMPARE(ProviderProfileStore::activeProfileName(),
                 QStringLiteral("Local llama"));
        QCOMPARE(ProviderProfileStore::activeModelScopeId(),
                 QStringLiteral("custom:profile:Local llama"));
        // The remembered cloud key is still there - a local profile must not
        // erase it - and it does NOT drag the match to the keyed profile.
        QCOMPARE(s->value(QStringLiteral("AI/api_key/custom")).toString(),
                 QStringLiteral("hf-token"));

        // Away to the keyed endpoint and back: both keep their own scope.
        QVERIFY(ProviderProfileStore::apply(QStringLiteral("HF")));
        QCOMPARE(ProviderProfileStore::activeModelScopeId(),
                 QStringLiteral("custom:profile:HF"));
        QVERIFY(ProviderProfileStore::apply(QStringLiteral("Local llama")));
        QCOMPARE(ProviderProfileStore::activeModelScopeId(),
                 QStringLiteral("custom:profile:Local llama"));
    }

    void deletingTheActiveProfileFallsBackToTheAdHocScope() {
        // The mirror image: the endpoint keeps working, it just stops being a
        // named one, so its model list and favourites return to "custom".
        QVERIFY(ProviderProfileStore::save(
            makeProfile(QStringLiteral("HF"), QStringLiteral("custom"),
                        QStringLiteral("https://router.example/v1"),
                        QStringLiteral("a/model")),
            QStringLiteral("hf-token")));
        QVERIFY(ProviderProfileStore::apply(QStringLiteral("HF")));
        QCOMPARE(ProviderProfileStore::activeModelScopeId(),
                 QStringLiteral("custom:profile:HF"));

        QVERIFY(ProviderProfileStore::remove(QStringLiteral("HF")));

        auto s = AppPaths::settings();
        QCOMPARE(s->value(QStringLiteral("AI/api_base_url")).toString(),
                 QStringLiteral("https://router.example/v1"));
        QVERIFY(ProviderProfileStore::activeProfileName().isEmpty());
        QCOMPARE(ProviderProfileStore::activeModelScopeId(),
                 QStringLiteral("custom"));
    }

    // --- 8. deleting a profile is a COMPLETE deletion ----------------------

    void deletingAProfileTakesItsScopedStateWithIt() {
        QVERIFY(ProviderProfileStore::save(
            makeProfile(QStringLiteral("HF"), QStringLiteral("custom"),
                        QStringLiteral("https://router.example/v1"),
                        QStringLiteral("a/model")),
            QStringLiteral("hf-token")));
        const QString scope =
            ProviderProfileStore::modelScopeIdForProfile(QStringLiteral("HF"));
        QCOMPARE(scope, QStringLiteral("custom:profile:HF"));

        ModelFavorites::setFavorites(scope, {QStringLiteral("a/model")});
        ModelListCache::store(scope, oneModel(QStringLiteral("a/model")));
        QVERIFY(ModelFavorites::hasFavorites(scope));
        QCOMPARE(ModelListCache::models(scope).size(), 1);

        QVERIFY(ProviderProfileStore::remove(QStringLiteral("HF")));

        // The scope is derived from the NAME. Leaving its state behind means a
        // profile created later under the same name silently inherits another
        // server's model list and favourites.
        QVERIFY(!ModelFavorites::hasFavorites(scope));
        QVERIFY(ModelListCache::models(scope).isEmpty());
        QVERIFY(ModelListCache::isStale(scope));

        // The same name, a different server: nothing carried over.
        QVERIFY(ProviderProfileStore::save(
            makeProfile(QStringLiteral("HF"), QStringLiteral("custom"),
                        QStringLiteral("https://elsewhere.example/v1"),
                        QString()),
            QStringLiteral("other-token")));
        QCOMPARE(ProviderProfileStore::modelScopeIdForProfile(QStringLiteral("HF")),
                 scope);
        QVERIFY(ModelFavorites::favorites(scope).isEmpty());
        QVERIFY(ModelListCache::models(scope).isEmpty());
    }

    void deletingAProfileLeavesTheSharedProviderScopeAlone() {
        // A non-custom profile shares "openai" with every other OpenAI
        // configuration - deleting it must not wipe favourites that were never
        // its own. Same for the ad-hoc "custom" bucket.
        QVERIFY(ProviderProfileStore::save(
            makeProfile(QStringLiteral("Work"), QStringLiteral("openai"),
                        QStringLiteral("https://api.openai.com/v1"),
                        QStringLiteral("gpt-4o")),
            QStringLiteral("openai-key")));
        QCOMPARE(ProviderProfileStore::modelScopeIdForProfile(QStringLiteral("Work")),
                 QStringLiteral("openai"));

        ModelFavorites::setFavorites(QStringLiteral("openai"),
                                     {QStringLiteral("gpt-4o")});
        ModelListCache::store(QStringLiteral("openai"),
                              oneModel(QStringLiteral("gpt-4o")));
        ModelFavorites::setFavorites(QStringLiteral("custom"),
                                     {QStringLiteral("ad-hoc/model")});

        QVERIFY(ProviderProfileStore::remove(QStringLiteral("Work")));

        QVERIFY(ModelFavorites::hasFavorites(QStringLiteral("openai")));
        QCOMPARE(ModelListCache::models(QStringLiteral("openai")).size(), 1);
        QVERIFY(ModelFavorites::hasFavorites(QStringLiteral("custom")));
    }

    void forgettingAnUncachedScopeIsANoOp() {
        ModelListCache::store(QStringLiteral("openai"),
                              oneModel(QStringLiteral("gpt-4o")));
        ModelListCache::forget(QStringLiteral("custom:profile:never cached"));
        ModelListCache::forget(QString());
        // Only the named scope disappears - the file keeps everything else.
        QCOMPARE(ModelListCache::models(QStringLiteral("openai")).size(), 1);
        ModelListCache::forget(QStringLiteral("openai"));
        QVERIFY(ModelListCache::models(QStringLiteral("openai")).isEmpty());
    }

    // --- 9. ids the settings backend actually accepts ----------------------

    void aNonAsciiNameStaysReachableAndSaveReportsTheTruth() {
        // A full-length non-Latin name percent-encodes to several hundred
        // characters - past what a Windows registry key name takes. save() used
        // to report success for a profile that was never written, leaving its
        // API key behind as an orphan.
        const QString name(ProviderProfileStore::maxNameLength(), QChar(0x4E2D));
        const QString id = ProviderProfileStore::encodeName(name);
        QVERIFY(!id.isEmpty());
        QVERIFY(id.size() <= ProviderProfileStore::maxEncodedIdLength());
        // Deterministic, and a shortened id can never look like a plain one.
        QCOMPARE(ProviderProfileStore::encodeName(name), id);
        QVERIFY(id.contains(QLatin1Char('~')));

        QVERIFY(ProviderProfileStore::save(
            makeProfile(name, QStringLiteral("custom"),
                        QStringLiteral("https://router.example/v1"),
                        QStringLiteral("a/model")),
            QStringLiteral("hf-token")));

        // Reachable under the display name, which is kept verbatim.
        QVERIFY(ProviderProfileStore::exists(name));
        QCOMPARE(ProviderProfileStore::profileNames(), (QStringList{name}));
        bool ok = false;
        const Profile p = ProviderProfileStore::load(name, &ok);
        QVERIFY(ok);
        QCOMPARE(p.name, name);
        QCOMPARE(p.baseUrl, QStringLiteral("https://router.example/v1"));
        QCOMPARE(ProviderProfileStore::apiKeyFor(name),
                 QStringLiteral("hf-token"));
        QVERIFY(ProviderProfileStore::apply(name));
        QCOMPARE(ProviderProfileStore::activeProfileName(), name);

        // ... and it can be deleted again, key included.
        QVERIFY(ProviderProfileStore::remove(name));
        QVERIFY(ProviderProfileStore::profileNames().isEmpty());
        QVERIFY(ProviderProfileStore::apiKeyFor(name).isEmpty());
    }

    void twoLongNamesGetDifferentIds() {
        const QString a = QString(ProviderProfileStore::maxNameLength() - 1,
                                  QChar(0x4E2D)) + QStringLiteral("A");
        const QString b = QString(ProviderProfileStore::maxNameLength() - 1,
                                  QChar(0x4E2D)) + QStringLiteral("B");
        QVERIFY(ProviderProfileStore::encodeName(a)
                != ProviderProfileStore::encodeName(b));

        QVERIFY(ProviderProfileStore::save(
            makeProfile(a, QStringLiteral("custom"),
                        QStringLiteral("http://host-a/v1"), QString()),
            QStringLiteral("k-a")));
        QVERIFY(ProviderProfileStore::save(
            makeProfile(b, QStringLiteral("custom"),
                        QStringLiteral("http://host-b/v1"), QString()),
            QStringLiteral("k-b")));
        QCOMPARE(ProviderProfileStore::profileNames().size(), 2);
        QCOMPARE(ProviderProfileStore::apiKeyFor(a), QStringLiteral("k-a"));
        QCOMPARE(ProviderProfileStore::apiKeyFor(b), QStringLiteral("k-b"));
        QVERIFY(ProviderProfileStore::modelScopeIdForProfile(a)
                != ProviderProfileStore::modelScopeIdForProfile(b));
    }

    // --- 10. two profiles, one endpoint: both pickers must agree -----------

    void theLiveSelectionOutranksTheStoredHint() {
        QVERIFY(ProviderProfileStore::save(
            makeProfile(QStringLiteral("AAA"), QStringLiteral("custom"),
                        QStringLiteral("https://router.example/v1"),
                        QStringLiteral("a/model")),
            QStringLiteral("hf-token")));
        QVERIFY(ProviderProfileStore::save(
            makeProfile(QStringLiteral("BBB"), QStringLiteral("custom"),
                        QStringLiteral("https://router.example/v1"),
                        QStringLiteral("b/model")),
            QStringLiteral("hf-token")));

        QVERIFY(ProviderProfileStore::apply(QStringLiteral("AAA")));
        auto s = AppPaths::settings();
        s->setValue(QStringLiteral("AI/model"), QStringLiteral("b/model"));

        // The exact match is BBB now - that is what the profile picker shows.
        QCOMPARE(ProviderProfileStore::activeProfileName(),
                 QStringLiteral("BBB"));
        // The endpoint lookup alone still answers with the stored hint, which
        // is exactly how the two pickers ended up naming different profiles.
        QCOMPARE(ProviderProfileStore::nameMatchingEndpoint(
                     QStringLiteral("custom"),
                     QStringLiteral("https://router.example/v1"),
                     QStringLiteral("hf-token")),
                 QStringLiteral("AAA"));
        // Told what the caller currently shows, it agrees.
        QCOMPARE(ProviderProfileStore::nameMatchingEndpoint(
                     QStringLiteral("custom"),
                     QStringLiteral("https://router.example/v1"),
                     QStringLiteral("hf-token"), QStringLiteral("BBB")),
                 QStringLiteral("BBB"));
        // A preference that does not describe this endpoint is ignored, not
        // echoed back: the picker must never name a profile you are not on.
        QCOMPARE(ProviderProfileStore::nameMatchingEndpoint(
                     QStringLiteral("custom"),
                     QStringLiteral("https://router.example/v1"),
                     QStringLiteral("hf-token"),
                     QStringLiteral("Not on this machine")),
                 QStringLiteral("AAA"));
        QVERIFY(ProviderProfileStore::nameMatchingEndpoint(
                    QStringLiteral("custom"),
                    QStringLiteral("https://elsewhere.example/v1"),
                    QStringLiteral("hf-token"), QStringLiteral("BBB"))
                    .isEmpty());
    }
};

QTEST_APPLESS_MAIN(TestProviderProfileStore)
#include "test_provider_profile_store.moc"
