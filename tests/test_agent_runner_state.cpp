#include <QtTest/QtTest>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>

#include "../src/ai/AiClient.h"
#include "../src/ai/AgentRunner.h"
#include "../src/ai/ToolDefinitions.h"

class MidiFile;
class MidiPilotWidget;

AiClient::AiClient(QObject *parent) : QObject(parent) {}
void AiClient::sendMessages(const QJsonArray &, const QJsonArray &) {}
void AiClient::sendStreamingMessages(const QJsonArray &, const QJsonArray &) {}
void AiClient::cancelRequest() {}
bool AiClient::isReasoningModel() const { return false; }
bool AiClient::agentStreamingEnabled() const { return false; }
void AiClient::markToolsIncapableForCurrentModel(const QString &) {}
bool AiClient::errorIndicatesNoToolSupport(const QString &) { return false; }
void AiClient::onReplyFinished(QNetworkReply *) {}
void AiClient::onStreamDataAvailable() {}
void AiClient::onGeminiStreamDataAvailable() {}
void AiClient::onResponsesStreamDataAvailable() {}
QString AiClient::model() const { return QString(); }
QString AiClient::provider() const { return QString(); }
void AiClient::setNextRequestPolicyOverride(bool, const QString &) {}

QJsonArray ToolDefinitions::toolSchemas() { return QJsonArray(); }
QJsonArray ToolDefinitions::toolSchemas(const ToolDefinitions::ToolSchemaOptions &) { return QJsonArray(); }
bool ToolDefinitions::isPitchBendOnlyPayload(const QJsonArray &) { return false; }
QJsonObject ToolDefinitions::executeTool(const QString &, const QJsonObject &, MidiFile *, MidiPilotWidget *, const QString &)
{
    return QJsonObject{{QStringLiteral("success"), true}};
}

class TestAgentRunnerState : public QObject {
    Q_OBJECT

private slots:
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
};

QTEST_MAIN(TestAgentRunnerState)
#include "test_agent_runner_state.moc"
