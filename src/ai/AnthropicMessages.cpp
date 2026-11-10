#include "AnthropicMessages.h"

#include "ChatAttachments.h"

#include <QCoreApplication>
#include <QHash>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QRegularExpression>
#include <QStringList>

namespace AnthropicMessages {

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("AnthropicMessages", text);
}

QJsonObject textBlock(const QString &text)
{
    return QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                       {QStringLiteral("text"), text}};
}

QJsonObject ephemeral()
{
    return QJsonObject{{QStringLiteral("type"), QStringLiteral("ephemeral")}};
}

QString compactJson(const QJsonObject &object)
{
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

// Adds blocks to the last message when it has the same role, so consecutive
// tool results and the notes after them form ONE user turn (the API wants all
// tool_result blocks of an assistant turn in the next user message).
void appendTurn(QJsonArray &out, const QString &role, const QJsonArray &blocks)
{
    if (blocks.isEmpty())
        return;
    if (!out.isEmpty()) {
        QJsonObject last = out.last().toObject();
        if (last.value(QStringLiteral("role")).toString() == role) {
            QJsonArray content = last.value(QStringLiteral("content")).toArray();
            for (const QJsonValue &block : blocks)
                content.append(block);
            last[QStringLiteral("content")] = content;
            out.replace(out.size() - 1, last);
            return;
        }
    }
    out.append(QJsonObject{{QStringLiteral("role"), role},
                           {QStringLiteral("content"), blocks}});
}

QJsonArray assistantBlocks(const QJsonObject &message)
{
    // Claude's own blocks win: they carry the thinking signatures the next
    // request has to send back unchanged.
    const QJsonValue replay = message.value(replayContentKey());
    if (replay.isArray())
        return replayContent(replay.toArray());

    QJsonArray blocks;
    const QString text = ChatAttachments::plainText(message.value(QStringLiteral("content")));
    if (!text.trimmed().isEmpty())
        blocks.append(textBlock(text));
    for (const QJsonValue &value : message.value(QStringLiteral("tool_calls")).toArray()) {
        const QJsonObject call = value.toObject();
        const QJsonObject fn = call.value(QStringLiteral("function")).toObject();
        const QString name = fn.value(QStringLiteral("name")).toString();
        if (name.isEmpty())
            continue;
        const QJsonDocument args = QJsonDocument::fromJson(
            fn.value(QStringLiteral("arguments")).toString().toUtf8());
        blocks.append(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("tool_use")},
            {QStringLiteral("id"), call.value(QStringLiteral("id")).toString()},
            {QStringLiteral("name"), name},
            {QStringLiteral("input"), args.isObject() ? args.object() : QJsonObject()}});
    }
    return blocks;
}

QJsonObject chatUsage(const QJsonObject &usage)
{
    const int input = usage.value(QStringLiteral("input_tokens")).toInt();
    const int cacheRead = usage.value(QStringLiteral("cache_read_input_tokens")).toInt();
    const int cacheWrite = usage.value(QStringLiteral("cache_creation_input_tokens")).toInt();
    const int output = usage.value(QStringLiteral("output_tokens")).toInt();
    // input_tokens is only the part after the last cache breakpoint - the
    // whole prompt is the sum of the three.
    const int prompt = input + cacheRead + cacheWrite;
    return QJsonObject{
        {QStringLiteral("prompt_tokens"), prompt},
        {QStringLiteral("completion_tokens"), output},
        {QStringLiteral("total_tokens"), prompt + output},
        {QStringLiteral("prompt_tokens_details"),
         QJsonObject{{QStringLiteral("cached_tokens"), cacheRead}}}};
}

} // namespace

ModelTraits traitsFor(const QString &model)
{
    ModelTraits t;
    // claude-<family>-<major>[-<minor>][-<date>|-<suffix>]. A date suffix
    // (claude-sonnet-4-20250514) is not a minor version: the minor group
    // takes one or two digits followed by the end or a dash.
    static const QRegularExpression re(QStringLiteral(
        "^claude-(opus|sonnet|haiku|fable|mythos)-(\\d+)(?:-(\\d{1,2}))?(?:$|-)"));
    const QRegularExpressionMatch m = re.match(model.trimmed().toLower());
    if (!m.hasMatch())
        return t;

    t.known = true;
    const QString family = m.captured(1);
    const int version = m.captured(2).toInt() * 10
                        + (m.captured(3).isEmpty() ? 0 : m.captured(3).toInt());

    if (family == QLatin1String("fable") || family == QLatin1String("mythos")) {
        t.adaptiveThinking = t.thinksByDefault = t.thinkingDisplay = true;
        t.effort = t.effortXhigh = t.effortMax = true;
        t.serverFallback = family == QLatin1String("fable") && version >= 51;
        t.contextWindow = 1000000;
        t.maxOutputTokens = 128000;
    } else if (family == QLatin1String("opus")) {
        t.adaptiveThinking = version >= 46;
        t.thinksByDefault = version >= 50;
        t.thinkingDisplay = version >= 47;
        t.effort = version >= 45;
        t.effortXhigh = version >= 47;
        t.effortMax = version >= 46;
        t.serverFallback = version >= 50;
        t.contextWindow = version >= 46 ? 1000000 : 200000;
        t.maxOutputTokens = version >= 46 ? 128000 : (version >= 45 ? 64000 : 32000);
    } else if (family == QLatin1String("sonnet")) {
        t.adaptiveThinking = version >= 46;
        t.thinksByDefault = version >= 50;
        t.thinkingDisplay = version >= 50;
        t.effort = version >= 46;
        t.effortXhigh = version >= 50;
        t.effortMax = version >= 46;
        t.serverFallback = version >= 55;
        t.contextWindow = version >= 46 ? 1000000 : 200000;
        t.maxOutputTokens = version >= 46 ? 128000 : 64000;
    } else { // haiku
        const bool current = version >= 50;
        t.adaptiveThinking = t.thinksByDefault = t.thinkingDisplay = current;
        t.effort = t.effortXhigh = t.effortMax = current;
        t.contextWindow = current ? 1000000 : 200000;
        t.maxOutputTokens = current ? 128000 : (version >= 45 ? 64000 : 8192);
    }
    return t;
}

QString effortFor(const QString &model, const QString &effort)
{
    const ModelTraits t = traitsFor(model);
    const QString e = effort.trimmed().toLower();
    if (e == QLatin1String("none") || e == QLatin1String("minimal"))
        return QStringLiteral("low");
    if (e == QLatin1String("low") || e == QLatin1String("medium") || e == QLatin1String("high"))
        return e;
    if (e == QLatin1String("xhigh"))
        return t.effortXhigh ? e : QStringLiteral("high");
    if (e == QLatin1String("max"))
        return t.effortMax ? e : (t.effortXhigh ? QStringLiteral("xhigh") : QStringLiteral("high"));
    return QStringLiteral("medium");
}

void convertMessages(const QJsonArray &messages, QJsonArray &outMessages, QString &outSystem)
{
    QStringList system;
    bool conversationStarted = false;
    QJsonArray out;

    for (const QJsonValue &value : messages) {
        const QJsonObject m = value.toObject();
        const QString role = m.value(QStringLiteral("role")).toString();

        if (role == QLatin1String("system") || role == QLatin1String("developer")) {
            const QString text = ChatAttachments::plainText(m.value(QStringLiteral("content")));
            if (!conversationStarted) {
                if (!text.trimmed().isEmpty())
                    system << text;
            } else if (!text.trimmed().isEmpty()) {
                // A note added mid-run goes at the END of the current user
                // turn. Moving it into the system prompt would change the
                // start of every later request: the prompt cache would miss
                // and every earlier thinking block would no longer match the
                // conversation it was written for.
                appendTurn(out, QStringLiteral("user"), QJsonArray{textBlock(text)});
            }
            continue;
        }

        conversationStarted = true;
        if (role == QLatin1String("assistant")) {
            appendTurn(out, QStringLiteral("assistant"), assistantBlocks(m));
        } else if (role == QLatin1String("tool")) {
            QJsonObject result{
                {QStringLiteral("type"), QStringLiteral("tool_result")},
                {QStringLiteral("tool_use_id"), m.value(QStringLiteral("tool_call_id")).toString()}};
            const QString text = m.value(QStringLiteral("content")).toString();
            if (!text.isEmpty())
                result[QStringLiteral("content")] = text;
            appendTurn(out, QStringLiteral("user"), QJsonArray{result});
        } else {
            appendTurn(out, QStringLiteral("user"),
                       ChatAttachments::toAnthropicContent(m.value(QStringLiteral("content"))));
        }
    }

    // The API wants the conversation to open with a user turn.
    if (!out.isEmpty()
        && out.first().toObject().value(QStringLiteral("role")).toString() != QLatin1String("user")) {
        out.prepend(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                                {QStringLiteral("content"),
                                 QJsonArray{textBlock(QStringLiteral("(conversation continues)"))}}});
    }

    outMessages = out;
    outSystem = system.join(QStringLiteral("\n\n"));
}

QJsonArray convertTools(const QJsonArray &tools, bool eagerInputStreaming)
{
    QJsonArray out;
    for (const QJsonValue &value : tools) {
        const QJsonObject fn = value.toObject().value(QStringLiteral("function")).toObject();
        const QString name = fn.value(QStringLiteral("name")).toString();
        if (name.isEmpty())
            continue;
        QJsonObject schema = fn.value(QStringLiteral("parameters")).toObject();
        if (!schema.contains(QStringLiteral("type")))
            schema[QStringLiteral("type")] = QStringLiteral("object");
        if (!schema.contains(QStringLiteral("properties")))
            schema[QStringLiteral("properties")] = QJsonObject();
        QJsonObject tool{{QStringLiteral("name"), name},
                         {QStringLiteral("input_schema"), schema}};
        const QString description = fn.value(QStringLiteral("description")).toString();
        if (!description.isEmpty())
            tool[QStringLiteral("description")] = description;
        // Streams a large tool input (a whole track of notes) as it is
        // written instead of in one burst at the end. The API then no longer
        // validates the JSON - StreamAccumulator::finish() does.
        if (eagerInputStreaming)
            tool[QStringLiteral("eager_input_streaming")] = true;
        out.append(tool);
    }
    return out;
}

QJsonObject requestBody(const QJsonArray &messages, const QJsonArray &tools,
                        const RequestOptions &options)
{
    const ModelTraits traits = traitsFor(options.model);

    QJsonObject body;
    body[QStringLiteral("model")] = options.model;

    QJsonArray converted;
    QString system;
    convertMessages(messages, converted, system);
    body[QStringLiteral("messages")] = converted;

    // Prompt caching, three breakpoints: the tool list (the same for every
    // request - cached across runs too), the system prompt (the same for every
    // step of a run), and the growing conversation (automatic, top level).
    if (!system.isEmpty()) {
        QJsonObject block = textBlock(system);
        block[QStringLiteral("cache_control")] = ephemeral();
        body[QStringLiteral("system")] = QJsonArray{block};
    }
    if (!tools.isEmpty()) {
        QJsonArray claudeTools = convertTools(tools, options.stream);
        if (!claudeTools.isEmpty()) {
            QJsonObject last = claudeTools.last().toObject();
            last[QStringLiteral("cache_control")] = ephemeral();
            claudeTools.replace(claudeTools.size() - 1, last);
            body[QStringLiteral("tools")] = claudeTools;
            if (options.sequentialTools) {
                body[QStringLiteral("tool_choice")] = QJsonObject{
                    {QStringLiteral("type"), QStringLiteral("auto")},
                    {QStringLiteral("disable_parallel_tool_use"), true}};
            }
        }
    }
    body[QStringLiteral("cache_control")] = ephemeral();

    int maxTokens = options.maxTokens > 0 ? options.maxTokens
                                          : (options.stream ? 64000 : 16000);
    body[QStringLiteral("max_tokens")] = qMin(maxTokens, traits.maxOutputTokens);
    if (options.stream)
        body[QStringLiteral("stream")] = true;

    // Thinking. "Off" in MidiPilot means as little thinking as the model
    // allows: models that always think run at low effort, the others get no
    // `thinking` field. The thinking blocks of a tool loop go back with every
    // request; drop_block lets the API drop one that no longer matches its
    // conversation instead of rejecting the whole request.
    const QString effort = options.effort.trimmed().toLower();
    const bool thinkingOn = options.thinking && effort != QLatin1String("none")
                            && effort != QLatin1String("minimal");
    if (traits.adaptiveThinking && (thinkingOn || traits.thinksByDefault)) {
        QJsonObject thinking{{QStringLiteral("type"), QStringLiteral("adaptive")}};
        if (thinkingOn && traits.thinkingDisplay)
            thinking[QStringLiteral("display")] = QStringLiteral("summarized");
        thinking[QStringLiteral("block_binding")] = QJsonObject{
            {QStringLiteral("prefix_mismatch_behavior"), QStringLiteral("drop_block")}};
        body[QStringLiteral("thinking")] = thinking;
    }
    if (traits.effort) {
        body[QStringLiteral("output_config")] = QJsonObject{
            {QStringLiteral("effort"),
             thinkingOn ? effortFor(options.model, options.effort) : QStringLiteral("low")}};
    }

    // A safety classifier can decline a harmless request; the API then
    // answers it with the model Anthropic recommends for that case.
    if (traits.serverFallback)
        body[QStringLiteral("fallbacks")] = QStringLiteral("default");

    return body;
}

QByteArray betaHeader(const QJsonObject &body)
{
    QStringList betas;
    if (body.value(QStringLiteral("thinking")).toObject().contains(QStringLiteral("block_binding")))
        betas << QStringLiteral("thinking-binding-controls-2026-08-01");
    if (body.contains(QStringLiteral("fallbacks")))
        betas << QStringLiteral("server-side-fallback-2026-07-01");
    return betas.join(QLatin1Char(',')).toLatin1();
}

QJsonArray replayContent(const QJsonArray &content)
{
    int boundary = -1;
    for (int i = 0; i < content.size(); ++i) {
        if (content.at(i).toObject().value(QStringLiteral("type")).toString()
            == QLatin1String("fallback"))
            boundary = i;
    }

    QJsonArray out;
    for (int i = 0; i < content.size(); ++i) {
        const QJsonObject block = content.at(i).toObject();
        const QString type = block.value(QStringLiteral("type")).toString();
        if (i == boundary || type == QLatin1String("fallback"))
            continue;
        if (type == QLatin1String("text")) {
            // The API rejects a text block without visible text, also one
            // that Claude itself wrote ("\n\n" before a tool call).
            if (!block.value(QStringLiteral("text")).toString().trimmed().isEmpty())
                out.append(block);
            continue;
        }
        // Before the last fallback: the declined model's thinking and tool
        // calls. Only its text goes back.
        if (i < boundary)
            continue;
        out.append(block);
    }
    return out;
}

Result fromMessage(const QJsonObject &message)
{
    Result r;
    const QJsonArray content = message.value(QStringLiteral("content")).toArray();
    r.stopReason = message.value(QStringLiteral("stop_reason")).toString();
    r.servedModel = message.value(QStringLiteral("model")).toString();
    r.inputTransformations = message.value(QStringLiteral("input_transformations")).toArray();
    r.usage = message.value(QStringLiteral("usage")).toObject();
    if (r.stopReason == QLatin1String("refusal")) {
        r.refusalCategory = message.value(QStringLiteral("stop_details")).toObject()
                                .value(QStringLiteral("category")).toString();
    }

    QStringList thoughts;
    for (const QJsonValue &value : content) {
        const QJsonObject block = value.toObject();
        if (block.value(QStringLiteral("type")).toString() == QLatin1String("thinking")) {
            const QString t = block.value(QStringLiteral("thinking")).toString();
            if (!t.isEmpty())
                thoughts << t;
        }
    }
    r.reasoning = thoughts.join(QStringLiteral("\n\n"));

    const QJsonArray replay = replayContent(content);
    QJsonArray toolCalls;
    for (const QJsonValue &value : replay) {
        const QJsonObject block = value.toObject();
        const QString type = block.value(QStringLiteral("type")).toString();
        if (type == QLatin1String("text")) {
            r.text += block.value(QStringLiteral("text")).toString();
        } else if (type == QLatin1String("tool_use")) {
            toolCalls.append(QJsonObject{
                {QStringLiteral("id"), block.value(QStringLiteral("id")).toString()},
                {QStringLiteral("type"), QStringLiteral("function")},
                {QStringLiteral("function"),
                 QJsonObject{{QStringLiteral("name"), block.value(QStringLiteral("name")).toString()},
                             {QStringLiteral("arguments"),
                              compactJson(block.value(QStringLiteral("input")).toObject())}}}});
        }
    }
    r.toolCalls = toolCalls.size();

    QJsonObject assistant{{QStringLiteral("role"), QStringLiteral("assistant")}};
    if (!toolCalls.isEmpty()) {
        assistant[QStringLiteral("tool_calls")] = toolCalls;
        assistant[QStringLiteral("content")] =
            r.text.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(r.text);
    } else {
        assistant[QStringLiteral("content")] = r.text;
    }
    assistant[replayContentKey()] = replay;

    QString finishReason = QStringLiteral("stop");
    if (r.stopReason == QLatin1String("max_tokens"))
        finishReason = QStringLiteral("length");
    else if (r.stopReason == QLatin1String("refusal"))
        finishReason = QStringLiteral("content_filter");
    else if (!toolCalls.isEmpty())
        finishReason = QStringLiteral("tool_calls");

    QJsonObject response{
        {QStringLiteral("choices"),
         QJsonArray{QJsonObject{{QStringLiteral("message"), assistant},
                                {QStringLiteral("finish_reason"), finishReason}}}}};
    if (!r.servedModel.isEmpty())
        response[QStringLiteral("model")] = r.servedModel;
    if (!r.usage.isEmpty())
        response[QStringLiteral("usage")] = chatUsage(r.usage);
    r.response = response;
    return r;
}

QList<StreamEvent> StreamAccumulator::consume(const QJsonObject &event)
{
    QList<StreamEvent> out;
    const QString type = event.value(QStringLiteral("type")).toString();

    if (type == QLatin1String("message_start")) {
        _started = true;
        _message = event.value(QStringLiteral("message")).toObject();
        _message.remove(QStringLiteral("content")); // rebuilt from the block events
    } else if (type == QLatin1String("content_block_start")) {
        _started = true;
        Block &block = _blocks[event.value(QStringLiteral("index")).toInt()];
        block.start = event.value(QStringLiteral("content_block")).toObject();
        const QString blockType = block.start.value(QStringLiteral("type")).toString();
        if (blockType == QLatin1String("text")) {
            block.text = block.start.value(QStringLiteral("text")).toString();
            if (!block.text.isEmpty())
                out << StreamEvent{StreamEvent::Text, QString(), block.text};
        } else if (blockType == QLatin1String("thinking")) {
            block.text = block.start.value(QStringLiteral("thinking")).toString();
            block.signature = block.start.value(QStringLiteral("signature")).toString();
            if (!block.text.isEmpty())
                out << StreamEvent{StreamEvent::Reasoning, QString(), block.text};
        } else if (blockType == QLatin1String("tool_use")) {
            out << StreamEvent{StreamEvent::ToolStarted,
                               block.start.value(QStringLiteral("id")).toString(),
                               block.start.value(QStringLiteral("name")).toString()};
        }
    } else if (type == QLatin1String("content_block_delta")) {
        const int index = event.value(QStringLiteral("index")).toInt();
        if (!_blocks.contains(index))
            return out;
        Block &block = _blocks[index];
        const QJsonObject delta = event.value(QStringLiteral("delta")).toObject();
        const QString deltaType = delta.value(QStringLiteral("type")).toString();
        if (deltaType == QLatin1String("text_delta")) {
            const QString s = delta.value(QStringLiteral("text")).toString();
            block.text += s;
            if (!s.isEmpty())
                out << StreamEvent{StreamEvent::Text, QString(), s};
        } else if (deltaType == QLatin1String("thinking_delta")) {
            const QString s = delta.value(QStringLiteral("thinking")).toString();
            block.text += s;
            if (!s.isEmpty())
                out << StreamEvent{StreamEvent::Reasoning, QString(), s};
        } else if (deltaType == QLatin1String("signature_delta")) {
            block.signature += delta.value(QStringLiteral("signature")).toString();
        } else if (deltaType == QLatin1String("input_json_delta")) {
            const QString s = delta.value(QStringLiteral("partial_json")).toString();
            block.partialJson += s;
            if (!s.isEmpty())
                out << StreamEvent{StreamEvent::ToolArgs,
                                   block.start.value(QStringLiteral("id")).toString(), s};
        }
    } else if (type == QLatin1String("content_block_stop")) {
        const int index = event.value(QStringLiteral("index")).toInt();
        if (_blocks.contains(index)) {
            const QJsonObject &start = _blocks[index].start;
            if (start.value(QStringLiteral("type")).toString() == QLatin1String("tool_use"))
                out << StreamEvent{StreamEvent::ToolDone,
                                   start.value(QStringLiteral("id")).toString(), QString()};
        }
    } else if (type == QLatin1String("message_delta")) {
        const QJsonObject delta = event.value(QStringLiteral("delta")).toObject();
        for (auto it = delta.constBegin(); it != delta.constEnd(); ++it)
            _message[it.key()] = it.value();
        // message_delta carries the final output count (and, after a
        // fallback, the serving attempt's input counts) - merge, not replace.
        QJsonObject usage = _message.value(QStringLiteral("usage")).toObject();
        const QJsonObject more = event.value(QStringLiteral("usage")).toObject();
        for (auto it = more.constBegin(); it != more.constEnd(); ++it)
            usage[it.key()] = it.value();
        if (!usage.isEmpty())
            _message[QStringLiteral("usage")] = usage;
        if (event.contains(QStringLiteral("input_transformations")))
            _message[QStringLiteral("input_transformations")] =
                event.value(QStringLiteral("input_transformations"));
    } else if (type == QLatin1String("error")) {
        const QJsonObject error = event.value(QStringLiteral("error")).toObject();
        _errorType = error.value(QStringLiteral("type")).toString();
        _errorMessage = error.value(QStringLiteral("message")).toString();
    }
    return out;
}

Result StreamAccumulator::finish() const
{
    QJsonObject message = _message;
    QJsonArray content;
    QHash<QString, QString> brokenTools; // tool_use id -> name
    for (auto it = _blocks.constBegin(); it != _blocks.constEnd(); ++it) {
        const Block &b = it.value();
        QJsonObject block = b.start;
        const QString type = block.value(QStringLiteral("type")).toString();
        if (type == QLatin1String("text")) {
            block[QStringLiteral("text")] = b.text;
        } else if (type == QLatin1String("thinking")) {
            block[QStringLiteral("thinking")] = b.text;
            block[QStringLiteral("signature")] = b.signature;
        } else if (type == QLatin1String("tool_use")) {
            if (!b.partialJson.trimmed().isEmpty()) {
                QJsonParseError error;
                const QJsonDocument doc = QJsonDocument::fromJson(b.partialJson.toUtf8(), &error);
                if (error.error != QJsonParseError::NoError || !doc.isObject()) {
                    brokenTools.insert(block.value(QStringLiteral("id")).toString(),
                                       block.value(QStringLiteral("name")).toString());
                    block[QStringLiteral("input")] = QJsonObject();
                } else {
                    block[QStringLiteral("input")] = doc.object();
                }
            } else if (!block.value(QStringLiteral("input")).isObject()) {
                block[QStringLiteral("input")] = QJsonObject();
            }
        }
        content.append(block);
    }
    message[QStringLiteral("content")] = content;

    Result r = fromMessage(message);

    // Only calls that would run count - a declined model's partial call
    // before a fallback marker is dropped by replayContent() anyway.
    QStringList broken;
    const QJsonArray calls = r.response.value(QStringLiteral("choices")).toArray().first()
                                 .toObject().value(QStringLiteral("message")).toObject()
                                 .value(QStringLiteral("tool_calls")).toArray();
    for (const QJsonValue &call : calls) {
        const QString id = call.toObject().value(QStringLiteral("id")).toString();
        if (brokenTools.contains(id))
            broken << brokenTools.value(id);
    }

    if (!_errorType.isEmpty() || !_errorMessage.isEmpty()) {
        r.errorType = _errorType;
        r.errorMessage = _errorMessage;
    } else if (r.stopReason.isEmpty()) {
        // No message_delta arrived: the connection closed before the answer
        // was complete. Whatever came so far is a fragment, not an answer.
        // Worded as a connection problem so the retries treat it as one.
        r.errorType = QStringLiteral("incomplete_stream");
        r.errorMessage = tr("The connection closed before the answer was complete.");
    } else if (!broken.isEmpty() && r.stopReason != QLatin1String("max_tokens")
               && r.stopReason != QLatin1String("refusal")) {
        // With eager input streaming the API does not check a tool input.
        // Running a call whose arguments did not parse would run it with
        // NONE - report it instead, worded so AgentRunner retries with its
        // "write a smaller step" hint (errorIndicatesUnparsableToolCall).
        r.errorType = QStringLiteral("invalid_tool_json");
        r.errorMessage = tr("Claude produced a tool call with invalid JSON arguments (%1) - "
                            "usually because one call tried to write too much at once.")
                             .arg(broken.join(QStringLiteral(", ")));
    }
    return r;
}

QString httpErrorMessage(int status, const QString &errorType, const QString &apiMessage)
{
    const QString detail = apiMessage.isEmpty() ? errorType : apiMessage;
    if (apiMessage.contains(QStringLiteral("credit balance"), Qt::CaseInsensitive)) {
        // No "HTTP 400" in this text on purpose: that pattern makes MidiPilot
        // strip the message's attachments and retry (a content refusal).
        return tr("Claude: %1 With the API credits of a Max or Team plan, the key must "
                  "belong to the Console organization linked to the plan "
                  "(platform.claude.com, Settings > Billing).")
            .arg(apiMessage);
    }
    if (status == 404 || errorType == QLatin1String("not_found_error"))
        return tr("Claude API error (HTTP 404): %1 - check the model name.").arg(detail);
    return tr("Claude API error (HTTP %1): %2").arg(status).arg(detail);
}

QString refusalMessage(const Result &result)
{
    const QString category = result.refusalCategory.isEmpty()
                                 ? QStringLiteral("unspecified")
                                 : result.refusalCategory;
    return tr("Claude declined this request (refusal, category: %1). "
              "Rephrase the request or pick another model.")
        .arg(category);
}

} // namespace AnthropicMessages
