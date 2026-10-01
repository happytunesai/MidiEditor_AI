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
#include <QUrl>

#include "../src/AppPaths.h"
#include "../src/ai/AiClient.h"
#include "../src/ai/SecretRedactor.h"

#include <QJsonArray>
#include <QJsonObject>

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
        // A base URL typed without a scheme is the normal way people fill that
        // field; it must reach the same verdict as the spelled-out one.
        client.setApiBaseUrl(QStringLiteral("localhost:8080/v1"));
        QVERIFY(client.isConfigured());
        // ... and a public host that merely LOOKS local must not be exempted.
        client.setApiBaseUrl(QStringLiteral("http://127.0.0.1.evil.com/v1"));
        QVERIFY(!client.isConfigured());
    }

    // v2.3 review (F23): the keyless exemption is decided on the PARSED HOST,
    // never on a string prefix. "127.0.0.1.evil.com" is a public name that
    // merely starts with "127.", and a URL without a scheme used to parse with
    // an empty host, which silently dropped the exemption.
    void keylessExemption_onlyForRealLoopbackHosts()
    {
        auto needsKey = [](const QString &url) {
            return AiClient::providerRequiresKey(QStringLiteral("custom"), url);
        };

        // Loopback, in every spelling the Base URL field accepts.
        QVERIFY(!needsKey(QStringLiteral("http://localhost:8080/v1")));
        QVERIFY(!needsKey(QStringLiteral("localhost:8080/v1")));
        QVERIFY(!needsKey(QStringLiteral("  http://localhost:8080/v1  ")));
        QVERIFY(!needsKey(QStringLiteral("http://LOCALHOST:8080/v1")));
        QVERIFY(!needsKey(QStringLiteral("http://localhost./v1")));
        QVERIFY(!needsKey(QStringLiteral("http://127.0.0.1:1234/v1")));
        QVERIFY(!needsKey(QStringLiteral("127.0.0.1:1234/v1")));
        QVERIFY(!needsKey(QStringLiteral("http://127.5.6.7:1234/v1")));
        QVERIFY(!needsKey(QStringLiteral("http://[::1]:8080/v1")));
        QVERIFY(!needsKey(QStringLiteral("https://localhost:8443/v1")));

        // Public hosts that only look local.
        QVERIFY(needsKey(QStringLiteral("http://127.0.0.1.evil.com/v1")));
        QVERIFY(needsKey(QStringLiteral("http://127.example.com/v1")));
        QVERIFY(needsKey(QStringLiteral("http://localhost.evil.com/v1")));
        QVERIFY(needsKey(QStringLiteral("http://notlocalhost/v1")));
        // v2.3 review L1: "*.localhost" is reserved by RFC 6761 but Windows
        // does not resolve it locally - it goes to the configured DNS server
        // like any other name, so it can answer from off-box. Only the exact
        // name "localhost" and loopback literals earn the keyless exemption.
        QVERIFY(needsKey(QStringLiteral("http://llama.localhost:1234/v1")));
        QVERIFY(needsKey(QStringLiteral("http://a.b.localhost/v1")));
        QVERIFY(needsKey(QStringLiteral("https://api.openai.com/v1")));
        // Another machine on the LAN is not this machine.
        QVERIFY(needsKey(QStringLiteral("http://10.0.0.5:8080/v1")));
        // Nothing to judge.
        QVERIFY(needsKey(QString()));

        // The provider decides first: Ollama is keyless wherever it runs, and
        // a cloud provider is never exempted by a local-looking URL.
        QVERIFY(!AiClient::providerRequiresKey(QStringLiteral("ollama"),
                                               QStringLiteral("http://nas.example:11434/v1")));
        QVERIFY(AiClient::providerRequiresKey(QStringLiteral("openai"),
                                              QStringLiteral("http://localhost:8080/v1")));
        QVERIFY(AiClient::providerRequiresKey(QStringLiteral("openrouter"),
                                              QStringLiteral("http://127.0.0.1/v1")));
        // Provider ids arrive from settings and combos - case must not matter.
        QVERIFY(!AiClient::providerRequiresKey(QStringLiteral("Custom"),
                                               QStringLiteral("http://localhost:8080/v1")));
        QVERIFY(!AiClient::providerRequiresKey(QStringLiteral(" ollama "),
                                               QStringLiteral("https://example.com/v1")));
    }

    // v2.3 review M5: a base URL typed without a scheme ("localhost:8080/v1")
    // used to pass the keyless-loopback check (which prepended http:// only
    // for the check) while the request builders concatenated the raw text -
    // QUrl then read "localhost" as the SCHEME and every request died with
    // ProtocolUnknownError while the UI reported the endpoint as ready. The
    // normalisation now happens where the value enters the client, so the
    // check and the builders see the same string.
    void schemelessBaseUrl_isNormalisedForRequests()
    {
        // The trap, spelled out: without a scheme QUrl parses the host away.
        QCOMPARE(QUrl(QStringLiteral("localhost:8080/v1")).scheme(),
                 QStringLiteral("localhost"));
        QVERIFY(QUrl(QStringLiteral("localhost:8080/v1")).host().isEmpty());

        AiClient client;
        client.setProvider(QStringLiteral("custom"));
        client.setApiBaseUrl(QStringLiteral("  localhost:8080/v1  "));

        // Every request builder is "_apiBaseUrl + <endpoint path>", and
        // _apiBaseUrl is exactly what apiBaseUrl() returns.
        const QString builderUrl = client.apiBaseUrl()
                                   + QStringLiteral("/chat/completions");
        QVERIFY2(builderUrl.startsWith(QStringLiteral("http://localhost:8080")),
                 qPrintable(builderUrl));
        const QUrl parsed(builderUrl);
        QCOMPARE(parsed.scheme(), QStringLiteral("http"));
        QCOMPARE(parsed.host(), QStringLiteral("localhost"));
        QCOMPARE(parsed.port(), 8080);
        QCOMPARE(parsed.path(), QStringLiteral("/v1/chat/completions"));

        // ... and the same endpoint is still judged keyless, so the two
        // verdicts can no longer disagree.
        QVERIFY(client.isConfigured());

        // A spelled-out URL must survive untouched.
        client.setApiBaseUrl(QStringLiteral("https://api.openai.com/v1"));
        QCOMPARE(client.apiBaseUrl(), QStringLiteral("https://api.openai.com/v1"));

        // A value stored by an older build (or hand-edited) is normalised on
        // the way in as well.
        auto s = AppPaths::settings();
        s->setValue(QStringLiteral("AI/api_base_url"),
                    QStringLiteral("localhost:9999/v1"));
        s->sync();
        AiClient fresh;
        QVERIFY2(fresh.apiBaseUrl().startsWith(QStringLiteral("http://localhost:9999")),
                 qPrintable(fresh.apiBaseUrl()));
        fresh.reloadSettings();
        QVERIFY(fresh.apiBaseUrl().startsWith(QStringLiteral("http://localhost:9999")));
    }

    void normalizedBaseUrl_keepsRealSchemesAndEmpty()
    {
        auto norm = [](const QString &u) { return AiClient::normalizedBaseUrl(u); };

        QCOMPARE(norm(QStringLiteral("localhost:8080/v1")),
                 QStringLiteral("http://localhost:8080/v1"));
        QCOMPARE(norm(QStringLiteral("  127.0.0.1:1234/v1  ")),
                 QStringLiteral("http://127.0.0.1:1234/v1"));
        QCOMPARE(norm(QStringLiteral("example.com/v1")),
                 QStringLiteral("http://example.com/v1"));
        // Scheme-relative input must not end up with four slashes.
        QCOMPARE(norm(QStringLiteral("//example.com/v1")),
                 QStringLiteral("http://example.com/v1"));
        // Real schemes are left alone.
        QCOMPARE(norm(QStringLiteral("https://api.openai.com/v1")),
                 QStringLiteral("https://api.openai.com/v1"));
        QCOMPARE(norm(QStringLiteral("http://localhost:11434/v1")),
                 QStringLiteral("http://localhost:11434/v1"));
        // Nothing in, nothing out - an empty base URL stays empty so callers
        // can still tell "not configured" from "configured".
        QCOMPARE(norm(QString()), QString());
        QCOMPARE(norm(QStringLiteral("   ")), QString());
    }

    // ------------------------------------------------------------------
    // v2.3 review H1/L12: the shared secret redactor. Gemini's native
    // streaming endpoint carries the API key in the URL query, so
    // QNetworkReply::errorString() (which quotes the URL) reached both the
    // plaintext API log and the chat bubble with the key in it. Every log /
    // errorOccurred site in AiClient now runs through this helper.
    // ------------------------------------------------------------------
    void redactSecrets_stripsKeyFromGeminiStyleUrl()
    {
        const QString key = QStringLiteral("AIzaSyD-1234567890abcdefghijklmnopqrs");
        const QString errorString = QStringLiteral(
            "Error transferring https://generativelanguage.googleapis.com/v1beta/"
            "models/gemini-3-pro:streamGenerateContent?alt=sse&key=%1 - "
            "server replied: Bad Request").arg(key);

        const QString out = AiSecrets::redactSecrets(errorString, key);
        QVERIFY2(!out.contains(key), qPrintable(out));
        QVERIFY(out.contains(QStringLiteral("key=***")));
        // Everything that makes the message useful must survive.
        QVERIFY(out.contains(QStringLiteral("streamGenerateContent")));
        QVERIFY(out.contains(QStringLiteral("alt=sse")));
        QVERIFY(out.contains(QStringLiteral("server replied: Bad Request")));

        // The key is stripped even when the caller does not know it (a stale
        // request, a profile key that is not the active one).
        const QString blind = AiSecrets::redactSecrets(errorString);
        QVERIFY2(!blind.contains(key), qPrintable(blind));
        QVERIFY(blind.contains(QStringLiteral("key=***")));
    }

    void redactSecrets_coversTheUsualParamNames()
    {
        auto red = [](const QString &t) { return AiSecrets::redactSecrets(t); };

        QCOMPARE(red(QStringLiteral("?api_key=sk-abcdef123456")),
                 QStringLiteral("?api_key=***"));
        QCOMPARE(red(QStringLiteral("?apikey=sk-abcdef123456")),
                 QStringLiteral("?apikey=***"));
        QCOMPARE(red(QStringLiteral("?access_token=abc.def.ghi")),
                 QStringLiteral("?access_token=***"));
        QCOMPARE(red(QStringLiteral("?token=abc123")), QStringLiteral("?token=***"));
        // Case-insensitive, and only the secret value is cut out.
        QCOMPARE(red(QStringLiteral("?alt=sse&KEY=SECRETVALUE&foo=bar")),
                 QStringLiteral("?alt=sse&KEY=***&foo=bar"));
        // A word that merely ENDS in "key" is not a secret parameter.
        QCOMPARE(red(QStringLiteral("donkey=7")), QStringLiteral("donkey=7"));
        QCOMPARE(red(QString()), QString());
    }

    // L12: the literal-key replacement must not fire for a 1-2 character
    // "key" (a placeholder, or a field caught half-typed) - it would turn
    // ordinary words into "***" and make the log unreadable. Real provider
    // keys are far longer than the 8-character floor.
    void redactSecrets_shortKeyDoesNotOverRedact()
    {
        QCOMPARE(AiSecrets::redactSecrets(QStringLiteral("a monkey ate a banana"),
                                          QStringLiteral("a")),
                 QStringLiteral("a monkey ate a banana"));
        QCOMPARE(AiSecrets::redactSecrets(QStringLiteral("HTTP 500 from the server"),
                                          QStringLiteral("er")),
                 QStringLiteral("HTTP 500 from the server"));
        // Seven characters: still too short to be a real credential.
        QCOMPARE(AiSecrets::redactSecrets(QStringLiteral("prefix 1234567 suffix"),
                                          QStringLiteral("1234567")),
                 QStringLiteral("prefix 1234567 suffix"));
        // Eight and up: replaced wherever it appears, query item or not.
        QCOMPARE(AiSecrets::redactSecrets(QStringLiteral("prefix 12345678 suffix"),
                                          QStringLiteral("12345678")),
                 QStringLiteral("prefix *** suffix"));
        QCOMPARE(AiSecrets::redactSecrets(
                     QStringLiteral("Bearer sk-proj-0123456789 rejected"),
                     QStringLiteral("sk-proj-0123456789")),
                 QStringLiteral("Bearer *** rejected"));
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
    // ASTRA-001 (2026-09-07): gpt-6-astra's function tools are served only by
    // /v1/responses; chat/completions answered HTTP 400 because the Agent
    // transport was chosen by the "gpt-5" prefix alone. The family predicates
    // are the single definition for both the streaming and the non-streaming
    // send, the reasoning-model detection, and the effort clamp.
    // ------------------------------------------------------------------
    void responsesApiRouting_gpt6FamilyUsesResponsesForTools()
    {
        QVERIFY(AiClient::modelUsesResponsesApiForTools(QStringLiteral("openai"), QStringLiteral("gpt-6-astra")));
        QVERIFY(AiClient::modelUsesResponsesApiForTools(QString(), QStringLiteral("gpt-6-astra")));
        QVERIFY(AiClient::modelUsesResponsesApiForTools(QStringLiteral("openai"), QStringLiteral("GPT-6-ASTRA-2026-09-03")));
        QVERIFY(AiClient::modelUsesResponsesApiForTools(QStringLiteral("openai"), QStringLiteral("gpt-5.6-sol")));
        QVERIFY(AiClient::modelUsesResponsesApiForTools(QStringLiteral("openai"), QStringLiteral("gpt-5.5")));
        // Chat-only families stay on chat/completions.
        QVERIFY(!AiClient::modelUsesResponsesApiForTools(QStringLiteral("openai"), QStringLiteral("gpt-4o")));
        QVERIFY(!AiClient::modelUsesResponsesApiForTools(QStringLiteral("openai"), QStringLiteral("gpt-4.1")));
        // Never forced through a proxy provider.
        QVERIFY(!AiClient::modelUsesResponsesApiForTools(QStringLiteral("openrouter"), QStringLiteral("openai/gpt-6-astra")));
        QVERIFY(!AiClient::modelUsesResponsesApiForTools(QStringLiteral("custom"), QStringLiteral("gpt-6-astra")));

        // Reasoning family: no temperature, developer role, reasoning_effort.
        QVERIFY(AiClient::isOpenAiReasoningFamily(QStringLiteral("gpt-6-astra")));
        QVERIFY(AiClient::isOpenAiReasoningFamily(QStringLiteral("gpt-5.6-terra")));
        QVERIFY(AiClient::isOpenAiReasoningFamily(QStringLiteral("o3-pro")));
        QVERIFY(!AiClient::isOpenAiReasoningFamily(QStringLiteral("gpt-4o")));
        QVERIFY(!AiClient::isOpenAiReasoningFamily(QStringLiteral("gpt-4.1-mini")));
        QVERIFY(!AiClient::isOpenAiReasoningFamily(QStringLiteral("gemini-2.5-pro")));
    }

    // "max" exists only on gpt-6*; "none"/"minimal" do not exist there.
    void reasoningEffort_clampedPerFamily()
    {
        QCOMPARE(AiClient::reasoningEffortForModel(QStringLiteral("gpt-6-astra"), QStringLiteral("max")), QStringLiteral("max"));
        QCOMPARE(AiClient::reasoningEffortForModel(QStringLiteral("gpt-6-astra"), QStringLiteral("xhigh")), QStringLiteral("xhigh"));
        QCOMPARE(AiClient::reasoningEffortForModel(QStringLiteral("gpt-6-astra"), QStringLiteral("high")), QStringLiteral("high"));
        QCOMPARE(AiClient::reasoningEffortForModel(QStringLiteral("gpt-6-astra"), QStringLiteral("none")), QStringLiteral("low"));
        QCOMPARE(AiClient::reasoningEffortForModel(QStringLiteral("gpt-6-astra"), QStringLiteral("minimal")), QStringLiteral("low"));
        QCOMPARE(AiClient::reasoningEffortForModel(QStringLiteral("gpt-5.6-sol"), QStringLiteral("max")), QStringLiteral("xhigh"));
        QCOMPARE(AiClient::reasoningEffortForModel(QStringLiteral("gpt-5.5"), QStringLiteral("none")), QStringLiteral("none"));
        QCOMPARE(AiClient::reasoningEffortForModel(QStringLiteral("o3"), QStringLiteral("high")), QStringLiteral("high"));
        QCOMPARE(AiClient::reasoningEffortForModel(QStringLiteral("gemini-2.5-pro"), QStringLiteral("max")), QStringLiteral("xhigh"));
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

    // ------------------------------------------------------------------
    // Phase 52: the native Gemini path (agent mode) turns a user message
    // with attachments into text + inline_data parts - the Chat-shaped part
    // array must never reach Gemini as an empty text. A plain string user
    // message keeps its single text part.
    // ------------------------------------------------------------------
    void geminiContents_userAttachmentsBecomeInlineData()
    {
        const QByteArray png("fake-png-bytes");
        QJsonArray parts;
        parts.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                                 {QStringLiteral("text"), QStringLiteral("{\"instruction\":\"x\"}")}});
        parts.append(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("image_url")},
            {QStringLiteral("image_url"),
             QJsonObject{{QStringLiteral("url"),
                          QStringLiteral("data:image/png;base64,")
                              + QString::fromLatin1(png.toBase64())},
                         {QStringLiteral("detail"), QStringLiteral("high")}}}});
        QJsonArray messages;
        messages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("system")},
                                    {QStringLiteral("content"), QStringLiteral("sys")}});
        messages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                                    {QStringLiteral("content"), parts}});
        messages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("assistant")},
                                    {QStringLiteral("content"), QStringLiteral("ok")}});
        messages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                                    {QStringLiteral("content"), QStringLiteral("plain")}});

        QJsonArray contents;
        QJsonObject systemInstruction;
        AiClient::geminiContentsFromMessages(messages, contents, systemInstruction);

        QCOMPARE(contents.size(), 3);
        const QJsonArray first = contents[0].toObject()[QStringLiteral("parts")].toArray();
        QCOMPARE(first.size(), 2);
        QCOMPARE(first[0].toObject()[QStringLiteral("text")].toString(),
                 QStringLiteral("{\"instruction\":\"x\"}"));
        const QJsonObject inl = first[1].toObject()[QStringLiteral("inline_data")].toObject();
        QCOMPARE(inl[QStringLiteral("mime_type")].toString(), QStringLiteral("image/png"));
        QCOMPARE(QByteArray::fromBase64(inl[QStringLiteral("data")].toString().toLatin1()), png);

        const QJsonArray last = contents[2].toObject()[QStringLiteral("parts")].toArray();
        QCOMPARE(last.size(), 1);
        QCOMPARE(last[0].toObject()[QStringLiteral("text")].toString(), QStringLiteral("plain"));
    }
};

QTEST_MAIN(TestStreamingFallback)
#include "test_streaming_fallback.moc"
