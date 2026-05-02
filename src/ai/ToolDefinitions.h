#ifndef TOOLDEFINITIONS_H
#define TOOLDEFINITIONS_H

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

class MidiFile;
class MidiPilotWidget;

/**
 * \class ToolDefinitions
 *
 * \brief OpenAI function-calling tool schemas and executor for Agent Mode.
 *
 * Provides tool schemas matching the OpenAI tools API format, and an executor
 * that delegates tool calls to existing MidiPilotWidget handlers.
 */
class ToolDefinitions {
public:
    /**
     * \struct ToolSchemaOptions
     *
     * \brief Per-call schema toggles introduced in Phase 31. Defaults
     * preserve the historical schema byte-for-byte; only callers that
     * opt in (currently AgentRunner for gpt-5.5* composition/edit) get
     * a different `events.anyOf` shape. The MCP server uses the
     * default-argument overload and is therefore unaffected.
     */
    struct ToolSchemaOptions {
        /// When false, omit `pitch_bend` from the `events.anyOf` of
        /// `insert_events`/`replace_events`.
        bool includePitchBend = true;
    };

    /**
     * \brief Returns the OpenAI tools array for the API request.
     *
     * Default overload — byte-identical to the pre-Phase-31 schema.
     * Callers that want the Phase 31 conditional shape pass an explicit
     * \ref ToolSchemaOptions argument to the second overload.
     */
    static QJsonArray toolSchemas();

    /// Phase 31 overload. Default-constructed options reproduce the
    /// classic schema.
    static QJsonArray toolSchemas(const ToolSchemaOptions &options);

    /**
     * \brief True when every event in \a events is a `pitch_bend` item
     *        (and the array is non-empty). Shared between the AgentRunner
     *        pre-execution guard and the executor-side guard so both
     *        agree on the exact rejection criterion.
     */
    static bool isPitchBendOnlyPayload(const QJsonArray &events);

    /**
     * \brief Executes a tool call by delegating to existing handlers.
     * \param toolName The function name from the tool call.
     * \param args The parsed arguments object.
     * \param file The current MIDI file.
     * \param widget The MidiPilotWidget for write operations.
     * \return Result JSON to send back to the LLM as tool output.
     */
    static QJsonObject executeTool(const QString &toolName,
                                   const QJsonObject &args,
                                   MidiFile *file,
                                   MidiPilotWidget *widget,
                                   const QString &source = QString());

private:
    static QJsonObject makeTool(const QString &name,
                                const QString &description,
                                const QJsonObject &parameters);

    // Read-only tools
    static QJsonObject execGetEditorState(MidiFile *file);
    static QJsonObject execGetTrackInfo(const QJsonObject &args, MidiFile *file);
    static QJsonObject execQueryEvents(const QJsonObject &args, MidiFile *file);

    // Write tools (delegate to widget handlers)
    static QJsonObject execWriteAction(const QString &action,
                                       const QJsonObject &args,
                                       MidiPilotWidget *widget);

    // FFXIV tools
    static QJsonObject execValidateFFXIV(MidiFile *file);
    static QJsonObject execConvertDrumsFFXIV(const QJsonObject &args,
                                             MidiFile *file,
                                             MidiPilotWidget *widget);
    static QJsonObject execSetupChannelPattern(MidiFile *file,
                                               MidiPilotWidget *widget);
    // Phase 32.6: read-only voice-load analysis for the agent
    static QJsonObject execAnalyzeVoiceLoad(const QJsonObject &args, MidiFile *file);
};

#endif // TOOLDEFINITIONS_H
