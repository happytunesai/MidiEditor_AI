// Streaming-fallback safety-net unit tests (PHASE-27.7).
//
// Exercises the public surface of AiClient that backs the auto-fallback to
// the non-streaming path when a streaming request fails:
//   * streamingDisabledForCurrentModel
//   * markStreamingUnsupportedForCurrentModel
//   * clearStreamingBlocklist (single + bulk)
//
// The blocklist is session-only (in-memory static QSet) since PHASE-27.7,
// so persistence is scoped to the current process. Everything this test
// touches in QSettings goes to a throwaway scope installed via
// AppPaths::setSettingsScopeForTests() in initTestCase (v2.2 review): AiClient
// writes AI/provider and AI/model through AppPaths::settings(), so without the
// seam a run - and worse, an ABORTED run - left the real configuration
// pointing at a synthetic test provider. QStandardPaths::setTestModeEnabled
// does NOT sandbox the Windows registry, so the seam is the only protection.
//
// Network-touching paths (sendStreamingMessages, finished-lambda fallback)
// are covered separately by tests/test_provider_matrix.cpp, which is opt-in
// via MIDIPILOT_TEST_<PROVIDER>_KEY env vars.

#include <QCoreApplication>
#include <QDateTime>
#include <QSettings>
#include <QSignalSpy>
#include <QTest>

#include "../src/AppPaths.h"
#include "../src/ai/AiClient.h"

class TestStreamingFallback : public QObject {
    Q_OBJECT

private:
    static constexpr const char *kTestOrg = "MidiEditorTest";
    static constexpr const char *kTestApp = "StreamingFallback";

    QString providerName() const
    {
        return QStringLiteral("custom__test");
    }
    QString modelName(const QString &base) const
    {
        return base + QStringLiteral("__test");
    }
    QString blocklistKey(const QString &provider, const QString &model) const
    {
        return QStringLiteral("AI/streaming_blocklist/")
               + provider + QStringLiteral(":") + model;
    }

private slots:

    // O1 (v2.3 provider profiles): a keyless CUSTOM endpoint on this machine
    // must count as configured - locality is its auth. Cloud custom still
    // requires a key.
    void keylessLocalCustomEndpointIsConfigured() {
        AiClient client;
        client.setProvider(QStringLiteral("custom"));
        client.setApiKey(QString());
        client.setApiBaseUrl(QStringLiteral("http://localhost:8080/v1"));
        QVERIFY(client.isConfigured());
        client.setApiBaseUrl(QStringLiteral("http://127.0.0.1:1234/v1"));
        QVERIFY(client.isConfigured());
        client.setApiBaseUrl(QStringLiteral("https://router.example/v1"));
        QVERIFY(!client.isConfigured());
    }

    void initTestCase()
    {
        // FIRST statement, before any AiClient exists: AiClient captures
        // AppPaths::settings() in its constructor, and setProvider/setModel
        // write AI/provider + AI/model. Everything below therefore has to land
        // in a throwaway scope, never in the developer's real configuration.
        AppPaths::setSettingsScopeForTests(QLatin1String(kTestOrg),
                                          QLatin1String(kTestApp));
    }

    void cleanupTestCase()
    {
        QSettings(QLatin1String(kTestOrg), QLatin1String(kTestApp)).clear();
        AppPaths::setSettingsScopeForTests(QString(), QString());
    }

    void cleanup()
    {
        // Wipe between test methods so they remain order-independent.
        // The session-only blocklist lives in a process-wide static QSet,
        // so we have to clear it via the public API – wiping QSettings was
        // never enough since PHASE-27.7.
        AiClient client;
        client.clearStreamingBlocklist();
        AppPaths::settings()->clear();
    }

    // ------------------------------------------------------------------
    // streamingDisabledForCurrentModel returns false for a fresh model
    // and true once markStreamingUnsupportedForCurrentModel was called.
    // ------------------------------------------------------------------
    void blocklist_marksAndDetectsCurrentModel()
    {
        AiClient client;
        client.setProvider(providerName());
        const QString model = modelName(QStringLiteral("alpha"));
        client.setModel(model);

        QVERIFY2(!client.streamingDisabledForCurrentModel(),
                 "Fresh model must not be blocklisted.");

        client.markStreamingUnsupportedForCurrentModel(
            QStringLiteral("HTTP 400 (test)"));

        QVERIFY2(client.streamingDisabledForCurrentModel(),
                 "Model must be blocklisted after mark()."
                 );

        // The blocklist is session-only since PHASE-27.7, so it must NOT
        // leak into QSettings under either the v1 or v2 prefix. Otherwise
        // a previous failing run would silently disable streaming forever.
        auto s = AppPaths::settings();
        QVERIFY2(!s->contains(blocklistKey(providerName(), model)),
                 "Session-only blocklist must not persist to QSettings.");
        QVERIFY2(!s->contains(QStringLiteral("AI/streaming_blocklist_v2/")
                              + providerName() + QStringLiteral(":") + model),
                 "Session-only blocklist must not persist to v2 QSettings key.");
    }

    // ------------------------------------------------------------------
    // The blocklist is keyed on the (provider, model) pair, not the model
    // alone — switching to a different model on the same provider must
    // NOT inherit the disable state.
    // ------------------------------------------------------------------
    void blocklist_isPerProviderModelPair()
    {
        AiClient client;
        client.setProvider(providerName());

        const QString modelA = modelName(QStringLiteral("alpha"));
        const QString modelB = modelName(QStringLiteral("beta"));

        client.setModel(modelA);
        client.markStreamingUnsupportedForCurrentModel(QStringLiteral("test"));
        QVERIFY(client.streamingDisabledForCurrentModel());

        client.setModel(modelB);
        QVERIFY2(!client.streamingDisabledForCurrentModel(),
                 "Different model on same provider must not inherit "
                 "the blocklist flag.");

        // And switching back must still see modelA as blocklisted.
        client.setModel(modelA);
        QVERIFY(client.streamingDisabledForCurrentModel());
    }

    // ------------------------------------------------------------------
    // markStreamingUnsupportedForCurrentModel must no-op when no model is
    // set, otherwise it would write a malformed key like
    // "AI/streaming_blocklist/<provider>:" to QSettings.
    // ------------------------------------------------------------------
    void blocklist_noopsWhenModelEmpty()
    {
        AiClient client;
        client.setProvider(providerName());
        client.setModel(QString());

        const QString badKey = blocklistKey(providerName(), QString());

        client.markStreamingUnsupportedForCurrentModel(QStringLiteral("test"));
        QVERIFY2(!client.streamingDisabledForCurrentModel(),
                 "Empty model name must report not-blocklisted.");

        auto s = AppPaths::settings();
        QVERIFY2(!s->contains(badKey),
                 "Empty model name must not write a malformed blocklist key.");
    }

    // ------------------------------------------------------------------
    // clearStreamingBlocklist(provider, model) removes a single entry.
    // ------------------------------------------------------------------
    void clearBlocklist_singleEntry()
    {
        AiClient client;
        client.setProvider(providerName());
        const QString model = modelName(QStringLiteral("alpha"));
        client.setModel(model);

        client.markStreamingUnsupportedForCurrentModel(QStringLiteral("test"));
        QVERIFY(client.streamingDisabledForCurrentModel());

        client.clearStreamingBlocklist(providerName(), model);
        QVERIFY2(!client.streamingDisabledForCurrentModel(),
                 "Targeted clear must re-enable streaming.");

        auto s = AppPaths::settings();
        QVERIFY(!s->contains(blocklistKey(providerName(), model)));
    }

    // ------------------------------------------------------------------
    // clearStreamingBlocklist() with no args wipes the entire group, so
    // a "Reset streaming flags" Settings button can use the no-arg form.
    // ------------------------------------------------------------------
    void clearBlocklist_bulkWipe()
    {
        AiClient client;
        client.setProvider(providerName());

        const QString modelA = modelName(QStringLiteral("alpha"));
        const QString modelB = modelName(QStringLiteral("beta"));

        client.setModel(modelA);
        client.markStreamingUnsupportedForCurrentModel(QStringLiteral("test"));
        client.setModel(modelB);
        client.markStreamingUnsupportedForCurrentModel(QStringLiteral("test"));
        QVERIFY(client.streamingDisabledForCurrentModel());

        client.clearStreamingBlocklist();

        client.setModel(modelA);
        QVERIFY2(!client.streamingDisabledForCurrentModel(),
                 "Bulk clear must re-enable modelA.");
        client.setModel(modelB);
        QVERIFY2(!client.streamingDisabledForCurrentModel(),
                 "Bulk clear must re-enable modelB.");
    }

    // ------------------------------------------------------------------
    // The blocklist is session-only (process-wide static QSet) since
    // PHASE-27.7, so a fresh AiClient constructed with the same
    // provider+model must still see the disable flag a previous client
    // wrote within the same process. (It does NOT survive process exit.)
    // ------------------------------------------------------------------
    void blocklist_persistsAcrossClientInstances()
    {
        const QString model = modelName(QStringLiteral("alpha"));

        {
            AiClient writer;
            writer.setProvider(providerName());
            writer.setModel(model);
            writer.markStreamingUnsupportedForCurrentModel(
                QStringLiteral("persist test"));
            QVERIFY(writer.streamingDisabledForCurrentModel());
        }

        AiClient reader;
        reader.setProvider(providerName());
        reader.setModel(model);
        QVERIFY2(reader.streamingDisabledForCurrentModel(),
                 "Blocklist must survive AiClient destruction within the "
                 "same process (session-only static QSet).");

        // Clean up the session-level entry so subsequent tests start fresh.
        reader.clearStreamingBlocklist(providerName(), model);
    }

    // ------------------------------------------------------------------
    // The retrying() signal is part of the public contract used by
    // MidiPilotWidget to show "Streaming failed — retrying without
    // streaming…" in the UI. Verify the signal exists with the documented
    // QString signature so a future signature change breaks the test
    // instead of silently breaking the UI hookup.
    // ------------------------------------------------------------------
    void retryingSignal_isExposedWithExpectedSignature()
    {
        AiClient client;
        QSignalSpy spy(&client, &AiClient::retrying);
        QVERIFY2(spy.isValid(),
                 "AiClient::retrying(QString) must be a connectable signal.");
    }

    // ------------------------------------------------------------------
    // modelRequiresResponsesApi(): OpenAI "pro" models are served only by the
    // Responses API (/v1/chat/completions 404s for them), so they must route
    // there in BOTH Simple and Agent mode. Non-pro OpenAI models and non-OpenAI
    // providers must NOT be forced. Guards the Simple-Mode pro-model fix.
    // ------------------------------------------------------------------
    void responsesApiRouting_proModelsOnly()
    {
        // OpenAI "pro" families -> Responses API.
        QVERIFY(AiClient::modelRequiresResponsesApi(QStringLiteral("openai"), QStringLiteral("gpt-5.5-pro")));
        QVERIFY(AiClient::modelRequiresResponsesApi(QStringLiteral("openai"), QStringLiteral("gpt-5-pro")));
        QVERIFY(AiClient::modelRequiresResponsesApi(QStringLiteral("openai"), QStringLiteral("gpt-5.4-pro")));
        QVERIFY(AiClient::modelRequiresResponsesApi(QStringLiteral("openai"), QStringLiteral("o3-pro")));
        QVERIFY(AiClient::modelRequiresResponsesApi(QStringLiteral("openai"), QStringLiteral("o1-pro")));
        // Case-insensitive + dated variants still match.
        QVERIFY(AiClient::modelRequiresResponsesApi(QStringLiteral("openai"), QStringLiteral("GPT-5.5-PRO")));
        QVERIFY(AiClient::modelRequiresResponsesApi(QStringLiteral("openai"), QStringLiteral("gpt-5-pro-2026-01-01")));
        // Empty provider == native OpenAI default.
        QVERIFY(AiClient::modelRequiresResponsesApi(QString(), QStringLiteral("gpt-5.5-pro")));

        // Non-pro OpenAI models stay on chat/completions.
        QVERIFY(!AiClient::modelRequiresResponsesApi(QStringLiteral("openai"), QStringLiteral("gpt-5.5")));
        QVERIFY(!AiClient::modelRequiresResponsesApi(QStringLiteral("openai"), QStringLiteral("gpt-5")));
        QVERIFY(!AiClient::modelRequiresResponsesApi(QStringLiteral("openai"), QStringLiteral("gpt-4o")));

        // Non-OpenAI providers proxy via their own chat endpoint - never forced,
        // even when the model name happens to contain "-pro".
        QVERIFY(!AiClient::modelRequiresResponsesApi(QStringLiteral("openrouter"), QStringLiteral("openai/gpt-5.5-pro")));
        QVERIFY(!AiClient::modelRequiresResponsesApi(QStringLiteral("gemini"), QStringLiteral("gemini-2.5-pro")));
        QVERIFY(!AiClient::modelRequiresResponsesApi(QStringLiteral("ollama"), QStringLiteral("qwen3-pro")));

        // "-pro" as a SUBSTRING must not match: fine-tune IDs and custom names
        // embed arbitrary text ("-prompt"/"-prod"/"-project"), and those models
        // are chat-only - routing them to /v1/responses breaks them entirely.
        QVERIFY(!AiClient::modelRequiresResponsesApi(QStringLiteral("openai"),
                 QStringLiteral("ft:gpt-4o-mini-2024-07-18:acme:midi-prompt:xyz")));
        QVERIFY(!AiClient::modelRequiresResponsesApi(QStringLiteral("openai"), QStringLiteral("gpt-4o-prod-v2")));
        QVERIFY(!AiClient::modelRequiresResponsesApi(QStringLiteral("openai"), QStringLiteral("my-project-model")));
        QVERIFY(!AiClient::modelRequiresResponsesApi(QStringLiteral("openai"), QStringLiteral("gpt-5-provider")));
    }

    // ------------------------------------------------------------------
    // TOOLS-INCAPABLE-EXPIRY: the "this model cannot call tools" flag used to
    // be a permanent bool, so one misclassified error refused a model in Agent
    // mode forever. It must now (a) latch right after mark(), (b) expire once
    // the stored observation is older than toolsIncapableExpiryDays(),
    // (c) count a legacy bool entry (no timestamp) as expired, and (d) be
    // clearable on demand.
    // ------------------------------------------------------------------
    void toolsIncapableFlag_expiresAndClears()
    {
        const QString model = modelName(QStringLiteral("nontools"));
        const QString key = QStringLiteral("AI/incapable_tools/")
                            + providerName() + QStringLiteral(":") + model;

        AiClient client;
        client.setProvider(providerName());
        client.setModel(model);

        QVERIFY2(!client.toolsIncapableForCurrentModel(),
                 "Fresh model must not be flagged.");

        client.markToolsIncapableForCurrentModel(
            QStringLiteral("HTTP 404 no endpoints found that support tool use (test)"));
        QVERIFY2(client.toolsIncapableForCurrentModel(),
                 "Model must be flagged right after mark().");

        // The stored value is a timestamp, not a bool - that is what makes the
        // flag expirable at all.
        auto s = AppPaths::settings();
        QVERIFY(s->contains(key));
        QVERIFY2(QDateTime::fromString(s->value(key).toString(), Qt::ISODate).isValid(),
                 "The flag must persist an ISO-8601 timestamp.");

        // Just inside the window: still flagged.
        s->setValue(key, QDateTime::currentDateTimeUtc()
                             .addDays(-(AiClient::toolsIncapableExpiryDays() - 1))
                             .toString(Qt::ISODate));
        s->sync();
        QVERIFY2(client.toolsIncapableForCurrentModel(),
                 "An observation younger than the expiry must still refuse.");

        // Past the window: expired, the model gets re-probed.
        s->setValue(key, QDateTime::currentDateTimeUtc()
                             .addDays(-(AiClient::toolsIncapableExpiryDays() + 1))
                             .toString(Qt::ISODate));
        s->sync();
        QVERIFY2(!client.toolsIncapableForCurrentModel(),
                 "An observation older than the expiry must be re-probed.");

        // Backward compatibility: a bare bool from an older build carries no
        // first-sight time and counts as expired.
        s->setValue(key, true);
        s->sync();
        QVERIFY2(!client.toolsIncapableForCurrentModel(),
                 "A legacy bool entry must be treated as expired.");

        // Explicit reset (the gear-menu action / the successful-run path).
        client.markToolsIncapableForCurrentModel(QStringLiteral("test"));
        QVERIFY(client.toolsIncapableForCurrentModel());
        client.clearToolsIncapableFlag(providerName(), model);
        QVERIFY2(!client.toolsIncapableForCurrentModel(),
                 "clearToolsIncapableFlag must remove the flag.");
        QVERIFY(!AppPaths::settings()->contains(key));
    }
};

QTEST_MAIN(TestStreamingFallback)
#include "test_streaming_fallback.moc"
