#include "ToolDefinitions.h"

#include "../gui/MidiPilotWidget.h"
#include "EditorContext.h"
#include "MidiEventSerializer.h"
#include "../midi/MidiFile.h"
#include "../midi/MidiTrack.h"
#include "../midi/MidiChannel.h"
#include "../MidiEvent/MidiEvent.h"
#include "../MidiEvent/NoteOnEvent.h"
#include "../MidiEvent/OffEvent.h"

#include <QJsonDocument>

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

// Helper: build the MIDI event schema as a discriminated union (anyOf)
static QJsonObject makeEventSchema() {
    // note (compact format with duration)
    QJsonObject noteProps;
    noteProps["type"] = QJsonObject{{"type", "string"}, {"enum", QJsonArray{"note"}}};
    noteProps["tick"] = QJsonObject{{"type", "integer"}, {"description", "Tick position."}};
    noteProps["note"] = QJsonObject{{"type", "integer"}, {"description", "MIDI note number (0-127). 60 = Middle C."}};
    noteProps["velocity"] = QJsonObject{{"type", "integer"}, {"description", "Note velocity (1-127)."}};
    noteProps["duration"] = QJsonObject{{"type", "integer"}, {"description", "Duration in ticks."}};
    QJsonObject noteSchema;
    noteSchema["type"] = QString("object");
    noteSchema["properties"] = noteProps;
    noteSchema["required"] = QJsonArray{"type", "tick", "note", "velocity", "duration"};
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
    pbProps["value"] = QJsonObject{{"type", "integer"}, {"description", "Pitch bend value (0-16383, center=8192)."}};
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
    eventSchema["anyOf"] = QJsonArray{noteSchema, ccSchema, pbSchema, pcSchema};
    return eventSchema;
}

// ---------------------------------------------------------------------------
// toolSchemas — returns the full QJsonArray of tool definitions
// ---------------------------------------------------------------------------
QJsonArray ToolDefinitions::toolSchemas() {
    QJsonArray tools;

    // --- Read-only tools ---

    // get_editor_state (no parameters)
    {
        tools.append(makeTool(
            "get_editor_state",
            "Get the current editor state including file info, tracks, cursor position, "
            "tempo, time signature, and selected events.",
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
            "Create a new MIDI track with the given name and channel.",
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
            {"items", makeEventSchema()},
            {"description", "Array of MIDI event objects to insert. Include a program_change event at tick 0 to set the GM instrument."}};
        tools.append(makeTool(
            "insert_events",
            "Insert new MIDI events into a track without removing existing events.",
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
            {"items", makeEventSchema()},
            {"description", "New events to insert after removing existing ones in the range."}};
        tools.append(makeTool(
            "replace_events",
            "Remove all events in a tick range on a track and insert new events. "
            "Used for editing/modifying existing passages.",
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

    return tools;
}

// ---------------------------------------------------------------------------
// executeTool — dispatches tool calls to read-only or write handlers
// ---------------------------------------------------------------------------
QJsonObject ToolDefinitions::executeTool(const QString &toolName,
                                         const QJsonObject &args,
                                         MidiFile *file,
                                         MidiPilotWidget *widget) {
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
        return execWriteAction(toolName, args, widget);
    }
    if (toolName == "insert_events") {
        // Validate events array exists and is non-empty
        QJsonArray events = args["events"].toArray();
        if (events.isEmpty()) {
            QJsonObject result;
            result["success"] = false;
            result["recoverable"] = true;
            result["error"] = QString("Events array missing or empty (likely output truncation). "
                                      "Please retry with the complete events array for this track.");
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
                                      "Please retry with the complete events array. "
                                      "If you want to delete events instead, use the delete_events tool.");
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
        return widget->executeAction(actionObj);
    }
    if (toolName == "set_tempo") {
        return execWriteAction("set_tempo", args, widget);
    }
    if (toolName == "set_time_signature") {
        return execWriteAction("set_time_signature", args, widget);
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
        return widget->executeAction(actionObj);
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
