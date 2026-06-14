/*
 * test_model_list_fetcher
 *
 * Unit tests for ModelListFetcher::normaliseOllama() — the transform that turns
 * Ollama's /api/tags `models` array into the cache schema (id, displayName,
 * contextWindow, supportsTools, supportsReasoning). Pure JSON-in/JSON-out, no
 * network: normaliseOllama is static and uses no instance state.
 *
 * These cover the defensive edge cases the live MCP test can't easily exercise
 * repeatedly: capabilities present / absent / present-but-empty, the
 * embedding-model filter, the size badge, the name/model id fallback, and the
 * large-size (multi-GB) value that must not overflow a 32-bit int.
 */

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QTest>

#include "../src/ai/ModelListFetcher.h"

namespace {

// Build one /api/tags-style model object. Pass capsList=nullptr to omit the
// "capabilities" key entirely (older Ollama); pass an empty list for present-
// but-empty.
QJsonObject makeModel(const QString &name, qint64 sizeBytes,
                      const QString &paramSize,
                      const QStringList *capsList)
{
    QJsonObject m;
    if (!name.isEmpty()) m[QStringLiteral("name")] = name;
    if (sizeBytes > 0) m[QStringLiteral("size")] = static_cast<double>(sizeBytes);
    QJsonObject details;
    if (!paramSize.isEmpty()) details[QStringLiteral("parameter_size")] = paramSize;
    if (!details.isEmpty()) m[QStringLiteral("details")] = details;
    if (capsList) {
        QJsonArray caps;
        for (const QString &c : *capsList) caps.append(c);
        m[QStringLiteral("capabilities")] = caps;
    }
    return m;
}

// Find the normalised entry for a given model id (returns {} if absent).
QJsonObject byId(const QJsonArray &out, const QString &id)
{
    for (const QJsonValue &v : out) {
        if (v.toObject().value(QStringLiteral("id")).toString() == id)
            return v.toObject();
    }
    return QJsonObject();
}

} // namespace

class TestModelListFetcher : public QObject {
    Q_OBJECT

private slots:

    // capabilities present: tools + thinking -> flags set; completion kept.
    void capabilities_toolsAndThinking_flagged() {
        QStringList caps{"vision", "completion", "tools", "thinking"};
        QJsonArray raw{makeModel("qwen3.6:latest", 22000000000LL, "36.0B", &caps)};
        QJsonArray out = ModelListFetcher::normaliseOllama(raw);

        QCOMPARE(out.size(), 1);
        QJsonObject m = byId(out, QStringLiteral("qwen3.6:latest"));
        QVERIFY(!m.isEmpty());
        QCOMPARE(m.value("supportsTools").toBool(), true);
        QCOMPARE(m.value("supportsReasoning").toBool(), true);
    }

    // completion + tools but no thinking -> reasoning false, tools true.
    void capabilities_noThinking_reasoningFalse() {
        QStringList caps{"completion", "tools"};
        QJsonArray raw{makeModel("llama3.1:8b", 4700000000LL, "8.0B", &caps)};
        QJsonObject m = byId(ModelListFetcher::normaliseOllama(raw), QStringLiteral("llama3.1:8b"));
        QCOMPARE(m.value("supportsTools").toBool(), true);
        QCOMPARE(m.value("supportsReasoning").toBool(), false);
    }

    // No capabilities key at all (older Ollama): keep the model, default
    // supportsTools=true so Agent Mode isn't falsely blocked.
    void capabilities_absent_keptToolsDefaultTrue() {
        QJsonArray raw{makeModel("mistral:latest", 4100000000LL, "7.0B", nullptr)};
        QJsonArray out = ModelListFetcher::normaliseOllama(raw);
        QCOMPARE(out.size(), 1);
        QJsonObject m = byId(out, QStringLiteral("mistral:latest"));
        QCOMPARE(m.value("supportsTools").toBool(), true);
        QCOMPARE(m.value("supportsReasoning").toBool(), false);
    }

    // Present-but-empty capabilities behaves like absent (kept, tools default).
    void capabilities_empty_treatedAsAbsent() {
        QStringList empty;
        QJsonArray raw{makeModel("foo:latest", 1000000000LL, "3.0B", &empty)};
        QJsonArray out = ModelListFetcher::normaliseOllama(raw);
        QCOMPARE(out.size(), 1);
        QCOMPARE(byId(out, QStringLiteral("foo:latest")).value("supportsTools").toBool(), true);
    }

    // Embedding-only model (capabilities known, no "completion") is dropped.
    void embeddingModel_isFiltered() {
        QStringList emb{"embedding"};
        QStringList chat{"completion", "tools"};
        QJsonArray raw{
            makeModel("nomic-embed-text:latest", 270000000LL, "137M", &emb),
            makeModel("llama3.1:8b", 4700000000LL, "8.0B", &chat)};
        QJsonArray out = ModelListFetcher::normaliseOllama(raw);
        QCOMPARE(out.size(), 1); // embedding model filtered out
        QVERIFY(byId(out, QStringLiteral("nomic-embed-text:latest")).isEmpty());
        QVERIFY(!byId(out, QStringLiteral("llama3.1:8b")).isEmpty());
    }

    // displayName carries the param-size badge and the model id.
    void displayName_hasIdAndSizeBadge() {
        QStringList caps{"completion"};
        QJsonArray raw{makeModel("gemma4:latest", 9600000000LL, "8.0B", &caps)};
        QString disp = byId(ModelListFetcher::normaliseOllama(raw),
                            QStringLiteral("gemma4:latest")).value("displayName").toString();
        QVERIFY2(disp.contains(QStringLiteral("gemma4:latest")), qPrintable(disp));
        QVERIFY2(disp.contains(QStringLiteral("8.0B")), qPrintable(disp));
    }

    // Falls back to "model" when "name" is missing.
    void id_fallsBackToModelField() {
        QJsonObject m;
        m[QStringLiteral("model")] = QStringLiteral("phi3:mini");
        QJsonArray raw{m};
        QJsonArray out = ModelListFetcher::normaliseOllama(raw);
        QCOMPARE(out.size(), 1);
        QCOMPARE(out.at(0).toObject().value("id").toString(), QStringLiteral("phi3:mini"));
    }

    // Entries with neither name nor model are skipped.
    void entryWithoutId_isSkipped() {
        QJsonArray raw{QJsonObject{{"size", 123.0}}};
        QCOMPARE(ModelListFetcher::normaliseOllama(raw).size(), 0);
    }

    // Multi-GB size must survive as a real byte count (no 32-bit overflow) and
    // produce a non-empty size badge.
    void largeSize_noOverflow() {
        QStringList caps{"completion"};
        QJsonArray raw{makeModel("big:latest", 22000000000LL, "36.0B", &caps)};
        QString disp = byId(ModelListFetcher::normaliseOllama(raw),
                            QStringLiteral("big:latest")).value("displayName").toString();
        // SI formatter renders ~22 GB; just assert a GB-scale unit is present
        // (exact decimal separator is locale-dependent, so don't pin the number).
        QVERIFY2(disp.contains(QStringLiteral("GB")), qPrintable(disp));
    }

    // Empty input -> empty output.
    void emptyInput_emptyOutput() {
        QCOMPARE(ModelListFetcher::normaliseOllama(QJsonArray{}).size(), 0);
    }
};

QTEST_GUILESS_MAIN(TestModelListFetcher)
#include "test_model_list_fetcher.moc"
