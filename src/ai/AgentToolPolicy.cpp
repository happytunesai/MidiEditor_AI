#include "AgentToolPolicy.h"

#include <QStringList>

namespace AgentToolPolicyUtil {

bool isGpt55Model(const QString &model, const QString &provider)
{
    if (model.isEmpty())
        return false;

    const QString m = model.toLower();
    const QString p = provider.toLower();

    // Accept the canonical OpenAI id `gpt-5.5*` as well as the OpenRouter
    // form `openai/gpt-5.5*`. We deliberately scope this to known-good
    // providers so a custom endpoint that re-uses the name does not pull
    // in our mitigations unexpectedly.
    const bool nameMatches = m.startsWith(QStringLiteral("gpt-5.5"))
                             || m.startsWith(QStringLiteral("openai/gpt-5.5"));
    if (!nameMatches)
        return false;

    if (p.isEmpty() || p == QStringLiteral("openai") || p == QStringLiteral("openrouter"))
        return true;

    return false;
}

bool isOpenAiNative(const QString &provider)
{
    const QString p = provider.toLower();
    return p.isEmpty() || p == QStringLiteral("openai");
}

AgentToolPolicy buildPolicyFor(const QString &model,
                               const QString &provider,
                               bool isCompositionOrEdit)
{
    AgentToolPolicy policy;

    if (!isGpt55Model(model, provider))
        return policy;

    // Schema / prompt mitigations apply to both OpenAI and OpenRouter
    // because they affect content we control fully (tool schemas and
    // outbound message text), not transport-specific fields.
    policy.sanitizeRejectionGuidance = true;
    policy.boundedIncompleteWriteStop = true;
    policy.redactPolluedHistory = false; // step 7 — keep off until needed

    if (isCompositionOrEdit)
        policy.allowPitchBendEvents = false;

    // API-body mitigations are Responses-API specific and only safe to send
    // on the OpenAI-native endpoint. OpenRouter passes through unknown
    // fields with no guarantee, so we keep these off there.
    if (isOpenAiNative(provider)) {
        policy.forceSequentialTools = true;
        if (isCompositionOrEdit)
            policy.overrideReasoningEffortLow = true;
    }

    return policy;
}

QString describe(const AgentToolPolicy &policy)
{
    QStringList flags;
    if (!policy.allowPitchBendEvents)
        flags << QStringLiteral("schema-light");
    if (policy.sanitizeRejectionGuidance)
        flags << QStringLiteral("positive-rejection");
    if (policy.forceSequentialTools)
        flags << QStringLiteral("sequential-tools");
    if (policy.overrideReasoningEffortLow)
        flags << QStringLiteral("low-effort");
    if (policy.boundedIncompleteWriteStop)
        flags << QStringLiteral("bounded-stop");
    if (policy.redactPolluedHistory)
        flags << QStringLiteral("redact-history");
    if (flags.isEmpty())
        return QStringLiteral("default");
    return flags.join(QLatin1Char(','));
}

} // namespace AgentToolPolicyUtil
