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
// Helper: create a tool schema object in OpenAI format
// ---------------------------------------------------------------------------
QJsonObject ToolDefinitions::makeTool(const QString &name,
                                      const QString &description,
                                      const QJsonObject &parameters) {
    QJsonObject func;
    func["name"] = name;
    func["description"] = description;
    func["parameters"] = parameters;

    QJsonObject tool;
    tool["type"] = QString("function");
    tool["function"] = func;
    return tool;
}

// ---------------------------------------------------------------------------
// toolSchemas — returns the full QJsonArray of tool definitions
// ---------------------------------------------------------------------------
QJsonArray ToolDefinitions::toolSchemas() {
    QJsonArray tools;

    // --- Read-only tools ---

    // get_editor_state
    {
        QJsonObject params;
        params["type"] = QString("object");
        params["properties"] = QJsonObject();
        tools.append(makeTool(
            "get_editor_state",
            "Get the current editor state including file info, tracks, cursor position, "
            "tempo, time signature, and selected events.",
            params));
    }

    // get_track_info
    {
        QJsonObject props;
        props["trackIndex"] = QJsonObject{
            {"type", "integer"},
            {"description", "Zero-based index of the track to inspect."}};
        QJsonObject params;
        params["type"] = QString("object");
        params["properties"] = props;
        params["required"] = QJsonArray{"trackIndex"};
        tools.append(makeTool(
            "get_track_info",
            "Get detailed information about a specific track: name, channel, event count.",
            params));
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
        QJsonObject params;
        params["type"] = QString("object");
        params["properties"] = props;
        params["required"] = QJsonArray{"trackIndex", "startTick", "endTick"};
        tools.append(makeTool(
            "query_events",
            "Query MIDI events in a tick range on a specific track. Returns serialized events.",
            params));
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
            {"minimum", 0}, {"maximum", 15},
            {"description", "MIDI channel to assign (0-15)."}};
        QJsonObject params;
        params["type"] = QString("object");
        params["properties"] = props;
        params["required"] = QJsonArray{"trackName", "channel"};
        tools.append(makeTool(
            "create_track",
            "Create a new MIDI track with the given name and channel.",
            params));
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
        QJsonObject params;
        params["type"] = QString("object");
        params["properties"] = props;
        params["required"] = QJsonArray{"trackIndex", "newName"};
        tools.append(makeTool(
            "rename_track",
            "Rename an existing track.",
            params));
    }

    // set_channel
    {
        QJsonObject props;
        props["trackIndex"] = QJsonObject{
            {"type", "integer"},
            {"description", "Zero-based index of the track."}};
        props["channel"] = QJsonObject{
            {"type", "integer"},
            {"minimum", 0}, {"maximum", 15},
            {"description", "MIDI channel to assign (0-15)."}};
        QJsonObject params;
        params["type"] = QString("object");
        params["properties"] = props;
        params["required"] = QJsonArray{"trackIndex", "channel"};
        tools.append(makeTool(
            "set_channel",
            "Set the MIDI channel for a track.",
            params));
    }

    // insert_events
    {
        QJsonObject eventSchema;
        eventSchema["type"] = QString("object");
        eventSchema["description"] = QString("A MIDI event object with type, tick, note/value, velocity, duration fields.");

        QJsonObject props;
        props["trackIndex"] = QJsonObject{
            {"type", "integer"},
            {"description", "Zero-based track index to insert events into."}};
        props["channel"] = QJsonObject{
            {"type", "integer"},
            {"minimum", 0}, {"maximum", 15},
            {"description", "MIDI channel for the events (0-15)."}};
        props["events"] = QJsonObject{
            {"type", "array"},
            {"items", eventSchema},
            {"description", "Array of MIDI event objects to insert."}};
        QJsonObject params;
        params["type"] = QString("object");
        params["properties"] = props;
        params["required"] = QJsonArray{"trackIndex", "events"};
        tools.append(makeTool(
            "insert_events",
            "Insert new MIDI events into a track without removing existing events.",
            params));
    }

    // replace_events
    {
        QJsonObject eventSchema;
        eventSchema["type"] = QString("object");
        eventSchema["description"] = QString("A MIDI event object.");

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
            {"items", eventSchema},
            {"description", "New events to insert after removing existing ones in the range."}};
        QJsonObject params;
        params["type"] = QString("object");
        params["properties"] = props;
        params["required"] = QJsonArray{"trackIndex", "startTick", "endTick", "events"};
        tools.append(makeTool(
            "replace_events",
            "Remove all events in a tick range on a track and insert new events. "
            "Used for editing/modifying existing passages.",
            params));
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
        QJsonObject params;
        params["type"] = QString("object");
        params["properties"] = props;
        params["required"] = QJsonArray{"trackIndex", "startTick", "endTick"};
        tools.append(makeTool(
            "delete_events",
            "Delete all MIDI events in a tick range on a specific track.",
            params));
    }

    // set_tempo
    {
        QJsonObject props;
        props["bpm"] = QJsonObject{
            {"type", "integer"},
            {"minimum", 1}, {"maximum", 999},
            {"description", "Tempo in beats per minute."}};
        props["tick"] = QJsonObject{
            {"type", "integer"},
            {"minimum", 0},
            {"description", "Tick position for the tempo change (0 = beginning). Default: 0."}};
        QJsonObject params;
        params["type"] = QString("object");
        params["properties"] = props;
        params["required"] = QJsonArray{"bpm"};
        tools.append(makeTool(
            "set_tempo",
            "Set the tempo (BPM) at a specific tick position.",
            params));
    }

    // set_time_signature
    {
        QJsonObject props;
        props["numerator"] = QJsonObject{
            {"type", "integer"},
            {"minimum", 1}, {"maximum", 32},
            {"description", "Time signature numerator (beats per measure)."}};
        props["denominator"] = QJsonObject{
            {"type", "integer"},
            {"enum", QJsonArray{1, 2, 4, 8, 16, 32}},
            {"description", "Time signature denominator (note value: 1, 2, 4, 8, 16, or 32)."}};
        props["tick"] = QJsonObject{
            {"type", "integer"},
            {"minimum", 0},
            {"description", "Tick position for the time signature change. Default: 0."}};
        QJsonObject params;
        params["type"] = QString("object");
        params["properties"] = props;
        params["required"] = QJsonArray{"numerator", "denominator"};
        tools.append(makeTool(
            "set_time_signature",
            "Set the time signature at a specific tick position.",
            params));
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
        QJsonObject params;
        params["type"] = QString("object");
        params["properties"] = props;
        params["required"] = QJsonArray{"sourceTrackIndex", "targetTrackIndex", "startTick", "endTick"};
        tools.append(makeTool(
            "move_events_to_track",
            "Move MIDI events from one track to another within a tick range.",
            params));
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
        // Map to "edit" action (insert without selection = pure insert)
        QJsonObject actionObj;
        actionObj["action"] = QString("edit");
        actionObj["events"] = args["events"];
        if (args.contains("trackIndex"))
            actionObj["track"] = args["trackIndex"];
        if (args.contains("channel"))
            actionObj["channel"] = args["channel"];
        actionObj["explanation"] = QString("Agent: insert events");
        return widget->executeAction(actionObj);
    }
    if (toolName == "replace_events") {
        // Map to "select_and_edit"
        QJsonObject actionObj;
        actionObj["action"] = QString("select_and_edit");
        actionObj["trackIndex"] = args["trackIndex"];
        actionObj["startTick"] = args["startTick"];
        actionObj["endTick"] = args["endTick"];
        actionObj["events"] = args["events"];
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
