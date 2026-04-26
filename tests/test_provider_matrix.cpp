// Provider-matrix smoke test.
//
// For each provider/model that has an API key configured (via env var), runs
// the four code paths of AiClient:
//   1. simple non-streaming  (sendMessages, no tools)
//   2. simple streaming       (sendStreamingMessages, no tools)
//   3. agent-style non-streaming (sendMessages, tools=[noop])
//   4. agent-style streaming   (sendStreamingMessages, tools=[noop])
//
// API keys are resolved per provider in this order:
//   1. env var  MIDIPILOT_TEST_<PROVIDER>_KEY    (CI / explicit override)
//   2. QSettings  AI/api_key/<provider>          (whatever the running
//                                                 MidiEditor app has saved)
//   3. legacy QSettings  AI/api_key
// If none of those produces a key, the provider is SKIPPED (not failed) so
// you can run the test on a machine that only has, say, the OpenAI key
// configured. To force-skip a provider that does have a key configured, set
//   MIDIPILOT_TEST_SKIP_<PROVIDER>=1
//
// Optional model overrides (env wins, else QSettings AI/model is used only
// when AI/provider matches, else a sane default):
//   MIDIPILOT_TEST_OPENAI_MODEL      (default gpt-4o-mini)
//   MIDIPILOT_TEST_OPENROUTER_MODEL  (default openai/gpt-4o-mini)
//   MIDIPILOT_TEST_GEMINI_MODEL      (default gemini-2.5-flash)
//
// The test reuses MidiPilot's QSettings store ("MidiEditor"/"NONE") because
// AiClient reads its key/model directly from there. Original values are
// snapshotted in initTestCase and restored in cleanupTestCase, so running
// this test does NOT corrupt the user's MidiPilot config.

#include <QCoreApplication>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonObject>
#include <QSettings>
#include <QSignalSpy>
#include <QString>
#include <QTest>
#include <QTimer>

#include "../src/ai/AiClient.h"

namespace {
constexpr int RESPONSE_TIMEOUT_MS = 60000;

QString env(const char *name, const QString &fallback = QString())
{
    const QByteArray v = qgetenv(name);
    return v.isEmpty() ? fallback : QString::fromUtf8(v);
}

QJsonArray noopTool()
{
    QJsonObject params;
    params.insert("type", "object");
    params.insert("properties", QJsonObject{});

    QJsonObject fn;
    fn.insert("name", "noop");
    fn.insert("description", "Returns immediately with no side effect. "
                              "Use only if the user explicitly asks you to.");
    fn.insert("parameters", params);

    QJsonObject tool;
    tool.insert("type", "function");
    tool.insert("function", fn);
    return QJsonArray{tool};
}

QJsonArray simpleMessages(const QString &userText)
{
    QJsonArray msgs;
    QJsonObject sys;
    sys.insert("role", "system");
    sys.insert("content", "You are a terse assistant. Reply with one short sentence.");
    msgs.append(sys);

    QJsonObject usr;
    usr.insert("role", "user");
    usr.insert("content", userText);
    msgs.append(usr);
    return msgs;
}
} // namespace

class TestProviderMatrix : public QObject {
    Q_OBJECT

private:
    QSettings _settings{QStringLiteral("MidiEditor"), QStringLiteral("NONE")};
    QHash<QString, QVariant> _backup;

    void backup(const QString &k) { _backup.insert(k, _settings.value(k)); }
    void restore()
    {
        for (auto it = _backup.constBegin(); it != _backup.constEnd(); ++it) {
            if (it.value().isValid()) _settings.setValue(it.key(), it.value());
            else _settings.remove(it.key());
        }
    }

    bool waitForOneOf(const QList<QSignalSpy *> &spies, int timeoutMs)
    {
        // QSignalSpy::wait blocks the event loop until the spied signal fires
        // or timeout elapses. We poll a few times so we react to whichever
        // spy fires first without missing one that fires while we're waiting
        // on the other.
        const int slice = 200;
        int waited = 0;
        while (waited < timeoutMs) {
            for (QSignalSpy *s : spies) {
                if (!s->isEmpty()) return true;
                s->wait(slice);
                if (!s->isEmpty()) return true;
            }
            waited += slice * spies.size();
        }
        for (QSignalSpy *s : spies) {
            if (!s->isEmpty()) return true;
        }
        return false;
    }

    // Configure AiClient for the provider and run the four variants.
    // Returns false only on a hard configuration error; per-variant failures
    // are surfaced as QFAILs but don't abort the whole row.
    void runProvider(const QString &provider,
                     const QString &model,
                     const QString &apiKey,
                     const QString &baseUrl)
    {
        if (apiKey.isEmpty()) {
            QSKIP(qPrintable(QStringLiteral("%1: no API key in env, skipping").arg(provider)));
            return;
        }

        // Push provider/model/key into settings so AiClient picks them up.
        _settings.setValue("AI/provider", provider);
        _settings.setValue("AI/model", model);
        _settings.setValue("AI/api_base_url", baseUrl);
        _settings.setValue(QString("AI/api_key/%1").arg(provider), apiKey);
        _settings.setValue("AI/api_key", apiKey); // legacy fallback
        _settings.sync();

        AiClient client;
        client.reloadSettings();
        QVERIFY(client.isConfigured());

        struct Variant { const char *label; bool stream; bool tools; };
        const QList<Variant> variants = {
            {"non-stream / no tools", false, false},
            {"stream     / no tools", true,  false},
            {"non-stream / +tool",     false, true},
            {"stream     / +tool",     true,  true},
        };

        for (const Variant &v : variants) {
            QSignalSpy spyOk(&client, &AiClient::responseReceived);
            QSignalSpy spyErr(&client, &AiClient::errorOccurred);

            const QJsonArray msgs = simpleMessages(
                v.tools ? QStringLiteral("Reply with a single word: pong.")
                        : QStringLiteral("Reply with a single word: pong."));
            const QJsonArray tools = v.tools ? noopTool() : QJsonArray();

            if (v.stream) client.sendStreamingMessages(msgs, tools);
            else          client.sendMessages(msgs, tools);

            const bool gotSomething =
                waitForOneOf({&spyOk, &spyErr}, RESPONSE_TIMEOUT_MS);

            const QString tag =
                QStringLiteral("[%1 / %2 / %3]").arg(provider, model,
                                                     QString::fromLatin1(v.label));

            if (!gotSomething) {
                QFAIL(qPrintable(tag + " timeout, no response and no error"));
                continue;
            }
            if (!spyErr.isEmpty()) {
                QString err = spyErr.first().value(0).toString();
                QFAIL(qPrintable(tag + " error: " + err));
                continue;
            }
            QString reply = spyOk.first().value(0).toString();
            qInfo().noquote() << tag << "OK ->" << reply.left(80);
        }
    }

private slots:
    void initTestCase()
    {
        for (const QString &k : QStringList{
                "AI/provider", "AI/model", "AI/api_base_url", "AI/api_key",
                "AI/api_key/openai", "AI/api_key/openrouter",
                "AI/api_key/gemini", "AI/api_key/custom"}) {
            backup(k);
        }
    }

    void cleanupTestCase() { restore(); _settings.sync(); }

    QString resolveKey(const char *envName, const QString &provider)
    {
        const QString fromEnv = env(envName);
        if (!fromEnv.isEmpty()) return fromEnv;
        // Fall back to whatever the running MidiEditor app has saved.
        const QVariant perProv = _backup.value(QStringLiteral("AI/api_key/%1").arg(provider));
        if (perProv.isValid() && !perProv.toString().isEmpty()) return perProv.toString();
        // Legacy single-slot key — only honour it when the app's last selected
        // provider matches this one, otherwise we'd send e.g. an OpenAI key
        // to Gemini.
        const QVariant lastProv = _backup.value(QStringLiteral("AI/provider"));
        if (lastProv.toString() == provider) {
            const QVariant legacy = _backup.value(QStringLiteral("AI/api_key"));
            if (legacy.isValid() && !legacy.toString().isEmpty()) return legacy.toString();
        }
        return {};
    }

    QString resolveModel(const char *envName,
                         const QString &provider,
                         const QString &fallback)
    {
        const QString fromEnv = env(envName);
        if (!fromEnv.isEmpty()) return fromEnv;
        const QVariant lastProv = _backup.value(QStringLiteral("AI/provider"));
        if (lastProv.toString() == provider) {
            const QVariant m = _backup.value(QStringLiteral("AI/model"));
            if (m.isValid() && !m.toString().isEmpty()) return m.toString();
        }
        return fallback;
    }

    bool skipRequested(const char *envName)
    {
        return !qgetenv(envName).isEmpty();
    }

    void openai()
    {
        if (skipRequested("MIDIPILOT_TEST_SKIP_OPENAI")) QSKIP("openai: skipped via env");
        runProvider(QStringLiteral("openai"),
                    resolveModel("MIDIPILOT_TEST_OPENAI_MODEL", "openai",
                                 QStringLiteral("gpt-4o-mini")),
                    resolveKey("MIDIPILOT_TEST_OPENAI_KEY", "openai"),
                    QStringLiteral("https://api.openai.com/v1"));
    }

    void openrouter()
    {
        if (skipRequested("MIDIPILOT_TEST_SKIP_OPENROUTER")) QSKIP("openrouter: skipped via env");
        runProvider(QStringLiteral("openrouter"),
                    resolveModel("MIDIPILOT_TEST_OPENROUTER_MODEL", "openrouter",
                                 QStringLiteral("openai/gpt-4o-mini")),
                    resolveKey("MIDIPILOT_TEST_OPENROUTER_KEY", "openrouter"),
                    QStringLiteral("https://openrouter.ai/api/v1"));
    }

    void gemini()
    {
        if (skipRequested("MIDIPILOT_TEST_SKIP_GEMINI")) QSKIP("gemini: skipped via env");
        runProvider(QStringLiteral("gemini"),
                    resolveModel("MIDIPILOT_TEST_GEMINI_MODEL", "gemini",
                                 QStringLiteral("gemini-2.5-flash")),
                    resolveKey("MIDIPILOT_TEST_GEMINI_KEY", "gemini"),
                    QStringLiteral("https://generativelanguage.googleapis.com/v1beta/openai"));
    }
};

QTEST_MAIN(TestProviderMatrix)
#include "test_provider_matrix.moc"
