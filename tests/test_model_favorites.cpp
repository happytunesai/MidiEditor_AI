#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QSettings>
#include <QTest>

#include "../src/ai/ModelFavorites.h"

class TestModelFavorites : public QObject {
    Q_OBJECT

private:
    // Unique per-process suffix so two test runs (e.g. one Release + one
    // Debug from build-vs against the same registry) cannot leak QSettings
    // state into each other. ModelFavorites uses a hardcoded
    // QSettings("MidiEditor","NONE") and ignores setOrganizationName, so the
    // sandboxing has to live here.
    QString _suffix;
    QStringList _touchedKeys;

    QString providerName(const QString &base) const
    {
        return base + QStringLiteral("__test_") + _suffix;
    }

    void clearTouchedKeys()
    {
        QSettings s(QStringLiteral("MidiEditor"), QStringLiteral("NONE"));
        for (const QString &k : _touchedKeys)
            s.remove(k);
        s.sync();
    }

private slots:
    void initTestCase()
    {
        _suffix = QString::number(QCoreApplication::applicationPid());
        _touchedKeys = {
            QStringLiteral("AI/favorites/") + providerName("openai"),
            QStringLiteral("AI/favorites/") + providerName("openai_visible"),
        };
        clearTouchedKeys();
    }

    void cleanupTestCase()
    {
        clearTouchedKeys();
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
