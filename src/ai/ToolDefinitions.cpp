#include "ToolDefinitions.h"
#include "FFXIVChannelFixer.h"
#ifndef TOOLDEFINITIONS_TEST_STUB_FFXIV
#include "../converter/AutoFitVoiceLoadService.h"
#include "../converter/TempoConversionService.h"
#include "../midi/TempoMapThinner.h"
#include "../MidiEvent/TempoChangeEvent.h"
#include "../MidiEvent/OnEvent.h"
#include "FfxivPlayabilityValidator.h"
// v2.3.1 cross-tab tools: the document list lives on MainWindow. Real builds
// only - test_tool_definitions ODR-stubs MidiPilotWidget as a non-QWidget, so
// the qobject_cast/window() path (and this include) must never enter the stub
// link; every exec* that uses it is #ifdef-stubbed like thin_tempo_map.
#include "../gui/MainWindow.h"
#endif
#include "HelpDatabase.h" // pure QtCore - fine in the schema-test stub build too
#include "../AppPaths.h"  // Phase 45: settings scope decided in ONE place
#ifndef TOOLDEFINITIONS_TEST_STUB_FFXIV
#include "FfxivVoiceAnalyzer.h"
#endif

#include "../gui/MidiPilotWidget.h"
#include "EditorContext.h"
#include "MidiEventSerializer.h"
#include "../midi/MidiFile.h"
#include "../midi/MidiTrack.h"
#include "../midi/MidiChannel.h"
#include "../MidiEvent/MidiEvent.h"
#include "../MidiEvent/NoteOnEvent.h"
#include "../MidiEvent/OffEvent.h"
#include "../protocol/Protocol.h"
#include "../tool/Selection.h"

#include <QJsonDocument>
#include <QPair>
#include <QSettings>
#include <QSet>
#include <QStringList>
#include <QRegularExpression>
#include <algorithm>
#include <climits>
#include <cmath>
#include <functional>

// ---------------------------------------------------------------------------
// Helper: create a tool schema object in OpenAI format (strict mode)
// ---------------------------------------------------------------------------
QJsonObject ToolDefinitions::makeTool(const QString &name,
                                      const QString &description,
                                      const QJsonObject &parameters) {
    QJsonObject func;
    func["name"] = name;
    func["description"] = description;
    func["strict"] = true;
    func["parameters"] = parameters;

    QJsonObject tool;
    tool["type"] = QString("function");
    tool["function"] = func;
    return tool;
}

// Helper: build a strict-mode-compatible parameter object
static QJsonObject makeParams(const QJsonObject &properties,
                               const QJsonArray &required) {
    QJsonObject params;
    params["type"] = QString("object");
    params["properties"] = properties;
    params["required"] = required;
    params["additionalProperties"] = false;
    return params;
}

// Helper: build the MIDI event schema as a discriminated union (anyOf).
// `includePitchBend` controls whether the `pitch_bend` branch is exposed.
// Default = true preserves the pre-Phase-31 shape for the MCP server and
// every model other than gpt-5.5*.
static QJsonObject makeEventSchema(bool includePitchBend = true) {
    // note (compact format with duration)
    QJsonObject noteProps;
    noteProps["type"] = QJsonObject{{"type", "string"}, {"enum", QJsonArray{"note"}}};
    noteProps["tick"] = QJsonObject{{"type", "integer"}, {"description", "Tick position."}};
    noteProps["note"] = QJsonObject{{"type", "integer"}, {"description", "MIDI note number (0-127). 60 = Middle C."}};
    noteProps["velocity"] = QJsonObject{{"type", "integer"}, {"description", "Note velocity (1-127)."}};
    noteProps["duration"] = QJsonObject{{"type", "integer"}, {"description", "Duration in ticks."}};
    noteProps["channel"] = QJsonObject{
        {"anyOf", QJsonArray{QJsonObject{{"type", "integer"}}, QJsonObject{{"type", "null"}}}},
        {"description", "Required by the schema. Use null for the default track channel. Use an integer 0-15 only for an intentional per-note channel override such as FFXIV guitar switches."}};
    QJsonObject noteSchema;
    noteSchema["type"] = QString("object");
    noteSchema["properties"] = noteProps;
    noteSchema["required"] = QJsonArray{"type", "tick", "note", "velocity", "duration", "channel"};
    noteSchema["additionalProperties"] = false;

    // cc (control change)
    QJsonObject ccProps;
    ccProps["type"] = QJsonObject{{"type", "string"}, {"enum", QJsonArray{"cc"}}};
    ccProps["tick"] = QJsonObject{{"type", "integer"}, {"description", "Tick position."}};
    ccProps["control"] = QJsonObject{{"type", "integer"}, {"description", "CC number (0-127)."}};
    ccProps["value"] = QJsonObject{{"type", "integer"}, {"description", "CC value (0-127)."}};
    QJsonObject ccSchema;
    ccSchema["type"] = QString("object");
    ccSchema["properties"] = ccProps;
    ccSchema["required"] = QJsonArray{"type", "tick", "control", "value"};
    ccSchema["additionalProperties"] = false;

    // pitch_bend
    QJsonObject pbProps;
    pbProps["type"] = QJsonObject{{"type", "string"}, {"enum", QJsonArray{"pitch_bend"}}};
    pbProps["tick"] = QJsonObject{{"type", "integer"}, {"description", "Tick position."}};
    pbProps["value"] = QJsonObject{{"type", "integer"}, {"description", "Pitch bend amount as a 14-bit unsigned integer. Advanced/rare: only use when the user explicitly asks for bends, vibrato, or pitch automation. Never use as a standalone placeholder for notes."}};
    QJsonObject pbSchema;
    pbSchema["type"] = QString("object");
    pbSchema["properties"] = pbProps;
    pbSchema["required"] = QJsonArray{"type", "tick", "value"};
    pbSchema["additionalProperties"] = false;

    // program_change
    QJsonObject pcProps;
    pcProps["type"] = QJsonObject{{"type", "string"}, {"enum", QJsonArray{"program_change"}}};
    pcProps["tick"] = QJsonObject{{"type", "integer"}, {"description", "Tick position."}};
    pcProps["program"] = QJsonObject{{"type", "integer"}, {"description", "GM program number (0-127)."}};
    QJsonObject pcSchema;
    pcSchema["type"] = QString("object");
    pcSchema["properties"] = pcProps;
    pcSchema["required"] = QJsonArray{"type", "tick", "program"};
    pcSchema["additionalProperties"] = false;

    QJsonObject eventSchema;
    QJsonArray branches;
    branches.append(noteSchema);
    branches.append(ccSchema);
    if (includePitchBend)
        branches.append(pbSchema);
    branches.append(pcSchema);
    eventSchema["anyOf"] = branches;
    return eventSchema;
}

bool ToolDefinitions::isPitchBendOnlyPayload(const QJsonArray &events) {
    if (events.isEmpty())
        return false;

    int pitchBendCount = 0;
    int otherCount = 0;
    for (const QJsonValue &eventValue : events) {
        const QString type = eventValue.toObject().value(QStringLiteral("type")).toString();
        if (type == QStringLiteral("pitch_bend"))
            ++pitchBendCount;
        else if (!type.isEmpty())
            ++otherCount;
    }

    return pitchBendCount > 0 && otherCount == 0;
}

// NOTE: a pre-Phase-31 universal pitch_bend-only rejection lived here
// (`isPitchBendOnlyWrite` + `makePitchBendOnlyRejectedResult`). Phase 31
// moved that guard into `AgentRunner::processToolCalls` where it now runs
// only for `gpt-5.5*` (the sole model that ever produced the placeholder
// pattern), via `ToolDefinitions::isPitchBendOnlyPayload`. For every other
// model — and for direct executeTool calls (MCP / scripts) — pitch_bend-only
// writes are valid input and pass through to widget->executeAction.
//
// Keep `isPitchBendOnlyPayload` exported as the single source of truth so
// the AgentRunner gate stays trivial.

// ---------------------------------------------------------------------------
// toolSchemas — returns the full QJsonArray of tool definitions
// ---------------------------------------------------------------------------
QJsonArray ToolDefinitions::toolSchemas() {
    return toolSchemas(ToolSchemaOptions{});
}

QJsonArray ToolDefinitions::toolSchemas(const ToolSchemaOptions &options) {
    const bool includePitchBend = options.includePitchBend;

    // Description suffix for `events` — when pitch_bend is structurally
    // absent (Phase 31 schema-light for gpt-5.5*), do not mention the
    // word at all so we do not re-anchor the model on the forbidden
    // token. When it is present, keep the negative reminder so models
    // that historically misuse it are warned.
    const QString insertEventsDesc = includePitchBend
        ? QStringLiteral("Array of MIDI event objects to insert. Include a program_change event at tick 0 to set the GM instrument. For notes, use channel:null unless overriding the track channel. Do not send pitch_bend-only arrays; they are rejected unless accompanied by actual note/program material and explicitly requested.")
        : QStringLiteral("Array of MIDI event objects to insert. Include a program_change event at tick 0 to set the GM instrument, then note objects with explicit pitch, velocity, duration, and tick. Use channel:null on notes unless overriding the track channel.");

    const QString replaceEventsDesc = includePitchBend
        ? QStringLiteral("New events to insert after removing existing ones in the range. For melody/composition edits, include program_change plus note events. Do not send pitch_bend-only arrays; they are rejected unless pitch automation was explicitly requested.")
        : QStringLiteral("New events to insert after removing existing ones in the range. For melody/composition edits, include program_change plus note objects with explicit pitch, velocity, duration, and tick.");

    QJsonArray tools;

    // --- Read-only tools ---

    // get_editor_state (no parameters)
    {
        tools.append(makeTool(
            "get_editor_state",
            "Get the current editor state including file info, tracks, cursor position, "
            "tempo, time signature, and selected events. "
            "Rate limit: 100 tool calls per minute across all tools.",
            makeParams(QJsonObject(), QJsonArray())));
    }

    // get_track_info
    {
        QJsonObject props;
        props["trackIndex"] = QJsonObject{
            {"type", "integer"},
            {"description", "Zero-based index of the track to inspect."}};
        tools.append(makeTool(
            "get_track_info",
            "Get detailed information about a specific track: name, channel, event count.",
            makeParams(props, {"trackIndex"})));
    }

    // query_events
    {
        QJsonObject props;
        props["trackIndex"] = QJsonObject{
            {"type", "integer"},
            {"description", "Zero-based track index to query events from."}};
        props["startTick"] = QJsonObject{
            {"type", "integer"},
            {"description", "Start tick of the range (inclusive)."}};
        props["endTick"] = QJsonObject{
            {"type", "integer"},
            {"description", "End tick of the range (inclusive)."}};
        tools.append(makeTool(
            "query_events",
            "Query MIDI events in a tick range on a specific track. Returns serialized events.",
            makeParams(props, {"trackIndex", "startTick", "endTick"})));
    }

    // get_selection (no parameters)
    {
        tools.append(makeTool(
            "get_selection",
            "Get the events the user has currently selected in the editor, with full "
            "details (note, tick, duration, channel, track) and a 0-based index per event. "
            "Those indices are exactly what delete_events_by_index expects, so call this "
            "first when the user refers to 'the selected notes' and you need to act on "
            "specific ones (e.g. only the high notes, or every second). Returns an empty "
            "list when nothing is selected.",
            makeParams(QJsonObject(), QJsonArray())));
    }

    // --- v2.3.1 cross-tab tools (read-only half) ---
    // list_documents was MCP-only from v2.0; promoted to CORE so the MidiPilot
    // agent can see the other tabs too. The description is kept VERBATIM from
    // the old MCP-side append (McpServer::convertToolSchemas) so MCP clients
    // see an identical tool; McpServer no longer appends its own copy.

    // list_documents (no parameters)
    {
        tools.append(makeTool(
            "list_documents",
            "List all documents (tabs) open in the editor across both editor "
            "groups: index, title, file path, group (0 = left, 1 = right), "
            "active and modified flags. Use the index with switch_document.",
            makeParams(QJsonObject(), QJsonArray())));
    }

    // get_document_overview - read ANOTHER tab without binding to it
    {
        QJsonObject props;
        props["documentIndex"] = QJsonObject{
            {"type", "integer"},
            {"description", "Document index from list_documents."}};
        tools.append(makeTool(
            "get_document_overview",
            "Read-only overview of ANY open document (tab) by its list_documents index, "
            "including one that is not the document being edited: track list with names, "
            "note/event counts and channels, channels in use, duration, ticksPerQuarter, "
            "and tempo/time-signature event counts. Does NOT switch, activate, or re-bind "
            "anything - use it to inspect another tab (e.g. before "
            "import_tracks_from_document) while all edits keep landing on the current "
            "document.",
            makeParams(props, {"documentIndex"})));
    }

    // --- Write tools ---

    // create_track
    {
        QJsonObject props;
        props["trackName"] = QJsonObject{
            {"type", "string"},
            {"description", "Name for the new track."}};
        props["channel"] = QJsonObject{
            {"type", "integer"},
            {"description", "MIDI channel to assign (0-15)."}};
        tools.append(makeTool(
            "create_track",
            "Create a new MIDI track with the given name and channel. "
            "IMPORTANT: After creating a track, insert a program_change event at tick 0 "
            "via insert_events to set the GM instrument sound (otherwise it defaults to Piano). "
            "In FFXIV mode, use setup_channel_pattern instead. "
            "Up to 100 tracks supported.",
            makeParams(props, {"trackName", "channel"})));
    }

    // rename_track
    {
        QJsonObject props;
        props["trackIndex"] = QJsonObject{
            {"type", "integer"},
            {"description", "Zero-based index of the track to rename."}};
        props["newName"] = QJsonObject{
            {"type", "string"},
            {"description", "New name for the track."}};
        tools.append(makeTool(
            "rename_track",
            "Rename an existing track.",
            makeParams(props, {"trackIndex", "newName"})));
    }

    // set_channel
    {
        QJsonObject props;
        props["trackIndex"] = QJsonObject{
            {"type", "integer"},
            {"description", "Zero-based index of the track."}};
        props["channel"] = QJsonObject{
            {"type", "integer"},
            {"description", "MIDI channel to assign (0-15)."}};
        tools.append(makeTool(
            "set_channel",
            "Set the MIDI channel for a track.",
            makeParams(props, {"trackIndex", "channel"})));
    }

    // remove_track
    {
        QJsonObject props;
        props["trackIndex"] = QJsonObject{
            {"type", "integer"},
            {"description", "Zero-based index of the track to remove."}};
        tools.append(makeTool(
            "remove_track",
            "Remove a track and all its events. Cannot remove the file's last track. "
            "NOTE: track indices after the removed one shift down by one, so re-read "
            "get_editor_state before referring to tracks by index again.",
            makeParams(props, {"trackIndex"})));
    }

    // insert_events
    {
        QJsonObject props;
        props["trackIndex"] = QJsonObject{
            {"type", "integer"},
            {"description", "Zero-based track index to insert events into."}};
        props["channel"] = QJsonObject{
            {"type", "integer"},
            {"description", "MIDI channel for the events (0-15). Required."}};
        props["events"] = QJsonObject{
            {"type", "array"},
            {"items", makeEventSchema(includePitchBend)},
            {"description", insertEventsDesc}};
        tools.append(makeTool(
            "insert_events",
            "Insert new MIDI events into a track without removing existing events. "
            "Always include a program_change at tick 0 for the first insert on a track "
            "to set the correct GM instrument sound. "
            "Limits: max ~2000 events per call for fast response (<500ms), "
            "up to 10000 events per call (may take several seconds). "
            "For large compositions, split into chunks of 4-8 measures per call. "
            "Max request body size: 1MB. Rate limit: 100 calls/min.",
            makeParams(props, {"trackIndex", "channel", "events"})));
    }

    // replace_events
    {
        QJsonObject props;
        props["trackIndex"] = QJsonObject{
            {"type", "integer"},
            {"description", "Zero-based track index."}};
        props["startTick"] = QJsonObject{
            {"type", "integer"},
            {"description", "Start tick of the range to replace (inclusive)."}};
        props["endTick"] = QJsonObject{
            {"type", "integer"},
            {"description", "End tick of the range to replace (inclusive)."}};
        props["events"] = QJsonObject{
            {"type", "array"},
            {"items", makeEventSchema(includePitchBend)},
            {"description", replaceEventsDesc}};
        tools.append(makeTool(
            "replace_events",
            "Remove all events in a tick range on a track and insert new events. "
            "Used for editing/modifying existing passages. "
            "Same limits as insert_events: max ~2000 events for fast response, 10000 max.",
            makeParams(props, {"trackIndex", "startTick", "endTick", "events"})));
    }

    // delete_events
    {
        QJsonObject props;
        props["trackIndex"] = QJsonObject{
            {"type", "integer"},
            {"description", "Zero-based track index."}};
        props["startTick"] = QJsonObject{
            {"type", "integer"},
            {"description", "Start tick of the range to delete (inclusive)."}};
        props["endTick"] = QJsonObject{
            {"type", "integer"},
            {"description", "End tick of the range to delete (inclusive)."}};
        tools.append(makeTool(
            "delete_events",
            "Delete all MIDI events in a tick range on a specific track.",
            makeParams(props, {"trackIndex", "startTick", "endTick"})));
    }

    // delete_events_by_index
    {
        QJsonObject props;
        QJsonObject indicesItems;
        indicesItems["type"] = "integer";
        props["indices"] = QJsonObject{
            {"type", "array"},
            {"items", indicesItems},
            {"description",
             "0-based indices into the user's current selection (the selectedEvents "
             "array in the editor state). Only the events at these positions are "
             "deleted; the rest of the selection is kept."}};
        tools.append(makeTool(
            "delete_events_by_index",
            "Delete specific events from the user's CURRENT SELECTION by their 0-based "
            "index in the selectedEvents array. Use this for selection-relative deletes "
            "such as 'delete every second selected note' (e.g. indices [1,3,5,...]) "
            "instead of computing tick ranges or deleting notes one tick at a time. "
            "Requires events to be selected; indices out of range are ignored.",
            makeParams(props, {"indices"})));
    }

    // set_tempo
    {
        QJsonObject props;
        props["bpm"] = QJsonObject{
            {"type", "integer"},
            {"description", "Tempo in beats per minute (1-999)."}};
        props["tick"] = QJsonObject{
            {"type", "integer"},
            {"description", "Tick position for the tempo change (0 = beginning)."}};
        tools.append(makeTool(
            "set_tempo",
            "Set the tempo (BPM) at a specific tick position.",
            makeParams(props, {"bpm", "tick"})));
    }

    // set_time_signature
    {
        QJsonObject props;
        props["numerator"] = QJsonObject{
            {"type", "integer"},
            {"description", "Time signature numerator (beats per measure, 1-32)."}};
        props["denominator"] = QJsonObject{
            {"type", "integer"},
            {"enum", QJsonArray{1, 2, 4, 8, 16, 32}},
            {"description", "Time signature denominator (note value: 1, 2, 4, 8, 16, or 32)."}};
        props["tick"] = QJsonObject{
            {"type", "integer"},
            {"description", "Tick position for the time signature change (0 = beginning)."}};
        tools.append(makeTool(
            "set_time_signature",
            "Set the time signature at a specific tick position.",
            makeParams(props, {"numerator", "denominator", "tick"})));
    }

    // move_events_to_track
    {
        QJsonObject props;
        props["sourceTrackIndex"] = QJsonObject{
            {"type", "integer"},
            {"description", "Zero-based index of the source track."}};
        props["targetTrackIndex"] = QJsonObject{
            {"type", "integer"},
            {"description", "Zero-based index of the target track."}};
        props["startTick"] = QJsonObject{
            {"type", "integer"},
            {"description", "Start tick of events to move (inclusive)."}};
        props["endTick"] = QJsonObject{
            {"type", "integer"},
            {"description", "End tick of events to move (inclusive)."}};
        tools.append(makeTool(
            "move_events_to_track",
            "Move MIDI events from one track to another within a tick range.",
            makeParams(props, {"sourceTrackIndex", "targetTrackIndex", "startTick", "endTick"})));
    }

    // convert_tempo_preserve_duration (v2.2 #2, Phase 33.5) — deliberately a
    // CORE tool, not FFXIV-gated: tempo conversion is generic, and core tools
    // are also what the MCP server exposes.
    {
        QJsonObject props;
        props["sourceBpm"] = QJsonObject{
            {"anyOf", QJsonArray{QJsonObject{{"type", "number"}}, QJsonObject{{"type", "null"}}}},
            {"description", "Current musical tempo of the material in BPM. Use null to "
                            "auto-detect from the file's first tempo event."}};
        props["targetBpm"] = QJsonObject{
            {"type", "number"},
            {"description", "Tempo to convert to, in BPM (e.g. the project tempo from "
                            "get_editor_state)."}};
        props["scope"] = QJsonObject{
            {"type", "string"},
            {"enum", QJsonArray{"whole", "selected_tracks", "selected_channels", "selected_events"}},
            {"description", "What to convert: the whole file, specific tracks (trackIds), "
                            "specific channels (channelIds), or the user's current selection."}};
        props["trackIds"] = QJsonObject{
            {"anyOf", QJsonArray{
                 QJsonObject{{"type", "array"}, {"items", QJsonObject{{"type", "integer"}}}},
                 QJsonObject{{"type", "null"}}}},
            {"description", "Track numbers to convert. Required when scope == selected_tracks, "
                            "ignored otherwise."}};
        props["channelIds"] = QJsonObject{
            {"anyOf", QJsonArray{
                 QJsonObject{{"type", "array"}, {"items", QJsonObject{{"type", "integer"}}}},
                 QJsonObject{{"type", "null"}}}},
            {"description", "MIDI channels (0-15) to convert. Required when scope == "
                            "selected_channels, ignored otherwise."}};
        props["tempoMode"] = QJsonObject{
            {"anyOf", QJsonArray{
                 QJsonObject{{"type", "string"},
                             {"enum", QJsonArray{"replace", "scale_map", "events_only"}}},
                 QJsonObject{{"type", "null"}}}},
            {"description", "How to update the tempo map: replace = collapse to one tempo event "
                            "at the target BPM (default for scope=whole), scale_map = keep the "
                            "tempo curve's shape, events_only = only re-tick the scoped events. "
                            "Partial scopes REQUIRE events_only (default there): the tempo map is "
                            "shared, so replacing it from a partial scope would retime everything "
                            "outside the scope too. null = the scope's default."}};
        props["dryRun"] = QJsonObject{
            {"type", "boolean"},
            {"description", "true = report what would change WITHOUT modifying the file. "
                            "ALWAYS run with dryRun=true first, show the user the summary and ask "
                            "for confirmation before running with dryRun=false."}};
        tools.append(makeTool(
            "convert_tempo_preserve_duration",
            "Scale event ticks and update the tempo map so the material plays back with the SAME "
            "real-time duration but lives at a different musical tempo - e.g. fit a 90 BPM vocal "
            "line into a 180 BPM project so bars line up again. One undoable step. MUST be "
            "confirmed by the user: call with dryRun=true, present the summary, and only call "
            "with dryRun=false after explicit user approval.",
            // STRICT-SCHEMA-001: strict function schemas (what the Responses
            // API enforces BY DEFAULT) require `required` to list EVERY key in
            // `properties` - optionality is expressed by an anyOf[<type>, null]
            // branch, never by leaving a key out. One violating tool fails the
            // WHOLE request; see auto_fit_voice_load below for the reference.
            makeParams(props, {"sourceBpm", "targetBpm", "scope", "trackIds",
                               "channelIds", "tempoMode", "dryRun"})));
    }

    // thin_tempo_map (v2.3, Phase 49) - CORE like the conversion above: a
    // DAW-exported tempo ramp is a MIDI problem, not an FFXIV one.
    {
        QJsonObject props;
        props["toleranceMs"] = QJsonObject{
            {"anyOf", QJsonArray{QJsonObject{{"type", "number"}}, QJsonObject{{"type", "null"}}}},
            {"description", "How far any point of the piece may end up from where it is "
                            "now, in milliseconds. null = the default 2 ms, which is "
                            "inaudible. Smaller keeps more tempo events, larger keeps "
                            "fewer."}};
        props["dryRun"] = QJsonObject{
            {"anyOf", QJsonArray{QJsonObject{{"type", "boolean"}}, QJsonObject{{"type", "null"}}}},
            {"description", "true = report what would change WITHOUT modifying the file. "
                            "ALWAYS run with dryRun=true first, show the user the summary "
                            "and ask for confirmation before running with dryRun=false. "
                            "null = true."}};
        tools.append(makeTool(
            "thin_tempo_map",
            "Reduce a dense tempo map to the events that actually carry the timing. Use "
            "this when a file drags: DAW-exported tempo ramps arrive as one tempo event "
            "every few ticks (thousands of them), which makes every playback start, every "
            "scroll and every edit slow, and the game pays for them too. The music is not "
            "moved - the thinning keeps every point of the piece within the given "
            "millisecond corridor and the file's end time with it. Call it with dryRun=true "
            "to learn how many tempo events the file has and what thinning would cost, show "
            "the user those numbers, and only then apply. One undoable step.",
            // STRICT-SCHEMA-001: every property listed in `required`;
            // optionality via anyOf[<type>, null].
            makeParams(props, {"toleranceMs", "dryRun"})));
    }

    // set_ffxiv_mode (Phase 46) - CORE, deliberately OUTSIDE the FFXIV gate
    // below: the whole point is that an agent can turn the mode ON to reach
    // the gated bundle. Octet finding #1: a client saw 17 tools, had no hint
    // 5 more existed, and no way to ask for them.
    {
        QJsonObject props;
        props["enabled"] = QJsonObject{
            {"type", "boolean"},
            {"description", "true = enable FFXIV Bard Performance mode, false = disable."}};
        tools.append(makeTool(
            "set_ffxiv_mode",
            "Enable or disable FFXIV Bard Performance mode. The mode gates the FFXIV tool "
            "bundle (validate_ffxiv, convert_drums_ffxiv, setup_channel_pattern, "
            "analyze_voice_load, auto_fit_voice_load): enabling adds them to the tool list, "
            "disabling removes them. The change takes effect from your next step - in an "
            "agent run the tool list and the FFXIV rules are re-derived automatically, and "
            "MCP clients receive notifications/tools/list_changed. The current mode is "
            "reported by get_editor_state as ffxivMode.",
            makeParams(props, {"enabled"})));
    }

    // --- Phase 44: the "Manual Bot" - search->read->answer over the manual.
    // MidiPilot could edit music but only GUESS about the editor itself;
    // these two tools ground "how do I ..." answers in manual/*.html
    // (help_db.json, regenerated from the manual so it cannot drift).

    // search_help
    {
        QJsonObject props;
        props["query"] = QJsonObject{
            {"type", "string"},
            {"description", "Free-text question or keywords about the editor, e.g. "
                            "'how do I split drums' or 'tempo curve'."}};
        tools.append(makeTool(
            "search_help",
            "Search the built-in manual. Use this FIRST for any question about the editor "
            "itself (features, dialogs, menus, 'how do I ...'), then read the best match "
            "with get_help_section and answer from it, citing the manual page. Returns the "
            "top matching sections with page, anchor, title and a snippet.",
            makeParams(props, {"query"})));
    }

    // get_help_section
    {
        QJsonObject props;
        props["page"] = QJsonObject{
            {"type", "string"},
            {"description", "Manual page file name from a search_help result, e.g. "
                            "'ffxiv-drum-split.html'."}};
        props["anchor"] = QJsonObject{
            {"type", "string"},
            {"description", "Section anchor from the search_help result. Empty string = "
                            "the page's intro section."}};
        tools.append(makeTool(
            "get_help_section",
            "Read one manual section's full text (found via search_help). Answer the user "
            "from this text and cite the page.",
            makeParams(props, {"page", "anchor"})));
    }

    // --- Phase 46 pt 3: the three arrangement tools (octet finding #2). ---
    // The GUI has had all three for years (Transpose, Explode Chords, copy
    // via explode's keep-original); the octet experiment had to re-insert
    // 4248 notes by hand because the tool surface had none of them. CORE:
    // transposing/splitting/copying is generic, not FFXIV-specific.

    // transpose_events
    {
        QJsonObject props;
        props["trackIndex"] = QJsonObject{
            {"anyOf", QJsonArray{QJsonObject{{"type", "integer"}}, QJsonObject{{"type", "null"}}}},
            {"description", "Track to transpose. null = all tracks."}};
        props["startTick"] = QJsonObject{
            {"anyOf", QJsonArray{QJsonObject{{"type", "integer"}}, QJsonObject{{"type", "null"}}}},
            {"description", "Inclusive start tick. null = from the beginning."}};
        props["endTick"] = QJsonObject{
            {"anyOf", QJsonArray{QJsonObject{{"type", "integer"}}, QJsonObject{{"type", "null"}}}},
            {"description", "Inclusive end tick. null = to the end."}};
        props["semitones"] = QJsonObject{
            {"type", "integer"},
            {"description", "Semitones to transpose by: positive = up, negative = down, "
                            "12 = one octave. 0 is allowed with foldToRange=true for a "
                            "pure range fold."}};
        props["foldToRange"] = QJsonObject{
            {"anyOf", QJsonArray{QJsonObject{{"type", "boolean"}}, QJsonObject{{"type", "null"}}}},
            {"description", "After transposing, fold each note by octaves into the FFXIV "
                            "bard range C3-C6 (MIDI 48-84). Default false."}};
        tools.append(makeTool(
            "transpose_events",
            "Transpose notes by semitones - a track, a tick range, or the whole file - as one "
            "undoable step. Much cheaper than re-inserting notes: 'move the bass up an octave' "
            "is one call instead of re-writing every event. With foldToRange=true the result "
            "is folded octave-wise into C3-C6; notes that would leave the MIDI range 0-127 "
            "are left unchanged and reported as skipped.",
            makeParams(props, {"trackIndex", "startTick", "endTick", "semitones",
                               "foldToRange"})));
    }

    // split_chords_to_tracks
    {
        QJsonObject props;
        props["trackIndex"] = QJsonObject{
            {"type", "integer"},
            {"description", "Track whose chords to split."}};
        props["minNotes"] = QJsonObject{
            {"anyOf", QJsonArray{QJsonObject{{"type", "integer"}}, QJsonObject{{"type", "null"}}}},
            {"description", "Minimum simultaneous notes to count as a chord (default 2)."}};
        props["keepOriginal"] = QJsonObject{
            {"anyOf", QJsonArray{QJsonObject{{"type", "boolean"}}, QJsonObject{{"type", "null"}}}},
            {"description", "true = copy the chord notes to the new tracks and keep the "
                            "originals; false (default) = move them."}};
        tools.append(makeTool(
            "split_chords_to_tracks",
            "Split a track's chords voice-wise onto NEW tracks: notes starting on the same "
            "tick are grouped, and voice 1 (highest note) goes to the first new track, voice "
            "2 to the second, and so on - the standard way to turn a chordal part into "
            "monophonic FFXIV performers (each new track = one performer). Track names get "
            "' - Voice N' suffixes; rename them to FFXIV instruments and run "
            "setup_channel_pattern afterwards. One undoable step.",
            makeParams(props, {"trackIndex", "minNotes", "keepOriginal"})));
    }

    // copy_events_to_track
    {
        QJsonObject props;
        props["sourceTrackIndex"] = QJsonObject{
            {"type", "integer"},
            {"description", "Track to copy notes from (they stay untouched)."}};
        props["targetTrackIndex"] = QJsonObject{
            {"type", "integer"},
            {"description", "Track the copies are assigned to."}};
        props["startTick"] = QJsonObject{
            {"anyOf", QJsonArray{QJsonObject{{"type", "integer"}}, QJsonObject{{"type", "null"}}}},
            {"description", "Inclusive start tick. null = from the beginning."}};
        props["endTick"] = QJsonObject{
            {"anyOf", QJsonArray{QJsonObject{{"type", "integer"}}, QJsonObject{{"type", "null"}}}},
            {"description", "Inclusive end tick. null = to the end."}};
        tools.append(makeTool(
            "copy_events_to_track",
            "Copy notes from one track to another (optionally restricted to a tick range) as "
            "one undoable step - e.g. double a melody onto a second performer, then transpose "
            "the copy. Notes only; the copies keep their channel and timing and are assigned "
            "to the target track. Use move_events_to_track to move instead.",
            makeParams(props, {"sourceTrackIndex", "targetTrackIndex", "startTick", "endTick"})));
    }

    // --- v2.3.1 cross-tab tools (write half) ---

    // import_tracks_from_document
    {
        QJsonObject props;
        props["documentIndex"] = QJsonObject{
            {"type", "integer"},
            {"description", "Source document index from list_documents. Must be a "
                            "DIFFERENT document than the one being edited."}};
        props["trackIndexes"] = QJsonObject{
            {"anyOf", QJsonArray{
                 QJsonObject{{"type", "array"},
                             {"items", QJsonObject{{"type", "integer"}}}},
                 QJsonObject{{"type", "null"}}}},
            {"description", "Zero-based track indexes IN THE SOURCE document to import. "
                            "null = every source track that contains events."}};
        props["dryRun"] = QJsonObject{
            {"anyOf", QJsonArray{QJsonObject{{"type", "boolean"}}, QJsonObject{{"type", "null"}}}},
            {"description", "true = report what would be imported WITHOUT modifying the file. "
                            "ALWAYS run with dryRun=true first, show the user the summary "
                            "and ask for confirmation before running with dryRun=false. "
                            "null = true."}};
        tools.append(makeTool(
            "import_tracks_from_document",
            "Copy whole tracks from ANOTHER open document (tab) into the document being "
            "edited: they are appended as new tracks with their names preserved, and the "
            "source document is not modified. Events keep their source channels - "
            "collisions with channels this document already uses are reported, not "
            "remapped. Ticks are rescaled automatically when the two files' "
            "ticksPerQuarter differ. The source's tempo map is NOT imported: imported "
            "material follows THIS document's tempo, and when the two tempo maps differ "
            "the timing will audibly differ from the source (reported, never blocked). "
            "One undoable step. MUST be confirmed by the user: call with dryRun=true, "
            "present the summary, and only call with dryRun=false after explicit user "
            "approval.",
            // STRICT-SCHEMA-001: every property listed in `required`;
            // optionality via anyOf[<type>, null].
            makeParams(props, {"documentIndex", "trackIndexes", "dryRun"})));
    }

    // switch_document - DEFINITION only, opt-in (includeDocumentSwitch).
    // The call itself never reaches executeTool: the AgentRunner intercepts
    // it before generic dispatch and re-binds the run atomically (file +
    // selection context + closed-mid-run guard), WITHOUT activating the tab
    // in the UI - MidiPilot's chat stays where it is. The MCP server keeps
    // its OWN switch_document (appended in convertToolSchemas) with the
    // different, deliberate contract of activating the tab; default options
    // omit this one so the two definitions never shadow each other.
    if (options.includeDocumentSwitch) {
        QJsonObject props;
        props["index"] = QJsonObject{
            {"type", "integer"},
            {"description", "Document index from list_documents."}};
        tools.append(makeTool(
            "switch_document",
            "Re-bind this agent run to another open document (tab): from the next tool "
            "call on, every read and write acts on that document, and undo steps land "
            "there. The editor's visible tab does NOT change - the user keeps seeing "
            "this chat; the switch is announced as a step instead. Call "
            "get_editor_state right after switching to learn the new document's state "
            "before editing it. Use ONLY when the user explicitly asks you to work on "
            "another tab/document; otherwise stay on the document the run started on.",
            makeParams(props, {"index"})));
    }

    // --- FFXIV tools (only when FFXIV mode is active) ---
    if (AppPaths::settings()->value("AI/ffxiv_mode", false).toBool()) {

        // validate_ffxiv (no parameters)
        {
            tools.append(makeTool(
                "validate_ffxiv",
                "Check if the current MIDI file meets FFXIV Bard Performance constraints. "
                "Reports ALL issues with their ticks: simultaneous notes STARTING on the same "
                "tick on one performer (performers are monophonic; a held note overlapped by "
                "later notes is fine in game and NOT flagged; guitar tracks spread notes over "
                "several channels for variant switches, so only same-channel groups count "
                "there), stacked duplicate notes (same pitch, same tick), notes outside C3-C6, "
                "and track names matching no FFXIV instrument (the legal spellings are "
                "returned as legalInstruments). NOTE: in game the TRACK NAME selects the "
                "instrument - program changes only affect the editor's SoundFont playback and "
                "are not validated here.",
                makeParams(QJsonObject(), QJsonArray())));
        }

        // convert_drums_ffxiv
        {
            QJsonObject props;
            props["trackIndex"] = QJsonObject{
                {"type", "integer"},
                {"description", "Zero-based index of the GM drum track (channel 9) to convert."}};
            tools.append(makeTool(
                "convert_drums_ffxiv",
                "Convert a GM drum track (channel 9) into separate FFXIV drum instrument tracks "
                "(Bass Drum, Snare Drum, Cymbal, Timpani). Splits drum hits by GM note mapping.",
                makeParams(props, {"trackIndex"})));
        }

        // setup_channel_pattern (no parameters)
        {
            tools.append(makeTool(
                "setup_channel_pattern",
                "Fix FFXIV channel assignments and program_change events for all tracks. "
                "Moves events to the correct channel (track N → channel N, percussion → CH9), "
                "removes old program_change at tick 0, inserts correct program_change for every "
                "used channel on every track, and configures guitar switch channels. "
                "Call once after all tracks are created/renamed.",
                makeParams(QJsonObject(), QJsonArray())));
        }

        // analyze_voice_load (Phase 32.6)
        {
            QJsonObject props;
            props["startTick"] = QJsonObject{
                {"anyOf", QJsonArray{QJsonObject{{"type", "integer"}}, QJsonObject{{"type", "null"}}}},
                {"description", "Inclusive start tick to restrict the report. Use null for the full file."}};
            props["endTick"] = QJsonObject{
                {"anyOf", QJsonArray{QJsonObject{{"type", "integer"}}, QJsonObject{{"type", "null"}}}},
                {"description", "Inclusive end tick to restrict the report. Use null for the full file."}};
            tools.append(makeTool(
                "analyze_voice_load",
                "Read-only check of the FFXIV 16-voice ceiling and 14 notes/sec/channel rate ceiling. "
                "TWO models are reported: rawPeak/rawOverflowRangeCount count notes really ON at once "
                "- the number the game enforces and what auto_fit_voice_load uses; JUDGE BY THESE. "
                "globalPeak/overflowRanges use the tail-extended display model (release tails count as "
                "sounding), which reads higher on a perfectly fine file - do NOT 'fix' a file because "
                "only the display numbers are high. Also returns rateHotspots (per-channel passages "
                "exceeding the rate cap).",
                makeParams(props, {"startTick", "endTick"})));
        }

        // auto_fit_voice_load (v2.1.0 feature #1)
        {
            QJsonObject props;
            props["startTick"] = QJsonObject{
                {"anyOf", QJsonArray{QJsonObject{{"type", "integer"}}, QJsonObject{{"type", "null"}}}},
                {"description", "Inclusive start tick. Use null for the full file."}};
            props["endTick"] = QJsonObject{
                {"anyOf", QJsonArray{QJsonObject{{"type", "integer"}}, QJsonObject{{"type", "null"}}}},
                {"description", "Inclusive end tick. Use null for the full file."}};
            props["dryRun"] = QJsonObject{
                {"type", "boolean"},
                {"description", "true = report what would be removed WITHOUT modifying the file. "
                                "ALWAYS run with dryRun=true first, show the user the summary and ask "
                                "for confirmation before running with dryRun=false."}};
            props["targetCeiling"] = QJsonObject{
                {"anyOf", QJsonArray{QJsonObject{{"type", "integer"}}, QJsonObject{{"type", "null"}}}},
                {"description", "Voice ceiling to fit to (default 16, clamped 2-32)."}};
            props["chordLimit"] = QJsonObject{
                {"anyOf", QJsonArray{QJsonObject{{"type", "integer"}}, QJsonObject{{"type", "null"}}}},
                {"description", "Max simultaneous voices per channel in overflow ranges (default 3, "
                                "0 disables the chord limit). Outer voices always survive."}};
            props["desaturateRates"] = QJsonObject{
                // anyOf[..., null] like every other optional parameter here:
                // strict mode forces all keys into "required", so optionality
                // MUST be expressed by an allowed null - otherwise an MCP
                // client or a non-strict provider that simply omits the key is
                // rejected by executeTool's runtime required-gate.
                {"anyOf", QJsonArray{QJsonObject{{"type", "boolean"}}, QJsonObject{{"type", "null"}}}},
                {"description", "Also thin each track's DENSEST passages (dense cymbal walls, 32nd/64th "
                                "staccato runs) by keeping 1 of rateKeepOneOf notes per group. "
                                "Default true."}};
            props["ratePercent"] = QJsonObject{
                {"anyOf", QJsonArray{QJsonObject{{"type", "integer"}}, QJsonObject{{"type", "null"}}}},
                {"description", "Density target: thin about this percent of each track's notes, densest "
                                "passages first (clamped 0-85). For THIS tool the default is 0 (off): "
                                "fix only real overflows, opt in to density thinning explicitly. (The "
                                "GUI dialog defaults to 10 - density-as-tone is a human choice.) "
                                "Percent-of-track instead of an absolute rate because density is "
                                "relative to tempo and instrument - the tool auto-tunes the cutoff "
                                "per track."}};
            props["rateKeepOneOf"] = QJsonObject{
                {"anyOf", QJsonArray{QJsonObject{{"type", "integer"}}, QJsonObject{{"type", "null"}}}},
                {"description", "Keep 1 of N notes in dense passages: 2 = halve (default), 3 = third, "
                                "4 = quarter."}};
            props["preferLoudest"] = QJsonObject{
                {"anyOf", QJsonArray{QJsonObject{{"type", "boolean"}}, QJsonObject{{"type", "null"}}}},
                {"description", "Prefer keeping louder notes (accents) over the higher voice when "
                                "thinning. Default false: fixer-normalized files have uniform velocity, "
                                "so the higher voice (usually the melody) wins."}};
            tools.append(makeTool(
                "auto_fit_voice_load",
                "Thin the file so it fits FFXIV's mixer. Voice ceiling uses RAW concurrency (notes really "
                "sounding at once - NOT the tail-extended display numbers, which overestimate). Removal "
                "priority at overflows: duplicates, then chords over the chord limit (outer voices "
                "survive), then quietest/shortest; percussion voice counts are never touched. Rate "
                "desaturation works per TRACK (one FFXIV performer): dense runs are halved/thirded, the "
                "loudest note of each group survives. Deterministic, one undoable step. MUST be confirmed "
                "by the user: call with dryRun=true, present the summary (including the percentage of "
                "notes affected), and only call with dryRun=false after explicit user approval.",
                // STRICT-SCHEMA-001: the Responses API enforces strict function
                // schemas BY DEFAULT, and strict mode demands that `required`
                // list EVERY key in `properties` - optionality is expressed by
                // the anyOf[..., null] types above, not by omission. Listing
                // only dryRun made every request carrying this tool fail with
                // HTTP 400 "Missing 'chordLimit'" (the first missing key in
                // QJsonObject's alphabetical order), which is why FFXIV mode
                // was unusable on gpt-5.5 / gpt-5.6 while chat-completions
                // models were fine.
                makeParams(props, {"startTick", "endTick", "dryRun", "targetCeiling",
                                   "chordLimit", "desaturateRates", "ratePercent",
                                   "rateKeepOneOf", "preferLoudest"})));
        }
    }

    return tools;
}

// ---------------------------------------------------------------------------
// protocolActorPrefix — who gets the credit in the Protocol panel
// ---------------------------------------------------------------------------
// MUST stay byte-identical to the static protoPrefix() in MidiPilotWidget.cpp
// (the widget-routed tools' path) - the two forms are documented as ONE format
// in manual/mcp-server.html, and test_tool_definitions locks these strings.
QString ToolDefinitions::protocolActorPrefix(const QString &source) {
    if (!source.startsWith(QLatin1String("mcp")))
        return QStringLiteral("MidiPilot");

    // "mcp" -> "MidiPilotMCP", "mcp:ClientName" -> "MidiPilotMCP (ClientName)"
    const int colon = source.indexOf(QLatin1Char(':'));
    if (colon > 0 && colon + 1 < source.length())
        return QStringLiteral("MidiPilotMCP (%1)").arg(source.mid(colon + 1));
    return QStringLiteral("MidiPilotMCP");
}

// ---------------------------------------------------------------------------
// executeTool — dispatches tool calls to read-only or write handlers
// ---------------------------------------------------------------------------
QJsonObject ToolDefinitions::executeTool(const QString &toolName,
                                         const QJsonObject &args,
                                         MidiFile *file,
                                         MidiPilotWidget *widget,
                                         const QString &source) {
    // Phase 28 (editor groups): pin the apply target to THIS call's file for the
    // whole tool call. Write tools route through widget->executeAction(), which
    // reads the widget's live _file; without this an agent edit applied after a
    // tab switch - or any MCP edit - would land on the wrong document. The guard
    // sets the target on entry and clears it on every return (RAII).
    struct ApplyTargetScope {
        MidiPilotWidget *w;
        ApplyTargetScope(MidiPilotWidget *w, MidiFile *f) : w(w) { if (w) w->setApplyTarget(f); }
        ~ApplyTargetScope() { if (w) w->setApplyTarget(nullptr); }
    } applyTargetScope(widget, file);

    // Hardening: validate required parameters against the tool's own schema
    // before dispatching. Without this, a missing or misnamed argument (common
    // from MCP clients, which — unlike the OpenAI/Gemini strict-mode path —
    // don't enforce the schema) produces a confusing tool-specific failure
    // (e.g. "Invalid target track index" when targetTrackIndex was simply
    // absent). Here it yields a clear, actionable message instead.
    {
        const QJsonArray schemas = toolSchemas();
        for (const QJsonValue &sv : schemas) {
            const QJsonObject fn = sv.toObject().value(QStringLiteral("function")).toObject();
            if (fn.value(QStringLiteral("name")).toString() != toolName)
                continue;
            const QJsonObject params = fn.value(QStringLiteral("parameters")).toObject();
            const QJsonObject props = params.value(QStringLiteral("properties")).toObject();
            // A key is only truly mandatory if its schema does NOT offer a null
            // branch. Strict-mode schemas (which the OpenAI Responses API
            // demands) must list EVERY property in "required" and express
            // optionality as anyOf[<type>, null] instead - so treating
            // "required" literally here would reject callers that simply omit
            // an optional parameter. That is exactly what happened to
            // auto_fit_voice_load the moment its required list was completed:
            // a perfectly valid {"dryRun": true} started failing. MCP clients
            // and non-strict providers omit such keys as a matter of course.
            auto allowsNull = [](const QJsonObject &propSchema) {
                for (const QJsonValue &b : propSchema.value(QStringLiteral("anyOf")).toArray()) {
                    if (b.toObject().value(QStringLiteral("type")).toString()
                        == QStringLiteral("null"))
                        return true;
                }
                return false;
            };

            QStringList missing;
            for (const QJsonValue &rv : params.value(QStringLiteral("required")).toArray()) {
                const QString key = rv.toString();
                if (args.contains(key))
                    continue;
                if (allowsNull(props.value(key).toObject()))
                    continue; // optional in substance - the handler defaults it
                missing << key;
            }
            if (!missing.isEmpty()) {
                QJsonObject result;
                result["success"] = false;
                result["error"] = QString("Tool '%1' is missing required parameter(s): %2.")
                                      .arg(toolName, missing.join(QStringLiteral(", ")));
                return result;
            }
            break; // schema found and satisfied
        }
    }

    // Read-only tools
    if (toolName == "get_editor_state") {
        return execGetEditorState(file);
    }
    if (toolName == "get_track_info") {
        return execGetTrackInfo(args, file);
    }
    if (toolName == "query_events") {
        return execQueryEvents(args, file);
    }
    if (toolName == "get_selection") {
        return execGetSelection(file);
    }
    // v2.3.1 cross-tab reads - window-level, never re-bind anything
    if (toolName == "list_documents") {
        return execListDocuments(widget);
    }
    if (toolName == "get_document_overview") {
        return execGetDocumentOverview(args, widget);
    }

    // Write tools — delegate to widget handlers via executeAction
    if (toolName == "create_track" || toolName == "rename_track"
        || toolName == "set_channel" || toolName == "remove_track") {
        QJsonObject a = args;
        if (!source.isEmpty()) a["_source"] = source;
        return execWriteAction(toolName, a, widget);
    }
    if (toolName == "insert_events") {
        // Validate events array exists and is non-empty
        QJsonArray events = args["events"].toArray();
        if (events.isEmpty()) {
            QJsonObject result;
            result["success"] = false;
            result["recoverable"] = true;
            result["error"] = QString("Events array missing or empty (likely output truncation). "
                                      "Split the work into smaller chunks (4 measures at a time) and retry. "
                                      "Use insert_events for each chunk separately.");
            result["trackIndex"] = args["trackIndex"];
            return result;
        }
        // Map to "edit" action (insert without selection = pure insert)
        QJsonObject actionObj;
        actionObj["action"] = QString("edit");
        actionObj["events"] = events;
        if (args.contains("trackIndex"))
            actionObj["track"] = args["trackIndex"];
        if (args.contains("channel"))
            actionObj["channel"] = args["channel"];
        actionObj["explanation"] = QString("Agent: insert events");
        if (!source.isEmpty()) actionObj["_source"] = source;
        return widget->executeAction(actionObj);
    }
    if (toolName == "replace_events") {
        // Validate events array exists and is non-empty
        QJsonArray events = args["events"].toArray();
        if (events.isEmpty()) {
            QJsonObject result;
            result["success"] = false;
            result["recoverable"] = true;
            result["error"] = QString("Events array missing or empty (likely output truncation). "
                                      "Split the tick range into smaller chunks (4 measures) and use "
                                      "multiple replace_events calls. "
                                      "If you want to delete events instead, use the delete_events tool.");
            result["trackIndex"] = args["trackIndex"];
            result["startTick"] = args["startTick"];
            result["endTick"] = args["endTick"];
            return result;
        }
        // Map to "select_and_edit"
        QJsonObject actionObj;
        actionObj["action"] = QString("select_and_edit");
        actionObj["trackIndex"] = args["trackIndex"];
        actionObj["startTick"] = args["startTick"];
        actionObj["endTick"] = args["endTick"];
        actionObj["events"] = events;
        actionObj["explanation"] = QString("Agent: replace events");
        if (!source.isEmpty()) actionObj["_source"] = source;
        return widget->executeAction(actionObj);
    }
    if (toolName == "delete_events") {
        // Map to "select_and_delete"
        QJsonObject actionObj;
        actionObj["action"] = QString("select_and_delete");
        actionObj["trackIndex"] = args["trackIndex"];
        actionObj["startTick"] = args["startTick"];
        actionObj["endTick"] = args["endTick"];
        actionObj["explanation"] = QString("Agent: delete events");
        if (!source.isEmpty()) actionObj["_source"] = source;
        return widget->executeAction(actionObj);
    }
    if (toolName == "delete_events_by_index") {
        // Map to the selection-relative "delete" action — the same engine Simple
        // mode uses (applyAiDeletes), which indexes into Selection::selectedEvents()
        // with bounds-checking. Gives the agent parity with Simple mode for tasks
        // like "delete every second selected note" without range math.
        QJsonObject actionObj;
        actionObj["action"] = QString("delete");
        actionObj["deleteIndices"] = args["indices"];
        actionObj["explanation"] = QString("Agent: delete selected events by index");
        if (!source.isEmpty()) actionObj["_source"] = source;
        return widget->executeAction(actionObj);
    }
    if (toolName == "set_tempo") {
        QJsonObject a = args;
        if (!source.isEmpty()) a["_source"] = source;
        return execWriteAction("set_tempo", a, widget);
    }
    if (toolName == "set_time_signature") {
        QJsonObject a = args;
        if (!source.isEmpty()) a["_source"] = source;
        return execWriteAction("set_time_signature", a, widget);
    }
    if (toolName == "move_events_to_track") {
        // Select events from source track in range, then move to target
        QJsonObject actionObj;
        actionObj["action"] = QString("move_to_track");
        actionObj["trackIndex"] = args["targetTrackIndex"];
        actionObj["sourceTrackIndex"] = args["sourceTrackIndex"];
        actionObj["startTick"] = args["startTick"];
        actionObj["endTick"] = args["endTick"];
        actionObj["explanation"] = QString("Agent: move events to track");
        if (!source.isEmpty()) actionObj["_source"] = source;
        return widget->executeAction(actionObj);
    }
    if (toolName == "convert_tempo_preserve_duration") {
        return execConvertTempoPreserveDuration(args, file, source);
    }
    if (toolName == "thin_tempo_map") {
        return execThinTempoMap(args, file, source);
    }
    if (toolName == "set_ffxiv_mode") {
        return execSetFfxivMode(args, widget);
    }
    if (toolName == "search_help") {
        return execSearchHelp(args);
    }
    if (toolName == "get_help_section") {
        return execGetHelpSection(args);
    }
    if (toolName == "transpose_events") {
        return execTransposeEvents(args, file, source);
    }
    if (toolName == "split_chords_to_tracks") {
        return execSplitChordsToTracks(args, file, source);
    }
    if (toolName == "copy_events_to_track") {
        return execCopyEventsToTrack(args, file, source);
    }
    if (toolName == "import_tracks_from_document") {
        return execImportTracksFromDocument(args, file, widget, source);
    }
    if (toolName == "switch_document") {
        // Never executed here by design: the AgentRunner intercepts it before
        // generic dispatch (re-binds the run), and the MCP server intercepts
        // it before bound-file resolution (activates the tab). Reaching this
        // branch means a caller dispatched it without a runner - refuse with
        // a structured error instead of the generic "Unknown tool", exactly
        // like the MCP-only guard style, so nothing pretends a switch
        // happened.
        QJsonObject result;
        result["success"] = false;
        result["handledBy"] = QStringLiteral("runtime");
        result["error"] = QStringLiteral(
            "switch_document is intercepted by its runtime (the MidiPilot agent "
            "runner re-binds the run; the MCP server activates the tab) and is "
            "never dispatched here. No document was switched.");
        return result;
    }

    // FFXIV tools
    if (toolName == "validate_ffxiv") {
        return execValidateFFXIV(file);
    }
    if (toolName == "convert_drums_ffxiv") {
        return execConvertDrumsFFXIV(args, file, widget, source);
    }
    if (toolName == "setup_channel_pattern") {
        return execSetupChannelPattern(file, widget, source);
    }
    if (toolName == "analyze_voice_load") {
        return execAnalyzeVoiceLoad(args, file);
    }
    if (toolName == "auto_fit_voice_load") {
        return execAutoFitVoiceLoad(args, file, source);
    }

    QJsonObject result;
    result["success"] = false;
    result["error"] = QString("Unknown tool: ") + toolName;
    return result;
}

// ---------------------------------------------------------------------------
// Read-only tool implementations
// ---------------------------------------------------------------------------

QJsonObject ToolDefinitions::execGetEditorState(MidiFile *file) {
    QJsonObject result;
    if (!file) {
        result["success"] = false;
        result["error"] = QString("No file loaded.");
        return result;
    }
    result["success"] = true;
    result["state"] = EditorContext::captureState(file);
    return result;
}

QJsonObject ToolDefinitions::execGetSelection(MidiFile *file) {
    QJsonObject result;
    if (!file) {
        result["success"] = false;
        result["error"] = QString("No file loaded.");
        return result;
    }
    // Same list (and order) delete_events_by_index indexes into, so the "id"
    // field on each serialized event IS the index to pass to that tool. Read THIS
    // file's selection (forFile), not the globally-active one: during an agent run
    // the user may have switched tabs, but the run targets its own file, so the
    // indices the model gets here must match what the write tools act on.
    Selection *sel = Selection::forFile(file);
    QList<MidiEvent *> selected = sel ? sel->selectedEvents() : QList<MidiEvent *>();
    QJsonArray events = MidiEventSerializer::serialize(selected, file);
    result["success"] = true;
    result["count"] = selected.size();
    result["selectedEvents"] = events;
    return result;
}

// ---------------------------------------------------------------------------
// v2.3.1 cross-tab tools: list_documents / get_document_overview /
// import_tracks_from_document
// ---------------------------------------------------------------------------

#ifndef TOOLDEFINITIONS_TEST_STUB_FFXIV
// The document list lives on MainWindow. Both callers (AgentRunner and
// McpServer) hand in the ONE MidiPilotWidget instance, whose top-level window
// is the MainWindow - the same resolution the MCP pre-dispatch intercept has
// used since v2.0. Real builds only: test_tool_definitions ODR-stubs
// MidiPilotWidget as a non-QWidget, so window()/qobject_cast must never enter
// the stub link (each exec* below is #ifdef-stubbed like thin_tempo_map).
static MainWindow *mainWindowOf(MidiPilotWidget *widget) {
    if (!widget)
        return nullptr;
    if (MainWindow *mw = qobject_cast<MainWindow *>(widget->window()))
        return mw;
    // The MidiPilot panel lives in a floatable QDockWidget (MainWindow.cpp,
    // default QDockWidget features). While the dock FLOATS it is itself the
    // panel's top-level window, so window() no longer reaches the MainWindow;
    // the parent chain still does (a floating dock keeps its parent). Without
    // this, the cross-tab tools would fail with "Main window not available"
    // whenever the panel is undocked. (The MCP pre-dispatch intercept in
    // McpServer.cpp has the same window()-only resolution since v2.0 and is
    // left untouched for byte-compatibility - reported instead.)
    for (QWidget *p = widget->parentWidget(); p; p = p->parentWidget()) {
        if (MainWindow *mw = qobject_cast<MainWindow *>(p))
            return mw;
    }
    return nullptr;
}

// Meta entry (title/path/group/active/modified) for one flattened list index,
// taken from the SAME array list_documents returns - indexes and titles can
// never disagree between the cross-tab tools.
static QJsonObject documentMetaByListIndex(MainWindow *mw, int index) {
    const QJsonArray docs = mw->listOpenDocumentsJson();
    for (const QJsonValue &v : docs) {
        const QJsonObject o = v.toObject();
        if (o.value(QStringLiteral("index")).toInt(-1) == index)
            return o;
    }
    return QJsonObject();
}
#endif // TOOLDEFINITIONS_TEST_STUB_FFXIV

QJsonObject ToolDefinitions::execListDocuments(MidiPilotWidget *widget) {
#ifdef TOOLDEFINITIONS_TEST_STUB_FFXIV
    Q_UNUSED(widget);
    QJsonObject result;
    result["success"] = false;
    result["error"] = QStringLiteral("Stub build: list_documents is unavailable.");
    return result;
#else
    QJsonObject result;
    MainWindow *mw = mainWindowOf(widget);
    if (!mw) {
        result["success"] = false;
        result["error"] = QStringLiteral("Main window not available.");
        return result;
    }
    // Same result shape the MCP pre-dispatch intercept has returned since
    // v2.0 ({success, documents}) so both runtimes report identically.
    result["success"] = true;
    result["documents"] = mw->listOpenDocumentsJson();
    return result;
#endif // TOOLDEFINITIONS_TEST_STUB_FFXIV
}

QJsonObject ToolDefinitions::execGetDocumentOverview(const QJsonObject &args,
                                                     MidiPilotWidget *widget) {
#ifdef TOOLDEFINITIONS_TEST_STUB_FFXIV
    Q_UNUSED(args);
    Q_UNUSED(widget);
    QJsonObject result;
    result["success"] = false;
    result["error"] = QStringLiteral("Stub build: get_document_overview is unavailable.");
    return result;
#else
    QJsonObject result;
    MainWindow *mw = mainWindowOf(widget);
    if (!mw) {
        result["success"] = false;
        result["error"] = QStringLiteral("Main window not available.");
        return result;
    }
    const int documentIndex = args.value("documentIndex").toInt(-1);
    MidiFile *doc = mw->documentFileByListIndex(documentIndex);
    if (!doc) {
        result["success"] = false;
        result["error"] = QStringLiteral("Invalid documentIndex %1 - call "
                                         "list_documents for the current list.")
                              .arg(documentIndex);
        return result;
    }
    const QJsonObject meta = documentMetaByListIndex(mw, documentIndex);

    // One pass over the playable channels (0-15). Meta channels (16 general,
    // 17 tempo, 18 time signature) are summarized as counts only, and the
    // active-file-scoped singletons (ChannelVisibilityManager, NewNoteTool)
    // are deliberately not consulted - this may be a background tab.
    const int trackCount = doc->numTracks();
    QHash<MidiTrack *, int> trackIndexOf;
    for (int t = 0; t < trackCount; ++t)
        trackIndexOf.insert(doc->track(t), t);
    QVector<int> noteCounts(trackCount, 0);
    QVector<int> eventCounts(trackCount, 0);
    QVector<QSet<int>> trackChannels(trackCount);
    QSet<int> channelsUsed;
    for (int ch = 0; ch < 16; ++ch) {
        MidiChannel *channel = doc->channel(ch);
        if (!channel) continue;
        QMultiMap<int, MidiEvent *> *map = channel->eventMap();
        for (auto it = map->begin(); it != map->end(); ++it) {
            MidiEvent *ev = it.value();
            if (dynamic_cast<OffEvent *>(ev))
                continue; // count each note once (via its note-on)
            channelsUsed.insert(ch);
            const int t = trackIndexOf.value(ev->track(), -1);
            if (t < 0) continue;
            ++eventCounts[t];
            if (dynamic_cast<NoteOnEvent *>(ev))
                ++noteCounts[t];
            trackChannels[t].insert(ch);
        }
    }

    QJsonArray tracks;
    int totalNotes = 0;
    int totalEvents = 0;
    for (int t = 0; t < trackCount; ++t) {
        MidiTrack *track = doc->track(t);
        QJsonObject o;
        o["index"] = t;
        o["name"] = track ? track->name() : QString();
        o["noteCount"] = noteCounts[t];
        o["eventCount"] = eventCounts[t]; // playable events; note-offs not counted
        QList<int> chans = trackChannels[t].values();
        std::sort(chans.begin(), chans.end());
        QJsonArray chArr;
        for (int c : chans) chArr.append(c);
        o["channels"] = chArr;
        tracks.append(o);
        totalNotes += noteCounts[t];
        totalEvents += eventCounts[t];
    }
    QList<int> used = channelsUsed.values();
    std::sort(used.begin(), used.end());
    QJsonArray usedArr;
    for (int c : used) usedArr.append(c);

    result["success"] = true;
    result["documentIndex"] = documentIndex;
    result["title"] = meta.value(QStringLiteral("title")).toString();
    result["path"] = meta.value(QStringLiteral("path")).toString();
    result["group"] = meta.value(QStringLiteral("group")).toInt();
    result["active"] = meta.value(QStringLiteral("active")).toBool();
    result["modified"] = meta.value(QStringLiteral("modified")).toBool();
    result["ticksPerQuarter"] = doc->ticksPerQuarter();
    result["durationMs"] = doc->maxTime();
    result["endTick"] = doc->endTick();
    result["totalMeasures"] = doc->measureCount();
    result["trackCount"] = trackCount;
    result["noteCount"] = totalNotes;
    result["eventCount"] = totalEvents;
    result["channelsUsed"] = usedArr;
    result["tempoEventCount"] = doc->tempoEvents() ? doc->tempoEvents()->size() : 0;
    result["timeSignatureEventCount"] =
        doc->timeSignatureEvents() ? doc->timeSignatureEvents()->size() : 0;
    result["tracks"] = tracks;
    // Title CONCATENATED, never .arg()-substituted - a tab title comes from a
    // file name and could contain '%N' (same rule as the protocol labels).
    result["summary"] = QStringLiteral(
        "Document %1 ('").arg(documentIndex)
        + meta.value(QStringLiteral("title")).toString()
        + QStringLiteral("'): %1 track(s), %2 note(s), %3 measure(s), %4 ms. "
                         "Read-only overview - the run still edits its own document.")
              .arg(trackCount)
              .arg(totalNotes)
              .arg(doc->measureCount())
              .arg(doc->maxTime());
    return result;
#endif // TOOLDEFINITIONS_TEST_STUB_FFXIV
}

QJsonObject ToolDefinitions::execImportTracksFromDocument(const QJsonObject &args,
                                                          MidiFile *file,
                                                          MidiPilotWidget *widget,
                                                          const QString &source) {
#ifdef TOOLDEFINITIONS_TEST_STUB_FFXIV
    Q_UNUSED(args);
    Q_UNUSED(file);
    Q_UNUSED(widget);
    Q_UNUSED(source);
    QJsonObject result;
    result["success"] = false;
    result["error"] = QStringLiteral("Stub build: import_tracks_from_document is unavailable.");
    return result;
#else
    QJsonObject result;
    if (!file) {
        result["success"] = false;
        result["error"] = QStringLiteral("No MIDI file is open.");
        return result;
    }
    MainWindow *mw = mainWindowOf(widget);
    if (!mw) {
        result["success"] = false;
        result["error"] = QStringLiteral("Main window not available.");
        return result;
    }
    const int documentIndex = args.value("documentIndex").toInt(-1);
    MidiFile *src = mw->documentFileByListIndex(documentIndex);
    if (!src) {
        result["success"] = false;
        result["error"] = QStringLiteral("Invalid documentIndex %1 - call "
                                         "list_documents for the current list.")
                              .arg(documentIndex);
        return result;
    }
    if (src == file) {
        result["success"] = false;
        result["error"] = QStringLiteral(
            "documentIndex %1 is the document being edited - "
            "import_tracks_from_document copies FROM a different open document.")
                              .arg(documentIndex);
        return result;
    }

    // Strict schemas send every optional as an explicit null - .isNull() is
    // the check, never .contains() alone. Safe default: dry run.
    bool dryRun = true;
    if (args.contains("dryRun") && !args.value("dryRun").isNull()) {
        dryRun = args.value("dryRun").toBool(true);
    }
    QList<int> wanted;
    const bool explicitTracks =
        args.contains("trackIndexes") && !args.value("trackIndexes").isNull();
    if (explicitTracks) {
        for (const QJsonValue &v : args.value("trackIndexes").toArray()) {
            const int t = v.toInt(-1);
            if (t < 0 || t >= src->numTracks()) {
                result["success"] = false;
                result["error"] = QStringLiteral(
                    "Invalid source trackIndex %1 (source document has %2 tracks).")
                                      .arg(v.toInt(-1)).arg(src->numTracks());
                return result;
            }
            if (!wanted.contains(t))
                wanted.append(t);
        }
        if (wanted.isEmpty()) {
            result["success"] = false;
            result["error"] = QStringLiteral(
                "trackIndexes is an empty array - pass null to import every "
                "source track that contains events.");
            return result;
        }
    }

    // Plan: per source track, the playable events (channels 0-15) to clone.
    // Note-offs travel with their note-ons (skipped here, cloned alongside).
    // Meta channels (16 general, 17 tempo, 18 time signature) are never
    // imported - the tempo-map DIFFERENCE is reported below instead.
    const int srcTrackCount = src->numTracks();
    QHash<MidiTrack *, int> srcIndexOf;
    for (int t = 0; t < srcTrackCount; ++t)
        srcIndexOf.insert(src->track(t), t);
    QVector<QList<MidiEvent *>> planned(srcTrackCount);
    QVector<int> plannedNotes(srcTrackCount, 0);
    for (int ch = 0; ch < 16; ++ch) {
        MidiChannel *channel = src->channel(ch);
        if (!channel) continue;
        QMultiMap<int, MidiEvent *> *map = channel->eventMap();
        for (auto it = map->begin(); it != map->end(); ++it) {
            MidiEvent *ev = it.value();
            if (dynamic_cast<OffEvent *>(ev)) continue;
            const int t = srcIndexOf.value(ev->track(), -1);
            if (t < 0) continue;
            if (explicitTracks && !wanted.contains(t)) continue;
            planned[t].append(ev);
            if (dynamic_cast<NoteOnEvent *>(ev))
                ++plannedNotes[t];
        }
    }

    // Which tracks get created: the explicit list as given (empty ones too -
    // the caller asked for them by index, the name still carries meaning), or
    // every non-empty source track in file order.
    QList<int> importList;
    if (explicitTracks) {
        importList = wanted;
    } else {
        for (int t = 0; t < srcTrackCount; ++t) {
            if (!planned[t].isEmpty())
                importList.append(t);
        }
    }
    const QJsonObject meta = documentMetaByListIndex(mw, documentIndex);
    const QString srcTitle = meta.value(QStringLiteral("title")).toString();
    if (importList.isEmpty()) {
        result["success"] = true;
        result["dryRun"] = dryRun;
        result["sourceDocumentIndex"] = documentIndex;
        result["tracksImported"] = 0;
        result["eventsImported"] = 0;
        result["summary"] = QStringLiteral(
            "The source document has no events on channels 0-15 - nothing to import.");
        return result;
    }

    // ticksPerQuarter mismatch -> scale ticks (EventTool paste idiom).
    const int srcTpq = src->ticksPerQuarter();
    const int dstTpq = file->ticksPerQuarter();
    const double tickscale = (srcTpq > 0 && dstTpq != srcTpq)
                                 ? static_cast<double>(dstTpq) / static_cast<double>(srcTpq)
                                 : 1.0;
    auto scaledTick = [&](int tick) {
        return static_cast<int>(std::lround(tickscale * tick));
    };

    // Channel collisions: source channels kept, so report every imported
    // channel the target already uses (non-empty eventMap) - report, never
    // remap or block.
    QSet<int> importChannels;
    for (int t : importList) {
        for (MidiEvent *ev : planned[t])
            importChannels.insert(ev->channel());
    }
    QList<int> importChannelList = importChannels.values();
    std::sort(importChannelList.begin(), importChannelList.end());
    QStringList collisionStrings;
    QJsonArray collisions;
    for (int ch : importChannelList) {
        MidiChannel *channel = file->channel(ch);
        if (channel && channel->eventMap() && !channel->eventMap()->isEmpty()) {
            collisions.append(ch);
            collisionStrings << QString::number(ch);
        }
    }

    // Tempo-map difference: compare (tick, bpm) sequences, source ticks
    // brought into target resolution first. Reported, never blocked.
    bool tempoMapsDiffer = false;
    {
        QList<QPair<int, int>> srcTempo, dstTempo;
        auto collectTempo = [](QMultiMap<int, MidiEvent *> *map,
                               QList<QPair<int, int>> &out,
                               const std::function<int(int)> &tickMap) {
            if (!map) return;
            for (auto it = map->begin(); it != map->end(); ++it) {
                auto *tempo = dynamic_cast<TempoChangeEvent *>(it.value());
                if (tempo)
                    out.append(qMakePair(tickMap(it.key()), tempo->beatsPerQuarter()));
            }
        };
        collectTempo(src->tempoEvents(), srcTempo, scaledTick);
        collectTempo(file->tempoEvents(), dstTempo, [](int t) { return t; });
        tempoMapsDiffer = (srcTempo != dstTempo);
    }

    // Per-track report + totals (shared by dry run and apply).
    const int firstNewIndex = file->numTracks();
    QJsonArray trackReport;
    int totalEvents = 0;
    int totalNotes = 0;
    for (int i = 0; i < importList.size(); ++i) {
        const int t = importList[i];
        MidiTrack *srcTrack = src->track(t);
        QJsonObject o;
        o["sourceTrackIndex"] = t;
        o["name"] = srcTrack ? srcTrack->name() : QString();
        o["targetTrackIndex"] = firstNewIndex + i;
        o["noteCount"] = plannedNotes[t];
        o["eventCount"] = static_cast<int>(planned[t].size());
        trackReport.append(o);
        totalEvents += planned[t].size();
        totalNotes += plannedNotes[t];
    }

    if (!dryRun) {
        // ONE Protocol action: track creation + one snapshot per touched
        // target channel (documented bulk pattern - per-event snapshots cost
        // ~1 MB each on dense channels). Label concatenated, never
        // .arg()-substituted after the title (remote-ish input).
        file->protocol()->startNewAction(
            protocolActorPrefix(source)
            + QStringLiteral(": Agent import ")
            + QString::number(importList.size())
            + QStringLiteral(" track(s) from ") + srcTitle);

        QVector<MidiTrack *> dstTracks;
        dstTracks.reserve(importList.size());
        for (int t : importList) {
            file->addTrack();
            MidiTrack *dst = file->tracks()->last();
            MidiTrack *srcTrack = src->track(t);
            if (srcTrack)
                dst->setName(srcTrack->name()); // preserve the name
            dstTracks.append(dst);
        }

        // Group by target channel so each channel is snapshotted exactly once.
        QMap<int, QList<QPair<MidiEvent *, MidiTrack *>>> byChannel;
        for (int i = 0; i < importList.size(); ++i) {
            for (MidiEvent *ev : planned[importList[i]])
                byChannel[ev->channel()].append(qMakePair(ev, dstTracks[i]));
        }
        for (auto it = byChannel.begin(); it != byChannel.end(); ++it) {
            MidiChannel *channel = file->channel(it.key());
            ProtocolEntry *snapshot = channel->copy();
            for (const auto &pair : it.value()) {
                MidiEvent *orig = pair.first;
                MidiTrack *dst = pair.second;
                // Clone-and-insert idiom from EventTool::copyAction/pasteAction:
                // clone the event, pair a cloned note-off to a cloned note-on,
                // re-home both (file/channel/track), insert at the scaled tick.
                MidiEvent *ev = dynamic_cast<MidiEvent *>(orig->copy());
                if (!ev) continue;
                OffEvent *offClone = nullptr;
                int offTick = 0;
                OnEvent *onOrig = dynamic_cast<OnEvent *>(orig);
                OnEvent *onClone = dynamic_cast<OnEvent *>(ev);
                if (onOrig && onClone && onOrig->offEvent()) {
                    offClone = dynamic_cast<OffEvent *>(onOrig->offEvent()->copy());
                    if (offClone) {
                        offClone->setOnEvent(onClone); // links both directions
                        offTick = scaledTick(onOrig->offEvent()->midiTime());
                    }
                }
                ev->setFile(file);
                ev->setChannel(it.key(), false);
                ev->setTrack(dst, false);
                channel->insertEvent(ev, scaledTick(orig->midiTime()), false);
                if (offClone) {
                    offClone->setFile(file);
                    offClone->setChannel(it.key(), false);
                    offClone->setTrack(dst, false);
                    channel->insertEvent(offClone, offTick, false);
                }
            }
            channel->protocol(snapshot, channel);
        }
        file->calcMaxTime();
        file->protocol()->endAction();
    }

    result["success"] = true;
    result["dryRun"] = dryRun;
    result["sourceDocumentIndex"] = documentIndex;
    result["sourceTitle"] = srcTitle;
    result["tracksImported"] = static_cast<int>(importList.size());
    result["firstNewTrackIndex"] = firstNewIndex;
    result["eventsImported"] = totalEvents;
    result["notesImported"] = totalNotes;
    result["ticksRescaled"] = (tickscale != 1.0);
    result["tickScaleFactor"] = tickscale;
    result["channelCollisions"] = collisions;
    result["tempoMapsDiffer"] = tempoMapsDiffer;
    result["tracks"] = trackReport;

    // Honest summary, thin_tempo_map style: dry runs instruct the model to
    // confirm with the user first. Title concatenated, never .arg()'d.
    QString summary =
        QStringLiteral("%1 %2 track(s) (%3 event(s), %4 note(s)) from '")
            .arg(dryRun ? QStringLiteral("Would import") : QStringLiteral("Imported"))
            .arg(importList.size())
            .arg(totalEvents)
            .arg(totalNotes)
        + srcTitle
        + QStringLiteral("' as new track(s) %1-%2, names preserved; the source "
                         "document is untouched.")
              .arg(firstNewIndex)
              .arg(firstNewIndex + importList.size() - 1);
    if (tickscale != 1.0) {
        summary += QStringLiteral(" Ticks rescaled x%1 (source %2 -> target %3 "
                                  "ticksPerQuarter).")
                       .arg(tickscale, 0, 'g', 6)
                       .arg(srcTpq)
                       .arg(dstTpq);
    }
    if (!collisionStrings.isEmpty()) {
        summary += QStringLiteral(" Channel collision(s): imported events keep "
                                  "source channel(s) %1, which this document "
                                  "already uses - existing material stays "
                                  "untouched but shares those channels.")
                       .arg(collisionStrings.join(QStringLiteral(", ")));
    }
    if (tempoMapsDiffer) {
        summary += QStringLiteral(" The two documents' tempo maps differ and the "
                                  "tempo map is NOT imported: the imported "
                                  "material follows THIS document's tempo, so "
                                  "its timing will audibly differ from the "
                                  "source document.");
    }
    if (dryRun) {
        summary += QStringLiteral(" Present this to the user and ask for "
                                  "confirmation before calling again with "
                                  "dryRun=false.");
    } else {
        summary += QStringLiteral(" One undo step restores everything.");
    }
    result["summary"] = summary;
    return result;
#endif // TOOLDEFINITIONS_TEST_STUB_FFXIV
}

QJsonObject ToolDefinitions::execGetTrackInfo(const QJsonObject &args, MidiFile *file) {
    QJsonObject result;
    if (!file) {
        result["success"] = false;
        result["error"] = QString("No file loaded.");
        return result;
    }

    int trackIndex = args["trackIndex"].toInt(-1);
    if (trackIndex < 0 || trackIndex >= file->numTracks()) {
        result["success"] = false;
        result["error"] = QString("Invalid track index: %1").arg(trackIndex);
        return result;
    }

    MidiTrack *track = file->track(trackIndex);
    QJsonObject info;
    info["index"] = trackIndex;
    info["name"] = track->name();
    info["channel"] = track->assignedChannel();

    // Count events on this track
    int eventCount = 0;
    int noteCount = 0;
    for (int ch = 0; ch < 16; ch++) {
        MidiChannel *channel = file->channel(ch);
        if (!channel) continue;
        QMultiMap<int, MidiEvent *> *map = channel->eventMap();
        for (auto it = map->begin(); it != map->end(); ++it) {
            MidiEvent *ev = it.value();
            if (ev->track() == track) {
                eventCount++;
                if (dynamic_cast<NoteOnEvent *>(ev))
                    noteCount++;
            }
        }
    }
    info["eventCount"] = eventCount;
    info["noteCount"] = noteCount;

    result["success"] = true;
    result["track"] = info;
    return result;
}

QJsonObject ToolDefinitions::execQueryEvents(const QJsonObject &args, MidiFile *file) {
    QJsonObject result;
    if (!file) {
        result["success"] = false;
        result["error"] = QString("No file loaded.");
        return result;
    }

    int trackIndex = args["trackIndex"].toInt(-1);
    int startTick = args["startTick"].toInt(-1);
    int endTick = args["endTick"].toInt(-1);

    if (trackIndex < 0 || trackIndex >= file->numTracks()) {
        result["success"] = false;
        result["error"] = QString("Invalid track index: %1").arg(trackIndex);
        return result;
    }
    if (startTick < 0 || endTick < startTick) {
        result["success"] = false;
        result["error"] = QString("Invalid tick range: %1-%2").arg(startTick).arg(endTick);
        return result;
    }

    MidiTrack *track = file->track(trackIndex);
    QList<MidiEvent *> *allEvents = file->eventsBetween(startTick, endTick);

    QList<MidiEvent *> filtered;
    if (allEvents) {
        for (MidiEvent *ev : *allEvents) {
            if (dynamic_cast<OffEvent *>(ev)) continue;
            if (ev->channel() >= 16) continue;
            if (ev->track() != track) continue;
            filtered.append(ev);
        }
        delete allEvents;
    }

    QJsonArray serialized = MidiEventSerializer::serialize(filtered, file);

    result["success"] = true;
    result["events"] = serialized;
    result["count"] = filtered.size();
    return result;
}

// ---------------------------------------------------------------------------
// Write tool helper — builds action JSON and delegates to widget
// ---------------------------------------------------------------------------

QJsonObject ToolDefinitions::execWriteAction(const QString &action,
                                              const QJsonObject &args,
                                              MidiPilotWidget *widget) {
    QJsonObject actionObj = args;
    actionObj["action"] = action;
    if (!actionObj.contains("explanation"))
        actionObj["explanation"] = QString("Agent: ") + action;
    return widget->executeAction(actionObj);
}

// ---------------------------------------------------------------------------
// FFXIV tools
// ---------------------------------------------------------------------------

// Phase 46: the instrument table and classifiers live in FFXIVChannelFixer
// (the canonical source); the validation logic itself moved to
// FfxivPlayabilityValidator, shared with the GUI's playability dialog.

QJsonObject ToolDefinitions::execValidateFFXIV(MidiFile *file) {
#ifdef TOOLDEFINITIONS_TEST_STUB_FFXIV
    Q_UNUSED(file);
    QJsonObject result;
    result["success"] = false;
    result["error"] = QStringLiteral("Stub build: validate_ffxiv is unavailable.");
    return result;
#else
    QJsonObject result;
    // VALIDATE-TYPES-001: the tool's contract is IN-GAME playability - the
    // editor-playback (channelSpread) and informational (emptyTracks) checks
    // are workbench-only. Running them here mislabeled their findings as
    // "polyphonic" and flipped `valid` to false on healthy files, sending
    // agents off to "fix" chords that never existed.
    FfxivPlayabilityChecks toolChecks;
    toolChecks.channelSpread = false;
    toolChecks.emptyTracks = false;
    // Phase 49: the tempo-map finding is workbench-only for the same reason.
    // It is not a NOTE problem, so folding it into `valid` would report a
    // perfectly playable file as invalid and send the agent hunting for
    // chords. The count travels as its own field below instead.
    toolChecks.tempoMap = false;
    const FfxivPlayabilityReport report =
        FfxivPlayabilityValidator::validate(file, toolChecks);
    if (!report.ok) {
        result["success"] = false;
        result["error"] = report.error;
        return result;
    }

    auto typeString = [](FfxivPlayabilityIssue::Type t) -> QString {
        switch (t) {
        case FfxivPlayabilityIssue::Type::TrackName:
            return QStringLiteral("track_name");
        case FfxivPlayabilityIssue::Type::OutOfRange:
            return QStringLiteral("out_of_range");
        case FfxivPlayabilityIssue::Type::DuplicateNote:
            return QStringLiteral("duplicate_note");
        case FfxivPlayabilityIssue::Type::Overlap:
            break;
        }
        // "polyphonic" kept for Overlap - the name agents have always seen.
        return QStringLiteral("polyphonic");
    };

    // The validator reports ALL findings (the old inline code stopped at the
    // first overlap per track); cap the JSON so a badly broken file cannot
    // blow up the model's context.
    QJsonArray issues;
    const int cap = 100;
    bool hasNameIssue = false;
    for (const FfxivPlayabilityIssue &i : report.issues) {
        if (i.type == FfxivPlayabilityIssue::Type::TrackName)
            hasNameIssue = true;
        if (issues.size() >= cap)
            continue;
        QJsonObject issue;
        issue["track"] = i.track;
        issue["issue"] = typeString(i.type);
        issue["tick"] = i.tick;
        issue["details"] = i.details;
        issues.append(issue);
    }

    result["success"] = true;
    result["valid"] = report.issues.isEmpty();
    result["issues"] = issues;
    // Phase 49: not a playability verdict, a fact the agent cannot get
    // anywhere else - a DAW tempo ramp exported as thousands of events makes
    // the file heavy for the editor and for the game. thin_tempo_map is the
    // repair; the number is here so the agent knows when to reach for it.
    result["tempoEventCount"] = TempoMapThinner::tempoEventCount(file);
    if (report.issues.size() > cap)
        result["issuesTruncated"] = report.issues.size() - cap;
    // Octet finding #6: a rejected track name used to leave the agent
    // guessing spellings. Quote the canonical list when a name failed.
    if (hasNameIssue)
        result["legalInstruments"] =
            QJsonArray::fromStringList(FFXIVChannelFixer::instrumentNames());

    if (report.issues.isEmpty()) {
        result["summary"] = QStringLiteral("File is FFXIV-compliant");
    } else {
        result["summary"] = QStringLiteral(
            "%1 issue(s): %2 simultaneous-note chord(s), %3 stacked "
            "duplicate(s), %4 out of range, %5 track name(s)")
            .arg(report.issues.size())
            .arg(report.countOf(FfxivPlayabilityIssue::Type::Overlap))
            .arg(report.countOf(FfxivPlayabilityIssue::Type::DuplicateNote))
            .arg(report.countOf(FfxivPlayabilityIssue::Type::OutOfRange))
            .arg(report.countOf(FfxivPlayabilityIssue::Type::TrackName));
    }
    return result;
#endif // TOOLDEFINITIONS_TEST_STUB_FFXIV
}

QJsonObject ToolDefinitions::execConvertDrumsFFXIV(const QJsonObject &args,
                                                    MidiFile *file,
                                                    MidiPilotWidget *widget,
                                                    const QString &source) {
    QJsonObject result;
    if (!file) {
        result["success"] = false;
        result["error"] = QString("No file loaded.");
        return result;
    }

    int trackIndex = args["trackIndex"].toInt(-1);
    if (trackIndex < 0 || trackIndex >= file->numTracks()) {
        result["success"] = false;
        result["error"] = QString("Invalid track index: %1").arg(trackIndex);
        return result;
    }

    MidiTrack *srcTrack = file->track(trackIndex);

    // Collect all NoteOn events from the source track
    struct DrumHit { int tick; int note; int velocity; int duration; };
    QList<DrumHit> kicks, snares, cymbals, toms;

    for (int ch = 0; ch < 16; ch++) {
        MidiChannel *channel = file->channel(ch);
        if (!channel) continue;
        QMultiMap<int, MidiEvent *> *map = channel->eventMap();
        for (auto it = map->begin(); it != map->end(); ++it) {
            MidiEvent *ev = it.value();
            if (ev->track() != srcTrack) continue;
            auto *noteOn = dynamic_cast<NoteOnEvent *>(ev);
            if (!noteOn) continue;

            int note = noteOn->note();
            int tick = noteOn->midiTime();
            int vel = noteOn->velocity();
            int dur = 96; // default short duration
            MidiEvent *offEv = noteOn->offEvent();
            if (offEv) dur = offEv->midiTime() - tick;
            if (dur < 1) dur = 96;

            // GM drum mapping
            if (note == 35 || note == 36) {
                // Kick -> Bass Drum
                kicks.append({tick, 60, vel, dur}); // C4
            } else if (note == 38 || note == 40) {
                // Snare -> Snare Drum
                snares.append({tick, 72, vel, dur}); // C5
            } else if (note == 42 || note == 44 || note == 46) {
                // Hi-hat -> Cymbal (high range)
                cymbals.append({tick, 84, vel, dur}); // C6
            } else if (note == 49 || note == 57) {
                // Crash -> Cymbal (low range)
                cymbals.append({tick, 72, vel, dur}); // C5
            } else if (note == 51 || note == 59) {
                // Ride -> Cymbal (mid range)
                cymbals.append({tick, 78, vel, dur}); // F#5
            } else if (note >= 41 && note <= 50) {
                // Toms -> Timpani (map tonally: low toms = lower pitch)
                int timpaniNote = 60 + (note - 41) * 2; // spread across range
                if (timpaniNote > 84) timpaniNote = 84;
                if (timpaniNote < 48) timpaniNote = 48;
                toms.append({tick, timpaniNote, vel, dur});
            }
        }
    }

    if (kicks.isEmpty() && snares.isEmpty() && cymbals.isEmpty() && toms.isEmpty()) {
        result["success"] = false;
        result["error"] = QString("No GM drum events found on track %1").arg(trackIndex);
        return result;
    }

    // Helper: create a track and insert events
    auto createDrumTrack = [&](const QString &name, const QList<DrumHit> &hits) -> int {
        if (hits.isEmpty()) return -1;

        // Create track
        QJsonObject createAction;
        createAction["action"] = QString("create_track");
        createAction["trackName"] = name;
        createAction["channel"] = 0;
        createAction["explanation"] = QString("FFXIV drum: %1").arg(name);
        if (!source.isEmpty()) createAction["_source"] = source;
        QJsonObject createResult = widget->executeAction(createAction);
        int newTrackIdx = createResult["trackIndex"].toInt(-1);
        if (newTrackIdx < 0) return -1;

        // Build events array
        QJsonArray events;
        for (const DrumHit &hit : hits) {
            QJsonObject ev;
            ev["type"] = QString("note");
            ev["tick"] = hit.tick;
            ev["note"] = hit.note;
            ev["velocity"] = hit.velocity;
            ev["duration"] = hit.duration;
            events.append(ev);
        }

        // Insert events
        QJsonObject insertAction;
        insertAction["action"] = QString("edit");
        insertAction["events"] = events;
        insertAction["track"] = newTrackIdx;
        insertAction["channel"] = 0;
        insertAction["explanation"] = QString("FFXIV drum events: %1").arg(name);
        if (!source.isEmpty()) insertAction["_source"] = source;
        widget->executeAction(insertAction);
        return newTrackIdx;
    };

    QJsonArray createdTracks;
    int idx;

    idx = createDrumTrack("Bass Drum", kicks);
    if (idx >= 0) createdTracks.append(QJsonObject{{"name", "Bass Drum"}, {"trackIndex", idx}, {"events", kicks.size()}});

    idx = createDrumTrack("Snare Drum", snares);
    if (idx >= 0) createdTracks.append(QJsonObject{{"name", "Snare Drum"}, {"trackIndex", idx}, {"events", snares.size()}});

    idx = createDrumTrack("Cymbal", cymbals);
    if (idx >= 0) createdTracks.append(QJsonObject{{"name", "Cymbal"}, {"trackIndex", idx}, {"events", cymbals.size()}});

    idx = createDrumTrack("Timpani", toms);
    if (idx >= 0) createdTracks.append(QJsonObject{{"name", "Timpani"}, {"trackIndex", idx}, {"events", toms.size()}});

    result["success"] = true;
    result["createdTracks"] = createdTracks;
    result["summary"] = QString("Created %1 FFXIV drum tracks from GM drum track %2")
                            .arg(createdTracks.size()).arg(trackIndex);
    return result;
}

// ---------------------------------------------------------------------------
// FFXIV instrument → GM program number mapping
// ---------------------------------------------------------------------------
static int ffxivProgramNumber(const QString &instrumentName) {
    // Strip +N/-N suffix
    QString base = instrumentName;
    QRegularExpression suffixRe("[+-]\\d+$");
    base.remove(suffixRe);

    // NOTE: program numbers must match the actual presets in the FFXIV
    // SoundFont (FF14-c3c6-fixed.sf2). Mismatches cause silent fallback to
    // bank 0 / prog 0 (= Piano). Verified 2026-04-28 against phdr chunk.
    static const QHash<QString, int> map = {
        {"Piano", 0},       {"Harp", 46},       {"Fiddle", 45},
        {"Lute", 25},       {"Fife", 72},        {"Flute", 73},
        {"Oboe", 68},       {"Panpipes", 75},    {"Clarinet", 71},
        {"Trumpet", 56},    {"Saxophone", 65},   {"Trombone", 57},
        {"Horn", 60},       {"Tuba", 58},
        {"Violin", 40},     {"Viola", 41},       {"Cello", 42},
        {"Double Bass", 43},
        {"Timpani", 47},    {"Bongo", 116},      {"Bass Drum", 117},
        {"Snare Drum", 118}, {"Cymbal", 119},
        {"ElectricGuitarClean", 27},       {"ElectricGuitarMuted", 28},
        {"ElectricGuitarOverdriven", 29},  {"ElectricGuitarPowerChords", 30},
        {"ElectricGuitarSpecial", 31}
    };
    return map.value(base, -1);
}

// ---------------------------------------------------------------------------
// setup_channel_pattern — delegates to FFXIVChannelFixer
// ---------------------------------------------------------------------------
QJsonObject ToolDefinitions::execSetupChannelPattern(MidiFile *file,
                                                      MidiPilotWidget * /*widget*/,
                                                      const QString &source) {
    // V131-P2-04: FFXIVChannelFixer::fixChannels() calls
    // MidiChannel::removeEvent(..., true) in its CLEAN phase, which routes
    // through Protocol::enterUndoStep(). If no action is open, every undo
    // item is silently `delete`d and the event leaks (no one still owns it).
    // The interactive MainWindow path wraps in startNewAction/endAction, but
    // this AI-tool entry point previously did not. Wrap here so the fix runs
    // under a Protocol action regardless of caller.
    if (file && file->protocol())
        file->protocol()->startNewAction(
            protocolActorPrefix(source)
            + QStringLiteral(": Agent setup channel pattern"));
    QJsonObject result = FFXIVChannelFixer::fixChannels(file);
    if (file && file->protocol())
        file->protocol()->endAction();
    return result;
}

// ---------------------------------------------------------------------------
// analyze_voice_load — read-only FFXIV voice / rate ceiling report (Phase 32.6)
// ---------------------------------------------------------------------------
QJsonObject ToolDefinitions::execSetFfxivMode(const QJsonObject &args,
                                              MidiPilotWidget *widget) {
    const bool enabled = args.value("enabled").toBool();
    if (widget) {
        // Through the checkbox: persists the setting AND raises
        // ffxivModeChanged, which MainWindow forwards to the MCP server's
        // tools/list_changed broadcast.
        widget->setFfxivMode(enabled);
    } else {
        AppPaths::settings()->setValue("AI/ffxiv_mode", enabled);
    }
    QJsonObject result;
    result["success"] = true;
    result["ffxivMode"] = enabled;
    result["summary"] = enabled
        ? QStringLiteral("FFXIV mode enabled - the FFXIV tools (validate_ffxiv, "
                         "convert_drums_ffxiv, setup_channel_pattern, analyze_voice_load, "
                         "auto_fit_voice_load) and the FFXIV rules are available from your "
                         "next step.")
        : QStringLiteral("FFXIV mode disabled - the FFXIV tools are no longer available "
                         "from your next step.");
    return result;
}

QJsonObject ToolDefinitions::execAnalyzeVoiceLoad(const QJsonObject &args, MidiFile *file) {
#ifdef TOOLDEFINITIONS_TEST_STUB_FFXIV
    Q_UNUSED(args);
    Q_UNUSED(file);
    QJsonObject result;
    result["success"] = false;
    result["error"] = QStringLiteral("Stub build: analyze_voice_load is unavailable.");
    return result;
#else
    QJsonObject result;
    if (!file) {
        result["success"] = false;
        result["error"] = QStringLiteral("No MIDI file is open.");
        return result;
    }

    const bool hasStart = args.contains("startTick") && !args.value("startTick").isNull();
    const bool hasEnd = args.contains("endTick") && !args.value("endTick").isNull();
    const int startTick = hasStart ? args.value("startTick").toInt() : INT_MIN;
    const int endTick = hasEnd ? args.value("endTick").toInt() : INT_MAX;

    auto *analyzer = FfxivVoiceAnalyzer::instance();
    // Force a synchronous compute so the report is current even when the
    // analyser is disabled in the UI.
    FfxivVoiceAnalyzer::Result r = analyzer->recomputeNow(file);
    if (!r.valid) {
        result["success"] = false;
        result["error"] = QStringLiteral("Voice-load analysis is unavailable for this file.");
        return result;
    }

    // Compute peak / overflow ranges within [startTick, endTick].
    int peak = 0;
    int peakTick = 0;
    int overflowEvents = 0;
    QJsonArray overflowRanges;
    int curOverStart = -1;
    int curOverPeak = 0;

    const auto &samples = r.voiceSamples;
    for (int i = 0; i < samples.size(); ++i) {
        const int t = samples[i].tick;
        const int v = samples[i].voiceCount;
        const int nextT = (i + 1 < samples.size()) ? samples[i + 1].tick : t;
        // Skip samples whose [t, nextT) interval lies entirely outside the range.
        if (nextT < startTick) continue;
        if (t > endTick) break;

        if (v > peak) { peak = v; peakTick = t; }

        if (v > FfxivVoiceAnalyzer::kVoiceCeiling) {
            ++overflowEvents;
            if (curOverStart < 0) {
                curOverStart = qMax(t, startTick);
                curOverPeak = v;
            } else {
                curOverPeak = qMax(curOverPeak, v);
            }
        } else if (curOverStart >= 0) {
            QJsonObject range;
            range["startTick"] = curOverStart;
            range["endTick"] = qMin(t, endTick);
            range["peakVoices"] = curOverPeak;
            overflowRanges.append(range);
            curOverStart = -1;
            curOverPeak = 0;
        }
    }
    if (curOverStart >= 0) {
        QJsonObject range;
        range["startTick"] = curOverStart;
        range["endTick"] = qMin(samples.last().tick, endTick);
        range["peakVoices"] = curOverPeak;
        overflowRanges.append(range);
    }

    QJsonArray rateHotspots;
    for (const auto &h : r.rateHotspots) {
        if (h.endTick < startTick || h.startTick > endTick) continue;
        QJsonObject hotspot;
        hotspot["channel"] = h.channel;
        hotspot["startTick"] = h.startTick;
        hotspot["endTick"] = h.endTick;
        hotspot["notesInWindow"] = h.notesInWindow;
        hotspot["notesPerSecond"] = h.notesPerSecond;
        rateHotspots.append(hotspot);
    }

    // Octet finding #3: this report used the tail-extended DISPLAY model
    // (release tails keep sounding after note-off) while auto_fit_voice_load
    // judges RAW concurrency - the two contradicted each other (display 24-30
    // vs raw 7 on a fine file), and an agent reading only this tool "fixed"
    // files that were never broken. Compute the raw truth alongside via the
    // same engine auto_fit uses (a dry run with every removal pass off) and
    // base the verdict on it.
    int rawPeak = -1;
    int rawOverflowRanges = -1;
    {
        AutoFitOptions rawOpts;
        rawOpts.dryRun = true;
        rawOpts.ratePercent = 0;
        rawOpts.chordLimit = 0;        // 0 = chord pass off
        rawOpts.desaturateRates = false;
        if (hasStart) rawOpts.startTick = startTick;
        if (hasEnd) rawOpts.endTick = endTick;
        const AutoFitResult rawR = AutoFitVoiceLoadService::apply(file, rawOpts);
        if (rawR.ok) {
            rawPeak = rawR.peakBefore;
            rawOverflowRanges = rawR.overflowRangeCount;
        }
    }

    result["success"] = true;
    result["rawPeak"] = rawPeak;
    result["rawOverflowRangeCount"] = rawOverflowRanges;
    result["displayModelNote"] = QStringLiteral(
        "globalPeak/overflowRanges use the tail-extended DISPLAY model (release tails "
        "count as sounding). rawPeak counts notes that are really on - the number the "
        "game enforces and the one auto_fit_voice_load uses. Judge by rawPeak.");
    result["voiceCeiling"] = FfxivVoiceAnalyzer::kVoiceCeiling;
    result["noteRateCeilingPerChannel"] = FfxivVoiceAnalyzer::kNoteRateCeilingPerChannel;
    result["noteRateWindowMs"] = FfxivVoiceAnalyzer::kNoteRateWindowMs;
    result["globalPeak"] = peak;
    result["globalPeakTick"] = peakTick;
    result["overflowEventCount"] = overflowEvents;
    result["overflowRanges"] = overflowRanges;
    result["rateHotspots"] = rateHotspots;
    if (hasStart) result["startTick"] = startTick;
    if (hasEnd) result["endTick"] = endTick;
    // Verdict by the RAW model - the physical truth the game enforces.
    QString summary;
    const bool rawKnown = rawPeak >= 0;
    const bool rawOk = rawKnown && rawOverflowRanges == 0;
    if (peak == 0 && (!rawKnown || rawPeak == 0)) {
        summary = QStringLiteral("No notes in range; voice-load is 0/16.");
    } else if (rawKnown && rawOk && rateHotspots.isEmpty()) {
        summary = QStringLiteral("OK: raw peak %1/16 voices (display model shows %2 with "
                                 "release tails - that is normal), no rate hotspots.")
                      .arg(rawPeak).arg(peak);
    } else if (rawKnown) {
        summary = QStringLiteral("WARNING: raw peak %1/16 voices, %2 raw overflow range(s), "
                                 "%3 rate hotspot(s). (Display model: peak %4, %5 range(s).)")
                      .arg(rawPeak)
                      .arg(rawOverflowRanges)
                      .arg(rateHotspots.size())
                      .arg(peak)
                      .arg(overflowRanges.size());
    } else if (overflowRanges.isEmpty() && rateHotspots.isEmpty()) {
        summary = QStringLiteral("OK: peak %1/16 voices, no rate hotspots.").arg(peak);
    } else {
        summary = QStringLiteral("WARNING: peak %1/16 voices, %2 overflow range(s), %3 rate hotspot(s).")
                      .arg(peak)
                      .arg(overflowRanges.size())
                      .arg(rateHotspots.size());
    }
    result["summary"] = summary;
    return result;
#endif // TOOLDEFINITIONS_TEST_STUB_FFXIV
}

QJsonObject ToolDefinitions::execAutoFitVoiceLoad(const QJsonObject &args,
                                                  MidiFile *file,
                                                  const QString &source) {
#ifdef TOOLDEFINITIONS_TEST_STUB_FFXIV
    Q_UNUSED(args);
    Q_UNUSED(file);
    Q_UNUSED(source);
    QJsonObject result;
    result["success"] = false;
    result["error"] = QStringLiteral("Stub build: auto_fit_voice_load is unavailable.");
    return result;
#else
    QJsonObject result;
    if (!file) {
        result["success"] = false;
        result["error"] = QStringLiteral("No MIDI file is open.");
        return result;
    }

    AutoFitOptions opts;
    if (args.contains("startTick") && !args.value("startTick").isNull())
        opts.startTick = args.value("startTick").toInt();
    if (args.contains("endTick") && !args.value("endTick").isNull())
        opts.endTick = args.value("endTick").toInt();
    if (args.contains("targetCeiling") && !args.value("targetCeiling").isNull())
        opts.targetCeiling = args.value("targetCeiling").toInt();
    if (args.contains("chordLimit") && !args.value("chordLimit").isNull())
        opts.chordLimit = args.value("chordLimit").toInt();
    if (args.contains("desaturateRates"))
        opts.desaturateRates = args.value("desaturateRates").toBool(true);
    // Octet finding #4: the service default (10) is a GUI-dialog default -
    // for the AI tool a dry run on a perfectly fine file reported "would
    // remove 734 notes", which reads as "your file is broken". The agent
    // fixes real overflows by default and OPTS IN to density thinning.
    opts.ratePercent = 0;
    if (args.contains("ratePercent") && !args.value("ratePercent").isNull())
        opts.ratePercent = args.value("ratePercent").toInt();
    if (args.contains("rateKeepOneOf") && !args.value("rateKeepOneOf").isNull())
        opts.rateKeepOneOf = args.value("rateKeepOneOf").toInt();
    if (args.contains("preferLoudest"))
        opts.preferLoudest = args.value("preferLoudest").toBool(false);
    opts.dryRun = args.value("dryRun").toBool(true); // safe default: dry run

    // Protocol-panel attribution. The label is built HERE (not in the service)
    // so it uses the same "<actor>: Agent <verb>" format as every other
    // agent/MCP action; the service keeps its own label for the dialog.
    // Prefix concatenated, not .arg()-substituted - see execTransposeEvents.
    opts.actionLabel = protocolActorPrefix(source)
                       + QStringLiteral(": Agent auto-fit voice load");

    const AutoFitResult r = AutoFitVoiceLoadService::apply(file, opts);
    if (!r.ok) {
        result["success"] = false;
        result["error"] = r.error;
        return result;
    }
    if (!opts.dryRun && r.removedCount > 0) {
        // The selection may reference just-removed events; stale pointers get
        // re-inserted into the channel map by later selection edits. Clear
        // THIS file's selection (forFile, same rule as get_selection): during
        // an agent/MCP run the user may have switched tabs, and instance()
        // would wipe the wrong document while the stale pointers survive in
        // the target's retained selection.
        Selection *sel = Selection::forFile(file);
        if (sel) {
            sel->clearSelection();
        }
    }

    result["success"] = true;
    result["dryRun"] = opts.dryRun;
    result["peakBefore"] = r.peakBefore;
    result["remainingPeak"] = r.remainingPeak;
    result["overflowRangeCount"] = r.overflowRangeCount;
    result["removedCount"] = r.removedCount;
    result["duplicateRemoved"] = r.duplicateRemoved;
    result["chordRemoved"] = r.chordRemoved;
    result["ceilingRemoved"] = r.ceilingRemoved;
    result["rateRemoved"] = r.rateRemoved;
    result["totalNotesInScope"] = r.totalNotesInScope;
    if (r.totalNotesInScope > 0)
        result["removedPercent"] =
            qRound(1000.0 * r.removedCount / r.totalNotesInScope) / 10.0;
    QJsonArray trackSummaries;
    for (const AutoFitTrackSummary &s : r.trackSummaries) {
        QJsonObject o;
        o["track"] = s.track;
        o["name"] = s.name;
        o["removed"] = s.removed;
        o["notes"] = s.notes;
        trackSummaries.append(o);
    }
    result["trackSummaries"] = trackSummaries;

    // Cap the per-note list so huge thinning runs don't blow up the reply.
    QJsonArray removedNotes;
    const int cap = 100;
    for (int i = 0; i < r.removed.size() && i < cap; ++i) {
        const AutoFitRemovedNote &n = r.removed.at(i);
        QJsonObject o;
        o["tick"] = n.tick;
        o["channel"] = n.channel;
        o["pitch"] = n.pitch;
        o["velocity"] = n.velocity;
        o["reason"] = n.reason;
        removedNotes.append(o);
    }
    result["removedNotes"] = removedNotes;
    if (r.removed.size() > cap)
        result["removedNotesTruncated"] = r.removed.size() - cap;

    QString summary;
    if (r.removedCount == 0) {
        summary = QStringLiteral("Nothing to fit: peak %1/%2 voices, no rate hotspots in range.")
                      .arg(r.peakBefore).arg(opts.targetCeiling);
    } else {
        summary = QStringLiteral("%1 %2 note(s) (%3 duplicates, %4 chord-limit, %5 voice-ceiling, "
                                 "%6 note-rate); peak %7 -> %8 voices.")
                      .arg(opts.dryRun ? QStringLiteral("Would remove") : QStringLiteral("Removed"))
                      .arg(r.removedCount)
                      .arg(r.duplicateRemoved)
                      .arg(r.chordRemoved)
                      .arg(r.ceilingRemoved)
                      .arg(r.rateRemoved)
                      .arg(r.peakBefore)
                      .arg(r.remainingPeak);
        if (opts.dryRun)
            summary += QStringLiteral(" Present this to the user and ask for confirmation "
                                      "before calling again with dryRun=false.");
        else
            summary += QStringLiteral(" One undo step restores everything.");
    }
    result["summary"] = summary;
    return result;
#endif // TOOLDEFINITIONS_TEST_STUB_FFXIV
}

QJsonObject ToolDefinitions::execConvertTempoPreserveDuration(const QJsonObject &args,
                                                              MidiFile *file,
                                                              const QString &source) {
#ifdef TOOLDEFINITIONS_TEST_STUB_FFXIV
    Q_UNUSED(args);
    Q_UNUSED(file);
    Q_UNUSED(source);
    QJsonObject result;
    result["success"] = false;
    result["error"] = QStringLiteral("Stub build: convert_tempo_preserve_duration is unavailable.");
    return result;
#else
    QJsonObject result;
    if (!file) {
        result["success"] = false;
        result["error"] = QStringLiteral("No MIDI file is open.");
        return result;
    }

    TempoConversionOptions opts;

    // Scope first - it decides the tempoMode default and which id lists matter.
    const QString scopeStr = args.value("scope").toString();
    if (scopeStr == QLatin1String("whole")) {
        opts.scope = TempoConversionScope::WholeProject;
    } else if (scopeStr == QLatin1String("selected_tracks")) {
        opts.scope = TempoConversionScope::SelectedTracks;
    } else if (scopeStr == QLatin1String("selected_channels")) {
        opts.scope = TempoConversionScope::SelectedChannels;
    } else if (scopeStr == QLatin1String("selected_events")) {
        opts.scope = TempoConversionScope::SelectedEvents;
    } else {
        result["success"] = false;
        result["error"] = QStringLiteral("Invalid scope '%1'. Use whole, selected_tracks, "
                                         "selected_channels or selected_events.").arg(scopeStr);
        return result;
    }
    const bool partialScope = opts.scope != TempoConversionScope::WholeProject;

    // Tempo-map mode. The tempo map is SHARED: replacing or rescaling it from
    // a partial scope would retime every event OUTSIDE the scope too, so
    // partial scopes only re-tick their own events (events_only).
    if (args.contains("tempoMode") && !args.value("tempoMode").isNull()) {
        const QString modeStr = args.value("tempoMode").toString();
        if (modeStr == QLatin1String("replace")) {
            opts.tempoMode = TempoConversionTempoMode::ReplaceFixed;
        } else if (modeStr == QLatin1String("scale_map")) {
            opts.tempoMode = TempoConversionTempoMode::ScaleTempoMap;
        } else if (modeStr == QLatin1String("events_only")) {
            opts.tempoMode = TempoConversionTempoMode::EventsOnly;
        } else {
            result["success"] = false;
            result["error"] = QStringLiteral("Invalid tempoMode '%1'. Use replace, scale_map "
                                             "or events_only.").arg(modeStr);
            return result;
        }
        if (partialScope && opts.tempoMode != TempoConversionTempoMode::EventsOnly) {
            result["success"] = false;
            result["error"] = QStringLiteral(
                "tempoMode '%1' is only valid with scope=whole: the tempo map is shared, so "
                "changing it from a partial scope would retime everything outside the scope. "
                "Use tempoMode=events_only (or omit it) for partial scopes.").arg(modeStr);
            return result;
        }
    } else {
        opts.tempoMode = partialScope ? TempoConversionTempoMode::EventsOnly
                                      : TempoConversionTempoMode::ReplaceFixed;
    }

    // BPM pair. sourceBpm auto-detects from the file's first tempo event so
    // the agent can say "fit this into the project tempo" without reading the
    // map first (same detection the Convert Tempo dialog uses).
    opts.targetBpm = args.value("targetBpm").toDouble();
    if (args.contains("sourceBpm") && !args.value("sourceBpm").isNull()) {
        opts.sourceBpm = args.value("sourceBpm").toDouble();
    } else {
        QMultiMap<int, MidiEvent *> *tempoMap = file->tempoEvents();
        int bestTick = -1;
        int bpm = 0;
        if (tempoMap) {
            for (auto it = tempoMap->begin(); it != tempoMap->end(); ++it) {
                if (it.key() < 0) continue;
                if (bestTick < 0 || it.key() < bestTick) {
                    if (auto *tc = dynamic_cast<TempoChangeEvent *>(it.value())) {
                        bestTick = it.key();
                        bpm = tc->beatsPerQuarter();
                    }
                }
            }
        }
        if (bpm <= 0) {
            result["success"] = false;
            result["error"] = QStringLiteral("Could not auto-detect the source tempo (no tempo "
                                             "event found) - pass sourceBpm explicitly.");
            return result;
        }
        opts.sourceBpm = static_cast<double>(bpm);
        result["detectedSourceBpm"] = opts.sourceBpm;
    }

    // Scope id lists.
    if (opts.scope == TempoConversionScope::SelectedTracks) {
        const QJsonArray ids = args.value("trackIds").toArray();
        for (const QJsonValue &v : ids) opts.trackIds.insert(v.toInt());
        if (opts.trackIds.isEmpty()) {
            result["success"] = false;
            result["error"] = QStringLiteral("scope=selected_tracks needs a non-empty trackIds "
                                             "array of track numbers.");
            return result;
        }
    } else if (opts.scope == TempoConversionScope::SelectedChannels) {
        const QJsonArray ids = args.value("channelIds").toArray();
        for (const QJsonValue &v : ids) {
            const int ch = v.toInt(-1);
            if (ch < 0 || ch > 15) {
                result["success"] = false;
                result["error"] = QStringLiteral("channelIds must contain MIDI channels 0-15 "
                                                 "(got %1).").arg(ch);
                return result;
            }
            opts.channelIds.insert(ch);
        }
        if (opts.channelIds.isEmpty()) {
            result["success"] = false;
            result["error"] = QStringLiteral("scope=selected_channels needs a non-empty "
                                             "channelIds array (0-15).");
            return result;
        }
    } else if (opts.scope == TempoConversionScope::SelectedEvents) {
        // forFile, not instance(): during an agent/MCP run the user may have
        // switched tabs, and instance() would read the wrong document.
        Selection *sel = Selection::forFile(file);
        const QList<MidiEvent *> events = sel ? sel->selectedEvents() : QList<MidiEvent *>();
        for (MidiEvent *ev : events) {
            opts.selectedEventPtrs.insert(reinterpret_cast<quintptr>(ev));
            // TEMPO-OFFEVENT-001: selections never contain OffEvents (the
            // editor refuses to select them) and the service matches exact
            // pointers - without the linked note-off every rescaled note
            // KEEPS its old end tick and can end before it starts. Always
            // carry the off event along.
            if (auto *on = dynamic_cast<OnEvent *>(ev)) {
                if (on->offEvent()) {
                    opts.selectedEventPtrs.insert(
                        reinterpret_cast<quintptr>(on->offEvent()));
                }
            }
        }
        if (opts.selectedEventPtrs.isEmpty()) {
            result["success"] = false;
            result["error"] = QStringLiteral("scope=selected_events, but nothing is selected in "
                                             "this document.");
            return result;
        }
    }

    // Protocol-panel attribution. The label is built HERE (not in the service)
    // so it uses the same "<actor>: Agent <verb> - <detail>" format as every
    // other agent/MCP action; the service keeps its own label for the dialog.
    // Prefix concatenated, not .arg()-substituted - see execTransposeEvents.
    opts.actionLabel = protocolActorPrefix(source)
                       + QStringLiteral(": Agent convert tempo - %1 to %2 BPM "
                                        "(duration preserved)")
                             .arg(opts.sourceBpm, 0, 'f', 2)
                             .arg(opts.targetBpm, 0, 'f', 2);

    const bool dryRun = args.value("dryRun").toBool(true); // safe default
    const TempoConversionResult r = dryRun ? TempoConversionService::preview(file, opts)
                                           : TempoConversionService::convert(file, opts);
    if (!r.ok) {
        result["success"] = false;
        result["error"] = r.error;
        return result;
    }
    if (!dryRun && (r.affectedEvents > 0 || r.tempoEventsRemoved > 0)) {
        // ReplaceFixed DELETES tempo events; if any were selected, the
        // selection now holds stale pointers - and re-ticked events break the
        // tick-order assumptions of a retained selection anyway. Clear THIS
        // file's selection (forFile, same rule as auto_fit_voice_load).
        Selection *sel = Selection::forFile(file);
        if (sel) {
            sel->clearSelection();
        }
    }

    result["success"] = true;
    result["dryRun"] = dryRun;
    result["scope"] = scopeStr;
    result["sourceBpm"] = opts.sourceBpm;
    result["targetBpm"] = opts.targetBpm;
    result["scaleFactor"] = r.scaleFactor;
    result["affectedEvents"] = r.affectedEvents;
    result["tempoEventsRemoved"] = r.tempoEventsRemoved;
    result["tempoEventsInserted"] = r.tempoEventsInserted;
    result["oldDurationMs"] = static_cast<double>(r.oldDurationMs);
    result["newDurationMs"] = static_cast<double>(r.newDurationMs);
    if (!r.warning.isEmpty())
        result["warning"] = r.warning;

    QString summary;
    if (r.affectedEvents == 0 && r.tempoEventsRemoved == 0) {
        summary = !r.warning.isEmpty()
                      ? r.warning
                      : QStringLiteral("Nothing in scope - no events to convert.");
    } else {
        summary = QStringLiteral("%1 %2 event(s) from %3 to %4 BPM (ticks x%5); tempo events: "
                                 "%6 removed, %7 inserted; duration %8s -> %9s.")
                      .arg(dryRun ? QStringLiteral("Would rescale") : QStringLiteral("Rescaled"))
                      .arg(r.affectedEvents)
                      .arg(opts.sourceBpm)
                      .arg(opts.targetBpm)
                      .arg(r.scaleFactor, 0, 'g', 6)
                      .arg(r.tempoEventsRemoved)
                      .arg(r.tempoEventsInserted)
                      .arg(r.oldDurationMs / 1000.0, 0, 'f', 1)
                      .arg(r.newDurationMs / 1000.0, 0, 'f', 1);
        if (dryRun)
            summary += QStringLiteral(" Present this to the user and ask for confirmation "
                                      "before calling again with dryRun=false.");
        else
            summary += QStringLiteral(" One undo step restores everything.");
    }
    result["summary"] = summary;
    return result;
#endif // TOOLDEFINITIONS_TEST_STUB_FFXIV
}

QJsonObject ToolDefinitions::execThinTempoMap(const QJsonObject &args,
                                              MidiFile *file,
                                              const QString &source) {
#ifdef TOOLDEFINITIONS_TEST_STUB_FFXIV
    Q_UNUSED(args);
    Q_UNUSED(file);
    Q_UNUSED(source);
    QJsonObject result;
    result["success"] = false;
    result["error"] = QStringLiteral("Stub build: thin_tempo_map is unavailable.");
    return result;
#else
    QJsonObject result;
    if (!file) {
        result["success"] = false;
        result["error"] = QStringLiteral("No MIDI file is open.");
        return result;
    }

    // Strict schemas send every optional as an explicit null - .isNull() is
    // the check, never .contains().
    double toleranceMs = TempoMapThinner::kDefaultToleranceMs;
    if (args.contains("toleranceMs") && !args.value("toleranceMs").isNull()) {
        toleranceMs = args.value("toleranceMs").toDouble(toleranceMs);
    }
    if (toleranceMs < 0.0) {
        result["success"] = false;
        result["error"] = QStringLiteral("toleranceMs must not be negative.");
        return result;
    }
    bool dryRun = true; // safe default: dry run
    if (args.contains("dryRun") && !args.value("dryRun").isNull()) {
        dryRun = args.value("dryRun").toBool(true);
    }

    // Protocol-panel attribution, built here (not in the service) so the
    // action reads like every other agent/MCP action. Prefix concatenated,
    // not .arg()-substituted - see execTransposeEvents.
    const QString label =
        dryRun ? QString()
               : protocolActorPrefix(source)
                     + QStringLiteral(": Agent thin tempo map");

    const TempoMapThinner::Result r =
        TempoMapThinner::thin(file, toleranceMs, dryRun, label);
    if (!r.ok) {
        result["success"] = false;
        result["error"] = r.error;
        return result;
    }

    result["success"] = true;
    result["dryRun"] = r.dryRun;
    result["toleranceMs"] = toleranceMs;
    result["tempoEventsBefore"] = r.before;
    result["tempoEventsAfter"] = r.kept;
    result["removed"] = r.removed;
    result["maxDriftMs"] = qRound(r.maxDriftMs * 1000.0) / 1000.0;
    result["endDriftMs"] = qRound(r.endDriftMs * 1000.0) / 1000.0;
    // The tolerance is a per-document budget measured against the map as it
    // was loaded, so a re-run reports a CUMULATIVE figure. Without this the
    // model would read "largest shift 2.00 ms" and think it still had 2 ms of
    // room after an earlier pass already spent it.
    result["alreadyDriftedMs"] = qRound(r.alreadyDriftedMs * 1000.0) / 1000.0;

    QString summary;
    // Did the branch below print drift FIGURES? The "already drifted" sentence
    // may only claim to be their baseline when there are figures to relate to.
    bool printedDriftFigures = false;
    if (r.removed == 0) {
        if (r.alreadyDriftedMs > 0.005) {
            // A re-run against a corridor an earlier pass already spent part
            // of. The events that are left are not "all carrying part of the
            // timing" - the budget is measured from the file as opened, and
            // what is already spent is what stops the next event from going.
            summary = QStringLiteral("Nothing to thin: this map was already "
                                     "thinned in this session and sits %1 ms "
                                     "from the file as opened. The %2 ms "
                                     "tolerance is measured against that same "
                                     "starting point, so all %3 remaining "
                                     "tempo event(s) stay.")
                          .arg(r.alreadyDriftedMs, 0, 'f', 2)
                          .arg(toleranceMs, 0, 'f', 2)
                          .arg(r.before);
        } else {
            summary = QStringLiteral("Nothing to thin: all %1 tempo event(s) "
                                     "carry part of the timing at a %2 ms "
                                     "tolerance.")
                          .arg(r.before)
                          .arg(toleranceMs, 0, 'f', 2);
        }
    } else {
        summary = QStringLiteral("%1 %2 of %3 tempo events (%4 left). Largest "
                                 "timing shift %5 ms, end of file %6 ms.")
                      .arg(r.dryRun ? QStringLiteral("Would remove")
                                    : QStringLiteral("Removed"))
                      .arg(r.removed)
                      .arg(r.before)
                      .arg(r.kept)
                      .arg(r.maxDriftMs, 0, 'f', 2)
                      .arg(r.endDriftMs, 0, 'f', 2);
        printedDriftFigures = true;
        if (r.dryRun)
            summary += QStringLiteral(" Present this to the user and ask for "
                                      "confirmation before calling again with "
                                      "dryRun=false.");
        else
            summary += QStringLiteral(" One undo step restores everything.");
    }
    // Only where drift figures were actually printed can they be called a total
    // from the earlier pass - the removed==0 branch above states the spent
    // corridor itself and has no figures to relate to.
    if (r.alreadyDriftedMs > 0.005 && printedDriftFigures) {
        summary += QStringLiteral(" This map was already thinned in this session "
                                  "and sits %1 ms from the file as opened; the "
                                  "figures above are the total from there.")
                       .arg(r.alreadyDriftedMs, 0, 'f', 2);
    }
    if (r.maxDriftMs > toleranceMs + 0.005) {
        summary += QStringLiteral(" The requested tolerance can no longer be met - "
                                  "the earlier thinning would have to be undone first.");
    }
    result["summary"] = summary;
    return result;
#endif // TOOLDEFINITIONS_TEST_STUB_FFXIV
}

QJsonObject ToolDefinitions::execTransposeEvents(const QJsonObject &args, MidiFile *file,
                                                 const QString &source) {
#ifdef TOOLDEFINITIONS_TEST_STUB_FFXIV
    Q_UNUSED(args);
    Q_UNUSED(file);
    Q_UNUSED(source);
    QJsonObject result;
    result["success"] = false;
    result["error"] = QStringLiteral("Stub build: transpose_events is unavailable.");
    return result;
#else
    QJsonObject result;
    if (!file) {
        result["success"] = false;
        result["error"] = QStringLiteral("No MIDI file is open.");
        return result;
    }

    const int semitones = args.value("semitones").toInt();
    const bool fold = args.value("foldToRange").toBool(false);
    if (semitones == 0 && !fold) {
        result["success"] = false;
        result["error"] = QStringLiteral("semitones is 0 and foldToRange is false - nothing to do.");
        return result;
    }

    int trackIndex = -1;
    if (args.contains("trackIndex") && !args.value("trackIndex").isNull()) {
        trackIndex = args.value("trackIndex").toInt();
        if (trackIndex < 0 || trackIndex >= file->numTracks()) {
            result["success"] = false;
            result["error"] = QStringLiteral("Invalid trackIndex %1 (file has %2 tracks).")
                                  .arg(trackIndex).arg(file->numTracks());
            return result;
        }
    }
    const bool hasStart = args.contains("startTick") && !args.value("startTick").isNull();
    const bool hasEnd = args.contains("endTick") && !args.value("endTick").isNull();
    const int startTick = hasStart ? args.value("startTick").toInt() : INT_MIN;
    const int endTick = hasEnd ? args.value("endTick").toInt() : INT_MAX;
    MidiTrack *track = trackIndex >= 0 ? file->track(trackIndex) : nullptr;

    // Plan first, mutate second: an empty scope must not leave an empty
    // step in the undo history.
    struct Target {
        NoteOnEvent *on;
        int newNote;
    };
    QList<Target> targets;
    int skipped = 0;
    for (int ch = 0; ch < 16; ++ch) {
        MidiChannel *channel = file->channel(ch);
        if (!channel) continue;
        QMultiMap<int, MidiEvent *> *map = channel->eventMap();
        for (auto it = map->begin(); it != map->end(); ++it) {
            auto *on = dynamic_cast<NoteOnEvent *>(it.value());
            if (!on) continue;
            if (track && on->track() != track) continue;
            const int tick = on->midiTime();
            if (tick < startTick || tick > endTick) continue;
            int n = on->note() + semitones;
            if (fold) {
                while (n < 48) n += 12;
                while (n > 84) n -= 12;
            }
            if (n < 0 || n > 127) {
                ++skipped; // would leave the MIDI range - leave it alone
                continue;
            }
            if (n != on->note()) targets.append({on, n});
        }
    }

    if (targets.isEmpty()) {
        result["success"] = true;
        result["transposed"] = 0;
        result["skipped"] = skipped;
        result["summary"] = QStringLiteral("Nothing to transpose in scope.");
        return result;
    }

    // Actor prefix CONCATENATED, never .arg()-substituted: an MCP client name
    // is remote input and a "%2" inside it would eat the next argument.
    file->protocol()->startNewAction(
        protocolActorPrefix(source)
        + QStringLiteral(": Agent transpose notes - %1 semitones").arg(semitones));
    for (const Target &t : targets) {
        t.on->setNote(t.newNote);
    }
    file->protocol()->endAction();

    result["success"] = true;
    result["transposed"] = static_cast<int>(targets.size());
    result["skipped"] = skipped;
    result["summary"] = QStringLiteral(
        "Transposed %1 note(s) by %2 semitone(s)%3%4. One undo step.")
            .arg(targets.size())
            .arg(semitones)
            .arg(fold ? QStringLiteral(", folded into C3-C6") : QString())
            .arg(skipped > 0 ? QStringLiteral(" (%1 skipped at the MIDI range edge)")
                                   .arg(skipped)
                             : QString());
    return result;
#endif // TOOLDEFINITIONS_TEST_STUB_FFXIV
}

QJsonObject ToolDefinitions::execSplitChordsToTracks(const QJsonObject &args, MidiFile *file,
                                                     const QString &source) {
#ifdef TOOLDEFINITIONS_TEST_STUB_FFXIV
    Q_UNUSED(args);
    Q_UNUSED(file);
    Q_UNUSED(source);
    QJsonObject result;
    result["success"] = false;
    result["error"] = QStringLiteral("Stub build: split_chords_to_tracks is unavailable.");
    return result;
#else
    QJsonObject result;
    if (!file) {
        result["success"] = false;
        result["error"] = QStringLiteral("No MIDI file is open.");
        return result;
    }
    const int trackIndex = args.value("trackIndex").toInt(-1);
    if (trackIndex < 0 || trackIndex >= file->numTracks()) {
        result["success"] = false;
        result["error"] = QStringLiteral("Invalid trackIndex %1 (file has %2 tracks).")
                              .arg(trackIndex).arg(file->numTracks());
        return result;
    }
    MidiTrack *src = file->track(trackIndex);
    int minNotes = 2;
    if (args.contains("minNotes") && !args.value("minNotes").isNull())
        minNotes = qMax(2, args.value("minNotes").toInt());
    const bool keepOriginal = args.value("keepOriginal").toBool(false);

    // Group note starts by tick - same-start grouping, like the GUI's
    // Explode Chords with the "voices across chords" strategy.
    QMap<int, QList<NoteOnEvent *>> groups;
    for (int ch = 0; ch < 16; ++ch) {
        MidiChannel *channel = file->channel(ch);
        if (!channel) continue;
        QMultiMap<int, MidiEvent *> *map = channel->eventMap();
        for (auto it = map->begin(); it != map->end(); ++it) {
            auto *on = dynamic_cast<NoteOnEvent *>(it.value());
            if (on && on->offEvent() && on->track() == src)
                groups[on->midiTime()].append(on);
        }
    }
    QList<QList<NoteOnEvent *>> chords;
    for (auto it = groups.begin(); it != groups.end(); ++it) {
        if (it.value().size() >= minNotes) chords.append(it.value());
    }
    if (chords.isEmpty()) {
        result["success"] = true;
        result["chordCount"] = 0;
        result["summary"] = QStringLiteral(
            "No chords (>= %1 simultaneous notes) on track %2 - nothing to split.")
                .arg(minNotes).arg(trackIndex);
        return result;
    }

    int maxVoices = 0;
    for (const QList<NoteOnEvent *> &chord : chords)
        maxVoices = qMax(maxVoices, static_cast<int>(chord.size()));

    file->protocol()->startNewAction(
        protocolActorPrefix(source)
        + QStringLiteral(": Agent split chords to tracks - Track %1").arg(trackIndex));

    const int firstNewIndex = file->numTracks();
    QVector<MidiTrack *> voiceTracks;
    voiceTracks.reserve(maxVoices);
    for (int v = 0; v < maxVoices; ++v) {
        file->addTrack();
        MidiTrack *dst = file->tracks()->last();
        dst->setName(src->name() + QStringLiteral(" - Voice %1").arg(v + 1));
        voiceTracks.append(dst);
    }

    // keepOriginal copies notes; do it with ONE channel snapshot per touched
    // channel (the documented bulk pattern) - per-note insertNote snapshots
    // cost ~1 MB each on dense channels.
    int processed = 0;
    if (keepOriginal) {
        QMap<int, QList<QPair<NoteOnEvent *, MidiTrack *>>> byChannel;
        for (QList<NoteOnEvent *> chord : chords) {
            std::sort(chord.begin(), chord.end(),
                      [](NoteOnEvent *a, NoteOnEvent *b) { return a->note() > b->note(); });
            for (int v = 0; v < chord.size(); ++v)
                byChannel[chord[v]->channel()].append(qMakePair(chord[v], voiceTracks[v]));
        }
        for (auto it = byChannel.begin(); it != byChannel.end(); ++it) {
            MidiChannel *channel = file->channel(it.key());
            ProtocolEntry *toCopy = channel->copy();
            for (const auto &pair : it.value()) {
                NoteOnEvent *on = pair.first;
                channel->insertNote(on->note(), on->midiTime(),
                                    on->offEvent()->midiTime(), on->velocity(),
                                    pair.second, /*toProtocol=*/false);
                ++processed;
            }
            channel->protocol(toCopy, channel);
        }
    } else {
        for (QList<NoteOnEvent *> chord : chords) {
            std::sort(chord.begin(), chord.end(),
                      [](NoteOnEvent *a, NoteOnEvent *b) { return a->note() > b->note(); });
            for (int v = 0; v < chord.size(); ++v) {
                chord[v]->setTrack(voiceTracks[v]);
                chord[v]->offEvent()->setTrack(voiceTracks[v]);
                ++processed;
            }
        }
    }
    file->protocol()->endAction();

    QJsonArray newTrackIndexes;
    for (int v = 0; v < maxVoices; ++v) newTrackIndexes.append(firstNewIndex + v);
    result["success"] = true;
    result["chordCount"] = static_cast<int>(chords.size());
    result["voiceTrackCount"] = maxVoices;
    result["newTrackIndexes"] = newTrackIndexes;
    result["notesProcessed"] = processed;
    result["summary"] = QStringLiteral(
        "%1 %2 chord note(s) from %3 chord(s) onto %4 new track(s) (Voice 1 = highest; "
        "indexes %5-%6). Rename the new tracks to FFXIV instruments and run "
        "setup_channel_pattern LAST. One undo step.")
            .arg(keepOriginal ? QStringLiteral("Copied") : QStringLiteral("Moved"))
            .arg(processed)
            .arg(chords.size())
            .arg(maxVoices)
            .arg(firstNewIndex)
            .arg(firstNewIndex + maxVoices - 1);
    return result;
#endif // TOOLDEFINITIONS_TEST_STUB_FFXIV
}

QJsonObject ToolDefinitions::execCopyEventsToTrack(const QJsonObject &args, MidiFile *file,
                                                   const QString &source) {
#ifdef TOOLDEFINITIONS_TEST_STUB_FFXIV
    Q_UNUSED(args);
    Q_UNUSED(file);
    Q_UNUSED(source);
    QJsonObject result;
    result["success"] = false;
    result["error"] = QStringLiteral("Stub build: copy_events_to_track is unavailable.");
    return result;
#else
    QJsonObject result;
    if (!file) {
        result["success"] = false;
        result["error"] = QStringLiteral("No MIDI file is open.");
        return result;
    }
    const int sourceIndex = args.value("sourceTrackIndex").toInt(-1);
    const int targetIndex = args.value("targetTrackIndex").toInt(-1);
    if (sourceIndex < 0 || sourceIndex >= file->numTracks()
        || targetIndex < 0 || targetIndex >= file->numTracks()) {
        result["success"] = false;
        result["error"] = QStringLiteral("Invalid track index (file has %1 tracks).")
                              .arg(file->numTracks());
        return result;
    }
    if (sourceIndex == targetIndex) {
        result["success"] = false;
        result["error"] = QStringLiteral("Source and target track are the same.");
        return result;
    }
    const bool hasStart = args.contains("startTick") && !args.value("startTick").isNull();
    const bool hasEnd = args.contains("endTick") && !args.value("endTick").isNull();
    const int startTick = hasStart ? args.value("startTick").toInt() : INT_MIN;
    const int endTick = hasEnd ? args.value("endTick").toInt() : INT_MAX;
    MidiTrack *src = file->track(sourceIndex);
    MidiTrack *dst = file->track(targetIndex);

    QMap<int, QList<NoteOnEvent *>> byChannel;
    int found = 0;
    for (int ch = 0; ch < 16; ++ch) {
        MidiChannel *channel = file->channel(ch);
        if (!channel) continue;
        QMultiMap<int, MidiEvent *> *map = channel->eventMap();
        for (auto it = map->begin(); it != map->end(); ++it) {
            auto *on = dynamic_cast<NoteOnEvent *>(it.value());
            if (!on || !on->offEvent() || on->track() != src) continue;
            const int tick = on->midiTime();
            if (tick < startTick || tick > endTick) continue;
            byChannel[ch].append(on);
            ++found;
        }
    }
    if (found == 0) {
        result["success"] = true;
        result["copied"] = 0;
        result["summary"] = QStringLiteral("No notes in scope on track %1.").arg(sourceIndex);
        return result;
    }

    file->protocol()->startNewAction(
        protocolActorPrefix(source)
        + QStringLiteral(": Agent copy notes to track - Track %1 to Track %2")
              .arg(sourceIndex)
              .arg(targetIndex));
    // ONE channel snapshot per touched channel (documented bulk pattern) -
    // per-note insertNote snapshots cost ~1 MB each on dense channels.
    for (auto it = byChannel.begin(); it != byChannel.end(); ++it) {
        MidiChannel *channel = file->channel(it.key());
        ProtocolEntry *toCopy = channel->copy();
        for (NoteOnEvent *on : it.value()) {
            channel->insertNote(on->note(), on->midiTime(),
                                on->offEvent()->midiTime(), on->velocity(),
                                dst, /*toProtocol=*/false);
        }
        channel->protocol(toCopy, channel);
    }
    file->protocol()->endAction();

    result["success"] = true;
    result["copied"] = found;
    result["summary"] = QStringLiteral(
        "Copied %1 note(s) from track %2 to track %3 (channels kept). One undo step.")
            .arg(found).arg(sourceIndex).arg(targetIndex);
    return result;
#endif // TOOLDEFINITIONS_TEST_STUB_FFXIV
}

QJsonObject ToolDefinitions::execSearchHelp(const QJsonObject &args) {
    QJsonObject result;
    const QString query = args.value("query").toString().trimmed();
    if (query.isEmpty()) {
        result["success"] = false;
        result["error"] = QStringLiteral("Empty query.");
        return result;
    }
    HelpDatabase &db = HelpDatabase::instance();
    if (!db.isLoaded()) {
        result["success"] = false;
        result["error"] = QStringLiteral("Help database not available in this build.");
        return result;
    }
    const QList<HelpDatabase::Hit> hits = db.search(query, 5);
    QJsonArray results;
    for (const HelpDatabase::Hit &hit : hits) {
        const HelpDatabase::Section &s = *hit.section;
        QJsonObject r;
        r["page"] = s.page;
        r["anchor"] = s.anchor;
        r["title"] = s.title;
        r["pageTitle"] = s.pageTitle;
        r["link"] = s.anchor.isEmpty() ? s.page
                                       : QStringLiteral("%1#%2").arg(s.page, s.anchor);
        QString snippet = s.text.left(220);
        if (s.text.size() > 220) snippet += QStringLiteral("...");
        r["snippet"] = snippet;
        results.append(r);
    }
    result["success"] = true;
    result["results"] = results;
    result["summary"] = results.isEmpty()
        ? QStringLiteral("No manual section matches '%1'.").arg(query)
        : QStringLiteral("%1 manual section(s) match; read the best one with "
                         "get_help_section and cite it.").arg(results.size());
    return result;
}

QJsonObject ToolDefinitions::execGetHelpSection(const QJsonObject &args) {
    QJsonObject result;
    const QString page = args.value("page").toString();
    const QString anchor = args.value("anchor").toString();
    HelpDatabase &db = HelpDatabase::instance();
    if (!db.isLoaded()) {
        result["success"] = false;
        result["error"] = QStringLiteral("Help database not available in this build.");
        return result;
    }
    const HelpDatabase::Section *s = db.section(page, anchor);
    if (!s) {
        result["success"] = false;
        result["error"] = QStringLiteral(
            "No section '%1#%2' - use the page/anchor values a search_help "
            "result returned.").arg(page, anchor);
        return result;
    }
    result["success"] = true;
    result["page"] = s->page;
    result["anchor"] = s->anchor;
    result["title"] = s->title;
    result["pageTitle"] = s->pageTitle;
    result["link"] = s->anchor.isEmpty() ? s->page
                                         : QStringLiteral("%1#%2").arg(s->page, s->anchor);
    // Cap: one manual section can reach ~18k chars; keep the reply sane and
    // say so instead of silently cutting.
    const int cap = 8000;
    if (s->text.size() > cap) {
        result["text"] = s->text.left(cap);
        result["truncated"] = true;
        result["totalChars"] = static_cast<int>(s->text.size());
    } else {
        result["text"] = s->text;
    }
    return result;
}
