/*
 * test_agent_tool_policy
 *
 * Unit tests for the Phase 31 model-isolation policy table in
 * src/ai/AgentToolPolicy. The policy is a pure function from
 * (model, provider, isCompositionOrEdit) -> AgentToolPolicy struct, so
 * these tests link only AgentToolPolicy.cpp and do not need any other
 * project source.
 *
 * Hard requirement validated here:
 *   - Non-gpt-5.5 models always get the default-zero policy (no behavior
 *     change for Claude / Gemini / GPT-5.4 / etc.).
 *   - gpt-5.5 on the OpenAI-native provider gets the full mitigation set
 *     for composition / edit tasks; analysis / repair tasks keep
 *     pitch_bend in the schema (only sequential-tools + sanitize stay on).
 *   - gpt-5.5 on OpenRouter does NOT get the API-body fields
 *     (forceSequentialTools / overrideReasoningEffortLow) because those
 *     are Responses-API specific.
 */

#include <QtTest/QtTest>
#include <QObject>
#include <QString>

#include "../src/ai/AgentToolPolicy.h"

class TestAgentToolPolicy : public QObject {
    Q_OBJECT

private slots:

    // ---- isGpt55Model recognition ------------------------------------

    void isGpt55Model_recognizesCanonicalOpenAiId() {
        QVERIFY(AgentToolPolicyUtil::isGpt55Model(QStringLiteral("gpt-5.5"),
                                                  QStringLiteral("openai")));
        QVERIFY(AgentToolPolicyUtil::isGpt55Model(QStringLiteral("gpt-5.5-mini"),
                                                  QStringLiteral("openai")));
    }

    void isGpt55Model_recognizesOpenRouterPrefix() {
        QVERIFY(AgentToolPolicyUtil::isGpt55Model(QStringLiteral("openai/gpt-5.5"),
                                                  QStringLiteral("openrouter")));
    }

    void isGpt55Model_rejectsOtherGpt5Variants() {
        QVERIFY(!AgentToolPolicyUtil::isGpt55Model(QStringLiteral("gpt-5"),
                                                   QStringLiteral("openai")));
        QVERIFY(!AgentToolPolicyUtil::isGpt55Model(QStringLiteral("gpt-5.4"),
                                                   QStringLiteral("openai")));
        QVERIFY(!AgentToolPolicyUtil::isGpt55Model(QStringLiteral("gpt-4o"),
                                                   QStringLiteral("openai")));
    }

    void isGpt55Model_rejectsUntrustedProvider() {
        // A "custom" provider must not opt-in to the policy purely because
        // its model name happens to start with `gpt-5.5`.
        QVERIFY(!AgentToolPolicyUtil::isGpt55Model(QStringLiteral("gpt-5.5"),
                                                   QStringLiteral("custom")));
    }

    void isGpt55Model_rejectsEmptyModel() {
        QVERIFY(!AgentToolPolicyUtil::isGpt55Model(QString(),
                                                   QStringLiteral("openai")));
    }

    // ---- buildPolicyFor: non-gpt-5.5 models always default ----------

    void buildPolicyFor_otherModel_returnsAllDefaults() {
        AgentToolPolicy p = AgentToolPolicyUtil::buildPolicyFor(
            QStringLiteral("gpt-5.4"), QStringLiteral("openai"), true);
        QCOMPARE(p.allowPitchBendEvents, true);
        QCOMPARE(p.sanitizeRejectionGuidance, false);
        QCOMPARE(p.forceSequentialTools, false);
        QCOMPARE(p.overrideReasoningEffortLow, false);
        QCOMPARE(p.redactPolluedHistory, false);
        QCOMPARE(p.boundedIncompleteWriteStop, false);
    }

    void buildPolicyFor_claude_returnsAllDefaults() {
        AgentToolPolicy p = AgentToolPolicyUtil::buildPolicyFor(
            QStringLiteral("claude-3.5-sonnet"),
            QStringLiteral("openrouter"), true);
        QCOMPARE(p.allowPitchBendEvents, true);
        QCOMPARE(p.sanitizeRejectionGuidance, false);
        QCOMPARE(p.forceSequentialTools, false);
        QCOMPARE(p.overrideReasoningEffortLow, false);
        QCOMPARE(p.boundedIncompleteWriteStop, false);
    }

    // ---- buildPolicyFor: gpt-5.5 on OpenAI-native -------------------

    void buildPolicyFor_gpt55_openai_composition_appliesFullSet() {
        AgentToolPolicy p = AgentToolPolicyUtil::buildPolicyFor(
            QStringLiteral("gpt-5.5"), QStringLiteral("openai"), true);
        QCOMPARE(p.allowPitchBendEvents, false);
        QCOMPARE(p.sanitizeRejectionGuidance, true);
        QCOMPARE(p.boundedIncompleteWriteStop, true);
        QCOMPARE(p.forceSequentialTools, true);
        QCOMPARE(p.overrideReasoningEffortLow, true);
    }

    void buildPolicyFor_gpt55_openai_analysis_keepsPitchBend() {
        // Analysis / repair tasks do not need the schema-light variant
        // (no write side-effects), so pitch_bend stays in the schema.
        AgentToolPolicy p = AgentToolPolicyUtil::buildPolicyFor(
            QStringLiteral("gpt-5.5"), QStringLiteral("openai"), false);
        QCOMPARE(p.allowPitchBendEvents, true);
        QCOMPARE(p.sanitizeRejectionGuidance, true);
        QCOMPARE(p.boundedIncompleteWriteStop, true);
        // forceSequentialTools is OpenAI-native + gpt-5.5; we keep it on
        // for analysis too because it never harms a read-only run.
        QCOMPARE(p.forceSequentialTools, true);
        // Reasoning override is composition/edit only.
        QCOMPARE(p.overrideReasoningEffortLow, false);
    }

    // ---- buildPolicyFor: gpt-5.5 on OpenRouter (transport-safe) -----

    void buildPolicyFor_gpt55_openrouter_skipsApiBodyFields() {
        AgentToolPolicy p = AgentToolPolicyUtil::buildPolicyFor(
            QStringLiteral("openai/gpt-5.5"),
            QStringLiteral("openrouter"), true);
        // Schema + prompt mitigations apply (we control the content).
        QCOMPARE(p.allowPitchBendEvents, false);
        QCOMPARE(p.sanitizeRejectionGuidance, true);
        QCOMPARE(p.boundedIncompleteWriteStop, true);
        // API-body mitigations must stay off — OpenRouter does not
        // guarantee Responses-API field passthrough.
        QCOMPARE(p.forceSequentialTools, false);
        QCOMPARE(p.overrideReasoningEffortLow, false);
    }

    // ---- describe() smoke check -------------------------------------

    void describe_includesEnabledFlagNames() {
        AgentToolPolicy p = AgentToolPolicyUtil::buildPolicyFor(
            QStringLiteral("gpt-5.5"), QStringLiteral("openai"), true);
        const QString s = AgentToolPolicyUtil::describe(p);
        QVERIFY(s.contains(QStringLiteral("positive-rejection")));
        QVERIFY(s.contains(QStringLiteral("sequential-tools")));
    }
};

QTEST_APPLESS_MAIN(TestAgentToolPolicy)
#include "test_agent_tool_policy.moc"
