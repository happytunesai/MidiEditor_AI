#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QSettings>
#include <QTest>

#include "../src/AppPaths.h"
#include "../src/ai/ModelFavorites.h"

class TestModelFavorites : public QObject {
    Q_OBJECT

private:
    // ModelFavorites persists through AppPaths::settings(), i.e. into the
    // developer's real QSettings("MidiEditor","NONE") scope unless a test
    // installs the central seam. The per-PID key juggling this used to do only
    // narrowed the blast radius; an aborted run still left AI/favorites/*
    // entries behind. The throwaway scope removes the class of problem
    // (QStandardPaths::setTestModeEnabled does not sandbox the Windows
    // registry, so this is the only protection there is).
    static constexpr const char *kTestOrg = "MidiEditorTest";
    static constexpr const char *kTestApp = "ModelFavorites";

    QString providerName(const QString &base) const
    {
        return base + QStringLiteral("__test");
    }

private slots:
    void initTestCase()
    {
        AppPaths::setSettingsScopeForTests(QLatin1String(kTestOrg),
                                          QLatin1String(kTestApp));
        AppPaths::settings()->clear();
    }

    void cleanupTestCase()
    {
        QSettings(QLatin1String(kTestOrg), QLatin1String(kTestApp)).clear();
        AppPaths::setSettingsScopeForTests(QString(), QString());
    }

    void chatModel_keepsLLMs()
    {
        QVERIFY(ModelFavorites::isLikelyChatModel(QStringLiteral("gpt-5.4")));
        QVERIFY(ModelFavorites::isLikelyChatModel(QStringLiteral("gpt-4o-mini")));
        QVERIFY(ModelFavorites::isLikelyChatModel(QStringLiteral("o4-mini")));
        QVERIFY(ModelFavorites::isLikelyChatModel(QStringLiteral("gemini-2.5-pro")));
        QVERIFY(ModelFavorites::isLikelyChatModel(QStringLiteral("gemini-3.1-pro-preview")));
        QVERIFY(ModelFavorites::isLikelyChatModel(QStringLiteral("anthropic/claude-sonnet-4")));
        QVERIFY(ModelFavorites::isLikelyChatModel(QStringLiteral("openai/gpt-5.4")));
        QVERIFY(ModelFavorites::isLikelyChatModel(QStringLiteral("meta-llama/llama-4-maverick")));
    }

    void chatModel_rejectsNonLLMs()
    {
        QVERIFY(!ModelFavorites::isLikelyChatModel(QStringLiteral("text-embedding-3-large")));
        QVERIFY(!ModelFavorites::isLikelyChatModel(QStringLiteral("dall-e-3")));
        QVERIFY(!ModelFavorites::isLikelyChatModel(QStringLiteral("whisper-1")));
        QVERIFY(!ModelFavorites::isLikelyChatModel(QStringLiteral("tts-1-hd")));
        QVERIFY(!ModelFavorites::isLikelyChatModel(QStringLiteral("openai/gpt-image-1")));
        QVERIFY(!ModelFavorites::isLikelyChatModel(QStringLiteral("google/imagen-3")));
        QVERIFY(!ModelFavorites::isLikelyChatModel(QStringLiteral("google/veo-2")));
        QVERIFY(!ModelFavorites::isLikelyChatModel(QStringLiteral("openai/sora")));
        QVERIFY(!ModelFavorites::isLikelyChatModel(QStringLiteral("text-moderation-latest")));
        QVERIFY(!ModelFavorites::isLikelyChatModel(QStringLiteral("voyage-3-embedding")));
    }

    void favorites_roundtrip()
    {
        const QString p = providerName(QStringLiteral("openai"));
        QVERIFY(!ModelFavorites::hasFavorites(p));

        ModelFavorites::setFavorites(p, {QStringLiteral("gpt-5.4"),
                                         QStringLiteral("o4-mini")});
        QVERIFY(ModelFavorites::hasFavorites(p));
        const auto favs = ModelFavorites::favorites(p);
        QCOMPARE(favs.size(), 2);
        QVERIFY(favs.contains(QStringLiteral("gpt-5.4")));
        QVERIFY(favs.contains(QStringLiteral("o4-mini")));

        // Empty list clears.
        ModelFavorites::setFavorites(p, {});
        QVERIFY(!ModelFavorites::hasFavorites(p));
    }

    // Favourites are keyed by SCOPE, not by provider: the plain "custom" scope
    // is the ad-hoc endpoint, "custom:profile:<id>" belongs to one stored
    // provider profile. ModelFavorites treats the scope as an opaque string
    // (ProviderProfileStore::modelScopeId builds it), so this pins the storage
    // contract the favourites dialog's per-profile tabs rely on.
    void favorites_areScopedPerCustomEndpoint()
    {
        const QString adHoc = providerName(QStringLiteral("custom"));
        const QString hf    = QStringLiteral("custom:profile:HF")
                              + QStringLiteral("__test");
        const QString local = QStringLiteral("custom:profile:Local llama")
                              + QStringLiteral("__test");

        ModelFavorites::setFavorites(adHoc, {QStringLiteral("ad-hoc/model")});
        ModelFavorites::setFavorites(hf, {QStringLiteral("hf/model-a"),
                                          QStringLiteral("hf/model-b")});
        ModelFavorites::setFavorites(local, {QStringLiteral("local-model")});

        QCOMPARE(ModelFavorites::favorites(adHoc).size(), 1);
        QCOMPARE(ModelFavorites::favorites(hf).size(), 2);
        QCOMPARE(ModelFavorites::favorites(local).size(), 1);
        QVERIFY(ModelFavorites::favorites(hf).contains(QStringLiteral("hf/model-a")));
        // No bleed in either direction.
        QVERIFY(!ModelFavorites::favorites(hf).contains(QStringLiteral("ad-hoc/model")));
        QVERIFY(!ModelFavorites::favorites(adHoc).contains(QStringLiteral("hf/model-a")));

        // visibleModels() follows the same scope.
        QJsonArray cached;
        for (const QString &id : {QStringLiteral("hf/model-a"),
                                  QStringLiteral("hf/model-b"),
                                  QStringLiteral("ad-hoc/model")}) {
            QJsonObject m;
            m.insert(QStringLiteral("id"), id);
            cached.append(m);
        }
        QCOMPARE(ModelFavorites::visibleModels(hf, cached).size(), 2);
        QCOMPARE(ModelFavorites::visibleModels(adHoc, cached).size(), 1);

        // Clearing one profile scope leaves the ad-hoc scope (the migration
        // promise: pre-profile favourites never move) and the other profile.
        ModelFavorites::setFavorites(hf, {});
        QVERIFY(!ModelFavorites::hasFavorites(hf));
        QVERIFY(ModelFavorites::hasFavorites(adHoc));
        QVERIFY(ModelFavorites::hasFavorites(local));

        ModelFavorites::setFavorites(adHoc, {});
        ModelFavorites::setFavorites(local, {});
    }

    // ProviderProfileStore::encodeName percent-escapes everything outside
    // [A-Za-z0-9 ._-], so a scope id can carry '%', ':' and spaces. That has to
    // survive the settings backend as ONE key.
    void favorites_scopeKeySurvivesEncodedProfileNames()
    {
        const QString scope = QStringLiteral("custom:profile:HF %2F local server")
                              + QStringLiteral("__test");
        ModelFavorites::setFavorites(scope, {QStringLiteral("m1"),
                                             QStringLiteral("m2")});
        QCOMPARE(ModelFavorites::favorites(scope).size(), 2);
        QVERIFY(ModelFavorites::favorites(scope).contains(QStringLiteral("m2")));
        // Not visible under a truncated or differently escaped scope.
        QVERIFY(!ModelFavorites::hasFavorites(QStringLiteral("custom:profile:HF")));
        ModelFavorites::setFavorites(scope, {});
        QVERIFY(!ModelFavorites::hasFavorites(scope));
    }

    void visibleModels_filtersAndRespectsFavorites()
    {
        QJsonArray cached;
        auto add = [&](const QString &id) {
            QJsonObject m;
            m.insert(QStringLiteral("id"), id);
            m.insert(QStringLiteral("displayName"), id);
            cached.append(m);
        };
        add(QStringLiteral("gpt-5.4"));
        add(QStringLiteral("gpt-4o-mini"));
        add(QStringLiteral("dall-e-3"));            // image — drop
        add(QStringLiteral("text-embedding-3-large")); // embedding — drop
        add(QStringLiteral("whisper-1"));           // audio — drop
        add(QStringLiteral("o4-mini"));

        const QString p = providerName(QStringLiteral("openai_visible"));
        ModelFavorites::setFavorites(p, {}); // start clean

        // No favourites: 3 chat models survive (6 raw - 3 non-LLM).
        QJsonArray noFav = ModelFavorites::visibleModels(p, cached);
        QCOMPARE(noFav.size(), 3);

        // Favourites: pick 2 -> only those.
        ModelFavorites::setFavorites(p, {QStringLiteral("gpt-5.4"),
                                         QStringLiteral("o4-mini")});
        QJsonArray fav = ModelFavorites::visibleModels(p, cached);
        QCOMPARE(fav.size(), 2);
        QStringList ids;
        for (const auto &v : fav) ids << v.toObject().value("id").toString();
        QVERIFY(ids.contains(QStringLiteral("gpt-5.4")));
        QVERIFY(ids.contains(QStringLiteral("o4-mini")));

        // A favourite that is also a non-LLM is still suppressed by the LLM filter.
        ModelFavorites::setFavorites(p, {QStringLiteral("dall-e-3")});
        QJsonArray onlyImage = ModelFavorites::visibleModels(p, cached);
        QCOMPARE(onlyImage.size(), 0);
    }
};

QTEST_MAIN(TestModelFavorites)
#include "test_model_favorites.moc"
