/*
 * MidiEditor AI - the Anthropic Messages API for MidiPilot (v2.5.4).
 *
 * MidiPilot keeps every conversation in the Chat Completions shape (system /
 * user / assistant with tool_calls / tool). This module is the one place that
 * knows Claude's wire format and translates in both directions:
 *
 *   - requestBody() turns a Chat Completions message array and tool list into
 *     a POST /v1/messages body: system prompt, content blocks, tool_use /
 *     tool_result pairs, adaptive thinking and effort per model, prompt-cache
 *     breakpoints and the refusal fallback;
 *   - fromMessage() and StreamAccumulator turn Claude's answer (one JSON
 *     message, or its SSE events) back into a synthetic Chat Completions
 *     response, so AgentRunner and Simple mode stay provider-neutral.
 *
 * Claude's thinking blocks must go back to the API unchanged in a tool loop,
 * and each one is bound to the exact conversation before it. The assistant
 * message therefore carries Claude's own content blocks under
 * replayContentKey(); convertMessages() sends those instead of rebuilding the
 * turn from text and tool_calls. Pure Qt Core - unit-tested in
 * test_anthropic_messages.
 */
#ifndef ANTHROPICMESSAGES_H
#define ANTHROPICMESSAGES_H

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QString>

namespace AnthropicMessages {

/// The API version sent in the anthropic-version header.
inline QByteArray apiVersion() { return QByteArrayLiteral("2026-10-21"); }

/// The base URL of the built-in provider entry.
inline QString defaultBaseUrl() { return QStringLiteral("https://api.anthropic.com/v1"); }

/// Field of a synthetic assistant message that holds Claude's own content
/// blocks (thinking with signature, text, tool_use) for the next request.
/// Never sent to any other provider: only convertMessages() reads it.
inline QString replayContentKey() { return QStringLiteral("_anthropic_content"); }

/// What a Claude model id accepts. Derived from family and version, so a
/// newer model of a known family gets the newest known behaviour.
struct ModelTraits {
    bool known = false;            ///< parsed as claude-<family>-<version>
    bool adaptiveThinking = false; ///< thinking:{type:"adaptive"} (4.6 and later)
    bool thinksByDefault = false;  ///< thinks without a `thinking` field; cannot or should not be turned off
    bool thinkingDisplay = false;  ///< thinking.display (Opus 4.7 and later)
    bool effort = false;           ///< output_config.effort
    bool effortXhigh = false;
    bool effortMax = false;
    bool serverFallback = false;   ///< fallbacks:"default" on a refusal
    int contextWindow = 200000;
    int maxOutputTokens = 8192;
};

ModelTraits traitsFor(const QString &model);

/// The effort level actually sent to \a model: "none" and "minimal" become
/// "low", a level the model does not have steps down ("max" -> "xhigh" ->
/// "high"), anything unknown becomes "medium".
QString effortFor(const QString &model, const QString &effort);

struct RequestOptions {
    QString model;
    bool stream = false;
    bool thinking = true;            ///< MidiPilot's thinking toggle
    QString effort = QStringLiteral("medium"); ///< "none" counts as thinking off
    int maxTokens = 0;               ///< 0: 64000 streamed, 16000 otherwise (capped per model)
    bool sequentialTools = false;    ///< at most one tool call per turn
};

/// Body for POST /v1/messages.
QJsonObject requestBody(const QJsonArray &messages, const QJsonArray &tools,
                        const RequestOptions &options);

/// The anthropic-beta header value \a body needs; empty when none.
QByteArray betaHeader(const QJsonObject &body);

/**
 * \brief Chat Completions messages -> Messages API `messages` and the system
 *        prompt text.
 *
 * Leading system / developer messages become the system prompt. A later one
 * (the agent's state notes, a mode switch) becomes a text block at the end of
 * the current user turn, so nothing before it changes when it is added.
 * Consecutive tool results share one user message; an assistant message with
 * replay content is sent as exactly those blocks.
 */
void convertMessages(const QJsonArray &messages, QJsonArray &outMessages, QString &outSystem);

/// Chat Completions tools -> Messages API tools (`eager_input_streaming` on
/// each when the request streams).
QJsonArray convertTools(const QJsonArray &tools, bool eagerInputStreaming);

/**
 * \brief An assistant turn's content blocks as they go back to the API.
 *
 * After a refusal fallback, only the text of the declined model's partial
 * answer stays (its thinking and tool calls are dropped), the `fallback`
 * marker goes, everything after it stays. Empty text blocks go too.
 */
QJsonArray replayContent(const QJsonArray &content);

/// \a messages without the replayContentKey() field - for a run that started
/// on Claude and continues on another provider (changed in Settings mid-run),
/// whose API rejects message fields it does not know.
/// Inline: AgentRunner uses it, and its unit test links no AnthropicMessages.cpp.
inline QJsonArray withoutReplayContent(const QJsonArray &messages)
{
    QJsonArray out;
    for (const QJsonValue &value : messages) {
        QJsonObject message = value.toObject();
        message.remove(replayContentKey());
        out.append(message);
    }
    return out;
}

/// One Claude answer, translated.
struct Result {
    QJsonObject response;          ///< synthetic Chat Completions response
    QString text;
    QString reasoning;             ///< summarized thinking, for the UI
    QString stopReason;
    QString refusalCategory;       ///< stop_details.category on a refusal
    QString errorType;             ///< SSE error event, or a broken tool call
    QString errorMessage;
    QJsonArray inputTransformations; ///< thinking blocks the API dropped
    QString servedModel;
    QJsonObject usage;             ///< Claude's own usage object
    int toolCalls = 0;
};

/// A complete (non-streamed) Messages API response.
Result fromMessage(const QJsonObject &message);

/// One live delta of a streamed answer.
struct StreamEvent {
    enum Kind { Text, Reasoning, ToolStarted, ToolArgs, ToolDone };
    Kind kind = Text;
    QString id;    ///< tool_use id (tool events)
    QString text;  ///< text, thinking, tool name (ToolStarted) or JSON fragment
};

/// Rebuilds one streamed message from its SSE events.
class StreamAccumulator {
public:
    /// Feeds one event (the JSON of its `data:` line); returns its deltas.
    QList<StreamEvent> consume(const QJsonObject &event);
    /// The message so far, translated. Call once the stream has ended.
    Result finish() const;
    /// True once any content block or the message itself started.
    bool started() const { return _started; }

private:
    struct Block {
        QJsonObject start;   ///< content_block of content_block_start
        QString text;        ///< text_delta / thinking_delta
        QString signature;   ///< signature_delta
        QString partialJson; ///< input_json_delta
    };
    QJsonObject _message;    ///< message of message_start, updated by message_delta
    QMap<int, Block> _blocks;
    QString _errorType;
    QString _errorMessage;
    bool _started = false;
};

/// The user-facing text for an HTTP error the API answered with.
QString httpErrorMessage(int status, const QString &errorType, const QString &apiMessage);

/// The user-facing text for a refusal.
QString refusalMessage(const Result &result);

} // namespace AnthropicMessages

#endif // ANTHROPICMESSAGES_H
