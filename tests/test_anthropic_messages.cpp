/*
 * test_anthropic_messages (v2.5.4)
 *
 * Claude's Messages API for MidiPilot (src/ai/AnthropicMessages):
 *   - model traits and the effort levels each model takes;
 *   - the Chat Completions -> Messages conversion: system prompt, tool_use /
 *     tool_result pairs, notes added mid-run, attachments, replayed blocks;
 *   - the request body: cache breakpoints, thinking and effort per toggle,
 *     eager tool streaming, the refusal fallback, the beta header, no
 *     sampling parameters;
 *   - the answer: a full message and an SSE stream rebuilt into a synthetic
 *     Chat Completions response (usage, refusal, the fallback boundary, broken
 *     tool JSON, a stream error);
 *   - an agent loop stays append-only: every request starts with the previous
 *     one, which keeps the prompt cache and the thinking blocks valid.
 * Pure Qt Core.
 */

#include <QtTest/QtTest>

#include <QJsonDocument>

#include "../src/ai/AnthropicMessages.h"

using namespace AnthropicMessages;

namespace {

QJsonObject msg(const QString &role, const QJsonValue &content)
{
    return QJsonObject{{QStringLiteral("role"), role}, {QStringLiteral("content"), content}};
}

QJsonObject toolCall(const QString &id, const QString &name, const QString &args)
{
    return QJsonObject{
        {QStringLiteral("id"), id},
        {QStringLiteral("type"), QStringLiteral("function")},
        {QStringLiteral("function"),
         QJsonObject{{QStringLiteral("name"), name}, {QStringLiteral("arguments"), args}}}};
}

QJsonObject toolResult(const QString &id, const QString &content)
{
    return QJsonObject{{QStringLiteral("role"), QStringLiteral("tool")},
                       {QStringLiteral("tool_call_id"), id},
                       {QStringLiteral("content"), content}};
}

QJsonArray chatTools()
{
    auto tool = [](const QString &name) {
        return QJsonObject{
            {QStringLiteral("type"), QStringLiteral("function")},
            {QStringLiteral("function"),
             QJsonObject{{QStringLiteral("name"), name},
                         {QStringLiteral("description"), QStringLiteral("Does ") + name},
                         {QStringLiteral("parameters"),
                          QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                                      {QStringLiteral("properties"), QJsonObject()}}}}}};
    };
    return QJsonArray{tool(QStringLiteral("get_editor_state")),
                      tool(QStringLiteral("insert_events"))};
}

QString type(const QJsonValue &block)
{
    return block.toObject().value(QStringLiteral("type")).toString();
}

// One SSE event's JSON, written with ' for " (raw string literals with JSON
// in them throw moc off - it then finds no test class at all).
QJsonObject sse(QString json)
{
    json.replace(QLatin1Char('\''), QLatin1Char('"'));
    return QJsonDocument::fromJson(json.toUtf8()).object();
}

// An input_json_delta event (its fragment carries escaped quotes).
QJsonObject jsonDelta(int index, const QString &partialJson)
{
    return QJsonObject{
        {QStringLiteral("type"), QStringLiteral("content_block_delta")},
        {QStringLiteral("index"), index},
        {QStringLiteral("delta"),
         QJsonObject{{QStringLiteral("type"), QStringLiteral("input_json_delta")},
                     {QStringLiteral("partial_json"), partialJson}}}};
}

// The events of one streamed answer: thinking, text, a tool call.
QList<QJsonObject> toolTurnEvents(const QString &partialJsonTail)
{
    return {
        sse(QStringLiteral("{'type':'message_start','message':{'id':'msg_1','model':'claude-opus-5-5','content':[],'usage':{'input_tokens':5,'cache_read_input_tokens':2000,'cache_creation_input_tokens':0,'output_tokens':1}}}")),
        sse(QStringLiteral("{'type':'content_block_start','index':0,'content_block':{'type':'thinking','thinking':'','signature':''}}")),
        sse(QStringLiteral("{'type':'content_block_delta','index':0,'delta':{'type':'thinking_delta','thinking':'Plan '}}")),
        sse(QStringLiteral("{'type':'ping'}")),
        sse(QStringLiteral("{'type':'content_block_delta','index':0,'delta':{'type':'thinking_delta','thinking':'it.'}}")),
        sse(QStringLiteral("{'type':'content_block_delta','index':0,'delta':{'type':'signature_delta','signature':'SIG'}}")),
        sse(QStringLiteral("{'type':'content_block_stop','index':0}")),
        sse(QStringLiteral("{'type':'content_block_start','index':1,'content_block':{'type':'text','text':''}}")),
        sse(QStringLiteral("{'type':'content_block_delta','index':1,'delta':{'type':'text_delta','text':'Writing '}}")),
        sse(QStringLiteral("{'type':'content_block_delta','index':1,'delta':{'type':'text_delta','text':'notes.'}}")),
        sse(QStringLiteral("{'type':'content_block_stop','index':1}")),
        sse(QStringLiteral("{'type':'content_block_start','index':2,'content_block':{'type':'tool_use','id':'toolu_9','name':'insert_events','input':{}}}")),
        jsonDelta(2, QStringLiteral("{\"track\": 2, ")),
        jsonDelta(2, partialJsonTail),
        sse(QStringLiteral("{'type':'content_block_stop','index':2}")),
        sse(QStringLiteral("{'type':'message_delta','delta':{'stop_reason':'tool_use'},'usage':{'output_tokens':42}}")),
        sse(QStringLiteral("{'type':'message_stop'}")),
    };
}

QJsonObject assistantOf(const Result &r)
{
    return r.response.value(QStringLiteral("choices")).toArray().first().toObject()
        .value(QStringLiteral("message")).toObject();
}

} // namespace

class TestAnthropicMessages : public QObject {
    Q_OBJECT

private slots:
    void traits_currentModels()
    {
        const ModelTraits opus = traitsFor(QStringLiteral("claude-opus-5-5"));
        QVERIFY(opus.known);
        QVERIFY(opus.adaptiveThinking && opus.thinksByDefault && opus.thinkingDisplay);
        QVERIFY(opus.effort && opus.effortXhigh && opus.effortMax);
        QVERIFY(opus.serverFallback);
        QCOMPARE(opus.contextWindow, 1000000);
        QCOMPARE(opus.maxOutputTokens, 128000);

        QVERIFY(traitsFor(QStringLiteral("claude-sonnet-5-5")).serverFallback);
        QVERIFY(traitsFor(QStringLiteral("claude-fable-5-1")).serverFallback);

        // Fable 5.2: thinking cannot be turned off, effort is the control,
        // 1M context, 128K output, refusal fallback.
        const ModelTraits fable = traitsFor(QStringLiteral("claude-fable-5-2"));
        QVERIFY(fable.known && fable.adaptiveThinking && fable.thinksByDefault);
        QVERIFY(fable.thinkingDisplay && fable.effortXhigh && fable.effortMax);
        QVERIFY(fable.serverFallback);
        QCOMPARE(fable.contextWindow, 1000000);
        QCOMPARE(fable.maxOutputTokens, 128000);
        QVERIFY(traitsFor(QStringLiteral("claude-mythos-5-2")).thinksByDefault);
        const ModelTraits haiku = traitsFor(QStringLiteral("claude-haiku-5-5"));
        QVERIFY(haiku.effort && haiku.thinksByDefault);
        QVERIFY(!haiku.serverFallback); // no server-side fallback for Haiku
        QCOMPARE(haiku.contextWindow, 1000000);
    }

    void traits_olderAndUnknownModels()
    {
        const ModelTraits opus47 = traitsFor(QStringLiteral("claude-opus-4-7"));
        QVERIFY(opus47.adaptiveThinking && !opus47.thinksByDefault);
        QVERIFY(opus47.thinkingDisplay && opus47.effortXhigh && !opus47.serverFallback);

        const ModelTraits opus46 = traitsFor(QStringLiteral("claude-opus-4-6"));
        QVERIFY(opus46.adaptiveThinking && !opus46.thinkingDisplay);
        QVERIFY(!opus46.effortXhigh && opus46.effortMax);

        const ModelTraits haiku45 = traitsFor(QStringLiteral("claude-haiku-4-5"));
        QVERIFY(haiku45.known && !haiku45.adaptiveThinking && !haiku45.effort);
        QCOMPARE(haiku45.contextWindow, 200000);
        QCOMPARE(haiku45.maxOutputTokens, 64000);

        // A date suffix is not a minor version.
        const ModelTraits sonnet4 = traitsFor(QStringLiteral("claude-sonnet-4-20250514"));
        QVERIFY(sonnet4.known && !sonnet4.adaptiveThinking);
        QCOMPARE(sonnet4.contextWindow, 200000);

        QVERIFY(!traitsFor(QStringLiteral("claude-3-5-sonnet-20241022")).known);
        QCOMPARE(traitsFor(QStringLiteral("claude-3-5-sonnet-20241022")).maxOutputTokens, 8192);
        QVERIFY(!traitsFor(QStringLiteral("gpt-5.4")).known);

        // A newer model of a known family gets the newest behaviour.
        QVERIFY(traitsFor(QStringLiteral("claude-opus-6")).serverFallback);
    }

    void effort_stepsDownToWhatTheModelTakes()
    {
        QCOMPARE(effortFor(QStringLiteral("claude-opus-5-5"), QStringLiteral("none")), QStringLiteral("low"));
        QCOMPARE(effortFor(QStringLiteral("claude-opus-5-5"), QStringLiteral("max")), QStringLiteral("max"));
        QCOMPARE(effortFor(QStringLiteral("claude-opus-4-6"), QStringLiteral("xhigh")), QStringLiteral("high"));
        QCOMPARE(effortFor(QStringLiteral("claude-opus-4-6"), QStringLiteral("max")), QStringLiteral("max"));
        QCOMPARE(effortFor(QStringLiteral("claude-opus-4-5"), QStringLiteral("max")), QStringLiteral("high"));
        QCOMPARE(effortFor(QStringLiteral("claude-opus-5-5"), QStringLiteral("bogus")), QStringLiteral("medium"));
    }

    void convert_toolLoop_pairsCallsAndResults()
    {
        QJsonObject assistant = msg(QStringLiteral("assistant"), QJsonValue::Null);
        assistant[QStringLiteral("tool_calls")] = QJsonArray{
            toolCall(QStringLiteral("toolu_1"), QStringLiteral("insert_events"),
                     QStringLiteral("{\"track\":1}")),
            toolCall(QStringLiteral("toolu_2"), QStringLiteral("get_editor_state"), QString())};

        const QJsonArray messages{
            msg(QStringLiteral("developer"), QStringLiteral("SYS")),
            msg(QStringLiteral("user"), QStringLiteral("Make a melody")),
            msg(QStringLiteral("developer"), QStringLiteral("## Current Agent State 1")),
            assistant,
            toolResult(QStringLiteral("toolu_1"), QStringLiteral("{\"success\":true}")),
            toolResult(QStringLiteral("toolu_2"), QStringLiteral("{\"ok\":1}")),
            msg(QStringLiteral("system"), QStringLiteral("## Current Agent State 2")),
        };

        QJsonArray out;
        QString system;
        convertMessages(messages, out, system);
        QCOMPARE(system, QStringLiteral("SYS"));
        QCOMPARE(out.size(), 3);

        // The user's message, then the note added before the first request.
        const QJsonArray first = out.at(0).toObject().value(QStringLiteral("content")).toArray();
        QCOMPARE(first.size(), 2);
        QCOMPARE(first.at(1).toObject().value(QStringLiteral("text")).toString(),
                 QStringLiteral("## Current Agent State 1"));

        const QJsonObject calls = out.at(1).toObject();
        QCOMPARE(calls.value(QStringLiteral("role")).toString(), QStringLiteral("assistant"));
        const QJsonArray uses = calls.value(QStringLiteral("content")).toArray();
        QCOMPARE(uses.size(), 2);
        QCOMPARE(type(uses.at(0)), QStringLiteral("tool_use"));
        QCOMPARE(uses.at(0).toObject().value(QStringLiteral("input")).toObject()
                     .value(QStringLiteral("track")).toInt(), 1);
        QVERIFY(uses.at(1).toObject().value(QStringLiteral("input")).isObject()); // empty args -> {}

        // Both results in ONE user turn, first, and the next note after them.
        const QJsonArray results = out.at(2).toObject().value(QStringLiteral("content")).toArray();
        QCOMPARE(results.size(), 3);
        QCOMPARE(type(results.at(0)), QStringLiteral("tool_result"));
        QCOMPARE(results.at(0).toObject().value(QStringLiteral("tool_use_id")).toString(),
                 QStringLiteral("toolu_1"));
        QCOMPARE(type(results.at(1)), QStringLiteral("tool_result"));
        QCOMPARE(type(results.at(2)), QStringLiteral("text"));
    }

    void convert_replayContentWins()
    {
        const QJsonArray replay{
            QJsonObject{{QStringLiteral("type"), QStringLiteral("thinking")},
                        {QStringLiteral("thinking"), QString()},
                        {QStringLiteral("signature"), QStringLiteral("SIG")}},
            QJsonObject{{QStringLiteral("type"), QStringLiteral("tool_use")},
                        {QStringLiteral("id"), QStringLiteral("toolu_1")},
                        {QStringLiteral("name"), QStringLiteral("get_editor_state")},
                        {QStringLiteral("input"), QJsonObject()}}};
        QJsonObject assistant = msg(QStringLiteral("assistant"), QJsonValue::Null);
        assistant[QStringLiteral("tool_calls")] = QJsonArray{
            toolCall(QStringLiteral("toolu_1"), QStringLiteral("get_editor_state"), QStringLiteral("{}"))};
        assistant[replayContentKey()] = replay;

        QJsonArray out;
        QString system;
        convertMessages(QJsonArray{msg(QStringLiteral("user"), QStringLiteral("Hi")), assistant,
                                   toolResult(QStringLiteral("toolu_1"), QStringLiteral("{}"))},
                        out, system);
        QCOMPARE(out.at(1).toObject().value(QStringLiteral("content")).toArray(), replay);
    }

    void convert_opensWithAUserTurn_andSkipsEmptyText()
    {
        QJsonArray out;
        QString system;
        convertMessages(QJsonArray{msg(QStringLiteral("assistant"), QStringLiteral("Earlier answer")),
                                   msg(QStringLiteral("user"), QString()),
                                   msg(QStringLiteral("user"), QStringLiteral("Next"))},
                        out, system);
        QCOMPARE(out.size(), 3);
        QCOMPARE(out.at(0).toObject().value(QStringLiteral("role")).toString(), QStringLiteral("user"));
        const QJsonArray last = out.at(2).toObject().value(QStringLiteral("content")).toArray();
        QCOMPARE(last.size(), 1); // the empty message added no block
    }

    void convert_textWithoutVisibleText_isLeftOut()
    {
        // The API rejects a text block that holds only whitespace - in a
        // message from another provider's history and in Claude's own reply.
        QJsonObject assistant = msg(QStringLiteral("assistant"), QStringLiteral("\n"));
        assistant[QStringLiteral("tool_calls")] = QJsonArray{
            toolCall(QStringLiteral("toolu_1"), QStringLiteral("get_editor_state"), QStringLiteral("{}"))};
        QJsonArray out;
        QString system;
        convertMessages(QJsonArray{msg(QStringLiteral("user"), QStringLiteral("Hi")), assistant,
                                   toolResult(QStringLiteral("toolu_1"), QStringLiteral("{}")),
                                   msg(QStringLiteral("developer"), QStringLiteral("  \n"))},
                        out, system);
        QCOMPARE(out.size(), 3);
        const QJsonArray calls = out.at(1).toObject().value(QStringLiteral("content")).toArray();
        QCOMPARE(calls.size(), 1);
        QCOMPARE(type(calls.at(0)), QStringLiteral("tool_use"));
        QCOMPARE(out.at(2).toObject().value(QStringLiteral("content")).toArray().size(), 1);

        const QJsonArray replay = replayContent(QJsonArray{
            QJsonObject{{QStringLiteral("type"), QStringLiteral("thinking")},
                        {QStringLiteral("thinking"), QString()},
                        {QStringLiteral("signature"), QStringLiteral("S")}},
            QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                        {QStringLiteral("text"), QStringLiteral("\n\n")}},
            QJsonObject{{QStringLiteral("type"), QStringLiteral("tool_use")},
                        {QStringLiteral("id"), QStringLiteral("toolu_2")},
                        {QStringLiteral("name"), QStringLiteral("x")},
                        {QStringLiteral("input"), QJsonObject()}}});
        QCOMPARE(replay.size(), 2);
        QCOMPARE(type(replay.at(0)), QStringLiteral("thinking")); // an empty thinking text stays
        QCOMPARE(type(replay.at(1)), QStringLiteral("tool_use"));
    }

    void replayContent_staysOffOtherProviders()
    {
        QJsonObject assistant = msg(QStringLiteral("assistant"), QStringLiteral("Hi"));
        assistant[replayContentKey()] = QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("text")}}};
        const QJsonArray cleaned = withoutReplayContent(
            QJsonArray{msg(QStringLiteral("user"), QStringLiteral("Hello")), assistant});
        QCOMPARE(cleaned.size(), 2);
        QVERIFY(!cleaned.at(1).toObject().contains(replayContentKey()));
        QCOMPARE(cleaned.at(1).toObject().value(QStringLiteral("content")).toString(), QStringLiteral("Hi"));
        QCOMPARE(cleaned.at(0), QJsonValue(msg(QStringLiteral("user"), QStringLiteral("Hello"))));
    }

    void convert_attachments_becomeImageAndDocumentBlocks()
    {
        const QJsonArray parts{
            QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                        {QStringLiteral("text"), QStringLiteral("Look at these")}},
            QJsonObject{{QStringLiteral("type"), QStringLiteral("image_url")},
                        {QStringLiteral("image_url"),
                         QJsonObject{{QStringLiteral("url"), QStringLiteral("data:image/png;base64,AAAA")}}}},
            QJsonObject{{QStringLiteral("type"), QStringLiteral("file")},
                        {QStringLiteral("file"),
                         QJsonObject{{QStringLiteral("filename"), QStringLiteral("score.pdf")},
                                     {QStringLiteral("file_data"),
                                      QStringLiteral("data:application/pdf;base64,JVBERi0=")}}}}};
        QJsonArray out;
        QString system;
        convertMessages(QJsonArray{msg(QStringLiteral("user"), parts)}, out, system);
        const QJsonArray blocks = out.at(0).toObject().value(QStringLiteral("content")).toArray();
        QCOMPARE(blocks.size(), 3);
        QCOMPARE(type(blocks.at(1)), QStringLiteral("image"));
        const QJsonObject imageSource = blocks.at(1).toObject().value(QStringLiteral("source")).toObject();
        QCOMPARE(imageSource.value(QStringLiteral("media_type")).toString(), QStringLiteral("image/png"));
        QCOMPARE(imageSource.value(QStringLiteral("data")).toString(), QStringLiteral("AAAA"));
        QCOMPARE(type(blocks.at(2)), QStringLiteral("document"));
        QCOMPARE(blocks.at(2).toObject().value(QStringLiteral("title")).toString(), QStringLiteral("score.pdf"));
        QCOMPARE(blocks.at(2).toObject().value(QStringLiteral("source")).toObject()
                     .value(QStringLiteral("media_type")).toString(), QStringLiteral("application/pdf"));
    }

    void body_streamed_cachesAndThinks()
    {
        RequestOptions o;
        o.model = QStringLiteral("claude-opus-5-5");
        o.stream = true;
        const QJsonObject body = requestBody(
            QJsonArray{msg(QStringLiteral("system"), QStringLiteral("SYS")),
                       msg(QStringLiteral("user"), QStringLiteral("Hi"))},
            chatTools(), o);

        QCOMPARE(body.value(QStringLiteral("model")).toString(), QStringLiteral("claude-opus-5-5"));
        QVERIFY(body.value(QStringLiteral("stream")).toBool());
        QCOMPARE(body.value(QStringLiteral("max_tokens")).toInt(), 64000);
        // The 2026-10-21 API version rejects sampling parameters.
        QVERIFY(!body.contains(QStringLiteral("temperature")));
        QVERIFY(!body.contains(QStringLiteral("top_p")));

        // Three cache breakpoints: last tool, system, top level.
        const QJsonArray tools = body.value(QStringLiteral("tools")).toArray();
        QCOMPARE(tools.size(), 2);
        QVERIFY(!tools.at(0).toObject().contains(QStringLiteral("cache_control")));
        QVERIFY(tools.at(1).toObject().contains(QStringLiteral("cache_control")));
        QVERIFY(tools.at(0).toObject().value(QStringLiteral("eager_input_streaming")).toBool());
        QVERIFY(tools.at(0).toObject().contains(QStringLiteral("input_schema")));
        QVERIFY(body.value(QStringLiteral("system")).toArray().at(0).toObject()
                    .contains(QStringLiteral("cache_control")));
        QVERIFY(body.contains(QStringLiteral("cache_control")));

        const QJsonObject thinking = body.value(QStringLiteral("thinking")).toObject();
        QCOMPARE(thinking.value(QStringLiteral("type")).toString(), QStringLiteral("adaptive"));
        QCOMPARE(thinking.value(QStringLiteral("display")).toString(), QStringLiteral("summarized"));
        QCOMPARE(thinking.value(QStringLiteral("block_binding")).toObject()
                     .value(QStringLiteral("prefix_mismatch_behavior")).toString(),
                 QStringLiteral("drop_block"));
        QCOMPARE(body.value(QStringLiteral("output_config")).toObject()
                     .value(QStringLiteral("effort")).toString(), QStringLiteral("medium"));
        QCOMPARE(body.value(QStringLiteral("fallbacks")).toString(), QStringLiteral("default"));

        const QByteArray beta = betaHeader(body);
        QVERIFY(beta.contains("thinking-binding-controls-2026-08-01"));
        QVERIFY(beta.contains("server-side-fallback-2026-07-01"));
    }

    void body_notStreamed_andTokenLimits()
    {
        RequestOptions o;
        o.model = QStringLiteral("claude-sonnet-5-5");
        QJsonObject body = requestBody(QJsonArray{msg(QStringLiteral("user"), QStringLiteral("Hi"))},
                                       chatTools(), o);
        QCOMPARE(body.value(QStringLiteral("max_tokens")).toInt(), 16000);
        QVERIFY(!body.contains(QStringLiteral("stream")));
        QVERIFY(!body.value(QStringLiteral("tools")).toArray().at(0).toObject()
                     .contains(QStringLiteral("eager_input_streaming")));

        o.maxTokens = 4096;
        body = requestBody(QJsonArray{msg(QStringLiteral("user"), QStringLiteral("Hi"))}, {}, o);
        QCOMPARE(body.value(QStringLiteral("max_tokens")).toInt(), 4096);

        o.model = QStringLiteral("claude-haiku-4-5");
        o.maxTokens = 100000;
        body = requestBody(QJsonArray{msg(QStringLiteral("user"), QStringLiteral("Hi"))}, {}, o);
        QCOMPARE(body.value(QStringLiteral("max_tokens")).toInt(), 64000); // the model's cap
    }

    void body_thinkingOff_perModel()
    {
        const QJsonArray hi{msg(QStringLiteral("user"), QStringLiteral("Hi"))};
        RequestOptions o;
        o.thinking = false;

        // Always thinks: adaptive without a displayed summary, low effort.
        o.model = QStringLiteral("claude-opus-5-5");
        QJsonObject body = requestBody(hi, {}, o);
        QVERIFY(body.contains(QStringLiteral("thinking")));
        QVERIFY(!body.value(QStringLiteral("thinking")).toObject().contains(QStringLiteral("display")));
        QCOMPARE(body.value(QStringLiteral("output_config")).toObject()
                     .value(QStringLiteral("effort")).toString(), QStringLiteral("low"));

        // Thinks only when asked: no thinking field at all.
        o.model = QStringLiteral("claude-opus-4-7");
        body = requestBody(hi, {}, o);
        QVERIFY(!body.contains(QStringLiteral("thinking")));
        QCOMPARE(body.value(QStringLiteral("output_config")).toObject()
                     .value(QStringLiteral("effort")).toString(), QStringLiteral("low"));
        QVERIFY(betaHeader(body).isEmpty());

        // No adaptive thinking and no effort.
        o.model = QStringLiteral("claude-haiku-4-5");
        body = requestBody(hi, {}, o);
        QVERIFY(!body.contains(QStringLiteral("thinking")));
        QVERIFY(!body.contains(QStringLiteral("output_config")));
        QVERIFY(!body.contains(QStringLiteral("fallbacks")));

        // Fable 5.2 rejects "disabled" and a thinking budget: off still sends
        // adaptive, and the effort is always explicit (its default is high).
        o.model = QStringLiteral("claude-fable-5-2");
        body = requestBody(hi, chatTools(), o);
        QCOMPARE(body.value(QStringLiteral("thinking")).toObject().value(QStringLiteral("type")).toString(),
                 QStringLiteral("adaptive"));
        QVERIFY(!body.value(QStringLiteral("thinking")).toObject().contains(QStringLiteral("budget_tokens")));
        QCOMPARE(body.value(QStringLiteral("output_config")).toObject()
                     .value(QStringLiteral("effort")).toString(), QStringLiteral("low"));
        QVERIFY(!body.contains(QStringLiteral("tool_choice"))); // auto by default, never forced
        QCOMPARE(body.value(QStringLiteral("fallbacks")).toString(), QStringLiteral("default"));

        // Effort "none" with the toggle on counts as off.
        o.model = QStringLiteral("claude-opus-5-5");
        o.thinking = true;
        o.effort = QStringLiteral("none");
        body = requestBody(hi, {}, o);
        QVERIFY(!body.value(QStringLiteral("thinking")).toObject().contains(QStringLiteral("display")));
        QCOMPARE(body.value(QStringLiteral("output_config")).toObject()
                     .value(QStringLiteral("effort")).toString(), QStringLiteral("low"));
    }

    void body_sequentialTools()
    {
        RequestOptions o;
        o.model = QStringLiteral("claude-opus-5-5");
        o.sequentialTools = true;
        const QJsonObject body = requestBody(
            QJsonArray{msg(QStringLiteral("user"), QStringLiteral("Hi"))}, chatTools(), o);
        const QJsonObject choice = body.value(QStringLiteral("tool_choice")).toObject();
        // Forced tool use (any / tool) is a 400 on current models - auto only.
        QCOMPARE(choice.value(QStringLiteral("type")).toString(), QStringLiteral("auto"));
        QVERIFY(choice.value(QStringLiteral("disable_parallel_tool_use")).toBool());
    }

    void message_toolUse_becomesChatCompletion()
    {
        const QJsonObject message = sse(QStringLiteral("{"
            "'model':'claude-opus-5-5','stop_reason':'tool_use',"
            "'content':["
            "{'type':'thinking','thinking':'plan','signature':'S'},"
            "{'type':'text','text':'Writing.'},"
            "{'type':'tool_use','id':'toolu_1','name':'insert_events','input':{'a':1}}],"
            "'usage':{'input_tokens':10,'cache_read_input_tokens':1000,"
            "'cache_creation_input_tokens':50,'output_tokens':20}}"));
        const Result r = fromMessage(message);
        QCOMPARE(r.reasoning, QStringLiteral("plan"));
        QCOMPARE(r.toolCalls, 1);

        const QJsonObject choice = r.response.value(QStringLiteral("choices")).toArray().first().toObject();
        QCOMPARE(choice.value(QStringLiteral("finish_reason")).toString(), QStringLiteral("tool_calls"));
        const QJsonObject assistant = assistantOf(r);
        QCOMPARE(assistant.value(QStringLiteral("content")).toString(), QStringLiteral("Writing."));
        QCOMPARE(assistant.value(QStringLiteral("tool_calls")).toArray().at(0).toObject()
                     .value(QStringLiteral("function")).toObject()
                     .value(QStringLiteral("arguments")).toString(),
                 QStringLiteral("{\"a\":1}"));
        QCOMPARE(assistant.value(replayContentKey()).toArray(),
                 message.value(QStringLiteral("content")).toArray());

        // The whole prompt is input + cache reads + cache writes.
        const QJsonObject usage = r.response.value(QStringLiteral("usage")).toObject();
        QCOMPARE(usage.value(QStringLiteral("prompt_tokens")).toInt(), 1060);
        QCOMPARE(usage.value(QStringLiteral("completion_tokens")).toInt(), 20);
        QCOMPARE(usage.value(QStringLiteral("total_tokens")).toInt(), 1080);
        QCOMPARE(usage.value(QStringLiteral("prompt_tokens_details")).toObject()
                     .value(QStringLiteral("cached_tokens")).toInt(), 1000);
    }

    void message_refusalAndTruncation()
    {
        const Result refused = fromMessage(sse(QStringLiteral("{'stop_reason':'refusal','stop_details':{'type':'refusal','category':'cyber'},'content':[]}")));
        QCOMPARE(refused.refusalCategory, QStringLiteral("cyber"));
        QCOMPARE(refused.response.value(QStringLiteral("choices")).toArray().first().toObject()
                     .value(QStringLiteral("finish_reason")).toString(),
                 QStringLiteral("content_filter"));
        QVERIFY(refusalMessage(refused).contains(QStringLiteral("cyber")));

        const Result cut = fromMessage(sse(QStringLiteral("{'stop_reason':'max_tokens','content':[{'type':'text','text':'Half'}]}")));
        QCOMPARE(cut.response.value(QStringLiteral("choices")).toArray().first().toObject()
                     .value(QStringLiteral("finish_reason")).toString(),
                 QStringLiteral("length"));
    }

    void replay_afterFallback_keepsOnlyTheDeclinedText()
    {
        const QJsonArray content = sse(QStringLiteral("{'c':["
            "{'type':'thinking','thinking':'','signature':'A'},"
            "{'type':'tool_use','id':'toolu_a','name':'x','input':{}},"
            "{'type':'text','text':'partial'},"
            "{'type':'fallback','from':{'model':'claude-opus-5-5'},'to':{'model':'claude-opus-5'}},"
            "{'type':'thinking','thinking':'','signature':'B'},"
            "{'type':'text','text':'answer'},"
            "{'type':'tool_use','id':'toolu_b','name':'y','input':{}}]}"))
                                       .value(QStringLiteral("c")).toArray();
        const QJsonArray replay = replayContent(content);
        QCOMPARE(replay.size(), 4);
        QCOMPARE(replay.at(0).toObject().value(QStringLiteral("text")).toString(), QStringLiteral("partial"));
        QCOMPARE(replay.at(1).toObject().value(QStringLiteral("signature")).toString(), QStringLiteral("B"));
        QCOMPARE(replay.at(3).toObject().value(QStringLiteral("id")).toString(), QStringLiteral("toolu_b"));

        QJsonObject message;
        message[QStringLiteral("content")] = content;
        message[QStringLiteral("stop_reason")] = QStringLiteral("tool_use");
        const Result r = fromMessage(message);
        QCOMPARE(r.toolCalls, 1); // the declined model's call never runs
    }

    void stream_rebuildsTheMessage()
    {
        StreamAccumulator acc;
        QList<StreamEvent> live;
        for (const QJsonObject &e : toolTurnEvents(QStringLiteral("\"events\": []}")))
            live += acc.consume(e);

        QList<StreamEvent::Kind> kinds;
        for (const StreamEvent &e : live)
            kinds << e.kind;
        const QList<StreamEvent::Kind> expected{
            StreamEvent::Reasoning, StreamEvent::Reasoning, StreamEvent::Text, StreamEvent::Text,
            StreamEvent::ToolStarted, StreamEvent::ToolArgs, StreamEvent::ToolArgs, StreamEvent::ToolDone};
        QCOMPARE(kinds, expected);
        QCOMPARE(live.at(4).id, QStringLiteral("toolu_9"));
        QCOMPARE(live.at(4).text, QStringLiteral("insert_events"));

        const Result r = acc.finish();
        QVERIFY(r.errorType.isEmpty());
        QCOMPARE(r.text, QStringLiteral("Writing notes."));
        QCOMPARE(r.stopReason, QStringLiteral("tool_use"));
        const QJsonObject assistant = assistantOf(r);
        const QJsonObject args = QJsonDocument::fromJson(
            assistant.value(QStringLiteral("tool_calls")).toArray().at(0).toObject()
                .value(QStringLiteral("function")).toObject()
                .value(QStringLiteral("arguments")).toString().toUtf8()).object();
        QCOMPARE(args.value(QStringLiteral("track")).toInt(), 2);
        QVERIFY(args.value(QStringLiteral("events")).isArray());

        // The thinking block goes back with its text and signature.
        const QJsonObject thinking = assistant.value(replayContentKey()).toArray().at(0).toObject();
        QCOMPARE(thinking.value(QStringLiteral("thinking")).toString(), QStringLiteral("Plan it."));
        QCOMPARE(thinking.value(QStringLiteral("signature")).toString(), QStringLiteral("SIG"));

        // message_start's input counts plus message_delta's output count.
        const QJsonObject usage = r.response.value(QStringLiteral("usage")).toObject();
        QCOMPARE(usage.value(QStringLiteral("prompt_tokens")).toInt(), 2005);
        QCOMPARE(usage.value(QStringLiteral("completion_tokens")).toInt(), 42);
    }

    void stream_brokenToolJson_isReportedNotRun()
    {
        StreamAccumulator acc;
        for (const QJsonObject &e : toolTurnEvents(QStringLiteral("\"events\": [")))
            acc.consume(e);
        const Result r = acc.finish();
        QCOMPARE(r.errorType, QStringLiteral("invalid_tool_json"));
        // Worded for AiClient::errorIndicatesUnparsableToolCall(): a tool
        // call, and JSON that did not parse.
        const QString lower = r.errorMessage.toLower();
        QVERIFY(lower.contains(QStringLiteral("tool call")));
        QVERIFY(lower.contains(QStringLiteral("invalid json")));
        QVERIFY(r.errorMessage.contains(QStringLiteral("insert_events")));
    }

    void stream_errorEvent_isKept()
    {
        StreamAccumulator acc;
        acc.consume(sse(QStringLiteral("{'type':'message_start','message':{'model':'claude-opus-5-5','usage':{'input_tokens':3}}}")));
        acc.consume(sse(QStringLiteral("{'type':'content_block_start','index':0,'content_block':{'type':'text','text':''}}")));
        acc.consume(sse(QStringLiteral("{'type':'content_block_delta','index':0,'delta':{'type':'text_delta','text':'Hi'}}")));
        acc.consume(sse(QStringLiteral("{'type':'error','error':{'type':'overloaded_error','message':'Overloaded'}}")));
        const Result r = acc.finish();
        QCOMPARE(r.errorType, QStringLiteral("overloaded_error"));
        QCOMPARE(r.errorMessage, QStringLiteral("Overloaded"));
        QCOMPARE(r.text, QStringLiteral("Hi"));
    }

    void stream_withoutItsEnd_isNotAnAnswer()
    {
        // The connection closed after some text but before message_delta:
        // a fragment must not be handed on as the finished answer.
        StreamAccumulator acc;
        acc.consume(sse(QStringLiteral("{'type':'message_start','message':{'model':'claude-opus-5-5','usage':{'input_tokens':3}}}")));
        acc.consume(sse(QStringLiteral("{'type':'content_block_start','index':0,'content_block':{'type':'text','text':''}}")));
        acc.consume(sse(QStringLiteral("{'type':'content_block_delta','index':0,'delta':{'type':'text_delta','text':'Half an ans'}}")));
        const Result r = acc.finish();
        QCOMPARE(r.errorType, QStringLiteral("incomplete_stream"));
        // AgentRunner and MidiPilot retry on "connection".
        QVERIFY(r.errorMessage.contains(QStringLiteral("connection"), Qt::CaseInsensitive));
    }

    void stream_fallbackMidAnswer_dropsTheDeclinedToolCall()
    {
        StreamAccumulator acc;
        acc.consume(sse(QStringLiteral("{'type':'message_start','message':{'model':'claude-opus-5-5'}}")));
        acc.consume(sse(QStringLiteral("{'type':'content_block_start','index':0,'content_block':{'type':'tool_use','id':'toolu_a','name':'insert_events','input':{}}}")));
        acc.consume(jsonDelta(0, QStringLiteral("{\"tr")));
        acc.consume(sse(QStringLiteral("{'type':'content_block_start','index':1,'content_block':{'type':'fallback','from':{'model':'claude-opus-5-5'},'to':{'model':'claude-opus-5'}}}")));
        acc.consume(sse(QStringLiteral("{'type':'content_block_stop','index':1}")));
        acc.consume(sse(QStringLiteral("{'type':'content_block_start','index':2,'content_block':{'type':'text','text':''}}")));
        acc.consume(sse(QStringLiteral("{'type':'content_block_delta','index':2,'delta':{'type':'text_delta','text':'Answer'}}")));
        acc.consume(sse(QStringLiteral("{'type':'message_delta','delta':{'stop_reason':'end_turn'},'usage':{'output_tokens':5}}")));
        const Result r = acc.finish();
        QCOMPARE(r.toolCalls, 0);
        QCOMPARE(r.text, QStringLiteral("Answer"));
        // The declined call's broken JSON is not an error: it never runs.
        QVERIFY(r.errorType.isEmpty());
    }

    void agentLoop_requestsAreAppendOnly()
    {
        // What AgentRunner does with Claude: append the state, send, keep
        // the assistant turn with its replay blocks, append the results and
        // the next state. Each request must START with the previous one -
        // the prompt cache and every thinking block depend on it.
        RequestOptions o;
        o.model = QStringLiteral("claude-opus-5-5");
        o.stream = true;

        QJsonArray history{msg(QStringLiteral("developer"), QStringLiteral("SYS")),
                           msg(QStringLiteral("user"), QStringLiteral("Compose")),
                           msg(QStringLiteral("developer"), QStringLiteral("## State 1"))};
        const QJsonObject first = requestBody(history, chatTools(), o);

        StreamAccumulator acc;
        for (const QJsonObject &e : toolTurnEvents(QStringLiteral("\"events\": []}")))
            acc.consume(e);
        const QJsonObject message = assistantOf(acc.finish());
        QJsonObject kept; // AgentRunner::onApiResponse's cleanMsg
        kept[QStringLiteral("role")] = message.value(QStringLiteral("role"));
        kept[QStringLiteral("content")] = message.value(QStringLiteral("content"));
        kept[QStringLiteral("tool_calls")] = message.value(QStringLiteral("tool_calls"));
        kept[replayContentKey()] = message.value(replayContentKey());
        history.append(kept);
        history.append(toolResult(QStringLiteral("toolu_9"), QStringLiteral("{\"success\":true}")));
        history.append(msg(QStringLiteral("developer"), QStringLiteral("## State 2")));
        const QJsonObject second = requestBody(history, chatTools(), o);

        QCOMPARE(second.value(QStringLiteral("system")), first.value(QStringLiteral("system")));
        QCOMPARE(second.value(QStringLiteral("tools")), first.value(QStringLiteral("tools")));
        const QJsonArray a = first.value(QStringLiteral("messages")).toArray();
        const QJsonArray b = second.value(QStringLiteral("messages")).toArray();
        QVERIFY(b.size() > a.size());
        for (int i = 0; i < a.size(); ++i)
            QCOMPARE(b.at(i), a.at(i));

        // The replayed assistant turn opens with the signed thinking block.
        const QJsonArray replayed = b.at(1).toObject().value(QStringLiteral("content")).toArray();
        QCOMPARE(type(replayed.at(0)), QStringLiteral("thinking"));
        QCOMPARE(replayed.at(0).toObject().value(QStringLiteral("signature")).toString(),
                 QStringLiteral("SIG"));
    }

    void httpErrors_nameTheCause()
    {
        // A credit-balance message must NOT look like a content refusal
        // ("HTTP 400" makes MidiPilot strip attachments and retry).
        const QString credit = httpErrorMessage(
            400, QStringLiteral("invalid_request_error"),
            QStringLiteral("Your credit balance is too low to access the Anthropic API."));
        QVERIFY(!credit.contains(QStringLiteral("HTTP 400")));
        QVERIFY(credit.contains(QStringLiteral("credit balance")));

        const QString image = httpErrorMessage(400, QStringLiteral("invalid_request_error"),
                                               QStringLiteral("Could not process image"));
        QVERIFY(image.contains(QStringLiteral("HTTP 400")));
        QVERIFY(httpErrorMessage(404, QStringLiteral("not_found_error"), QStringLiteral("model: x"))
                    .contains(QStringLiteral("model name")));
    }
};

QTEST_GUILESS_MAIN(TestAnthropicMessages)
#include "test_anthropic_messages.moc"
