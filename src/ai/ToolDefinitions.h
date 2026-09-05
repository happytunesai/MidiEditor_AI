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
        /// v2.4.0 cross-tab tools: when true, add the `switch_document`
        /// definition to the schema. Default false so BOTH default-options
        /// consumers keep their contract: the MCP server appends its OWN
        /// switch_document (activate-the-tab + get_editor_state re-bind)
        /// in convertToolSchemas() and must not see a second, shadowing
        /// definition; and executeTool's required-gate walks the default
        /// schema. Only the AgentRunner opts in - it intercepts the call
        /// before dispatch and re-binds the run (no tab activation).
        bool includeDocumentSwitch = false;
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
     * \brief Protocol-panel actor prefix for a tool-call \a source.
     *
     * The prefix names WHO made an edit, so the Protocol panel can tell the
     * built-in panel apart from an external MCP client:
     * \li empty / anything else -> \c "MidiPilot"
     * \li \c "mcp"              -> \c "MidiPilotMCP"
     * \li \c "mcp:<client>"     -> \c "MidiPilotMCP (<client>)"
     *
     * The tools that route through MidiPilotWidget::executeAction get this from
     * the widget (static \c protoPrefix in MidiPilotWidget.cpp, same three
     * forms) by passing \c _source along with the action. Tools that open their
     * own Protocol action - transpose_events, split_chords_to_tracks,
     * copy_events_to_track, import_tracks_from_document,
     * convert_tempo_preserve_duration, setup_channel_pattern - build their
     * label with this helper instead, so
     * both paths produce ONE format. Public because the exact strings are
     * documented in manual/mcp-server.html and unit-tested.
     */
    static QString protocolActorPrefix(const QString &source);

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
    static QJsonObject execGetSelection(MidiFile *file);
    // v2.4.0 cross-tab tools. These act on the WINDOW's document list (via
    // the widget's MainWindow), not on the bound file, and never re-bind
    // anything: list_documents was promoted from MCP-only to CORE, and
    // get_document_overview reads ANOTHER open tab without touching it.
    static QJsonObject execListDocuments(MidiPilotWidget *widget);
    static QJsonObject execGetDocumentOverview(const QJsonObject &args,
                                               MidiPilotWidget *widget);
    // v2.4.0: copy tracks from another open document INTO `file` (the bound
    // document) as ONE Protocol action - bulk channel-snapshot idiom, ticks
    // rescaled on ticksPerQuarter mismatch, tempo-map difference and channel
    // collisions REPORTED (never blocked). dryRun defaults to true like
    // thin_tempo_map.
    static QJsonObject execImportTracksFromDocument(const QJsonObject &args,
                                                    MidiFile *file,
                                                    MidiPilotWidget *widget,
                                                    const QString &source);

    // Write tools (delegate to widget handlers)
    static QJsonObject execWriteAction(const QString &action,
                                       const QJsonObject &args,
                                       MidiPilotWidget *widget);

    // FFXIV tools
    static QJsonObject execValidateFFXIV(MidiFile *file);
    static QJsonObject execConvertDrumsFFXIV(const QJsonObject &args,
                                             MidiFile *file,
                                             MidiPilotWidget *widget,
                                             const QString &source);
    static QJsonObject execSetupChannelPattern(MidiFile *file,
                                               MidiPilotWidget *widget,
                                               const QString &source);
    // Phase 32.6: read-only voice-load analysis for the agent
    static QJsonObject execAnalyzeVoiceLoad(const QJsonObject &args, MidiFile *file);
    // v2.1.0 #1: auto-fit thinning action (dry-run first, user-confirmed)
    static QJsonObject execAutoFitVoiceLoad(const QJsonObject &args,
                                            MidiFile *file,
                                            const QString &source);
    // v2.2 #2 (Phase 33.5): time-preserving tempo conversion. A CORE tool,
    // not FFXIV-gated - tempo conversion is generic, and core tools are what
    // the MCP server exposes. Dry-run first, user-confirmed.
    static QJsonObject execConvertTempoPreserveDuration(const QJsonObject &args,
                                                        MidiFile *file,
                                                        const QString &source);
    // v2.3 (Phase 49): thin a dense tempo map. CORE for the same reason as
    // the conversion above - a DAW tempo ramp is not an FFXIV problem, it is
    // a MIDI one. Dry-run first, user-confirmed.
    static QJsonObject execThinTempoMap(const QJsonObject &args,
                                        MidiFile *file,
                                        const QString &source);
    // Phase 46: FFXIV-mode switch - CORE (an agent needs it to REACH the
    // gated FFXIV bundle). Drives the MidiPilot checkbox so persistence and
    // the MCP tools/list_changed broadcast take the one existing path.
    static QJsonObject execSetFfxivMode(const QJsonObject &args,
                                        MidiPilotWidget *widget);
    // Phase 46 pt 3 (octet finding #2): the three arrangement tools the GUI
    // has had for years and the tool surface lacked. All CORE.
    // `source` reaches these three (and the tempo tool above) only so their
    // own Protocol action can carry the same actor attribution the
    // widget-routed tools get - see protocolActorPrefix().
    static QJsonObject execTransposeEvents(const QJsonObject &args, MidiFile *file,
                                           const QString &source);
    static QJsonObject execSplitChordsToTracks(const QJsonObject &args, MidiFile *file,
                                               const QString &source);
    static QJsonObject execCopyEventsToTrack(const QJsonObject &args, MidiFile *file,
                                             const QString &source);
    // Phase 44 ("Manual Bot"): grounded answers about the editor itself.
    // Need no MidiFile - they read the embedded help_db.json.
    static QJsonObject execSearchHelp(const QJsonObject &args);
    static QJsonObject execGetHelpSection(const QJsonObject &args);
};

#endif // TOOLDEFINITIONS_H
