#include "AgentRunner.h"

#include "AiClient.h"
#include "ToolDefinitions.h"
#include "../gui/MidiPilotWidget.h"
#include "../midi/MidiFile.h"

#include <QJsonDocument>
#include <QCoreApplication>
#include <QSettings>

AgentRunner::AgentRunner(AiClient *client, QObject *parent)
    : QObject(parent),
      _client(client),
      _file(nullptr),
      _widget(nullptr),
      _maxSteps(15),
      _currentStep(0),
      _running(false),
      _cancelled(false)
{
}

void AgentRunner::run(const QString &systemPrompt,
                      const QJsonArray &conversationHistory,
                      const QString &userMessage,
                      MidiFile *file,
                      MidiPilotWidget *widget)
{
    Q_UNUSED(userMessage);  // Already included in conversationHistory
    if (_running) {
        emit errorOccurred("Agent is already running.");
        return;
    }

    _file = file;
    _widget = widget;
    _currentStep = 0;
    _running = true;
    _cancelled = false;

    // Read configurable step limit from settings
    QSettings settings;
    _maxSteps = settings.value("AI/agent_max_steps", 25).toInt();
    if (_maxSteps < 5) _maxSteps = 5;
    if (_maxSteps > 100) _maxSteps = 100;

    // Build initial messages
    _messages = QJsonArray();

    bool reasoning = _client->isReasoningModel();
    QJsonObject sysMsg;
    sysMsg["role"] = reasoning ? QString("developer") : QString("system");
    sysMsg["content"] = systemPrompt;
    _messages.append(sysMsg);

    // Conversation history (already includes the current user message)
    for (const QJsonValue &msg : conversationHistory) {
        _messages.append(msg);
    }

    // Get tool schemas
    _tools = ToolDefinitions::toolSchemas();

    // Connect to AiClient signals
    _responseConn = connect(_client, &AiClient::responseReceived,
                            this, &AgentRunner::onApiResponse);
    _errorConn = connect(_client, &AiClient::errorOccurred,
                         this, &AgentRunner::onApiError);

    sendNextRequest();
}

void AgentRunner::cancel()
{
    _cancelled = true;
    _client->cancelRequest();
    cleanup();
    emit errorOccurred("Agent cancelled by user.");
}

bool AgentRunner::isRunning() const
{
    return _running;
}

void AgentRunner::sendNextRequest()
{
    if (_cancelled) {
        cleanup();
        return;
    }

    if (_currentStep >= _maxSteps) {
        cleanup();
        emit finished("Agent reached maximum step limit (" +
                      QString::number(_maxSteps) + " steps).");
        return;
    }

    _client->sendMessages(_messages, _tools);
}

void AgentRunner::onApiResponse(const QString &content, const QJsonObject &fullResponse)
{
    if (!_running) return;

    QJsonArray choices = fullResponse["choices"].toArray();
    if (choices.isEmpty()) {
        cleanup();
        emit errorOccurred("Empty response from API during agent loop.");
        return;
    }

    QJsonObject firstChoice = choices[0].toObject();
    QJsonObject message = firstChoice["message"].toObject();
    QString finishReason = firstChoice["finish_reason"].toString();

    // Check if the model wants to call tools
    if (message.contains("tool_calls") && !message["tool_calls"].toArray().isEmpty()) {
        // Strip response-only fields (annotations, refusal, etc.) before re-sending
        // to avoid HTTP 400 "could not parse JSON body" from the API.
        QJsonObject cleanMsg;
        cleanMsg["role"] = message["role"];
        cleanMsg["content"] = message["content"];
        cleanMsg["tool_calls"] = message["tool_calls"];
        _messages.append(cleanMsg);

        processToolCalls(message);
        return;
    }

    // No tool calls — this is the final response
    cleanup();
    emit finished(content.isEmpty() ? "Done." : content);
}

void AgentRunner::onApiError(const QString &error)
{
    if (!_running) return;
    cleanup();
    emit errorOccurred(error);
}

void AgentRunner::processToolCalls(const QJsonObject &assistantMessage)
{
    QJsonArray toolCalls = assistantMessage["tool_calls"].toArray();

    // Emit all planned steps with descriptive labels upfront
    int firstStep = _currentStep + 1;
    QStringList plannedLabels;
    for (const QJsonValue &tcVal : toolCalls) {
        QJsonObject fn = tcVal.toObject()["function"].toObject();
        QString name = fn["name"].toString();
        QString argsStr = fn["arguments"].toString();
        QJsonObject args = QJsonDocument::fromJson(argsStr.toUtf8()).object();
        plannedLabels << buildStepLabel(name, args);
    }
    emit stepsPlanned(firstStep, plannedLabels);

    for (const QJsonValue &tcVal : toolCalls) {
        if (_cancelled) {
            cleanup();
            return;
        }

        QJsonObject tc = tcVal.toObject();
        QString toolCallId = tc["id"].toString();
        QJsonObject function = tc["function"].toObject();
        QString toolName = function["name"].toString();
        QString argsStr = function["arguments"].toString();

        // Parse arguments
        QJsonDocument argsDoc = QJsonDocument::fromJson(argsStr.toUtf8());
        QJsonObject args = argsDoc.object();

        _currentStep++;
        emit stepStarted(_currentStep, buildStepLabel(toolName, args));

        // Execute the tool
        QJsonObject result = ToolDefinitions::executeTool(toolName, args, _file, _widget);

        emit stepCompleted(_currentStep, toolName, result);

        // Allow Qt event loop to process repaints so editor updates are visible live
        QCoreApplication::processEvents();

        // Append tool result message
        QJsonObject toolResultMsg;
        toolResultMsg["role"] = QString("tool");
        toolResultMsg["tool_call_id"] = toolCallId;
        toolResultMsg["content"] = QString::fromUtf8(
            QJsonDocument(result).toJson(QJsonDocument::Compact));
        _messages.append(toolResultMsg);
    }

    // Send the next request with tool results
    sendNextRequest();
}

QString AgentRunner::buildStepLabel(const QString &toolName, const QJsonObject &args)
{
    if (toolName == "create_track") {
        QString name = args["trackName"].toString();
        return name.isEmpty() ? QStringLiteral("Create track")
                              : QStringLiteral("Create track \u2014 %1").arg(name);
    }
    if (toolName == "insert_events") {
        int track = args["trackIndex"].toInt(-1);
        int count = args["events"].toArray().size();
        if (count == 0) {
            // Compact format: count semicolons + 1
            QString evts = args["events"].toString();
            if (!evts.isEmpty()) count = evts.count(';') + 1;
        }
        return QStringLiteral("Insert events \u2014 Track %1 (%2 events)").arg(track).arg(count);
    }
    if (toolName == "replace_events") {
        int track = args["trackIndex"].toInt(-1);
        int count = args["events"].toArray().size();
        if (count == 0) {
            QString evts = args["events"].toString();
            if (!evts.isEmpty()) count = evts.count(';') + 1;
        }
        return QStringLiteral("Replace events \u2014 Track %1 (%2 events)").arg(track).arg(count);
    }
    if (toolName == "delete_events") {
        int track = args["trackIndex"].toInt(-1);
        return QStringLiteral("Delete events \u2014 Track %1").arg(track);
    }
    if (toolName == "rename_track") {
        int track = args["trackIndex"].toInt(-1);
        QString name = args["newName"].toString();
        return QStringLiteral("Rename track \u2014 Track %1 \u2192 %2").arg(track).arg(name);
    }
    if (toolName == "set_channel") {
        int track = args["trackIndex"].toInt(-1);
        int ch = args["channel"].toInt(-1);
        return QStringLiteral("Set channel \u2014 Track %1 \u2192 Ch %2").arg(track).arg(ch);
    }
    if (toolName == "set_tempo") {
        int bpm = args["bpm"].toInt(0);
        return bpm > 0 ? QStringLiteral("Set tempo \u2014 %1 BPM").arg(bpm)
                       : QStringLiteral("Set tempo");
    }
    if (toolName == "set_time_signature") {
        int num = args["numerator"].toInt(0);
        int den = args["denominator"].toInt(0);
        return (num > 0 && den > 0)
            ? QStringLiteral("Set time signature \u2014 %1/%2").arg(num).arg(den)
            : QStringLiteral("Set time signature");
    }
    if (toolName == "move_events_to_track") {
        int from = args["sourceTrackIndex"].toInt(-1);
        int to = args["targetTrackIndex"].toInt(-1);
        return QStringLiteral("Move events \u2014 Track %1 \u2192 Track %2").arg(from).arg(to);
    }
    if (toolName == "query_events") {
        int track = args["trackIndex"].toInt(-1);
        return QStringLiteral("Query events \u2014 Track %1").arg(track);
    }
    if (toolName == "get_track_info") {
        return QStringLiteral("Get track info");
    }
    if (toolName == "get_editor_state") {
        return QStringLiteral("Get editor state");
    }
    return toolName;
}

void AgentRunner::cleanup()
{
    _running = false;
    disconnect(_responseConn);
    disconnect(_errorConn);
    _file = nullptr;
    _widget = nullptr;
}
