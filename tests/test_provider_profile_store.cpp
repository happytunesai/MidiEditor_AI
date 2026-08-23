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
 * v2.3 final review additions:
 *   8. Endpoints differing only in trailing slashes are one endpoint (L3).
 *   9. The per-provider key memory: an empty key the user cleared on purpose
 *      is representable and erases the memory, while a keyless profile leaves
 *      it alone; a remembered cloud token never re-attaches to a Custom
 *      endpoint on this machine (H4).
 *  10. The state table behind the footer's ad-hoc "Custom" entry (L11).
 *  11. ModelListCache::forget() reports whether the scope is really gone (L4).
 *
 * v2.3 owner smoke additions:
 *  12. BOTH connection pickers list every saved profile - the settings page's
 *      provider dropdown no longer filters to the Custom ones while the
 *      profile row above it offers them all.
 *  13. The ad-hoc "Custom" endpoint is by definition the one no stored profile
 *      describes, so it resolves to no profile name and the picker's
 *      endpoint-derived selection cannot snap off "Custom".
 *
 * v2.3 owner smoke round 2 additions:
 *  15. Selection INTENT: a connection picker's selection is state
 *      (Provider(X) or Profile(name)), and the stored hint is the ONLY thing
 *      it may be derived from - validatedActiveProfileName() on the way in,
 *      hintForSelection() on the way out. Neither scans the profiles, so a
 *      profile that describes the same endpoint as a built-in provider can no
 *      longer capture an explicit provider pick.
 *
 * v2.3 owner smoke round 3 additions:
 *  16. A key POURED from a profile is that profile's: leaving the profile must
 *      not write it into the provider's key memory (which silently replaced the
 *      key kept for the plain provider entry). A key the user typed still is.
 *  17. The plain "Custom" entry is listed whenever the SELECTION names it, not
 *      only when the endpoint scan calls it ad-hoc - a stored profile
 *      describing the live custom endpoint used to hide the entry the picker
 *      was on, leaving it displaying "OpenAI".
 *
 * v2.3 owner smoke round 4 additions:
 *  18. No endpoint, nothing: ad-hoc Custom without a base URL offers neither a
 *      remembered key nor a cached model list. A key and a catalogue belong to
 *      a SERVER, and the ones left over from the last hand-typed endpoint used
 *      to attach themselves to whatever URL was typed next. Display only -
 *      nothing is deleted.
 *  19. Naming an ad-hoc endpoint MIGRATES its leftovers into the profile (the
 *      remembered URL, AI/api_key/custom when it is this profile's key, the
 *      shared "custom" model list and favourites) instead of leaving a second,
 *      stale copy of that server behind.
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
        // The EMPTY per-provider slot is filled, so a later switch to
        // "custom" finds a key (a remembered one would not be overwritten -
        // see applyNeverOverwritesARememberedProviderKey).
        QCOMPARE(s->value(QStringLiteral("AI/api_key/custom")).toString(),
                 QStringLiteral("hf-token"));
        // The other provider's remembered key is untouched.
        QCOMPARE(s->value(QStringLiteral("AI/api_key/openai")).toString(),
                 QStringLiteral("openai-key"));
        QCOMPARE(ProviderProfileStore::activeProfileName(),
                 QStringLiteral("HF Router"));
    }

    void applyNeverOverwritesARememberedProviderKey() {
        auto s = AppPaths::settings();
        // The user's own key, remembered for the provider before any profile
        // comes into play.
        s->setValue(QStringLiteral("AI/api_key/openai"),
                    QStringLiteral("personal-key"));

        QVERIFY(ProviderProfileStore::save(
            makeProfile(QStringLiteral("work"), QStringLiteral("openai"),
                        QStringLiteral("https://api.openai.com/v1"),
                        QStringLiteral("gpt-4o")),
            QStringLiteral("work-key")));
        QVERIFY(ProviderProfileStore::apply(QStringLiteral("work")));

        // The profile's key becomes the ACTIVE key...
        QCOMPARE(s->value(QStringLiteral("AI/api_key")).toString(),
                 QStringLiteral("work-key"));
        // ...but the provider's remembered key survives: leaving the profile
        // for the plain provider must find personal-key again, not work-key.
        QCOMPARE(s->value(QStringLiteral("AI/api_key/openai")).toString(),
                 QStringLiteral("personal-key"));
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

    void everySavedProfileBecomesAFooterProviderComboEntry() {
        // v2.3 review (M3): the FOOTER lists every saved profile, whichever
        // provider it was saved with - a profile pins the key and the model
        // too, and the gear menu already reports "Provider profile saved: X".
        // The entry's own provider id is what the combo must carry, so the
        // preset writer and the fallback lookup see the real provider.
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

        QStringList entries;
        QStringList providerIds;
        for (const QString &n : ProviderProfileStore::profileNames()) {
            bool ok = false;
            const Profile p = ProviderProfileStore::load(n, &ok);
            if (!ok)
                continue;
            entries.append(p.name);
            providerIds.append(p.provider);
        }
        QCOMPARE(entries,
                 (QStringList{QStringLiteral("HF"), QStringLiteral("Work")}));
        QCOMPARE(providerIds,
                 (QStringList{QStringLiteral("custom"),
                              QStringLiteral("openai")}));

        // Applying the non-custom one behaves exactly like applying it from
        // the settings page: the same four keys, the same hint.
        QVERIFY(ProviderProfileStore::apply(QStringLiteral("Work")));
        auto s = AppPaths::settings();
        QCOMPARE(s->value(QStringLiteral("AI/provider")).toString(),
                 QStringLiteral("openai"));
        QCOMPARE(s->value(QStringLiteral("AI/api_key")).toString(),
                 QStringLiteral("openai-key"));
        QCOMPARE(s->value(QStringLiteral("AI/model")).toString(),
                 QStringLiteral("gpt-4o"));
        QCOMPARE(ProviderProfileStore::activeProfileName(),
                 QStringLiteral("Work"));
        // ... and it is findable as an endpoint, so the footer can select it.
        QCOMPARE(ProviderProfileStore::nameMatchingEndpoint(
                     QStringLiteral("openai"),
                     QStringLiteral("https://api.openai.com/v1"),
                     QStringLiteral("openai-key")),
                 QStringLiteral("Work"));
    }

    void theSettingsPageProviderComboListsEveryProfileToo() {
        // v2.3 owner smoke (parity bug): the SETTINGS PAGE used to filter this
        // list to the Custom profiles. An OpenAI profile was therefore absent
        // from the Provider dropdown while the "Provider profile" row directly
        // above it offered the very same profile and correctly jumped to
        // OpenAI when it was picked - two rows on one page disagreeing about
        // what exists. Both pickers now build from the SAME unfiltered list,
        // and every entry carries the profile's OWN provider id.
        QVERIFY(ProviderProfileStore::save(
            makeProfile(QStringLiteral("HF"), QStringLiteral("custom"),
                        QStringLiteral("https://router.example/v1"),
                        QStringLiteral("a/model")),
            QStringLiteral("hf-token")));
        QVERIFY(ProviderProfileStore::save(
            makeProfile(QStringLiteral("test"), QStringLiteral("openai"),
                        QStringLiteral("https://api.openai.com/v1"),
                        QStringLiteral("gpt-4o")),
            QStringLiteral("openai-key")));

        QStringList entries;
        QStringList providerIds;
        for (const QString &n : ProviderProfileStore::profileNames()) {
            bool ok = false;
            const Profile p = ProviderProfileStore::load(n, &ok);
            if (!ok)
                continue;
            entries.append(p.name);
            providerIds.append(p.provider);
        }
        QCOMPARE(entries,
                 (QStringList{QStringLiteral("HF"), QStringLiteral("test")}));
        QCOMPARE(providerIds,
                 (QStringList{QStringLiteral("custom"),
                              QStringLiteral("openai")}));

        // Picking the OpenAI profile's entry goes through the same apply path
        // as picking it in the profile row, so both land on the same four
        // values - the page stages them instead of writing them, but the
        // profile they come from is this one either way.
        bool ok = false;
        const Profile picked = ProviderProfileStore::load(QStringLiteral("test"),
                                                          &ok);
        QVERIFY(ok);
        QCOMPARE(picked.provider, QStringLiteral("openai"));
        QCOMPARE(ProviderProfileStore::apiKeyFor(QStringLiteral("test")),
                 QStringLiteral("openai-key"));
        // ... and it is findable as an endpoint, so the dropdown can select it.
        QCOMPARE(ProviderProfileStore::nameMatchingEndpoint(
                     picked.provider, picked.baseUrl,
                     ProviderProfileStore::apiKeyFor(QStringLiteral("test"))),
                 QStringLiteral("test"));
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

    // --- 11. trailing slashes are not a different server (v2.3 review L3) ---

    void aTrailingSlashDoesNotHideAProfile() {
        // The pickers normalise the endpoint before they compare; a profile
        // stored with the slash still typed could never be matched again.
        QVERIFY(ProviderProfileStore::save(
            makeProfile(QStringLiteral("Slashed"), QStringLiteral("custom"),
                        QStringLiteral("https://router.example/v1/"),
                        QStringLiteral("a/model")),
            QStringLiteral("hf-token")));

        QVERIFY(ProviderProfileStore::matchesEndpoint(
            QStringLiteral("Slashed"), QStringLiteral("custom"),
            QStringLiteral("https://router.example/v1"),
            QStringLiteral("hf-token")));
        QCOMPARE(ProviderProfileStore::nameMatchingEndpoint(
                     QStringLiteral("custom"),
                     QStringLiteral("https://router.example/v1"),
                     QStringLiteral("hf-token")),
                 QStringLiteral("Slashed"));
        // ... and therefore its own model-list scope, not the ad-hoc bucket.
        QCOMPARE(ProviderProfileStore::modelScopeId(
                     QStringLiteral("custom"),
                     QStringLiteral("https://router.example/v1"),
                     QStringLiteral("hf-token")),
                 QStringLiteral("custom:profile:Slashed"));
        // A genuinely different path is still a different endpoint.
        QVERIFY(!ProviderProfileStore::matchesEndpoint(
            QStringLiteral("Slashed"), QStringLiteral("custom"),
            QStringLiteral("https://router.example/v2"),
            QStringLiteral("hf-token")));
    }

    // --- 12. the per-provider key memory (v2.3 review H4) ------------------
    //
    // The empty key used to be unrepresentable: every writer skipped it, so a
    // key the user cleared came back on the next provider round-trip and was
    // sent again. The two halves of the rule are pure functions, pinned here.

    void clearingAKeyOnPurposeErasesTheMemory() {
        using Action = ProviderProfileStore::KeyMemoryAction;

        // A key in the field is always remembered.
        QCOMPARE(ProviderProfileStore::keyMemoryActionOnLeave(
                     QStringLiteral("sk-live"), false), Action::Store);
        QCOMPARE(ProviderProfileStore::keyMemoryActionOnLeave(
                     QStringLiteral("sk-live"), true), Action::Store);
        // Scenario A: the user emptied the field here - the memory must go, or
        // the deleted key resurrects on the next switch back to this provider.
        QCOMPARE(ProviderProfileStore::keyMemoryActionOnLeave(QString(), true),
                 Action::Erase);
        QCOMPARE(ProviderProfileStore::keyMemoryActionOnLeave(
                     QStringLiteral("   "), true), Action::Erase);
        // F1: an empty field the user never touched (a keyless local profile
        // was poured into it) leaves the remembered cloud key alone.
        QCOMPARE(ProviderProfileStore::keyMemoryActionOnLeave(QString(), false),
                 Action::Keep);
    }

    void aLocalCustomEndpointNeverGetsTheRememberedCloudKey() {
        // Scenario B: AI/api_key/custom holds a cloud token, the live Custom
        // endpoint is a server on this machine with an empty key. Custom ->
        // Ollama -> Custom must not re-attach the token to the local endpoint.
        QCOMPARE(ProviderProfileStore::keyMemoryOnEnter(
                     QStringLiteral("custom"), QStringLiteral("cloud-token"),
                     /*endpointNeedsKey*/ false,
                     QStringLiteral("http://localhost:8080/v1")),
                 QString());
        // The same provider pointing at a remote endpoint keeps its memory.
        QCOMPARE(ProviderProfileStore::keyMemoryOnEnter(
                     QStringLiteral("custom"), QStringLiteral("cloud-token"),
                     /*endpointNeedsKey*/ true,
                     QStringLiteral("https://router.example/v1")),
                 QStringLiteral("cloud-token"));
        // Only Custom is filtered: its endpoint is the user-defined one. An
        // Ollama behind an auth proxy still gets its remembered key back, even
        // though the provider never REQUIRES one.
        QCOMPARE(ProviderProfileStore::keyMemoryOnEnter(
                     QStringLiteral("ollama"), QStringLiteral("proxy-key"),
                     /*endpointNeedsKey*/ false,
                     QStringLiteral("http://otherhost:11434/v1")),
                 QStringLiteral("proxy-key"));
        QCOMPARE(ProviderProfileStore::keyMemoryOnEnter(
                     QStringLiteral("openai"), QStringLiteral("sk-live"), true,
                     QStringLiteral("https://api.openai.com/v1")),
                 QStringLiteral("sk-live"));
        // Case-insensitive on the provider id, and an absent memory stays
        // absent rather than becoming a stray empty value.
        QCOMPARE(ProviderProfileStore::keyMemoryOnEnter(
                     QStringLiteral("Custom"), QStringLiteral("cloud-token"),
                     false, QStringLiteral("http://127.0.0.1:8080/v1")),
                 QString());
        QCOMPARE(ProviderProfileStore::keyMemoryOnEnter(
                     QStringLiteral("openai"), QString(), true,
                     QStringLiteral("https://api.openai.com/v1")),
                 QString());
    }

    // --- 13. when the footer offers the ad-hoc "Custom" entry (L11) --------

    void theAdHocCustomEntryIsOfferedExactlyWhenItIsReal() {
        const QStringList builtIns{
            QStringLiteral("https://api.openai.com/v1"),
            QStringLiteral("https://openrouter.ai/api/v1"),
            QStringLiteral("https://generativelanguage.googleapis.com/v1beta/openai"),
            QStringLiteral("http://localhost:11434/v1")};
        auto offer = [&builtIns](const QString &provider, const QString &url,
                                 const QString &match) {
            return ProviderProfileStore::shouldOfferAdHocCustomEntry(
                provider, url, match, builtIns);
        };

        // On an ad-hoc custom connection the entry IS the active one.
        QVERIFY(offer(QStringLiteral("custom"),
                      QStringLiteral("https://router.example/v1"), QString()));
        // ... unless a stored profile describes exactly this endpoint: that
        // profile's own entry represents it, a second one would be a decoy.
        QVERIFY(!offer(QStringLiteral("custom"),
                       QStringLiteral("https://router.example/v1"),
                       QStringLiteral("HF")));
        // A built-in provider on a profile endpoint: no ad-hoc entry either.
        QVERIFY(!offer(QStringLiteral("openai"),
                       QStringLiteral("https://api.openai.com/v1"),
                       QStringLiteral("Work")));
        // A built-in provider sitting on its own default URL configured
        // nothing - that URL is what switching provider writes.
        QVERIFY(!offer(QStringLiteral("openai"),
                       QStringLiteral("https://api.openai.com/v1"), QString()));
        QVERIFY(!offer(QStringLiteral("ollama"),
                       QStringLiteral("http://localhost:11434/v1"), QString()));
        // ... and a trailing slash does not make it a different URL.
        QVERIFY(!offer(QStringLiteral("ollama"),
                       QStringLiteral("http://localhost:11434/v1/"), QString()));
        // A non-default URL left behind by a built-in provider does count:
        // picking "Custom" would talk to exactly that server.
        QVERIFY(offer(QStringLiteral("ollama"),
                      QStringLiteral("http://otherhost:11434/v1"), QString()));
        // Nothing configured at all: no dead entry.
        QVERIFY(!offer(QStringLiteral("openai"), QString(), QString()));
        QVERIFY(!offer(QStringLiteral("openai"), QStringLiteral("   "),
                       QString()));
    }

    void theAdHocCustomEndpointIsTheOneNoProfileDescribes() {
        // v2.3 owner smoke: picking "Custom" in the settings page used to leave
        // the previous profile's URL and key in the fields. The endpoint then
        // still resolved to that profile, and the endpoint-derived selection
        // snapped straight back to it - "Custom" was unreachable.
        //
        // The page now loads a REMEMBERED ad-hoc endpoint instead, and this is
        // the store-level rule that makes the selection stay put: only an
        // endpoint that no stored profile describes may be remembered as the
        // ad-hoc one, and such an endpoint resolves to no profile - so
        // nameMatchingEndpoint() hands the picker an empty name and the fixed
        // "Custom" entry wins.
        QVERIFY(ProviderProfileStore::save(
            makeProfile(QStringLiteral("huggingface.co"), QStringLiteral("custom"),
                        QStringLiteral("https://router.huggingface.co/v1"),
                        QStringLiteral("a/model")),
            QStringLiteral("hf-token")));
        QVERIFY(ProviderProfileStore::apply(QStringLiteral("huggingface.co")));

        // The state the bug produced: the profile's own values still in the
        // fields. That IS the profile, and the picker is right to say so.
        QCOMPARE(ProviderProfileStore::nameMatchingEndpoint(
                     QStringLiteral("custom"),
                     QStringLiteral("https://router.huggingface.co/v1"),
                     QStringLiteral("hf-token")),
                 QStringLiteral("huggingface.co"));

        // The defined ad-hoc state: no endpoint at all (nothing remembered
        // yet), or a remembered endpoint no profile owns. Both resolve to no
        // profile even though the stored hint still names one, so nothing can
        // pull the selection off "Custom".
        QVERIFY(ProviderProfileStore::nameMatchingEndpoint(
                    QStringLiteral("custom"), QString(),
                    QStringLiteral("hf-token")).isEmpty());
        QVERIFY(ProviderProfileStore::nameMatchingEndpoint(
                    QStringLiteral("custom"),
                    QStringLiteral("http://localhost:8080/v1"),
                    QString()).isEmpty());
        // ... and the ad-hoc endpoint keeps the shared, pre-profile model-list
        // scope, so the Custom entry shows the ad-hoc model list.
        QCOMPARE(ProviderProfileStore::modelScopeId(
                     QStringLiteral("custom"),
                     QStringLiteral("http://localhost:8080/v1"), QString()),
                 QStringLiteral("custom"));

        // A remembered URL that a profile is LATER saved for stops being
        // ad-hoc: it then belongs to that profile's entry, and the picker
        // naming it is the honest answer, not a snap-back.
        QVERIFY(ProviderProfileStore::save(
            makeProfile(QStringLiteral("Local"), QStringLiteral("custom"),
                        QStringLiteral("http://localhost:8080/v1"), QString()),
            QString()));
        QCOMPARE(ProviderProfileStore::nameMatchingEndpoint(
                     QStringLiteral("custom"),
                     QStringLiteral("http://localhost:8080/v1"), QString()),
                 QStringLiteral("Local"));
    }

    // --- 14. forget() reports the truth (v2.3 review L4) -------------------

    void forgettingReportsWhetherTheScopeIsGone() {
        // Nothing cached at all - the scope IS gone, so this is a success.
        QVERIFY(ModelListCache::forget(QStringLiteral("custom:profile:never")));
        // An empty scope addresses nothing: a caller bug, never a deletion.
        QVERIFY(!ModelListCache::forget(QString()));

        ModelListCache::store(QStringLiteral("openai"),
                              oneModel(QStringLiteral("gpt-4o")));
        QVERIFY(ModelListCache::forget(QStringLiteral("custom:profile:never")));
        QCOMPARE(ModelListCache::models(QStringLiteral("openai")).size(), 1);
        QVERIFY(ModelListCache::forget(QStringLiteral("openai")));
        QVERIFY(ModelListCache::models(QStringLiteral("openai")).isEmpty());
    }

    // --- 15. selection intent: the hint is the ONLY derivation source ------
    //
    // The owner's second report: a profile named "test" on OpenAI's default
    // endpoint with the remembered OpenAI key. Picking the FIXED "OpenAI"
    // entry kept snapping back to "test", because both pickers re-derived
    // their selection by scanning every profile for one that describes the
    // live settings - and "test" always won that scan. The selection is now
    // state (Provider(X) or Profile(name)); these two functions are the only
    // bridge between that state and the stored hint, and neither scans.

    void ownersProfileSharesTheProvidersEndpoint() {
        // The exact shape of the report: same provider, same (default) URL,
        // same key as the plain OpenAI configuration.
        QVERIFY(ProviderProfileStore::save(
            makeProfile(QStringLiteral("test"), QStringLiteral("openai"),
                        QStringLiteral("https://api.openai.com/v1"),
                        QStringLiteral("gpt-5.4")),
            QStringLiteral("sk-owner")));

        // A scan still finds it - that is what the scan is for, and the two
        // remaining users of it (the ad-hoc Custom rules) depend on it.
        QCOMPARE(ProviderProfileStore::nameMatchingEndpoint(
                     QStringLiteral("openai"),
                     QStringLiteral("https://api.openai.com/v1"),
                     QStringLiteral("sk-owner")),
                 QStringLiteral("test"));

        // The picker's derivation does NOT: with no hint, this connection is
        // the plain provider, whatever profiles describe the same endpoint.
        ProviderProfileStore::setActiveProfileHint(QString());
        QVERIFY(ProviderProfileStore::validatedActiveProfileName(
                    QStringLiteral("openai"),
                    QStringLiteral("https://api.openai.com/v1"),
                    QStringLiteral("sk-owner")).isEmpty());
    }

    void pickingAFixedProviderWritesNoHintAndReopensOnIt() {
        QVERIFY(ProviderProfileStore::save(
            makeProfile(QStringLiteral("test"), QStringLiteral("openai"),
                        QStringLiteral("https://api.openai.com/v1"),
                        QStringLiteral("gpt-5.4")),
            QStringLiteral("sk-owner")));
        // The state before: the profile was applied, so the hint names it.
        QVERIFY(ProviderProfileStore::apply(QStringLiteral("test")));
        QCOMPARE(ProviderProfileStore::validatedActiveProfileName(
                     QStringLiteral("openai"),
                     QStringLiteral("https://api.openai.com/v1"),
                     QStringLiteral("sk-owner")),
                 QStringLiteral("test"));

        // The user picks the FIXED "OpenAI" entry: the selection state is
        // Provider(openai), so the page's accept() writes NO hint...
        ProviderProfileStore::setActiveProfileHint(
            ProviderProfileStore::hintForSelection(
                QString(), QStringLiteral("openai"),
                QStringLiteral("https://api.openai.com/v1"),
                QStringLiteral("sk-owner")));
        // ... and reopening derives no profile either. This is S1.
        QVERIFY(ProviderProfileStore::validatedActiveProfileName(
                    QStringLiteral("openai"),
                    QStringLiteral("https://api.openai.com/v1"),
                    QStringLiteral("sk-owner")).isEmpty());
    }

    void pickingAProfileSurvivesTheRoundTrip() {
        QVERIFY(ProviderProfileStore::save(
            makeProfile(QStringLiteral("test"), QStringLiteral("openai"),
                        QStringLiteral("https://api.openai.com/v1"),
                        QStringLiteral("gpt-5.4")),
            QStringLiteral("sk-owner")));
        // S2: the state is Profile("test") and the endpoint is still its own,
        // so the hint is written and read back unchanged.
        ProviderProfileStore::setActiveProfileHint(
            ProviderProfileStore::hintForSelection(
                QStringLiteral("test"), QStringLiteral("openai"),
                QStringLiteral("https://api.openai.com/v1"),
                QStringLiteral("sk-owner")));
        QCOMPARE(ProviderProfileStore::validatedActiveProfileName(
                     QStringLiteral("openai"),
                     QStringLiteral("https://api.openai.com/v1"),
                     QStringLiteral("sk-owner")),
                 QStringLiteral("test"));
    }

    void aModelChangeKeepsTheProfileButAnEndpointEditDoesNot() {
        QVERIFY(ProviderProfileStore::save(
            makeProfile(QStringLiteral("Local"), QStringLiteral("custom"),
                        QStringLiteral("http://localhost:8080/v1"),
                        QStringLiteral("local-model")),
            QString()));

        // S3, first half: the model is not part of endpoint identity, so the
        // selection - and the hint - survive picking another model. (The
        // model-sensitive "Provider profile" row is what falls back there.)
        QCOMPARE(ProviderProfileStore::hintForSelection(
                     QStringLiteral("Local"), QStringLiteral("custom"),
                     QStringLiteral("http://localhost:8080/v1"), QString()),
                 QStringLiteral("Local"));
        // Trailing slashes are the same endpoint, here too.
        QCOMPARE(ProviderProfileStore::hintForSelection(
                     QStringLiteral("Local"), QStringLiteral("custom"),
                     QStringLiteral("http://localhost:8080/v1/"), QString()),
                 QStringLiteral("Local"));

        // S3, second half: a URL, key or provider edit leaves the profile.
        QVERIFY(ProviderProfileStore::hintForSelection(
                    QStringLiteral("Local"), QStringLiteral("custom"),
                    QStringLiteral("http://localhost:9999/v1"), QString())
                    .isEmpty());
        QVERIFY(ProviderProfileStore::hintForSelection(
                    QStringLiteral("Local"), QStringLiteral("custom"),
                    QStringLiteral("http://localhost:8080/v1"),
                    QStringLiteral("sk-typed")).isEmpty());
        QVERIFY(ProviderProfileStore::hintForSelection(
                    QStringLiteral("Local"), QStringLiteral("openai"),
                    QStringLiteral("http://localhost:8080/v1"), QString())
                    .isEmpty());
        // The same rule on the reading side.
        ProviderProfileStore::setActiveProfileHint(QStringLiteral("Local"));
        QVERIFY(ProviderProfileStore::validatedActiveProfileName(
                    QStringLiteral("custom"),
                    QStringLiteral("http://localhost:9999/v1"), QString())
                    .isEmpty());
    }

    void aDeletedProfileDegradesToTheProviderNotToItsTwin() {
        // S7: two profiles on ONE endpoint. Deleting the selected one must not
        // hand the selection to its sibling - which is exactly what a scan
        // would do, in name order.
        QVERIFY(ProviderProfileStore::save(
            makeProfile(QStringLiteral("A twin"), QStringLiteral("custom"),
                        QStringLiteral("http://localhost:8080/v1"),
                        QStringLiteral("m")),
            QStringLiteral("k")));
        QVERIFY(ProviderProfileStore::save(
            makeProfile(QStringLiteral("B twin"), QStringLiteral("custom"),
                        QStringLiteral("http://localhost:8080/v1"),
                        QStringLiteral("m")),
            QStringLiteral("k")));

        // The hint decides between twins - no name-order accident.
        ProviderProfileStore::setActiveProfileHint(QStringLiteral("B twin"));
        QCOMPARE(ProviderProfileStore::validatedActiveProfileName(
                     QStringLiteral("custom"),
                     QStringLiteral("http://localhost:8080/v1"),
                     QStringLiteral("k")),
                 QStringLiteral("B twin"));

        QVERIFY(ProviderProfileStore::remove(QStringLiteral("B twin")));
        // remove() drops the hint that named it, and nothing derives the twin.
        QVERIFY(ProviderProfileStore::validatedActiveProfileName(
                    QStringLiteral("custom"),
                    QStringLiteral("http://localhost:8080/v1"),
                    QStringLiteral("k")).isEmpty());
        // Even a stale hint naming the deleted profile stays harmless.
        ProviderProfileStore::setActiveProfileHint(QStringLiteral("B twin"));
        QVERIFY(ProviderProfileStore::validatedActiveProfileName(
                    QStringLiteral("custom"),
                    QStringLiteral("http://localhost:8080/v1"),
                    QStringLiteral("k")).isEmpty());
        QVERIFY(ProviderProfileStore::hintForSelection(
                    QStringLiteral("B twin"), QStringLiteral("custom"),
                    QStringLiteral("http://localhost:8080/v1"),
                    QStringLiteral("k")).isEmpty());
    }

    void applyingAProfileIsWhatTheOtherPickerDerivesFrom() {
        // S6: the footer applies a profile while the settings dialog is shut;
        // apply() writes the hint, and that is what the dialog starts from.
        QVERIFY(ProviderProfileStore::save(
            makeProfile(QStringLiteral("HF"), QStringLiteral("custom"),
                        QStringLiteral("https://router.example/v1"),
                        QStringLiteral("some/model")),
            QStringLiteral("hf-token")));
        QVERIFY(ProviderProfileStore::apply(QStringLiteral("HF")));

        auto s = AppPaths::settings();
        QCOMPARE(ProviderProfileStore::validatedActiveProfileName(
                     s->value(QStringLiteral("AI/provider")).toString(),
                     s->value(QStringLiteral("AI/api_base_url")).toString(),
                     s->value(QStringLiteral("AI/api_key")).toString()),
                 QStringLiteral("HF"));

        // S5: an explicit fixed-provider pick in the footer clears the hint,
        // so the dialog opened afterwards shows that provider, not "HF".
        ProviderProfileStore::setActiveProfileHint(QString());
        QVERIFY(ProviderProfileStore::validatedActiveProfileName(
                    s->value(QStringLiteral("AI/provider")).toString(),
                    s->value(QStringLiteral("AI/api_base_url")).toString(),
                    s->value(QStringLiteral("AI/api_key")).toString())
                    .isEmpty());
    }

    void anEmptySelectionNeverProducesAHint() {
        QVERIFY(ProviderProfileStore::save(
            makeProfile(QStringLiteral("test"), QStringLiteral("openai"),
                        QStringLiteral("https://api.openai.com/v1"),
                        QStringLiteral("gpt-5.4")),
            QStringLiteral("sk-owner")));
        // Whitespace-only is not a selection either, and an unknown name is
        // not one that could be persisted.
        QVERIFY(ProviderProfileStore::hintForSelection(
                    QString(), QStringLiteral("openai"),
                    QStringLiteral("https://api.openai.com/v1"),
                    QStringLiteral("sk-owner")).isEmpty());
        QVERIFY(ProviderProfileStore::hintForSelection(
                    QStringLiteral("   "), QStringLiteral("openai"),
                    QStringLiteral("https://api.openai.com/v1"),
                    QStringLiteral("sk-owner")).isEmpty());
        QVERIFY(ProviderProfileStore::hintForSelection(
                    QStringLiteral("never saved"), QStringLiteral("openai"),
                    QStringLiteral("https://api.openai.com/v1"),
                    QStringLiteral("sk-owner")).isEmpty());
    }

    // --- 16. a profile's key is the PROFILE's, not the provider's ----------
    //
    // Selection intent made this reachable in one click: leaving a profile for
    // the fixed entry of the SAME provider is a leave with the profile's key
    // still in the field. The old rule stored any non-empty field, so the
    // profile's key replaced the key the user keeps for the plain provider
    // entry - gone, with no way back - and on Custom it became the token the
    // next ad-hoc endpoint got attached.

    void aProfileKeyPouredIntoTheFieldIsNeverRemembered() {
        using Action = ProviderProfileStore::KeyMemoryAction;

        // The poured key, untouched: it travels with the profile.
        QCOMPARE(ProviderProfileStore::keyMemoryActionOnLeave(
                     QStringLiteral("K_work"), /*userEdited*/ false,
                     /*fieldKeyCameFromProfile*/ true),
                 Action::Keep);
        // A keyless profile's empty field was already Keep and stays Keep.
        QCOMPARE(ProviderProfileStore::keyMemoryActionOnLeave(
                     QString(), false, true),
                 Action::Keep);
        // F1, the other direction: a non-empty key that did NOT come from a
        // profile is the user's (even without a dirty flag - it may predate
        // this dialog session). This is the ad-hoc custom key that must
        // survive a custom -> custom-profile switch.
        QCOMPARE(ProviderProfileStore::keyMemoryActionOnLeave(
                     QStringLiteral("K_typed"), /*userEdited*/ false,
                     /*fieldKeyCameFromProfile*/ false),
                 Action::Store);
        // Typing over a poured key makes it the user's again - Store, and an
        // erase stays reachable.
        QCOMPARE(ProviderProfileStore::keyMemoryActionOnLeave(
                     QStringLiteral("K_edited"), /*userEdited*/ true, true),
                 Action::Store);
        QCOMPARE(ProviderProfileStore::keyMemoryActionOnLeave(
                     QString(), /*userEdited*/ true, true),
                 Action::Erase);
        // The default keeps every pre-existing caller on the old rule.
        QCOMPARE(ProviderProfileStore::keyMemoryActionOnLeave(
                     QStringLiteral("K_typed"), false),
                 Action::Store);
    }

    void leavingAProfileKeepsTheProvidersOwnKey() {
        // The reported shape, at the seam both pickers use: profile "work" on
        // OpenAI with its own key, while AI/api_key/openai is the user's
        // personal one. Picking the fixed OpenAI entry is a leave.
        QVERIFY(ProviderProfileStore::save(
            makeProfile(QStringLiteral("work"), QStringLiteral("openai"),
                        QStringLiteral("https://api.openai.com/v1"),
                        QStringLiteral("gpt-5.4")),
            QStringLiteral("K_work")));
        auto s = AppPaths::settings();
        s->setValue(QStringLiteral("AI/api_key/openai"),
                    QStringLiteral("K_personal"));

        // What the widgets compute: the field key IS the profile's key.
        const QString fieldKey = ProviderProfileStore::apiKeyFor(
            QStringLiteral("work"));
        QCOMPARE(fieldKey, QStringLiteral("K_work"));
        QCOMPARE(ProviderProfileStore::keyMemoryActionOnLeave(
                     fieldKey, false, /*cameFromProfile*/ true),
                 ProviderProfileStore::KeyMemoryAction::Keep);
        // ... so nothing is written and the personal key is still there.
        QCOMPARE(s->value(QStringLiteral("AI/api_key/openai")).toString(),
                 QStringLiteral("K_personal"));
        // The profile keeps its own key regardless.
        QCOMPARE(ProviderProfileStore::apiKeyFor(QStringLiteral("work")),
                 QStringLiteral("K_work"));
    }

    // --- 17. the fixed "Custom" entry the selection needs ------------------

    void theFixedCustomEntryIsListedWhenTheSelectionNamesIt() {
        // A stored profile describing the LIVE custom endpoint used to hide the
        // plain "Custom" entry unconditionally. With selection intent that is a
        // reachable state - the user picked the fixed entry, so no profile is
        // selected - and the footer then sat on entry 0 ("OpenAI") while every
        // request went to the custom server.
        QVERIFY(ProviderProfileStore::save(
            makeProfile(QStringLiteral("Local"), QStringLiteral("custom"),
                        QStringLiteral("http://localhost:8080/v1"), QString()),
            QString()));

        const QStringList builtIns{
            QStringLiteral("https://api.openai.com/v1"),
            QStringLiteral("https://openrouter.ai/api/v1"),
            QStringLiteral("https://generativelanguage.googleapis.com/v1beta/openai"),
            QStringLiteral("http://localhost:11434/v1")};
        const QString liveUrl = QStringLiteral("http://localhost:8080/v1");
        const QString match = ProviderProfileStore::nameMatchingEndpoint(
            QStringLiteral("custom"), liveUrl, QString());
        QCOMPARE(match, QStringLiteral("Local"));
        // The endpoint half still says "no" - that is what it is for.
        QVERIFY(!ProviderProfileStore::shouldOfferAdHocCustomEntry(
            QStringLiteral("custom"), liveUrl, match, builtIns));
        // The selection half says "yes" while no profile is selected, and the
        // picker ORs the two: the entry is listed.
        QVERIFY(ProviderProfileStore::selectionNeedsFixedCustomEntry(
            QStringLiteral("custom"), QString()));
        QVERIFY(ProviderProfileStore::selectionNeedsFixedCustomEntry(
            QStringLiteral("custom"), QStringLiteral("   ")));
        // While the profile IS selected, its own entry represents the
        // connection and "Custom" stays out - no decoy.
        QVERIFY(!ProviderProfileStore::selectionNeedsFixedCustomEntry(
            QStringLiteral("custom"), QStringLiteral("Local")));
        // Only Custom is conditional; the other providers are always listed.
        QVERIFY(!ProviderProfileStore::selectionNeedsFixedCustomEntry(
            QStringLiteral("openai"), QString()));
        QVERIFY(!ProviderProfileStore::selectionNeedsFixedCustomEntry(
            QString(), QString()));
        // Case and padding of the provider id do not change the answer.
        QVERIFY(ProviderProfileStore::selectionNeedsFixedCustomEntry(
            QStringLiteral(" Custom "), QString()));
    }

    // --- 18. no endpoint, nothing ------------------------------------------
    //
    // The owner's third report: picking the fixed "Custom" entry with nothing
    // remembered shows a blank URL - but the key field filled itself from
    // AI/api_key/custom (a token from a cloud endpoint he had typed weeks
    // earlier) and the model dropdown offered that endpoint's cached models.
    // Both halves are one rule: an ad-hoc Custom entry without a base URL is a
    // blank sheet, because a key and a model catalogue belong to a SERVER.

    void aBlankAdHocCustomEndpointOffersNoKeyAndNoModels() {
        auto s = AppPaths::settings();
        // His live state: no remembered ad-hoc URL, a token still in the
        // per-provider slot, that endpoint's models under the shared scope.
        s->setValue(QStringLiteral("AI/api_key/custom"),
                    QStringLiteral("hf-token"));
        ModelListCache::store(QStringLiteral("custom"),
                              oneModel(QStringLiteral("a/model")));
        ModelFavorites::setFavorites(QStringLiteral("custom"),
                                     {QStringLiteral("a/model")});

        // The key half: no endpoint, no key - whatever the endpoint check says
        // (an empty URL is not loopback, so it "requires" a key).
        QVERIFY(ProviderProfileStore::keyMemoryOnEnter(
                    QStringLiteral("custom"), QStringLiteral("hf-token"),
                    /*endpointNeedsKey*/ true, QString()).isEmpty());
        QVERIFY(ProviderProfileStore::keyMemoryOnEnter(
                    QStringLiteral("custom"), QStringLiteral("hf-token"), true,
                    QStringLiteral("   ")).isEmpty());
        // The models half: no endpoint, no scope - so every cache and
        // favourites lookup the pickers make answers empty.
        QVERIFY(ProviderProfileStore::modelScopeId(
                    QStringLiteral("custom"), QString(), QString()).isEmpty());
        QVERIFY(ProviderProfileStore::modelScopeId(
                    QStringLiteral("custom"), QStringLiteral("  "),
                    QStringLiteral("hf-token")).isEmpty());
        QVERIFY(ModelListCache::models(QString()).isEmpty());
        QVERIFY(!ModelListCache::lastFetched(QString()).isValid());

        // ... and that is what the LIVE settings say too, so both pickers get
        // the same answer without a rule of their own.
        s->setValue(QStringLiteral("AI/provider"), QStringLiteral("custom"));
        s->remove(QStringLiteral("AI/api_base_url"));
        QVERIFY(ProviderProfileStore::activeModelScopeId().isEmpty());

        // A remembered ad-hoc endpoint WITH a URL is untouched by all of this.
        QCOMPARE(ProviderProfileStore::keyMemoryOnEnter(
                     QStringLiteral("custom"), QStringLiteral("hf-token"), true,
                     QStringLiteral("https://router.example/v1")),
                 QStringLiteral("hf-token"));
        QCOMPARE(ProviderProfileStore::modelScopeId(
                     QStringLiteral("custom"),
                     QStringLiteral("https://router.example/v1"),
                     QStringLiteral("hf-token")),
                 QStringLiteral("custom"));
        // A loopback endpoint still follows the locality rule alone.
        QVERIFY(ProviderProfileStore::keyMemoryOnEnter(
                    QStringLiteral("custom"), QStringLiteral("hf-token"),
                    /*endpointNeedsKey*/ false,
                    QStringLiteral("http://localhost:8080/v1")).isEmpty());
        // A built-in provider is NOT blanked by an empty URL: its endpoint is
        // implied by the provider, never typed, so an empty field says nothing.
        QCOMPARE(ProviderProfileStore::keyMemoryOnEnter(
                     QStringLiteral("openai"), QStringLiteral("sk-live"), true,
                     QString()),
                 QStringLiteral("sk-live"));
        QCOMPARE(ProviderProfileStore::modelScopeId(
                     QStringLiteral("openai"), QString(),
                     QStringLiteral("sk-live")),
                 QStringLiteral("openai"));

        // Display only. Nothing was deleted - the token and the cached list are
        // still there for the endpoint they belong to (cleaning them up is what
        // naming that endpoint does, see below).
        QCOMPARE(s->value(QStringLiteral("AI/api_key/custom")).toString(),
                 QStringLiteral("hf-token"));
        QCOMPARE(ModelListCache::models(QStringLiteral("custom")).size(), 1);
        QVERIFY(ModelFavorites::hasFavorites(QStringLiteral("custom")));
    }

    // --- 19. naming an ad-hoc endpoint takes its leftovers with it ----------
    //
    // "Save as..." used to create the profile beside the ad-hoc traces of the
    // very same endpoint: the remembered URL, AI/api_key/custom and the shared
    // "custom" model list. Those traces are the ghost - they outlive the
    // endpoint's ad-hoc life and get offered to the NEXT custom URL.

    void namingTheAdHocEndpointHandsItsLeftoversToTheProfile() {
        auto s = AppPaths::settings();
        const QString url = QStringLiteral("https://router.example/v1");
        s->setValue(ProviderProfileStore::adHocCustomBaseUrlKey(), url);
        s->setValue(QStringLiteral("AI/api_key/custom"),
                    QStringLiteral("hf-token"));
        ModelListCache::store(QStringLiteral("custom"),
                              oneModel(QStringLiteral("a/model")));
        ModelFavorites::setFavorites(QStringLiteral("custom"),
                                     {QStringLiteral("a/model")});

        QVERIFY(ProviderProfileStore::save(
            makeProfile(QStringLiteral("HF"), QStringLiteral("custom"), url,
                        QStringLiteral("a/model")),
            QStringLiteral("hf-token")));
        QVERIFY(ProviderProfileStore::migrateAdHocStateIntoProfile(
            QStringLiteral("HF"), url));

        const QString scope =
            ProviderProfileStore::modelScopeIdForProfile(QStringLiteral("HF"));
        QCOMPARE(scope, QStringLiteral("custom:profile:HF"));
        // (a) the endpoint is not ad-hoc any more - it has an entry of its own.
        QVERIFY(s->value(ProviderProfileStore::adHocCustomBaseUrlKey())
                    .toString().isEmpty());
        // (b) its key lives in the profile now.
        QVERIFY(s->value(QStringLiteral("AI/api_key/custom")).toString().isEmpty());
        QCOMPARE(ProviderProfileStore::apiKeyFor(QStringLiteral("HF")),
                 QStringLiteral("hf-token"));
        // (c) so do its models and favourites - the profile's tab starts with
        // what was already fetched instead of empty.
        QCOMPARE(ModelListCache::models(scope).size(), 1);
        QVERIFY(ModelFavorites::favorites(scope)
                    .contains(QStringLiteral("a/model")));
        QVERIFY(ModelListCache::models(QStringLiteral("custom")).isEmpty());
        QVERIFY(!ModelFavorites::hasFavorites(QStringLiteral("custom")));

        // What the user sees next: the fixed "Custom" entry is genuinely blank,
        // and the endpoint itself is reachable through its profile.
        QVERIFY(ProviderProfileStore::modelScopeId(
                    QStringLiteral("custom"), QString(), QString()).isEmpty());
        QVERIFY(ProviderProfileStore::keyMemoryOnEnter(
                    QStringLiteral("custom"),
                    s->value(QStringLiteral("AI/api_key/custom")).toString(),
                    true, QString()).isEmpty());
        QCOMPARE(ProviderProfileStore::modelScopeId(
                     QStringLiteral("custom"), url, QStringLiteral("hf-token")),
                 scope);
    }

    void namingAnEndpointNeverWipesAnotherEndpointsRememberedKey() {
        auto s = AppPaths::settings();
        const QString url = QStringLiteral("https://router.example/v1");
        s->setValue(ProviderProfileStore::adHocCustomBaseUrlKey(), url);
        // The remembered key is somebody else's - another custom endpoint the
        // user still switches to by hand.
        s->setValue(QStringLiteral("AI/api_key/custom"),
                    QStringLiteral("other-token"));

        QVERIFY(ProviderProfileStore::save(
            makeProfile(QStringLiteral("HF"), QStringLiteral("custom"), url,
                        QString()),
            QStringLiteral("hf-token")));
        QVERIFY(ProviderProfileStore::migrateAdHocStateIntoProfile(
            QStringLiteral("HF"), url));

        QCOMPARE(s->value(QStringLiteral("AI/api_key/custom")).toString(),
                 QStringLiteral("other-token"));
        // The URL half still happens: that endpoint IS the profile now.
        QVERIFY(s->value(ProviderProfileStore::adHocCustomBaseUrlKey())
                    .toString().isEmpty());
    }

    void reSavingAProfileKeepsItsOwnModelList() {
        auto s = AppPaths::settings();
        const QString url = QStringLiteral("https://router.example/v1");
        QVERIFY(ProviderProfileStore::save(
            makeProfile(QStringLiteral("HF"), QStringLiteral("custom"), url,
                        QStringLiteral("profile/model")),
            QStringLiteral("hf-token")));
        const QString scope =
            ProviderProfileStore::modelScopeIdForProfile(QStringLiteral("HF"));
        ModelListCache::store(scope, oneModel(QStringLiteral("profile/model")));
        ModelFavorites::setFavorites(scope, {QStringLiteral("profile/model")});
        // A stale ad-hoc trace of the same endpoint, left over from before it
        // was named.
        s->setValue(ProviderProfileStore::adHocCustomBaseUrlKey(), url);
        ModelListCache::store(QStringLiteral("custom"),
                              oneModel(QStringLiteral("stale/model")));
        ModelFavorites::setFavorites(QStringLiteral("custom"),
                                     {QStringLiteral("stale/model")});

        // An overwrite-save of the very same profile.
        QVERIFY(ProviderProfileStore::save(
            makeProfile(QStringLiteral("HF"), QStringLiteral("custom"), url,
                        QStringLiteral("profile/model")),
            QStringLiteral("hf-token")));
        QVERIFY(ProviderProfileStore::migrateAdHocStateIntoProfile(
            QStringLiteral("HF"), url));

        // The profile keeps what it fetched itself - the shared bucket is not
        // poured over it.
        QCOMPARE(ModelListCache::models(scope).size(), 1);
        QCOMPARE(ModelListCache::models(scope).at(0).toObject()
                     .value(QStringLiteral("id")).toString(),
                 QStringLiteral("profile/model"));
        QVERIFY(ModelFavorites::favorites(scope)
                    .contains(QStringLiteral("profile/model")));
        QVERIFY(!ModelFavorites::favorites(scope)
                     .contains(QStringLiteral("stale/model")));
        // The trace itself is gone either way: it described THIS endpoint, and
        // no ad-hoc configuration can address it any more.
        QVERIFY(ModelListCache::models(QStringLiteral("custom")).isEmpty());
        QVERIFY(!ModelFavorites::hasFavorites(QStringLiteral("custom")));
        QVERIFY(s->value(ProviderProfileStore::adHocCustomBaseUrlKey())
                    .toString().isEmpty());
    }

    void savingAnUnrelatedProfileMigratesNothing() {
        auto s = AppPaths::settings();
        const QString adHoc = QStringLiteral("https://router.example/v1");
        s->setValue(ProviderProfileStore::adHocCustomBaseUrlKey(), adHoc);
        s->setValue(QStringLiteral("AI/api_key/custom"),
                    QStringLiteral("hf-token"));
        ModelListCache::store(QStringLiteral("custom"),
                              oneModel(QStringLiteral("a/model")));

        // Another server.
        QVERIFY(ProviderProfileStore::save(
            makeProfile(QStringLiteral("Local"), QStringLiteral("custom"),
                        QStringLiteral("http://localhost:8080/v1"), QString()),
            QString()));
        QVERIFY(!ProviderProfileStore::migrateAdHocStateIntoProfile(
            QStringLiteral("Local"), adHoc));
        // A built-in provider - it has no ad-hoc endpoint to inherit.
        QVERIFY(ProviderProfileStore::save(
            makeProfile(QStringLiteral("Work"), QStringLiteral("openai"),
                        QStringLiteral("https://api.openai.com/v1"),
                        QStringLiteral("gpt-4o")),
            QStringLiteral("sk-live")));
        QVERIFY(!ProviderProfileStore::migrateAdHocStateIntoProfile(
            QStringLiteral("Work"), adHoc));
        // A profile that does not exist.
        QVERIFY(!ProviderProfileStore::migrateAdHocStateIntoProfile(
            QStringLiteral("Nope"), adHoc));
        // The matching endpoint, but nothing was remembered as ad-hoc.
        QVERIFY(ProviderProfileStore::save(
            makeProfile(QStringLiteral("HF"), QStringLiteral("custom"), adHoc,
                        QString()),
            QStringLiteral("hf-token")));
        QVERIFY(!ProviderProfileStore::migrateAdHocStateIntoProfile(
            QStringLiteral("HF"), QString()));

        // Everything is exactly where it was.
        QCOMPARE(s->value(ProviderProfileStore::adHocCustomBaseUrlKey()).toString(),
                 adHoc);
        QCOMPARE(s->value(QStringLiteral("AI/api_key/custom")).toString(),
                 QStringLiteral("hf-token"));
        QCOMPARE(ModelListCache::models(QStringLiteral("custom")).size(), 1);

        // ... and the endpoint identity is the same one every other rule uses,
        // so a trailing slash does not hide it.
        QVERIFY(ProviderProfileStore::migrateAdHocStateIntoProfile(
            QStringLiteral("HF"), adHoc + QLatin1Char('/')));
        QVERIFY(s->value(ProviderProfileStore::adHocCustomBaseUrlKey())
                    .toString().isEmpty());
    }
};

QTEST_APPLESS_MAIN(TestProviderProfileStore)
#include "test_provider_profile_store.moc"
