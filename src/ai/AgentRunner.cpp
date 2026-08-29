#include "AgentRunner.h"

#include "AiClient.h"
#include "EditorContext.h"
#include "ToolDefinitions.h"
#include "../AppPaths.h"
#include "../gui/MidiPilotWidget.h"
#include "../midi/MidiFile.h"

#include <QJsonDocument>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QRegularExpression>
#include <QSettings>
#include <QTextStream>
#include <QTimer>

#include <limits>

namespace {
QString agentLogFilePath()
{
    return AppPaths::dataFilePath(QStringLiteral("midipilot_api.log"));
}

void logAgentToolResult(int step, const QString &toolName, const QJsonObject &result)
{
    QFile file(agentLogFilePath());
    if (!file.open(QIODevice::Append | QIODevice::Text))
        return;

    const QString error = result.value(QStringLiteral("error")).toString();
    const QString guidance = result.value(QStringLiteral("guidance")).toString();
    QTextStream stream(&file);
    stream << QDateTime::currentDateTime().toString(Qt::ISODate)
           << " [AGENT-TOOL-RESULT] step=" << step
           << " tool=" << toolName
           << " success=" << (result.value(QStringLiteral("success")).toBool() ? "true" : "false")
           << " recoverable=" << (result.value(QStringLiteral("recoverable")).toBool() ? "true" : "false")
           << " error=" << error.left(220)
           << " guidance=" << guidance.left(220)
           << " raw=" << QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact)).left(1200)
           << '\n';
}

QString compactText(QString text, int maxChars)
{
    text.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    text = text.trimmed();
    if (text.size() <= maxChars)
        return text;
    return text.left(qMax(0, maxChars - 3)) + QStringLiteral("...");
}

void appendFact(AgentRunner::AgentWorkingState &state, const QString &fact)
{
    const QString compact = compactText(fact, 220);
    if (compact.isEmpty())
        return;
    if (!state.confirmedFacts.contains(compact))
        state.confirmedFacts.append(compact);

    while (state.confirmedFacts.join(QStringLiteral("; ")).size() > 1000 && state.confirmedFacts.size() > 1)
        state.confirmedFacts.removeFirst();
}

QString eventRangeText(const QJsonObject &args)
{
    int startTick = args.value(QStringLiteral("startTick")).toInt(std::numeric_limits<int>::max());
    int endTick = args.value(QStringLiteral("endTick")).toInt(std::numeric_limits<int>::min());

    const QJsonArray events = args.value(QStringLiteral("events")).toArray();
    for (const QJsonValue &value : events) {
        const QJsonObject event = value.toObject();
        if (!event.contains(QStringLiteral("tick")))
            continue;
        const int tick = event.value(QStringLiteral("tick")).toInt();
        startTick = qMin(startTick, tick);
        endTick = qMax(endTick, tick + qMax(0, event.value(QStringLiteral("duration")).toInt(0)));
    }

    if (startTick == std::numeric_limits<int>::max() || endTick == std::numeric_limits<int>::min())
        return QString();
    return QStringLiteral(" ticks %1-%2").arg(startTick).arg(endTick);
}

QString firstDeveloperRole(const QJsonArray &messages)
{
    if (messages.isEmpty())
        return QStringLiteral("system");
    const QString role = messages.first().toObject().value(QStringLiteral("role")).toString();
    return role == QStringLiteral("developer") ? QStringLiteral("developer") : QStringLiteral("system");
}

/// Live FFXIV-mode flag. The tool schemas gate the FFXIV bundle on the same
/// key (ToolDefinitions::toolSchemas), and set_ffxiv_mode writes it, so this
/// is the one source of truth for "is the mode on right now".
bool ffxivModeActive()
{
    return AppPaths::settings()
        ->value(QStringLiteral("AI/ffxiv_mode"), false).toBool();
}

/// The FFXIV limits sentence carried by the per-turn state layer. Shared by
/// initialWorkingState() and the mid-run mode switch so the two cannot drift
/// (the switch also has to be able to REMOVE it again verbatim).
QString ffxivStateConstraints()
{
    return QStringLiteral(
        " FFXIV Bard Performance: <=16 simultaneous voices, "
        "<=14 notes/sec per channel, range C3..C6. Call analyze_voice_load "
        "after dense passages to verify before finishing.");
}

/// Names of the tools that appear/disappear with FFXIV mode - quoted to the
/// model when the mode flips so it knows what just changed in its tool list.
QString ffxivBundleNames()
{
    return QStringLiteral("validate_ffxiv, convert_drums_ffxiv, "
                          "setup_channel_pattern, analyze_voice_load, "
                          "auto_fit_voice_load");
}

void logAgentState(int step, const AgentRunner::AgentWorkingState &state)
{
    QFile file(agentLogFilePath());
    if (!file.open(QIODevice::Append | QIODevice::Text))
        return;

    QTextStream stream(&file);
    stream << QDateTime::currentDateTime().toString(Qt::ISODate)
           << " [AGENT-STATE] step=" << step
           << " task=" << AgentRunner::taskTypeName(state.taskType)
           << " confirmed=\"" << state.confirmedFacts.join(QStringLiteral("; ")).left(500) << "\""
           << " last=\"" << state.lastToolResult.left(240) << "\""
           << " next=\"" << state.nextStepHint.left(240) << "\""
           << '\n';
}
} // namespace

AgentRunner::TaskType AgentRunner::classifyTask(const QString &userMessage,
                                                const QString &systemPrompt)
{
    const QString text = (userMessage + QLatin1Char(' ') + systemPrompt).toLower();

    const QStringList compositionWords = {
        QStringLiteral("compose"), QStringLiteral("create song"), QStringLiteral("write a song"),
        QStringLiteral("arrange"), QStringLiteral("generate"), QStringLiteral("lofi"),
        QStringLiteral("octet"), QStringLiteral("orchestr"), QStringLiteral("komponiere"),
        QStringLiteral("erstelle"), QStringLiteral("arrangiere"), QStringLiteral("lied"),
        QStringLiteral("song")
    };
    for (const QString &word : compositionWords) {
        if (text.contains(word))
            return TaskType::Composition;
    }

    const QStringList repairWords = {
        QStringLiteral("repair"), QStringLiteral("fix"), QStringLiteral("validate"),
        QStringLiteral("clean"), QStringLiteral("ffxiv"), QStringLiteral("drum"),
        QStringLiteral("channel"), QStringLiteral("convert"), QStringLiteral("repariere"),
        QStringLiteral("korrigiere"), QStringLiteral("bereinige"), QStringLiteral("validiere")
    };
    for (const QString &word : repairWords) {
        if (text.contains(word))
            return TaskType::Repair;
    }

    const QStringList analysisWords = {
        QStringLiteral("analyze"), QStringLiteral("analyse"), QStringLiteral("explain"),
        QStringLiteral("what key"), QStringLiteral("what chord"), QStringLiteral("which chord"),
        QStringLiteral("show me"), QStringLiteral("tell me"), QStringLiteral("erklaere"),
        QStringLiteral("erkläre"), QStringLiteral("welche akkorde"), QStringLiteral("was ist")
    };
    for (const QString &word : analysisWords) {
        if (text.contains(word))
            return TaskType::Analysis;
    }

    return TaskType::Edit;
}

QString AgentRunner::taskTypeName(TaskType taskType)
{
    switch (taskType) {
    case TaskType::Composition: return QStringLiteral("composition");
    case TaskType::Edit: return QStringLiteral("edit");
    case TaskType::Analysis: return QStringLiteral("analysis");
    case TaskType::Repair: return QStringLiteral("repair");
    }
    return QStringLiteral("edit");
}

AgentRunner::AgentWorkingState AgentRunner::initialWorkingState(const QString &userMessage,
                                                                const QString &systemPrompt)
{
    AgentWorkingState state;
    state.goal = compactText(userMessage, 260);
    state.taskType = classifyTask(userMessage, systemPrompt);
    // Phase 31.3: neutralized constraint text. The previous wording singled out
    // pitch_bend as a placeholder anti-pattern, which leaked the token into
    // every agent run regardless of model. Per-model restrictions are now
    // expressed via AgentToolPolicy / sanitized guidance, not here.
    state.activeConstraints = QStringLiteral(
        "All exposed tools remain available. After a rejected write, choose a "
        "different valid action instead of retrying with placeholder values.");
    // Phase 32.6: surface FFXIV game limits whenever FFXIV mode is active so
    // the model is reminded every turn (and gets the analyze_voice_load tool
    // listed in its toolset).
    if (ffxivModeActive()) {
        state.activeConstraints += ffxivStateConstraints();
    }
    if (state.taskType == TaskType::Composition) {
        state.nextStepHint = QStringLiteral(
            "Use fewer coherent write rounds: set tempo/tracks, then insert substantial musical sections.");
    } else if (state.taskType == TaskType::Analysis) {
        state.nextStepHint = QStringLiteral("Prefer query/info tools and finish with a concise answer.");
    } else if (state.taskType == TaskType::Repair) {
        state.nextStepHint = QStringLiteral("Use deterministic repair or validation tools first, then validate the result.");
    } else {
        state.nextStepHint = QStringLiteral("Inspect only if needed, then make one corrected write per target.");
    }
    return state;
}

void AgentRunner::updateWorkingStateFromToolResult(AgentWorkingState &state,
                                                   const QString &toolName,
                                                   const QJsonObject &args,
                                                   const QJsonObject &result)
{
    const bool success = result.value(QStringLiteral("success")).toBool(false);
    if (!success) {
        const QString error = result.value(QStringLiteral("error")).toString();
        const QString guidance = result.value(QStringLiteral("guidance")).toString();
        state.lastToolResult = compactText(QStringLiteral("%1 rejected: %2 %3")
                                           .arg(toolName, error, guidance), 360);

        const QString lower = (error + QLatin1Char(' ') + guidance).toLower();
        // NOTE: a pre-Phase-31 special branch here detected "pitch_bend ... only"
        // in the rejection text and rewrote the next-step hint + active
        // constraints so the model would stop retrying pitch_bend-only writes.
        // Phase 31 (`AgentToolPolicy`) replaced this with sanitized,
        // positive-only guidance for `gpt-5.5*` (the only family that ever
        // hit the trap). Keeping the branch for other models meant their
        // working-state would echo the literal token "pitch_bend" back into
        // the prompt the moment any error string mentioned it — leaking the
        // exact pattern Phase 31 is trying to suppress. The generic `else`
        // branch below already handles the failure (increments
        // `repeatedFailureCount`, falls back to provider guidance) which is
        // what every non-5.5 model needs.
        if (lower.contains(QStringLiteral("repeated identical write"))) {
            ++state.repeatedFailureCount;
            state.nextStepHint = QStringLiteral(
                "Do not repeat the blocked write signature; choose a different tool call or finish with a status answer.");
        } else {
            ++state.repeatedFailureCount;
            state.nextStepHint = compactText(guidance.isEmpty()
                                             ? QStringLiteral("Choose a different valid next action.")
                                             : guidance, 260);
        }
        return;
    }

    state.repeatedFailureCount = 0;
    state.lastToolResult = compactText(QStringLiteral("%1 succeeded")
                                       .arg(toolName), 240);

    if (toolName == QStringLiteral("create_track")) {
        const int index = result.value(QStringLiteral("trackIndex")).toInt(args.value(QStringLiteral("trackIndex")).toInt(-1));
        const QString name = args.value(QStringLiteral("trackName")).toString(result.value(QStringLiteral("trackName")).toString());
        appendFact(state, QStringLiteral("Created track %1 \"%2\"").arg(index).arg(name));
    } else if (toolName == QStringLiteral("set_tempo")) {
        appendFact(state, QStringLiteral("Tempo set to %1 BPM").arg(args.value(QStringLiteral("bpm")).toInt()));
    } else if (toolName == QStringLiteral("insert_events") || toolName == QStringLiteral("replace_events")) {
        const int track = args.value(QStringLiteral("trackIndex")).toInt(result.value(QStringLiteral("trackIndex")).toInt(-1));
        const int count = args.value(QStringLiteral("events")).toArray().size();
        appendFact(state, QStringLiteral("%1 ok track %2 count %3%4")
                   .arg(toolName).arg(track).arg(count).arg(eventRangeText(args)));
        if (state.taskType == TaskType::Composition)
            state.nextStepHint = QStringLiteral("Continue with the next requested track/section, or finish if all sections are confirmed.");
    } else if (toolName == QStringLiteral("query_events")) {
        state.lastToolResult = QStringLiteral("query_events succeeded: %1 events on track %2")
            .arg(result.value(QStringLiteral("count")).toInt())
            .arg(args.value(QStringLiteral("trackIndex")).toInt(-1));
    } else if (toolName == QStringLiteral("get_track_info")) {
        const QJsonObject track = result.value(QStringLiteral("track")).toObject();
        state.lastToolResult = QStringLiteral("get_track_info succeeded: track %1 has %2 events")
            .arg(track.value(QStringLiteral("index")).toInt(args.value(QStringLiteral("trackIndex")).toInt(-1)))
            .arg(track.value(QStringLiteral("eventCount")).toInt());
    } else if (toolName == QStringLiteral("get_editor_state")) {
        const QJsonObject stateObj = result.value(QStringLiteral("state")).toObject();
        const QJsonArray tracks = stateObj.value(QStringLiteral("tracks")).toArray();
        state.lastToolResult = tracks.isEmpty()
            ? QStringLiteral("get_editor_state succeeded")
            : QStringLiteral("get_editor_state succeeded: %1 tracks").arg(tracks.size());
    } else if (toolName == QStringLiteral("validate_ffxiv")) {
        appendFact(state, result.value(QStringLiteral("summary")).toString(QStringLiteral("FFXIV validation completed")));
    } else if (toolName == QStringLiteral("analyze_voice_load")) {
        appendFact(state, result.value(QStringLiteral("summary")).toString(QStringLiteral("Voice-load analysis completed")));
    } else if (toolName == QStringLiteral("setup_channel_pattern")) {
        appendFact(state, QStringLiteral("FFXIV channel pattern configured"));
    } else if (toolName == QStringLiteral("convert_tempo_preserve_duration")) {
        // Keep the dry-run numbers in the working state so the confirm turn
        // still knows what it is confirming.
        appendFact(state, result.value(QStringLiteral("summary")).toString(
                              QStringLiteral("Tempo conversion completed")));
    } else if (toolName == QStringLiteral("thin_tempo_map")) {
        appendFact(state, result.value(QStringLiteral("summary")).toString(
                              QStringLiteral("Tempo map thinning completed")));
    } else if (toolName == QStringLiteral("list_documents")) {
        state.lastToolResult = QStringLiteral(
            "list_documents succeeded: %1 open document(s)")
            .arg(result.value(QStringLiteral("documents")).toArray().size());
    } else if (toolName == QStringLiteral("get_document_overview")) {
        state.lastToolResult = compactText(
            result.value(QStringLiteral("summary")).toString(
                QStringLiteral("get_document_overview succeeded")), 240);
    } else if (toolName == QStringLiteral("import_tracks_from_document")) {
        // Keep the dry-run numbers (collisions, tick scaling, tempo-map
        // difference) in the working state so the confirm turn still knows
        // what it is confirming - same idiom as convert_tempo/thin_tempo_map.
        appendFact(state, result.value(QStringLiteral("summary")).toString(
                              QStringLiteral("Track import completed")));
    } else if (toolName == QStringLiteral("switch_document")) {
        if (!result.value(QStringLiteral("alreadyBound")).toBool(false)) {
            // Title concatenated, never .arg()-substituted (file-name input).
            appendFact(state, QStringLiteral("Run re-bound to document '")
                                  + result.value(QStringLiteral("title")).toString()
                                  + QStringLiteral("' - reads, writes and undo "
                                                   "steps now act there"));
            state.nextStepHint = QStringLiteral(
                "Call get_editor_state to read the newly bound document "
                "before editing it.");
        } else {
            state.lastToolResult = QStringLiteral(
                "switch_document: already bound to that document");
        }
    }
}

QString AgentRunner::stateLayerContent(const AgentWorkingState &state)
{
    QStringList lines;
    lines << QStringLiteral("## Current Agent State")
          << QStringLiteral("Goal: %1").arg(state.goal.isEmpty() ? QStringLiteral("current user request") : state.goal)
          << QStringLiteral("Task type: %1.").arg(taskTypeName(state.taskType))
          << QStringLiteral("Confirmed state: %1.").arg(state.confirmedFacts.isEmpty()
                                                       ? QStringLiteral("none yet")
                                                       : state.confirmedFacts.join(QStringLiteral("; ")))
          << QStringLiteral("Last tool result: %1.").arg(state.lastToolResult.isEmpty()
                                                        ? QStringLiteral("none yet")
                                                        : state.lastToolResult)
          << QStringLiteral("Active constraints: %1").arg(state.activeConstraints)
          << QStringLiteral("Next step: %1").arg(state.nextStepHint);
    return compactText(lines.join(QLatin1Char('\n')), 1400);
}

QJsonArray AgentRunner::messagesForNextRequest(const QJsonArray &messages,
                                               const AgentWorkingState &state)
{
    QJsonArray requestMessages = messages;
    QJsonObject stateMessage;
    stateMessage[QStringLiteral("role")] = firstDeveloperRole(messages);
    stateMessage[QStringLiteral("content")] = stateLayerContent(state);

    if (requestMessages.isEmpty()) {
        requestMessages.append(stateMessage);
    } else {
        requestMessages.insert(1, stateMessage);
    }
    return requestMessages;
}

AgentRunner::AgentRunner(AiClient *client, QObject *parent)
    : QObject(parent),
      _client(client),
      _file(nullptr),
      _widget(nullptr),
      _maxSteps(15),
      _currentStep(0),
      _running(false),
      _cancelled(false),
      _retryCount(0),
      _maxRetries(3)
{
}

void AgentRunner::run(const QString &systemPrompt,
                      const QJsonArray &conversationHistory,
                      const QString &userMessage,
                      MidiFile *file,
                      MidiPilotWidget *widget)
{
    if (_running) {
        emit errorOccurred("Agent is already running.");
        return;
    }

    _file = file;
    _widget = widget;
    // v2.3.1 cross-tab: remember the run's home document. `_file` may be
    // re-bound by an intercepted switch_document call; `_originFile` is what
    // the step labels compare against ("is this step landing elsewhere?").
    _originFile = file;
    _boundDocTitle.clear();
    _currentStep = 0;
    _running = true;
    _cancelled = false;
    _retryCount = 0;
    _lastWriteToolSignature.clear();
    _repeatedWriteToolCalls = 0;
    _consecutiveIncompleteWrites = 0;
    _workingState = initialWorkingState(userMessage, systemPrompt);

    // Phase 46 follow-up — mid-run FFXIV-mode switching. The caller appends
    // EditorContext::ffxivContext() to `systemPrompt` exactly when the mode is
    // on at this point, so the run-start setting also tells us whether the
    // rules are already in the prompt we were handed.
    _ffxivModeActiveInRun = ffxivModeActive();
    _ffxivRulesInBasePrompt = _ffxivModeActiveInRun;
    _ffxivOverlayIndex = -1;
    _pendingFfxivMode = -1;

    // Phase 31 — compute the model/task policy once per run.
    const bool isCompositionOrEdit =
        _workingState.taskType == TaskType::Composition
        || _workingState.taskType == TaskType::Edit;
    _policy = AgentToolPolicyUtil::buildPolicyFor(_client->model(),
                                                  _client->provider(),
                                                  isCompositionOrEdit);

    // Phase 47 — the active prompt profile may additionally forbid
    // `pitch_bend` for the models it is bound to (set by MidiPilotWidget
    // before every run). This is an AND, never an OR: the profile can only
    // take the branch away, so gpt-5.5*'s policy above is never widened.
    if (_profileDisallowsPitchBend) {
        _policy.allowPitchBendEvents = false;
        // Dependent flag. buildPolicyFor() does NOT derive the sanitized
        // rejection guidance from allowPitchBendEvents — it sets both from
        // the same gpt-5.5* model check — so without this line a
        // profile-flagged model would keep the guidance texts that spell out
        // the literal token `pitch_bend` in every rejection, re-anchoring the
        // exact word we just removed from its schema. The runtime guard
        // itself (isPitchBendOnlyPayload) is unconditional and applies
        // either way. `boundedIncompleteWriteStop` is deliberately NOT
        // enabled here: it changes when a run gives up, which is a different
        // mitigation and not what this switch promises.
        _policy.sanitizeRejectionGuidance = true;
    }

    qInfo().noquote() << QStringLiteral("[POLICY] model=%1 provider=%2 task=%3 flags=%4 profileNoPitchBend=%5")
        .arg(_client->model(),
             _client->provider(),
             taskTypeName(_workingState.taskType),
             AgentToolPolicyUtil::describe(_policy),
             _profileDisallowsPitchBend ? QStringLiteral("1") : QStringLiteral("0"));

    if (_policy.sanitizeRejectionGuidance) {
        // Phase 31: scrub the default working state so that the very first
        // request to gpt-5.5* contains no `pitch_bend` token, which would
        // otherwise re-anchor the model on the broken behaviour.
        _workingState.activeConstraints = QStringLiteral(
            "Composition writes require at least one note object with explicit "
            "pitch, velocity, duration, and tick. After a rejected write, "
            "choose a different valid action.");
        if (_workingState.taskType == TaskType::Composition) {
            _workingState.nextStepHint = QStringLiteral(
                "Use fewer coherent write rounds: set tempo/tracks, then insert "
                "substantial musical sections (program_change at tick 0 followed "
                "by note objects).");
        }
    }

    // Read configurable step limit from settings
    auto settingsPtr = AppPaths::settings();
    QSettings &settings = *settingsPtr;
    _maxSteps = settings.value("AI/agent_max_steps", 50).toInt();
    if (_maxSteps < 5) _maxSteps = 5;
    if (_maxSteps > 100) _maxSteps = 100;
    _maxRetries = settings.value("AI/agent_max_retries", 3).toInt();
    if (_maxRetries < 0) _maxRetries = 0;
    if (_maxRetries > 10) _maxRetries = 10;

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

    // Get tool schemas (Phase 31: schema-light for gpt-5.5* composition/edit)
    rebuildToolSchemas();

    logAgentState(_currentStep, _workingState);

    // Connect to AiClient signals
    _responseConn = connect(_client, &AiClient::responseReceived,
                            this, &AgentRunner::onApiResponse);
    _errorConn = connect(_client, &AiClient::errorOccurred,
                         this, &AgentRunner::onApiError);

    sendNextRequest();
}

void AgentRunner::rebuildToolSchemas()
{
    // ONE derivation site for the request tool list. run() called this at the
    // start and the mid-run FFXIV switch calls it again, so both go through
    // the same options (Phase 31 schema-light) and the same settings read
    // inside toolSchemas() that gates the FFXIV bundle.
    ToolDefinitions::ToolSchemaOptions schemaOpts;
    schemaOpts.includePitchBend = _policy.allowPitchBendEvents;
    // v2.3.1 cross-tab: only the AgentRunner opts in to the switch_document
    // definition - it intercepts the call before dispatch and re-binds the
    // run (no tab activation). The MCP server keeps the default options and
    // appends its OWN switch_document with the activate-the-tab contract.
    schemaOpts.includeDocumentSwitch = true;
    _tools = ToolDefinitions::toolSchemas(schemaOpts);
}

void AgentRunner::applyFfxivModeChange(bool enabled)
{
    // (a) TOOLS. toolSchemas() gates the five FFXIV tools on the live
    // `AI/ffxiv_mode` setting, which set_ffxiv_mode has already written, so
    // re-deriving the schemas is all it takes for the bundle to appear (or
    // disappear) on the next request OF THIS RUN. Without this the panel
    // checkbox flipped and MCP clients got tools/list_changed while the run
    // that asked for the switch kept the old 17-tool list to its last step.
    rebuildToolSchemas();

    if (enabled == _ffxivModeActiveInRun)
        return; // redundant call - the mode already was what it asks for
    _ffxivModeActiveInRun = enabled;

    // (b) STATE LAYER. Same sentence initialWorkingState() adds at run start,
    // so the per-turn reminder matches a run that started in FFXIV mode.
    if (enabled) {
        if (!_workingState.activeConstraints.contains(ffxivStateConstraints()))
            _workingState.activeConstraints += ffxivStateConstraints();
    } else {
        _workingState.activeConstraints.remove(ffxivStateConstraints());
    }

    // (c) RULES. The FFXIV rules + arrangement craft are composed ONCE per run
    // (by the caller, into the system prompt), so a mode that turns on mid-run
    // would otherwise never get them. Carry them as one system-side overlay
    // message we own: appending keeps the system prompt (and its prompt-cache
    // prefix) untouched, and owning the index lets us drop the overlay again
    // when the mode is switched back off instead of leaving stale rules in the
    // conversation.
    if (_ffxivOverlayIndex >= 0 && _ffxivOverlayIndex < _messages.size()) {
        _messages.removeAt(_ffxivOverlayIndex);
        _ffxivOverlayIndex = -1;
    }

    QString content;
    if (enabled) {
        content = QStringLiteral(
                      "## FFXIV BARD PERFORMANCE MODE SWITCHED ON\n"
                      "\n"
                      "The tools %1 are available from your next step. "
                      "Follow the FFXIV rules for the rest of this task.\n")
                      .arg(ffxivBundleNames());
        // Only when the rules are not already in the system prompt - a run
        // that STARTED in FFXIV mode already carries them.
        if (!_ffxivRulesInBasePrompt)
            content += EditorContext::ffxivContext();
    } else {
        content = QStringLiteral(
                      "## FFXIV BARD PERFORMANCE MODE SWITCHED OFF\n"
                      "\n"
                      "The tools %1 are no longer available - do not call them. "
                      "The FFXIV-specific rules (8 tracks, monophonic, C3-C6, "
                      "instrument track names) no longer apply.\n")
                      .arg(ffxivBundleNames());
    }

    QJsonObject overlay;
    overlay[QStringLiteral("role")] = firstDeveloperRole(_messages);
    overlay[QStringLiteral("content")] = content;
    _ffxivOverlayIndex = _messages.size();
    _messages.append(overlay);
}

void AgentRunner::setProfileDisallowsPitchBend(bool disallow)
{
    // Contract: the caller sets this before EVERY run() (MidiPilotWidget does
    // it where it resolves the prompt profile), so run() does not reset it.
    _profileDisallowsPitchBend = disallow;
}

void AgentRunner::cancel()
{
    _cancelled = true;
    // Disconnect BEFORE aborting — abort() can synchronously trigger
    // onReplyFinished → onApiError, which would double-fire errorOccurred
    // and double-cleanup while we're still inside cancel().
    cleanup();
    _client->cancelRequest();
    emit errorOccurred("Agent cancelled by user.");
}

bool AgentRunner::isRunning() const
{
    return _running;
}

void AgentRunner::continueRunning(int additionalSteps)
{
    if (!_running) return;
    _maxSteps += additionalSteps;
    if (_maxSteps > 500) _maxSteps = 500;
    sendNextRequest();
}

void AgentRunner::stopAtLimit()
{
    cleanup();
    emit finished("Agent stopped at step limit (" +
                  QString::number(_currentStep) + " steps).");
}

void AgentRunner::sendNextRequest()
{
    if (_cancelled) {
        cleanup();
        return;
    }

    if (_currentStep >= _maxSteps) {
        emit stepLimitReached(_currentStep, _maxSteps);
        return;
    }

    const QJsonArray requestMessages = messagesForNextRequest();
    logAgentState(_currentStep, _workingState);

    // Phase 31 — apply per-request policy overrides (always called so that
    // the previous run's override does not bleed into a different model).
    const QString reasoningOverride = _policy.overrideReasoningEffortLow
                                          ? QStringLiteral("low")
                                          : QString();
    _client->setNextRequestPolicyOverride(_policy.forceSequentialTools,
                                          reasoningOverride);

    if (_client->agentStreamingEnabled())
        _client->sendStreamingMessages(requestMessages, _tools);
    else
        _client->sendMessages(requestMessages, _tools);
}

QJsonArray AgentRunner::messagesForNextRequest() const
{
    return messagesForNextRequest(_messages, _workingState);
}

void AgentRunner::onApiResponse(const QString &content, const QJsonObject &fullResponse)
{
    if (!_running) return;

    // Successful response — reset the self-healing retry counter so future
    // failures get a fresh budget of attempts.
    _retryCount = 0;

    // TOOLS-INCAPABLE-EXPIRY: this request CARRIED the tool schemas and the
    // upstream answered it, which proves the model/route does accept tool
    // calling. Drop any stale flag (it may have been set by a transient
    // routing error) instead of waiting for the 7-day expiry.
    if (_client && !_tools.isEmpty())
        _client->clearToolsIncapableFlag(_client->provider(), _client->model());

    // Emit token usage if present
    QJsonObject usage = fullResponse["usage"].toObject();
    if (!usage.isEmpty()) {
        emit tokenUsageUpdated(usage["prompt_tokens"].toInt(),
                              usage["completion_tokens"].toInt(),
                              usage["total_tokens"].toInt());
    }

    QJsonArray choices = fullResponse["choices"].toArray();
    if (choices.isEmpty()) {
        // Treat as a transient empty-response failure and let the retry
        // path handle it (with hint + backoff).
        onApiError(QStringLiteral("Empty response from API during agent loop."));
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

    // Capability-gap detection — if the upstream said this model can't
    // do tool calling at all (typical OpenRouter HTTP 404 "No endpoints
    // found that support tool use"), retrying makes no sense. Mark the
    // model so the next user request short-circuits with a friendly
    // bubble, and surface a clear, actionable error now.
    if (_client && AiClient::errorIndicatesNoToolSupport(error)) {
        _client->markToolsIncapableForCurrentModel(error);
        cleanup();
        emit errorOccurred(tr(
            "Model does not support tool calling — pick a different model in "
            "Settings → AI, or switch to Simple mode for this request. "
            "(Provider response: %1)").arg(error.left(240)));
        return;
    }

    // Self-healing path — classify the error and decide whether it's safe
    // to retry. For recoverable categories we inject a short corrective
    // user-role hint into the conversation so the model knows what went
    // wrong, then re-issue the request after a small backoff.
    RetryKind kind = classifyError(error);
    if (kind != RetryKind::None && _retryCount < _maxRetries && !_cancelled) {
        _retryCount++;

        // Inject a corrective hint as a synthetic user message so the
        // assistant sees concrete feedback in its context. We keep it
        // short to avoid bloating prompt tokens.
        QString hint = hintForRetry(kind, error);
        if (!hint.isEmpty())
            hint += QStringLiteral("\n\n") + stateLayerContent(_workingState);
        if (!hint.isEmpty()) {
            QJsonObject hintMsg;
            hintMsg[QStringLiteral("role")] = QStringLiteral("user");
            hintMsg[QStringLiteral("content")] = hint;
            _messages.append(hintMsg);
        }

        emit agentRetrying(_retryCount, _maxRetries, error);

        // Backoff — exponential, capped at 4s. Lets transient network
        // hiccups settle and avoids hammering the provider.
        int delayMs = qMin(4000, 500 * (1 << (_retryCount - 1)));
        QTimer::singleShot(delayMs, this, [this]() {
            if (_running && !_cancelled) sendNextRequest();
        });
        return;
    }

    cleanup();
    QString surfaced = error;
    if (_retryCount > 0)
        surfaced = tr("%1 (after %2 retry attempts)").arg(error).arg(_retryCount);
    emit errorOccurred(surfaced);
}

AgentRunner::RetryKind AgentRunner::classifyError(const QString &error)
{
    const QString lower = error.toLower();
    // TOOLJSON-500: must be checked BEFORE the generic HTTP-5xx branch below.
    // A local server rejecting an unparsable tool call answers 500, so the
    // message used to land in RetryKind::Network - whose hint is deliberately
    // EMPTY ("transient, just retry silently"). So the one mechanism that
    // could have rescued the run - the corrective hint fed back as a synthetic
    // user message - stayed silent, and the retries were guaranteed to fail
    // the same way. This kind carries a hint that actually changes the next
    // attempt.
    if (AiClient::errorIndicatesUnparsableToolCall(error))
        return RetryKind::ToolCallCutOff;
    if (lower.contains(QStringLiteral("malformed")))
        return RetryKind::Malformed;
    if (lower.contains(QStringLiteral("max_tokens"))
        || lower.contains(QStringLiteral("max tokens"))
        || lower.contains(QStringLiteral("cut off"))
        || lower.contains(QStringLiteral("truncated")))
        return RetryKind::MaxTokens;
    if (lower.contains(QStringLiteral("empty response"))
        || lower.contains(QStringLiteral("ended without output"))
        || lower.contains(QStringLiteral("no text and no tool call")))
        return RetryKind::Empty;
    // Network / transient HTTP errors. We deliberately exclude 4xx auth
    // errors which are not recoverable by retrying.
    if (lower.contains(QStringLiteral("timeout"))
        || lower.contains(QStringLiteral("timed out"))
        || lower.contains(QStringLiteral("connection"))
        || lower.contains(QStringLiteral("network"))
        || lower.contains(QStringLiteral("http 5"))
        || lower.contains(QStringLiteral("503"))
        || lower.contains(QStringLiteral("502"))
        || lower.contains(QStringLiteral("504"))
        || lower.contains(QStringLiteral("500"))
        || lower.contains(QStringLiteral("429")))
        return RetryKind::Network;
    // OpenRouter-specific: HTTP 400 with "Provider returned error" / a
    // provider_name in the metadata almost always means a single upstream
    // provider rejected the request. OpenRouter routes to a different
    // provider on retry, so this is effectively transient.
    if (lower.contains(QStringLiteral("provider returned error"))
        || lower.contains(QStringLiteral("provider_name"))
        || (lower.contains(QStringLiteral("http 400"))
            && lower.contains(QStringLiteral("openrouter"))))
        return RetryKind::Network;
    return RetryKind::None;
}

QString AgentRunner::hintForRetry(RetryKind kind, const QString &rawError)
{
    switch (kind) {
    case RetryKind::Malformed:
        return QStringLiteral(
            "Your previous tool call was rejected as malformed by the API "
            "(error: %1). Please retry with these constraints: "
            "(1) emit a single well-formed function call with valid JSON arguments; "
            "(2) omit any null or unknown optional fields; "
            "(3) for insert_events, send at most 30 events per call and split larger "
            "batches across multiple sequential calls; "
            "(4) ensure all tick/note/velocity/duration values are plain integers.")
            .arg(rawError.left(200));
    case RetryKind::MaxTokens:
        return QStringLiteral(
            "Your previous response was cut off by the token limit. "
            "Please retry by splitting the work into smaller pieces: "
            "emit fewer tool calls per turn and at most 20 events per insert_events call.");
    case RetryKind::Empty:
        return QStringLiteral(
            "Your previous response contained no text and no tool call. "
            "Please retry — emit at least one tool call to make progress, "
            "or a final text answer if the work is complete.");
    case RetryKind::ToolCallCutOff:
        // The one hint that has to teach a concrete WORKING RULE, because the
        // model cannot see why its call failed: the arguments JSON was cut off
        // mid-object when generation exhausted the context, so "try again"
        // reproduces it exactly. Numbers deliberately small - a local model
        // that already overran once needs a lot of headroom.
        // No "(server: ...)" quote here on purpose: the raw error at this point
        // is MidiEditor's own wrapper sentence, ~250 chars of our prose before
        // the server's text even starts, so rawError.left(200) only ever quoted
        // us back at the model. The rules below are what changes behaviour.
        Q_UNUSED(rawError);
        return QStringLiteral(
            "Your previous tool call was cut off before it ended and could not be "
            "parsed. This happens when ONE call tries to write too "
            "much and runs out of context. Do not repeat that call. Retry with "
            "much smaller steps: "
            "(1) write ONE track per call; "
            "(2) cover at most 1-4 bars per call; "
            "(3) send at most 30 events per insert_events/replace_events call; "
            "(4) issue write calls one after another, never several large ones at "
            "once; "
            "(5) continue the same track in a follow-up call until the section is "
            "complete. A smaller complete arrangement beats a truncated large one.");
    case RetryKind::Network:
        // Transient network — no semantic hint needed, just retry silently.
        return QString();
    case RetryKind::None:
        return QString();
    }
    return QString();
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
        // v2.3.1: labels carry the bound document's title while the run is
        // away from its origin. Planned labels use the suffix as of NOW - a
        // batch that switches mid-way keeps its pre-planned labels (the step
        // records and the chat's switch line still name the right document).
        plannedLabels << decorateStepLabel(buildStepLabel(name, args));
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
        emit stepStarted(_currentStep, decorateStepLabel(buildStepLabel(toolName, args)));

        QJsonObject result;
        const bool isWriteTool = toolName == QStringLiteral("insert_events")
                              || toolName == QStringLiteral("replace_events");
        if (isWriteTool) {
            const QString signature = toolName + QLatin1Char(':')
                + QString::fromUtf8(QJsonDocument(args).toJson(QJsonDocument::Compact));
            if (signature == _lastWriteToolSignature) {
                ++_repeatedWriteToolCalls;
            } else {
                _lastWriteToolSignature = signature;
                _repeatedWriteToolCalls = 0;
            }

            if (_repeatedWriteToolCalls >= 2) {
                cleanup();
                emit errorOccurred(tr(
                    "Agent stopped because the model repeated the same write tool call "
                    "without making progress. Try again with a shorter request or a different model."));
                return;
            }

            if (_repeatedWriteToolCalls == 1) {
                result[QStringLiteral("success")] = false;
                result[QStringLiteral("error")] = QStringLiteral(
                    "Repeated identical write tool call rejected to prevent an infinite loop.");
                if (_policy.sanitizeRejectionGuidance) {
                    // Phase 31: positive-only guidance for gpt-5.5* — never
                    // mention `pitch_bend` so we do not re-anchor the model.
                    result[QStringLiteral("guidance")] = QStringLiteral(
                        "Do not repeat this call. Issue exactly one corrected replace_events call "
                        "containing a program_change event at tick 0 followed by note objects with "
                        "explicit pitch, velocity, duration, and tick (channel:null on notes).");
                } else {
                    result[QStringLiteral("guidance")] = QStringLiteral(
                        "Do not repeat this call. If the user asked for a melody, emit exactly one corrected "
                        "replace_events call containing a program_change event and note events with channel:null. "
                        "Do not use pitch_bend unless the user explicitly asked for pitch bends.");
                }
            }

            // Pre-execution sanity check: a write tool whose entire `events`
            // array contains only `pitch_bend` items is almost always a
            // GPT-5.5 hallucination where the model copied the existing
            // pitch-bend "placeholder" instead of emitting notes. Reject up
            // front with a corrective message so we save one round-trip.
            if (result.isEmpty()) {
                const QJsonArray evs = args.value(QStringLiteral("events")).toArray();
                if (ToolDefinitions::isPitchBendOnlyPayload(evs)) {
                    result[QStringLiteral("success")] = false;
                    if (_policy.sanitizeRejectionGuidance) {
                        // Phase 31: positive-only error/guidance.
                        result[QStringLiteral("error")] = QStringLiteral(
                            "Rejected: composition writes require at least one note object.");
                        result[QStringLiteral("guidance")] = QStringLiteral(
                            "Emit a single replace_events call on the target track containing a "
                            "program_change at tick 0 plus note objects with explicit pitch, "
                            "velocity, duration, and tick (channel:null on notes).");
                    } else {
                        result[QStringLiteral("error")] = QStringLiteral(
                            "Rejected: events array contains only pitch_bend items.");
                        result[QStringLiteral("guidance")] = QStringLiteral(
                            "Do not call insert_events/replace_events with only pitch_bend events. "
                            "If the user asked for a melody, emit a single replace_events call on the "
                            "target track containing a program_change at tick 0 plus note events with "
                            "channel:null. Use pitch_bend only when the user explicitly asked for "
                            "pitch bends, vibrato, or pitch automation, and only alongside the notes.");
                    }
                }
            }
        }

        // v2.3.1 cross-tab: switch_document is intercepted HERE, before
        // generic dispatch - the runner (not ToolDefinitions) owns the run
        // bind, exactly like the MCP server owns its session bind and
        // intercepts its own switch_document before bound-file resolution.
        // The intercept is synchronous within this loop iteration, so the
        // very next tool call of the SAME batch already acts on the new
        // document. Only a tool RESULT is produced (no extra messages
        // mid-batch - same rule as the FFXIV mode switch below).
        if (result.isEmpty() && toolName == QStringLiteral("switch_document"))
            result = interceptSwitchDocument(args);

        // Execute the tool unless the duplicate-call guard produced a
        // corrective tool result above.
        if (result.isEmpty())
            result = ToolDefinitions::executeTool(toolName, args, _file, _widget);

        // Phase 46 follow-up: a set_ffxiv_mode call has to take effect INSIDE
        // the run that made it. Only remember it here - the re-derivation
        // appends a message, and an assistant message carrying tool_calls must
        // be followed directly by the tool results for every one of its call
        // ids, so nothing may be spliced in before this batch is finished.
        // `ffxivMode` from the result, not `enabled` from the args: the
        // executor is what decided the effective mode.
        if (toolName == QStringLiteral("set_ffxiv_mode")
            && result.value(QStringLiteral("success")).toBool(false)) {
            _pendingFfxivMode =
                result.value(QStringLiteral("ffxivMode")).toBool() ? 1 : 0;
        }

        // Phase 31: bounded-failure stop for gpt-5.5*. Count consecutive
        // write-tool calls that came back as an "incomplete payload"
        // rejection. After the second such miss we abort the whole run with
        // a clear user message instead of burning the rest of the step
        // budget on more empty placeholders.
        if (isWriteTool && _policy.boundedIncompleteWriteStop) {
            const bool failed = !result.value(QStringLiteral("success")).toBool(false);
            const QString errLower = result.value(QStringLiteral("error")).toString().toLower();
            const bool looksIncomplete = failed
                && (errLower.contains(QStringLiteral("only pitch_bend"))
                    || errLower.contains(QStringLiteral("require at least one note"))
                    || errLower.contains(QStringLiteral("incomplete")));
            if (looksIncomplete) {
                ++_consecutiveIncompleteWrites;
            } else {
                _consecutiveIncompleteWrites = 0;
            }
            if (_consecutiveIncompleteWrites >= 2) {
                cleanup();
                emit errorOccurred(tr(
                    "Agent stopped: setup steps were applied but the model did not produce "
                    "musical notes after repeated attempts. Try a different model "
                    "(Claude, Gemini, GPT-5.4) or refine the prompt."));
                return;
            }
        }

        logAgentToolResult(_currentStep, toolName, result);
        updateWorkingStateFromToolResult(_workingState, toolName, args, result);
        if (_policy.sanitizeRejectionGuidance) {
            // Phase 31: scrub `pitch_bend` mentions from the per-result state
            // hints so they never reach the gpt-5.5* prompt.
            if (_workingState.nextStepHint.contains(QStringLiteral("pitch_bend"))) {
                _workingState.nextStepHint = QStringLiteral(
                    "Replace the rejected write with program_change plus note "
                    "objects (channel:null, explicit pitch/velocity/duration/tick).");
            }
            if (_workingState.activeConstraints.contains(QStringLiteral("pitch_bend"))) {
                _workingState.activeConstraints = QStringLiteral(
                    "The last write was rejected because it did not contain note "
                    "objects. Composition writes require notes with pitch, "
                    "velocity, duration, and tick.");
            }
        }
        logAgentState(_currentStep, _workingState);
        emit stepCompleted(_currentStep, toolName, result);

        // Allow Qt event loop to process repaints so editor updates are visible live
        QCoreApplication::processEvents();

        // Check cancellation after processing events (user may have cancelled during repaint)
        if (_cancelled) {
            cleanup();
            return;
        }

        // Append tool result message
        QJsonObject toolResultMsg;
        toolResultMsg["role"] = QString("tool");
        toolResultMsg["tool_call_id"] = toolCallId;
        toolResultMsg["content"] = QString::fromUtf8(
            QJsonDocument(result).toJson(QJsonDocument::Compact));
        _messages.append(toolResultMsg);
    }

    // Every tool result of the batch is in place - now it is safe to re-derive
    // the tool list and the FFXIV rules for the following turns of this run.
    if (_pendingFfxivMode >= 0) {
        const bool enabled = _pendingFfxivMode == 1;
        _pendingFfxivMode = -1;
        applyFfxivModeChange(enabled);
    }

    // Send the next request with tool results
    sendNextRequest();
}

QJsonObject AgentRunner::interceptSwitchDocument(const QJsonObject &args)
{
    QJsonObject result;
    const int index = args.value(QStringLiteral("index")).toInt(-1);
    MidiFile *target = _widget ? _widget->documentFileByListIndex(index) : nullptr;
    if (!target) {
        // Guard rail: a closed/invalid index is a structured error result and
        // the run CONTINUES on its current document - never an abort.
        result[QStringLiteral("success")] = false;
        result[QStringLiteral("error")] = QStringLiteral(
            "Invalid document index %1 - the tab may have been closed or the "
            "list may have changed. Call list_documents for the current list. "
            "The run stays bound to its current document; nothing was switched.")
            .arg(index);
        return result;
    }

    // Title CONCATENATED into every string below, never .arg()-substituted -
    // a tab title comes from a file name and could contain '%N'.
    const QString title = _widget->documentTitleForFile(target);
    result[QStringLiteral("index")] = index;
    result[QStringLiteral("title")] = title;

    if (target == _file) {
        // Redundant switch: report, change nothing, announce nothing.
        result[QStringLiteral("success")] = true;
        result[QStringLiteral("alreadyBound")] = true;
        result[QStringLiteral("summary")] = QStringLiteral("Already working on '")
            + title + QStringLiteral("' - nothing changed.");
        return result;
    }

    // ATOMIC RE-BIND, main-thread-synchronous, no stale-bind window:
    //  (a) `_file`     - every following executeTool call (this batch
    //                    included) reads and writes the new document;
    //                    Selection::forFile/EditorContext follow per call.
    //  (b) the widget  - rebindAgentRun() moves `_runOriginFile` (the
    //                    closed-mid-run guard now aborts on the NEW target;
    //                    closing the ORIGINAL document no longer aborts the
    //                    run, which continues here - see the guard's docs),
    //                    records the document for the per-step undo
    //                    bookkeeping, and posts the unmissable chat line.
    //  Deliberately NOT: activating the tab in the UI. MidiPilot's chat is
    //  panel-wide and must stay visible; MCP's switch_document keeps
    //  activating the tab - that difference is by design.
    _file = target;
    _widget->rebindAgentRun(target, title);
    _boundDocTitle = (target == _originFile) ? QString() : title;

    result[QStringLiteral("success")] = true;
    result[QStringLiteral("switched")] = true;
    result[QStringLiteral("uiTabActivated")] = false;
    result[QStringLiteral("summary")] = QStringLiteral("Now working on '") + title
        + QStringLiteral("' - every following read and write acts on that document "
                         "and its undo steps land there. The visible tab did NOT "
                         "change. Call get_editor_state to read the document's "
                         "state before editing it.");
    return result;
}

QString AgentRunner::decorateStepLabel(const QString &label) const
{
    if (_boundDocTitle.isEmpty())
        return label;
    return label + QStringLiteral(" [in ") + _boundDocTitle + QLatin1Char(']');
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
    if (toolName == "delete_events_by_index") {
        int n = args["indices"].toArray().size();
        return QStringLiteral("Delete selected events \u2014 %1 by index").arg(n);
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
    if (toolName == "remove_track") {
        int track = args["trackIndex"].toInt(-1);
        return QStringLiteral("Remove track \u2014 Track %1").arg(track);
    }
    if (toolName == "get_selection") {
        return QStringLiteral("Read selection");
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
    if (toolName == "search_help") {
        const QString q = args["query"].toString();
        return q.isEmpty() ? QStringLiteral("Search manual")
                           : QStringLiteral("Search manual — \"%1\"").arg(q.left(40));
    }
    if (toolName == "get_help_section") {
        return QStringLiteral("Read manual — %1").arg(args["page"].toString());
    }
    if (toolName == "transpose_events") {
        const int semis = args["semitones"].toInt(0);
        return args["foldToRange"].toBool(false)
            ? QStringLiteral("Transpose %1%2 st + fold to C3-C6")
                  .arg(semis > 0 ? QStringLiteral("+") : QString()).arg(semis)
            : QStringLiteral("Transpose %1%2 st")
                  .arg(semis > 0 ? QStringLiteral("+") : QString()).arg(semis);
    }
    if (toolName == "split_chords_to_tracks") {
        int track = args["trackIndex"].toInt(-1);
        return QStringLiteral("Split chords — Track %1 → voice tracks").arg(track);
    }
    if (toolName == "copy_events_to_track") {
        int from = args["sourceTrackIndex"].toInt(-1);
        int to = args["targetTrackIndex"].toInt(-1);
        return QStringLiteral("Copy notes — Track %1 → Track %2").arg(from).arg(to);
    }
    if (toolName == "convert_tempo_preserve_duration") {
        const double source = args["sourceBpm"].toDouble(0);
        const double target = args["targetBpm"].toDouble(0);
        const bool dry = args["dryRun"].toBool(true);
        QString label;
        if (source > 0 && target > 0)
            label = QStringLiteral("Convert tempo \u2014 %1 \u2192 %2 BPM").arg(source).arg(target);
        else if (target > 0)
            label = QStringLiteral("Convert tempo \u2014 to %1 BPM").arg(target);
        else
            label = QStringLiteral("Convert tempo");
        if (dry) label += QStringLiteral(" (dry run)");
        return label;
    }
    if (toolName == "thin_tempo_map") {
        QString label = QStringLiteral("Thin tempo map");
        if (args["dryRun"].toBool(true)) label += QStringLiteral(" (dry run)");
        return label;
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
    if (toolName == "list_documents") {
        return QStringLiteral("List open documents");
    }
    if (toolName == "get_document_overview") {
        return QStringLiteral("Read document overview \u2014 #%1")
            .arg(args["documentIndex"].toInt(-1));
    }
    if (toolName == "import_tracks_from_document") {
        const int doc = args["documentIndex"].toInt(-1);
        const int n = args["trackIndexes"].toArray().size();
        QString label = n > 0
            ? QStringLiteral("Import %1 track(s) \u2014 from document #%2").arg(n).arg(doc)
            : QStringLiteral("Import tracks \u2014 from document #%1").arg(doc);
        // dryRun defaults to true (STRICT-SCHEMA-001: null = true).
        const QJsonValue dry = args.value(QStringLiteral("dryRun"));
        if (!dry.isBool() || dry.toBool())
            label += QStringLiteral(" (dry run)");
        return label;
    }
    if (toolName == "switch_document") {
        // Deliberately distinct from every other step label (guard rail):
        // arrows + caps make the re-bind stand out in the steps dock.
        return QStringLiteral("\u21C4 SWITCH DOCUMENT \u2192 #%1")
            .arg(args["index"].toInt(-1));
    }
    if (toolName == "validate_ffxiv") {
        return QStringLiteral("Validate FFXIV constraints");
    }
    if (toolName == "setup_channel_pattern") {
        return QStringLiteral("Setup channel pattern & guitar switches");
    }
    if (toolName == "convert_drums_ffxiv") {
        int track = args["trackIndex"].toInt(-1);
        return QStringLiteral("Convert GM drums \u2014 Track %1").arg(track);
    }
    return toolName;
}

void AgentRunner::cleanup()
{
    _running = false;
    disconnect(_responseConn);
    disconnect(_errorConn);
    // Phase 31 — release any per-request policy override so the next caller
    // (or a different model on the same client) starts from defaults.
    if (_client)
        _client->setNextRequestPolicyOverride(false, QString());
    _file = nullptr;
    _widget = nullptr;
    // v2.3.1 cross-tab: drop the run's home/bind bookkeeping with the run.
    _originFile = nullptr;
    _boundDocTitle.clear();
}
