#include <QtTest/QtTest>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QSettings>

#include "../src/AppPaths.h"
#include "../src/ai/AiClient.h"
#include "../src/ai/AgentRunner.h"
#include "../src/ai/EditorContext.h"
#include "../src/ai/ToolDefinitions.h"

class MidiFile;

// v2.4.0 cross-tab: AgentRunner::interceptSwitchDocument references three
// MidiPilotWidget members (this project's /OPT:REF configuration does not
// strip them - same root cause as the ODR shims in test_tool_definitions.cpp).
// The intercept itself needs a live widget and is not driven here; only the
// symbols are satisfied. Public non-virtual non-static, exactly like the real
// declarations in src/gui/MidiPilotWidget.h - MSVC mangling includes the
// access level and cv-qualifier, so the shim must match them.
class MidiPilotWidget {
public:
    MidiFile *documentFileByListIndex(int index) const;
    QString documentTitleForFile(MidiFile *f) const;
    void rebindAgentRun(MidiFile *target, const QString &title);
};
MidiFile *MidiPilotWidget::documentFileByListIndex(int) const { return nullptr; }
QString MidiPilotWidget::documentTitleForFile(MidiFile *) const { return QString(); }
void MidiPilotWidget::rebindAgentRun(MidiFile *, const QString &) {}

AiClient::AiClient(QObject *parent) : QObject(parent) {}
void AiClient::sendMessages(const QJsonArray &, const QJsonArray &) {}
void AiClient::sendStreamingMessages(const QJsonArray &, const QJsonArray &) {}
void AiClient::cancelRequest() {}
bool AiClient::isReasoningModel() const { return false; }
bool AiClient::agentStreamingEnabled() const { return false; }
void AiClient::markToolsIncapableForCurrentModel(const QString &) {}
void AiClient::clearToolsIncapableFlag(const QString &, const QString &) {}
bool AiClient::errorIndicatesNoToolSupport(const QString &) { return false; }
void AiClient::onReplyFinished(QNetworkReply *) {}
void AiClient::onStreamDataAvailable() {}
void AiClient::onGeminiStreamDataAvailable() {}
void AiClient::onResponsesStreamDataAvailable() {}
QString AiClient::model() const { return QString(); }
QString AiClient::provider() const { return QString(); }
void AiClient::setNextRequestPolicyOverride(bool, const QString &) {}

// AgentRunner's mid-run FFXIV-mode notice quotes the FFXIV rule block. Stubbed
// like the AiClient/ToolDefinitions symbols above so this target keeps linking
// without dragging EditorContext.cpp (and the whole editor) into it.
QString EditorContext::ffxivContext(bool, bool) { return QString(); }

QJsonArray ToolDefinitions::toolSchemas() { return QJsonArray(); }
QJsonArray ToolDefinitions::toolSchemas(const ToolDefinitions::ToolSchemaOptions &) { return QJsonArray(); }
bool ToolDefinitions::isPitchBendOnlyPayload(const QJsonArray &) { return false; }
QJsonObject ToolDefinitions::executeTool(const QString &, const QJsonObject &, MidiFile *, MidiPilotWidget *, const QString &)
{
    return QJsonObject{{QStringLiteral("success"), true}};
}

class TestAgentRunnerState : public QObject {
    Q_OBJECT

private:
    static constexpr const char *kTestOrg = "MidiEditorTest";
    static constexpr const char *kTestApp = "AgentRunnerState";

private slots:
    // AgentRunner reads AI/ffxiv_mode (and more) through AppPaths::settings(),
    // so without the central seam the assertions below depend on - and the code
    // under test reads - the developer's real configuration. Install the
    // throwaway scope before the first slot runs, so the prompt-building tests
    // see documented defaults instead of whatever this machine has configured.
    void initTestCase()
    {
        AppPaths::setSettingsScopeForTests(QLatin1String(kTestOrg),
                                          QLatin1String(kTestApp));
        AppPaths::settings()->clear();
    }

    void cleanupTestCase()
    {
        QSettings(QLatin1String(kTestOrg), QLatin1String(kTestApp)).clear();
        AppPaths::setSettingsScopeForTests(QString(), QString());
    }

    void classifyTask_detectsCompositionEditAnalysisRepair()
    {
        QCOMPARE(AgentRunner::classifyTask(QStringLiteral("Compose a two minute FFXIV lofi octet")),
                 AgentRunner::TaskType::Composition);
        QCOMPARE(AgentRunner::classifyTask(QStringLiteral("Compose a gentle lofi octet")),
                 AgentRunner::TaskType::Composition);
        QCOMPARE(AgentRunner::classifyTask(QStringLiteral("Transpose the selected melody up one octave")),
                 AgentRunner::TaskType::Edit);
        QCOMPARE(AgentRunner::classifyTask(QStringLiteral("What key and chords are in track 2?")),
                 AgentRunner::TaskType::Analysis);
        QCOMPARE(AgentRunner::classifyTask(QStringLiteral("Fix channel assignments and validate drums")),
                 AgentRunner::TaskType::Repair);
    }

    void workingState_tracksSuccessfulToolResultsCompactly()
    {
        AgentRunner::AgentWorkingState state = AgentRunner::initialWorkingState(
            QStringLiteral("Compose a lofi loop"));

        AgentRunner::updateWorkingStateFromToolResult(
            state,
            QStringLiteral("set_tempo"),
            QJsonObject{{QStringLiteral("bpm"), 82}, {QStringLiteral("tick"), 0}},
            QJsonObject{{QStringLiteral("success"), true}});

        QJsonArray events;
        events.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("program_change")},
                                  {QStringLiteral("tick"), 0},
                                  {QStringLiteral("program"), 0}});
        events.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("note")},
                                  {QStringLiteral("tick"), 120},
                                  {QStringLiteral("note"), 60},
                                  {QStringLiteral("velocity"), 88},
                                  {QStringLiteral("duration"), 240},
                                  {QStringLiteral("channel"), QJsonValue::Null}});

        AgentRunner::updateWorkingStateFromToolResult(
            state,
            QStringLiteral("insert_events"),
            QJsonObject{{QStringLiteral("trackIndex"), 3}, {QStringLiteral("events"), events}},
            QJsonObject{{QStringLiteral("success"), true}});

        const QString layer = AgentRunner::stateLayerContent(state);
        QVERIFY(layer.contains(QStringLiteral("Task type: composition")));
        QVERIFY(layer.contains(QStringLiteral("Tempo set to 82 BPM")));
        QVERIFY(layer.contains(QStringLiteral("insert_events ok track 3 count 2 ticks 0-360")));
        QVERIFY(layer.size() < 1401);
    }

    void workingState_saveQuestionIsHandedToTheUserNotTreatedAsFailure()
    {
        // save_document's "overwrite or copy" question comes back with
        // success:false. It must not count as a rejected step or steer the
        // model to "a different valid next action" - it has to ask the user.
        AgentRunner::AgentWorkingState state = AgentRunner::initialWorkingState(
            QStringLiteral("Save the song"));
        AgentRunner::updateWorkingStateFromToolResult(
            state, QStringLiteral("save_document"),
            QJsonObject{{QStringLiteral("name"), QJsonValue::Null},
                        {QStringLiteral("mode"), QJsonValue::Null}},
            QJsonObject{{QStringLiteral("success"), false},
                        {QStringLiteral("decisionNeeded"), QStringLiteral("overwrite_or_copy")},
                        {QStringLiteral("question"),
                         QStringLiteral("Overwrite mozart.mid with the changes, or keep it and "
                                        "save a copy as mozart.midipilot.mid?")}});
        QCOMPARE(state.repeatedFailureCount, 0);
        QVERIFY(!state.lastToolResult.contains(QStringLiteral("rejected")));
        QVERIFY(state.lastToolResult.contains(QStringLiteral("Overwrite mozart.mid")));
        QVERIFY(state.nextStepHint.contains(QStringLiteral("Ask the user")));
        QVERIFY(!state.nextStepHint.contains(QStringLiteral("different valid next action")));
    }

    void pitchBendOnlyRejectionBecomesNextStepSteering()
    {
        // Removed in Phase 31.2. The pre-Phase-31 working-state branch that
        // detected "pitch_bend ... only" in the rejection text and rewrote
        // both `nextStepHint` and `activeConstraints` was deleted because:
        //   * `gpt-5.5*` runs are already covered by `AgentToolPolicy`'s
        //     sanitized rejection guidance (Phase 31).
        //   * For non-5.5 models the branch echoed the literal "pitch_bend"
        //     token back into the working-state injection on every error
        //     whose text mentioned it — re-introducing exactly the leakage
        //     Phase 31 sanitises.
        // The generic failure path (increments `repeatedFailureCount`,
        // falls back to provider guidance) now handles this case for every
        // model. The dedicated assertion is no longer meaningful and was
        // dropped together with the branch.
        QSKIP("Phase 31.2: pitch_bend-only working-state branch removed; covered by generic failure path.");
    }

    void requestLocalStateInjectionDoesNotMutateCanonicalMessages()
    {
        AgentRunner::AgentWorkingState state = AgentRunner::initialWorkingState(
            QStringLiteral("Analyze the chords"));

        QJsonArray messages;
        messages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("developer")},
                                    {QStringLiteral("content"), QStringLiteral("System prompt")}});
        messages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                                    {QStringLiteral("content"), QStringLiteral("Analyze the chords")}});

        const QJsonArray requestMessages = AgentRunner::messagesForNextRequest(messages, state);
        QCOMPARE(messages.size(), 2);
        QCOMPARE(requestMessages.size(), 3);
        QCOMPARE(requestMessages.at(1).toObject().value(QStringLiteral("role")).toString(),
                 QStringLiteral("developer"));
        QVERIFY(requestMessages.at(1).toObject().value(QStringLiteral("content")).toString()
                    .contains(QStringLiteral("Current Agent State")));
    }

    void repeatedDuplicateWriteRejectionIncrementsFailureCount()
    {
        AgentRunner::AgentWorkingState state = AgentRunner::initialWorkingState(
            QStringLiteral("Change the bassline"));

        AgentRunner::updateWorkingStateFromToolResult(
            state,
            QStringLiteral("insert_events"),
            QJsonObject{{QStringLiteral("trackIndex"), 2}},
            QJsonObject{{QStringLiteral("success"), false},
                        {QStringLiteral("error"), QStringLiteral("Repeated identical write tool call rejected to prevent an infinite loop.")},
                        {QStringLiteral("guidance"), QStringLiteral("Do not repeat this call.")}});

        QCOMPARE(state.repeatedFailureCount, 1);
        QVERIFY(state.nextStepHint.contains(QStringLiteral("Do not repeat")));
    }

    // --- TOOLJSON-500 -----------------------------------------------------
    // A local server (llama.cpp / Ollama) answers HTTP 500 when the model
    // emitted a tool call it cannot parse - in the field, one song-length
    // insert_events call whose arguments JSON was cut off after 27,919
    // generated tokens. That reads like an outage but is deterministic, so it
    // used to be classified as Network, whose hint is empty - the model was
    // asked again with nothing changed and failed identically every time.

    void unparsableToolCall_isRecognisedRegardlessOfServerWording()
    {
        // llama.cpp's exact wording from the reported session.
        QVERIFY(AiClient::errorIndicatesUnparsableToolCall(QStringLiteral(
            "Failed to parse tool call arguments as JSON: "
            "[json.exception.parse_error.101] parse error at line 1, column 59246: "
            "syntax error while parsing value - unexpected end of input")));
        // Other servers word it differently - the substance is what matters.
        QVERIFY(AiClient::errorIndicatesUnparsableToolCall(
            QStringLiteral("invalid json in function call arguments")));
        QVERIFY(AiClient::errorIndicatesUnparsableToolCall(
            QStringLiteral("could not parse tool_call payload")));
    }

    void unparsableToolCall_doesNotSwallowOrdinaryOutages()
    {
        QVERIFY(!AiClient::errorIndicatesUnparsableToolCall(QString()));
        QVERIFY(!AiClient::errorIndicatesUnparsableToolCall(
            QStringLiteral("Ollama is busy or temporarily unavailable (HTTP 503)")));
        // Mentions a tool call but is a capability problem, not a parse failure.
        QVERIFY(!AiClient::errorIndicatesUnparsableToolCall(
            QStringLiteral("This model does not support tool call usage")));
        // Mentions parsing but is not about a tool call.
        QVERIFY(!AiClient::errorIndicatesUnparsableToolCall(
            QStringLiteral("failed to parse the response body")));
    }

    void cutOffToolCall_classifiesAwayFromTheSilentNetworkRetry()
    {
        // The string MUST carry an HTTP-5xx token, otherwise it could never
        // have classified as Network in the first place and the ordering this
        // test claims to pin would be untested. This is the real shape: the
        // provider error arrives wrapped with its status code.
        const QString err = QStringLiteral(
            "Streaming error (HTTP 500): The model produced a tool call that Ollama "
            "could not parse - usually because one call tried to write too much and "
            "ran out of context. Server said: Failed to parse tool call arguments "
            "as JSON: unexpected end of input");
        const AgentRunner::RetryKind kind = AgentRunner::classifyError(err);
        QCOMPARE(kind, AgentRunner::RetryKind::ToolCallCutOff);
        QVERIFY2(kind != AgentRunner::RetryKind::Network,
                 "a deterministic parse failure must not be retried silently");

        // The whole point: this kind carries a hint, and it tells the model the
        // concrete rule that avoids the failure.
        const QString hint = AgentRunner::hintForRetry(kind, err);
        QVERIFY(!hint.isEmpty());
        // And the Network hint - the one that used to fire - is still empty, so
        // a regression that re-routes this error would be caught by the emptiness.
        QVERIFY(AgentRunner::hintForRetry(AgentRunner::RetryKind::Network, err).isEmpty());
        QVERIFY(hint.contains(QStringLiteral("Do not repeat")));
        QVERIFY(hint.contains(QStringLiteral("ONE track per call")));
        QVERIFY(hint.contains(QStringLiteral("30 events")));
    }

    // Phase 47 — the gap the prompt-profile switch exists to close.
    //
    // The AND-composition itself lives inside AgentRunner::run(), which needs
    // a live AiClient, so it is not drivable here. What IS drivable, and what
    // actually matters, is the base policy it composes with: gpt-5.5* loses
    // pitch_bend on its own, every other model keeps it - including the local
    // ones that produce the placeholder bends. If this test ever flips to
    // "false" for the Ollama model, the profile flag has become redundant and
    // someone widened the model check instead.
    void buildPolicyFor_leavesPitchBendOnForEveryModelButGpt55()
    {
        const AgentToolPolicy gpt55 = AgentToolPolicyUtil::buildPolicyFor(
            QStringLiteral("gpt-5.5"), QStringLiteral("openai"), /*isCompositionOrEdit=*/true);
        QVERIFY2(!gpt55.allowPitchBendEvents,
                 "gpt-5.5 composition must keep its schema-light policy");
        QVERIFY(gpt55.sanitizeRejectionGuidance);

        const AgentToolPolicy local = AgentToolPolicyUtil::buildPolicyFor(
            QStringLiteral("hf.co/Qwen/Qwen3-14B-GGUF:Q4_K_M"),
            QStringLiteral("ollama"), /*isCompositionOrEdit=*/true);
        QVERIFY2(local.allowPitchBendEvents,
                 "a local model must keep pitch_bend unless a prompt profile opts out");
        // ...and it does NOT get the positive-only rejection guidance either,
        // which is why run() sets that flag alongside the AND.
        QVERIFY(!local.sanitizeRejectionGuidance);
    }

    void genuineOutageStillRetriesSilently()
    {
        const AgentRunner::RetryKind kind = AgentRunner::classifyError(
            QStringLiteral("Ollama is busy or temporarily unavailable (HTTP 503). "
                           "Please try again in a moment."));
        QCOMPARE(kind, AgentRunner::RetryKind::Network);
        QVERIFY(AgentRunner::hintForRetry(kind, QString()).isEmpty());
    }

    // --- v2.4.0 cross-tab tools: working-state semantics ------------------
    // The runner-side halves that ARE headlessly drivable: the model-facing
    // facts updateWorkingStateFromToolResult derives from the four new tools'
    // results. (interceptSwitchDocument / decorateStepLabel are private and
    // need a live MidiPilotWidget - covered only by the linked symbols above.)

    // An effective switch must leave two durable traces in the working state:
    // a confirmed fact saying reads/writes/undo now act elsewhere, and a next
    // step steering the model to re-read state before editing (the initial
    // editor state was captured for the ORIGIN document).
    void workingState_switchDocument_recordsRebindFactAndSteering()
    {
        AgentRunner::AgentWorkingState state = AgentRunner::initialWorkingState(
            QStringLiteral("Copy the drums over from the other tab"));

        // Title deliberately contains '%1': tab titles are file names, and the
        // fact must carry it VERBATIM - concatenation, never .arg()
        // substitution (which would swallow or replace the token).
        AgentRunner::updateWorkingStateFromToolResult(
            state,
            QStringLiteral("switch_document"),
            QJsonObject{{QStringLiteral("index"), 2}},
            QJsonObject{{QStringLiteral("success"), true},
                        {QStringLiteral("switched"), true},
                        {QStringLiteral("uiTabActivated"), false},
                        {QStringLiteral("title"), QStringLiteral("b%1.mid")}});

        QCOMPARE(state.confirmedFacts.size(), 1);
        const QString fact = state.confirmedFacts.first();
        QVERIFY2(fact.contains(QStringLiteral("Run re-bound to document 'b%1.mid'")),
                 qPrintable(fact));
        QVERIFY2(fact.contains(QStringLiteral("undo")), qPrintable(fact));
        QVERIFY2(state.nextStepHint.contains(QStringLiteral("get_editor_state")),
                 qPrintable(state.nextStepHint));
        // ...and both survive into the injected state layer the model reads.
        const QString layer = AgentRunner::stateLayerContent(state);
        QVERIFY2(layer.contains(QStringLiteral("b%1.mid")), qPrintable(layer));
    }

    // A redundant switch (already bound) must NOT pollute the fact list or
    // re-steer the model - it only notes itself as the last tool result.
    void workingState_switchDocument_alreadyBound_addsNoFactOrSteering()
    {
        AgentRunner::AgentWorkingState state = AgentRunner::initialWorkingState(
            QStringLiteral("Edit the bassline"));
        const QString hintBefore = state.nextStepHint;

        AgentRunner::updateWorkingStateFromToolResult(
            state,
            QStringLiteral("switch_document"),
            QJsonObject{{QStringLiteral("index"), 0}},
            QJsonObject{{QStringLiteral("success"), true},
                        {QStringLiteral("alreadyBound"), true},
                        {QStringLiteral("title"), QStringLiteral("a.mid")}});

        QVERIFY(state.confirmedFacts.isEmpty());
        QCOMPARE(state.nextStepHint, hintBefore);
        QVERIFY2(state.lastToolResult.contains(QStringLiteral("already bound")),
                 qPrintable(state.lastToolResult));
    }

    void workingState_listDocuments_reportsOpenDocumentCount()
    {
        AgentRunner::AgentWorkingState state = AgentRunner::initialWorkingState(
            QStringLiteral("What tabs are open?"));

        QJsonArray docs;
        docs.append(QJsonObject{{QStringLiteral("index"), 0}});
        docs.append(QJsonObject{{QStringLiteral("index"), 1}});
        docs.append(QJsonObject{{QStringLiteral("index"), 2}});
        AgentRunner::updateWorkingStateFromToolResult(
            state,
            QStringLiteral("list_documents"),
            QJsonObject{},
            QJsonObject{{QStringLiteral("success"), true},
                        {QStringLiteral("documents"), docs}});

        QCOMPARE(state.lastToolResult,
                 QStringLiteral("list_documents succeeded: 3 open document(s)"));
        // A read never earns a confirmed fact.
        QVERIFY(state.confirmedFacts.isEmpty());
    }

    void workingState_documentOverview_carriesTheSummary()
    {
        AgentRunner::AgentWorkingState state = AgentRunner::initialWorkingState(
            QStringLiteral("What is in the other tab?"));

        AgentRunner::updateWorkingStateFromToolResult(
            state,
            QStringLiteral("get_document_overview"),
            QJsonObject{{QStringLiteral("documentIndex"), 1}},
            QJsonObject{{QStringLiteral("success"), true},
                        {QStringLiteral("summary"), QStringLiteral(
                             "Document 1 ('b.mid'): 4 track(s), 2480 note(s), "
                             "64 measure(s), 128000 ms.")}});

        QVERIFY2(state.lastToolResult.contains(QStringLiteral("b.mid")),
                 qPrintable(state.lastToolResult));
        QVERIFY(state.confirmedFacts.isEmpty());

        // Result without a summary still yields a usable line.
        AgentRunner::updateWorkingStateFromToolResult(
            state,
            QStringLiteral("get_document_overview"),
            QJsonObject{{QStringLiteral("documentIndex"), 1}},
            QJsonObject{{QStringLiteral("success"), true}});
        QCOMPARE(state.lastToolResult,
                 QStringLiteral("get_document_overview succeeded"));
    }

    // The dry-run summary is what the user confirms; it must persist in the
    // CONFIRMED FACTS (not just lastToolResult, which the next tool call
    // overwrites) so the dryRun=false turn still knows the numbers - same
    // idiom as convert_tempo_preserve_duration / thin_tempo_map.
    void workingState_importTracks_keepsDryRunSummaryAsFact()
    {
        AgentRunner::AgentWorkingState state = AgentRunner::initialWorkingState(
            QStringLiteral("Import the strings from the other file"));

        AgentRunner::updateWorkingStateFromToolResult(
            state,
            QStringLiteral("import_tracks_from_document"),
            QJsonObject{{QStringLiteral("documentIndex"), 1}},
            QJsonObject{{QStringLiteral("success"), true},
                        {QStringLiteral("dryRun"), true},
                        {QStringLiteral("summary"), QStringLiteral(
                             "Would import 3 track(s). Channel collision(s): 0, 1. "
                             "Tempo maps differ.")}});

        QCOMPARE(state.confirmedFacts.size(), 1);
        const QString fact = state.confirmedFacts.first();
        QVERIFY2(fact.contains(QStringLiteral("Would import 3 track(s)")),
                 qPrintable(fact));
        QVERIFY2(fact.contains(QStringLiteral("collision")), qPrintable(fact));

        // Overwrite lastToolResult with a later read - the fact must survive
        // into the state layer regardless.
        AgentRunner::updateWorkingStateFromToolResult(
            state, QStringLiteral("get_editor_state"), QJsonObject{},
            QJsonObject{{QStringLiteral("success"), true}});
        const QString layer = AgentRunner::stateLayerContent(state);
        QVERIFY2(layer.contains(QStringLiteral("Would import 3 track(s)")),
                 qPrintable(layer));
    }
};

QTEST_MAIN(TestAgentRunnerState)
#include "test_agent_runner_state.moc"
