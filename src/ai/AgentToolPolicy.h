#ifndef AGENTTOOLPOLICY_H
#define AGENTTOOLPOLICY_H

#include <QString>

/**
 * \struct AgentToolPolicy
 *
 * \brief Centralised model- and task-scoped flags for Agent-Mode behaviour.
 *
 * Phase 31 (GPT-5.5 token-anchor sanitation, see Planning/07_GPT_5.5.md).
 * Every model-specific workaround introduced for GPT-5.5 must read its
 * configuration from this struct so that the special handling cannot
 * accidentally drift across AiClient, AgentRunner, ToolDefinitions and
 * PromptProfileStore.
 *
 * All defaults preserve the pre-Phase-31 behaviour byte-for-byte. Models
 * other than gpt-5.5* observe an empty/default policy and therefore see
 * exactly the same code paths as before.
 */
struct AgentToolPolicy {
    /// Include `pitch_bend` in the `events.anyOf` schema for write tools.
    /// When false, the constrained-JSON decoder cannot fall back to a
    /// `pitch_bend` "no-op" placeholder during composition/edit tasks.
    bool allowPitchBendEvents = true;

    /// Use rejection guidance that never mentions `pitch_bend` / `8192` and
    /// instead steers the model towards the positive `note` anchor.
    bool sanitizeRejectionGuidance = false;

    /// Force `parallel_tool_calls: false` on the OpenAI Responses API path.
    bool forceSequentialTools = false;

    /// Override `reasoning_effort` to `"low"` for composition/edit even if
    /// the user picked `medium`/`high` in settings. Preserves output token
    /// budget for note-array generation.
    bool overrideReasoningEffortLow = false;

    /// Redact prior `pitch_bend`-only assistant tool-calls from the outbound
    /// transcript so the model does not self-prime on its own failure.
    /// (Phase 31 step 7 — second line of defence; off by default.)
    bool redactPolluedHistory = false;

    /// Apply a stricter bounded-failure stop: after two consecutive
    /// "incomplete write" rejections, terminate with a clear user message
    /// instead of burning further tool steps.
    bool boundedIncompleteWriteStop = false;
};

namespace AgentToolPolicyUtil {

/**
 * \brief True when (model, provider) identifies an OpenAI / OpenRouter
 *        gpt-5.5* deployment that needs the Phase 31 mitigations.
 */
bool isGpt55Model(const QString &model, const QString &provider);

/**
 * \brief True when the OpenAI-native Responses-API request body may carry
 *        Phase 31 fields such as `parallel_tool_calls: false` or a
 *        `reasoning_effort: low` override. These fields are not portable
 *        to OpenRouter and must therefore be gated separately from the
 *        prompt/schema mitigations.
 */
bool isOpenAiNative(const QString &provider);

/**
 * \brief Builds the per-run policy for the given model/provider/task tuple.
 *
 * Default-constructed (all flags false / safe defaults) for everything
 * that is not the gpt-5.5* family. Schema/prompt mitigations apply for
 * both OpenAI and OpenRouter; API-body mitigations (`forceSequentialTools`,
 * `overrideReasoningEffortLow`) only when the provider is OpenAI-native.
 */
AgentToolPolicy buildPolicyFor(const QString &model,
                               const QString &provider,
                               bool isCompositionOrEdit);

/// Compact human-readable summary of which flags are active. Used in the
/// `[POLICY]` log line emitted at the start of each agent run.
QString describe(const AgentToolPolicy &policy);

} // namespace AgentToolPolicyUtil

#endif // AGENTTOOLPOLICY_H
