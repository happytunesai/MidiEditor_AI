#include "ToolDefinitions.h"
#include "FFXIVChannelFixer.h"

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

#include <QJsonDocument>
#include <QSettings>
#include <QSet>
#include <QRegularExpression>
#include <algorithm>

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

    // --- FFXIV tools (only when FFXIV mode is active) ---
    if (QSettings("MidiEditor", "NONE").value("AI/ffxiv_mode", false).toBool()) {

        // validate_ffxiv (no parameters)
        {
            tools.append(makeTool(
                "validate_ffxiv",
                "Check if the current MIDI file meets FFXIV Bard Performance constraints. "
                "Reports issues: polyphony, out-of-range notes, invalid track names, too many tracks.",
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
    }

    return tools;
}

// ---------------------------------------------------------------------------
// executeTool — dispatches tool calls to read-only or write handlers
// ---------------------------------------------------------------------------
QJsonObject ToolDefinitions::executeTool(const QString &toolName,
                                         const QJsonObject &args,
                                         MidiFile *file,
                                         MidiPilotWidget *widget,
                                         const QString &source) {
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

    // Write tools — delegate to widget handlers via executeAction
    if (toolName == "create_track" || toolName == "rename_track" || toolName == "set_channel") {
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

    // FFXIV tools
    if (toolName == "validate_ffxiv") {
        return execValidateFFXIV(file);
    }
    if (toolName == "convert_drums_ffxiv") {
        return execConvertDrumsFFXIV(args, file, widget);
    }
    if (toolName == "setup_channel_pattern") {
        return execSetupChannelPattern(file, widget);
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

static const QSet<QString> FFXIV_INSTRUMENT_NAMES = {
    "Piano", "Harp", "Fiddle", "Lute", "Fife", "Flute", "Oboe", "Panpipes",
    "Clarinet", "Trumpet", "Saxophone", "Trombone", "Horn", "Tuba",
    "Violin", "Viola", "Cello", "Double Bass",
    "Timpani", "Bongo", "Bass Drum", "Snare Drum", "Cymbal",
    "ElectricGuitarClean", "ElectricGuitarMuted", "ElectricGuitarOverdriven",
    "ElectricGuitarPowerChords", "ElectricGuitarSpecial"
};

static bool isGuitarInstrument(const QString &name) {
    QString base = name;
    QRegularExpression suffixRe("[+-]\\d+$");
    base.remove(suffixRe);
    return base.startsWith("ElectricGuitar");
}

QJsonObject ToolDefinitions::execValidateFFXIV(MidiFile *file) {
    QJsonObject result;
    if (!file) {
        result["success"] = false;
        result["error"] = QString("No file loaded.");
        return result;
    }

    QJsonArray issues;
    int trackCount = file->numTracks();

    // Note: track count is informational — more than 8 tracks is fine
    // when using guitar switches or additional instrument channels.

    for (int t = 0; t < trackCount; t++) {
        MidiTrack *track = file->track(t);
        QString name = track->name();

        // Check track name — strip +N/-N suffix for validation
        QString baseName = name;
        QRegularExpression suffixRe("[+-]\\d+$");
        baseName.remove(suffixRe);
        if (!baseName.isEmpty() && !FFXIV_INSTRUMENT_NAMES.contains(baseName)) {
            QJsonObject issue;
            issue["track"] = t;
            issue["issue"] = QString("track_name");
            issue["details"] = QString("Track name '%1' doesn't match any FFXIV instrument").arg(name);
            issues.append(issue);
        }

        // Collect NoteOn events on this track to check range and polyphony
        struct NoteInfo { int tick; int note; int endTick; int channel; };
        QList<NoteInfo> notes;
        bool trackIsGuitar = isGuitarInstrument(name);

        for (int ch = 0; ch < 16; ch++) {
            MidiChannel *channel = file->channel(ch);
            if (!channel) continue;
            QMultiMap<int, MidiEvent *> *map = channel->eventMap();
            for (auto it = map->begin(); it != map->end(); ++it) {
                MidiEvent *ev = it.value();
                if (ev->track() != track) continue;
                auto *noteOn = dynamic_cast<NoteOnEvent *>(ev);
                if (!noteOn) continue;

                int note = noteOn->note();
                int tick = noteOn->midiTime();

                // Check range (C3-C6 = MIDI 48-84)
                if (note < 48 || note > 84) {
                    static const char *noteNames[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
                    QString noteName = QString("%1%2").arg(noteNames[note % 12]).arg(note / 12 - 1);
                    QJsonObject issue;
                    issue["track"] = t;
                    issue["issue"] = QString("out_of_range");
                    issue["details"] = QString("Note %1 (%2) outside C3-C6 (MIDI 48-84) at tick %3")
                                           .arg(noteName).arg(note).arg(tick);
                    issues.append(issue);
                }

                // Find end tick for polyphony check
                int endTick = tick;
                MidiEvent *offEv = noteOn->offEvent();
                if (offEv) endTick = offEv->midiTime();
                notes.append({tick, note, endTick, ch});
            }
        }

        // Check polyphony (overlapping notes)
        // Sort by tick so the first overlap found is chronologically first (AI-006)
        std::sort(notes.begin(), notes.end(),
                  [](const NoteInfo &a, const NoteInfo &b) { return a.tick < b.tick; });
        // For guitar tracks: only flag overlaps on the SAME channel (different
        // channels are intentional guitar switches)
        for (int i = 0; i < notes.size(); i++) {
            for (int j = i + 1; j < notes.size(); j++) {
                // Notes are sorted by tick — if next note starts after this one ends, skip rest
                if (notes[j].tick >= notes[i].endTick)
                    break;
                if (trackIsGuitar && notes[i].channel != notes[j].channel)
                    continue; // different guitar switch channels — OK
                if (notes[i].endTick > notes[j].tick && notes[j].endTick > notes[i].tick) {
                    QJsonObject issue;
                    issue["track"] = t;
                    issue["issue"] = QString("polyphonic");
                    issue["details"] = QString("Overlapping notes at tick %1 and %2")
                                           .arg(notes[i].tick).arg(notes[j].tick);
                    issues.append(issue);
                    // Only report first overlap per track to avoid flooding
                    goto nextTrack;
                }
            }
        }
        nextTrack:;
    }

    result["success"] = true;
    result["valid"] = issues.isEmpty();
    result["issues"] = issues;
    result["summary"] = issues.isEmpty()
        ? QString("File is FFXIV-compliant")
        : QString("%1 issue(s) found").arg(issues.size());
    return result;
}

QJsonObject ToolDefinitions::execConvertDrumsFFXIV(const QJsonObject &args,
                                                    MidiFile *file,
                                                    MidiPilotWidget *widget) {
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

    static const QHash<QString, int> map = {
        {"Piano", 0},       {"Harp", 46},       {"Fiddle", 45},
        {"Lute", 24},       {"Fife", 72},        {"Flute", 73},
        {"Oboe", 68},       {"Panpipes", 75},    {"Clarinet", 71},
        {"Trumpet", 56},    {"Saxophone", 65},   {"Trombone", 57},
        {"Horn", 60},       {"Tuba", 58},
        {"Violin", 40},     {"Viola", 41},       {"Cello", 42},
        {"Double Bass", 43},
        {"Timpani", 47},    {"Bongo", 116},      {"Bass Drum", 117},
        {"Snare Drum", 115}, {"Cymbal", 127},
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
                                                      MidiPilotWidget * /*widget*/) {
    // V131-P2-04: FFXIVChannelFixer::fixChannels() calls
    // MidiChannel::removeEvent(..., true) in its CLEAN phase, which routes
    // through Protocol::enterUndoStep(). If no action is open, every undo
    // item is silently `delete`d and the event leaks (no one still owns it).
    // The interactive MainWindow path wraps in startNewAction/endAction, but
    // this AI-tool entry point previously did not. Wrap here so the fix runs
    // under a Protocol action regardless of caller.
    if (file && file->protocol())
        file->protocol()->startNewAction(QStringLiteral("Agent: setup channel pattern"));
    QJsonObject result = FFXIVChannelFixer::fixChannels(file);
    if (file && file->protocol())
        file->protocol()->endAction();
    return result;
}
