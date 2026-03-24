#include "EditorContext.h"
#include "MidiEventSerializer.h"

#include <cmath>

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>

#include "../midi/MidiFile.h"
#include "../midi/MidiTrack.h"
#include "../midi/MidiChannel.h"
#include "../MidiEvent/MidiEvent.h"
#include "../MidiEvent/NoteOnEvent.h"
#include "../MidiEvent/OffEvent.h"
#include "../MidiEvent/TempoChangeEvent.h"
#include "../MidiEvent/TimeSignatureEvent.h"
#include "../MidiEvent/KeySignatureEvent.h"
#include "../tool/Selection.h"
#include "../tool/NewNoteTool.h"
#include "../gui/MatrixWidget.h"

// Custom prompt storage (empty = use hardcoded default)
static QString s_customSimplePrompt;
static QString s_customAgentPrompt;
static QString s_customFfxivPrompt;
static QString s_customFfxivCompactPrompt;
static QString s_customPromptsPath;

const char *EditorContext::NOTE_NAMES[] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

QJsonObject EditorContext::captureState(MidiFile *file, MatrixWidget *matrix)
{
    QJsonObject state;

    if (!file) {
        state[QStringLiteral("error")] = QStringLiteral("No file open");
        return state;
    }

    // Cursor position
    int cursorTick = file->cursorTick();
    state[QStringLiteral("cursorTick")] = cursorTick;
    state[QStringLiteral("cursorMs")] = file->msOfTick(cursorTick);

    // Current measure
    int measureStart = 0, measureEnd = 0;
    int measureNum = file->measure(cursorTick, &measureStart, &measureEnd);
    QJsonObject measureObj;
    measureObj[QStringLiteral("number")] = measureNum + 1; // 1-based for user display
    measureObj[QStringLiteral("startTick")] = measureStart;
    measureObj[QStringLiteral("endTick")] = measureEnd;
    state[QStringLiteral("currentMeasure")] = measureObj;

    // Active track
    int trackIdx = NewNoteTool::editTrack();
    MidiTrack *track = file->track(trackIdx);
    QJsonObject trackObj;
    trackObj[QStringLiteral("index")] = trackIdx;
    trackObj[QStringLiteral("name")] = track ? track->name() : QStringLiteral("Unknown");
    trackObj[QStringLiteral("channel")] = NewNoteTool::editChannel();
    state[QStringLiteral("activeTrack")] = trackObj;

    // Active channel
    state[QStringLiteral("activeChannel")] = NewNoteTool::editChannel();

    // Selection info
    QList<MidiEvent *> &selected = Selection::instance()->selectedEvents();
    state[QStringLiteral("selectedEventCount")] = selected.size();

    // Viewport (visible tick range)
    if (matrix) {
        QJsonObject vpObj;
        vpObj[QStringLiteral("startTick")] = matrix->minVisibleMidiTime();
        vpObj[QStringLiteral("endTick")] = matrix->maxVisibleMidiTime();
        state[QStringLiteral("viewport")] = vpObj;
    }

    // Tempo, time signature, key signature at cursor
    state[QStringLiteral("tempo")] = captureTempo(file, cursorTick);
    state[QStringLiteral("timeSignature")] = captureTimeSignature(file, cursorTick);
    state[QStringLiteral("keySignature")] = captureKeySignature(file, cursorTick);

    // File info
    state[QStringLiteral("file")] = captureFileInfo(file);

    // Track list
    state[QStringLiteral("tracks")] = captureTrackList(file);

    return state;
}

QJsonObject EditorContext::captureFileInfo(MidiFile *file)
{
    QJsonObject info;
    if (!file) return info;

    info[QStringLiteral("path")] = file->path();
    info[QStringLiteral("ticksPerQuarter")] = file->ticksPerQuarter();
    info[QStringLiteral("totalTracks")] = file->numTracks();
    info[QStringLiteral("durationMs")] = file->maxTime();
    info[QStringLiteral("endTick")] = file->endTick();
    info[QStringLiteral("modified")] = !file->saved();

    // Total measures (1-based)
    int ms = 0, me = 0;
    int lastMeasure = file->measure(file->endTick(), &ms, &me);
    info[QStringLiteral("totalMeasures")] = lastMeasure + 1;

    return info;
}

QJsonObject EditorContext::captureTempo(MidiFile *file, int tick)
{
    QJsonObject tempoObj;
    QMap<int, MidiEvent *> *tempoMap = file->tempoEvents();
    if (!tempoMap || tempoMap->isEmpty()) {
        tempoObj[QStringLiteral("bpm")] = 120;
        return tempoObj;
    }

    // Find the tempo event at or before the cursor
    TempoChangeEvent *activeTempoEvent = nullptr;
    for (auto it = tempoMap->begin(); it != tempoMap->end(); ++it) {
        if (it.key() > tick) break;
        activeTempoEvent = dynamic_cast<TempoChangeEvent *>(it.value());
    }

    if (activeTempoEvent) {
        int bpm = activeTempoEvent->beatsPerQuarter();
        tempoObj[QStringLiteral("bpm")] = bpm;
    } else {
        tempoObj[QStringLiteral("bpm")] = 120;
    }

    return tempoObj;
}

QJsonObject EditorContext::captureTimeSignature(MidiFile *file, int tick)
{
    QJsonObject tsObj;
    int num = 4, denom = 4;
    file->meterAt(tick, &num, &denom);

    // denom is stored as power-of-2 (MIDI standard). Convert to actual denominator.
    int actualDenom = static_cast<int>(std::pow(2, denom));

    tsObj[QStringLiteral("numerator")] = num;
    tsObj[QStringLiteral("denominator")] = actualDenom;
    tsObj[QStringLiteral("display")] = QStringLiteral("%1/%2").arg(num).arg(actualDenom);

    return tsObj;
}

QJsonObject EditorContext::captureKeySignature(MidiFile *file, int tick)
{
    QJsonObject ksObj;
    int tonality = file->tonalityAt(tick);

    // Determine the key name from tonality
    // tonalityAt returns sharps (positive) or flats (negative)
    // We need to figure out if it's minor from the actual event
    QMap<int, MidiEvent *> *tsEvents = file->timeSignatureEvents();
    // Key signature events are on channel 18 alongside time signatures
    // Use tonality directly
    bool isMinor = false;

    // Search for key signature event at or before tick
    for (int ch = 0; ch < 19; ch++) {
        QMultiMap<int, MidiEvent *> *events = file->channelEvents(ch);
        if (!events) continue;
        for (auto it = events->begin(); it != events->end(); ++it) {
            if (it.key() > tick) break;
            KeySignatureEvent *ks = dynamic_cast<KeySignatureEvent *>(it.value());
            if (ks) {
                isMinor = ks->minor();
            }
        }
    }

    ksObj[QStringLiteral("display")] = KeySignatureEvent::toString(tonality, isMinor);

    return ksObj;
}

QJsonArray EditorContext::captureTrackList(MidiFile *file)
{
    QJsonArray arr;
    QList<MidiTrack *> *tracks = file->tracks();
    if (!tracks) return arr;

    for (int i = 0; i < tracks->size(); i++) {
        MidiTrack *t = tracks->at(i);
        QJsonObject obj;
        obj[QStringLiteral("index")] = i;
        obj[QStringLiteral("name")] = t->name();
        obj[QStringLiteral("channel")] = t->assignedChannel();
        obj[QStringLiteral("muted")] = t->muted();
        obj[QStringLiteral("hidden")] = t->hidden();
        arr.append(obj);
    }
    return arr;
}

QJsonObject EditorContext::captureSurroundingEvents(MidiFile *file, int cursorTick, int measures)
{
    QJsonObject result;
    if (!file || measures <= 0)
        return result;

    // Find the cursor's measure number (1-based)
    int measureStart = 0, measureEnd = 0;
    int cursorMeasure = file->measure(cursorTick, &measureStart, &measureEnd);

    // Calculate tick range: ±N measures around cursor
    int startMeasure = qMax(1, cursorMeasure - measures);
    int endMeasure = cursorMeasure + measures;

    int startTick = file->startTickOfMeasure(startMeasure);
    int endTick = file->startTickOfMeasure(endMeasure + 1);
    // Clamp to file bounds
    if (startTick < 0) startTick = 0;
    if (endTick > file->endTick()) endTick = file->endTick();

    // Store range info
    QJsonObject rangeObj;
    rangeObj[QStringLiteral("startTick")] = startTick;
    rangeObj[QStringLiteral("endTick")] = endTick;
    rangeObj[QStringLiteral("startMeasure")] = startMeasure;
    rangeObj[QStringLiteral("endMeasure")] = endMeasure;
    result[QStringLiteral("range")] = rangeObj;

    // Collect events in range, grouped by track
    QList<MidiEvent *> *allEvents = file->eventsBetween(startTick, endTick);

    // Group by track index
    QMap<int, QList<MidiEvent *>> trackEvents;
    QList<MidiTrack *> *tracks = file->tracks();
    if (tracks && allEvents) {
        for (MidiEvent *ev : *allEvents) {
            // Skip OffEvents (they're part of NoteOn pairs)
            if (dynamic_cast<OffEvent *>(ev))
                continue;

            MidiTrack *t = ev->track();
            if (!t) continue;
            int idx = tracks->indexOf(t);
            if (idx >= 0) {
                trackEvents[idx].append(ev);
            }
        }
    }
    delete allEvents;

    // Serialize per track
    QJsonArray tracksArr;
    int totalEventCount = 0;
    for (auto it = trackEvents.begin(); it != trackEvents.end(); ++it) {
        int idx = it.key();
        const QList<MidiEvent *> &events = it.value();
        if (events.isEmpty()) continue;

        MidiTrack *track = tracks->at(idx);
        QJsonObject trackObj;
        trackObj[QStringLiteral("index")] = idx;
        trackObj[QStringLiteral("name")] = track->name();
        trackObj[QStringLiteral("channel")] = track->assignedChannel();
        trackObj[QStringLiteral("events")] = MidiEventSerializer::serialize(events, file);
        trackObj[QStringLiteral("eventCount")] = events.size();
        tracksArr.append(trackObj);
        totalEventCount += events.size();
    }

    result[QStringLiteral("tracks")] = tracksArr;
    result[QStringLiteral("totalEventCount")] = totalEventCount;

    return result;
}

QString EditorContext::systemPrompt()
{
    if (!s_customSimplePrompt.isEmpty()) return s_customSimplePrompt;
    return QStringLiteral(
        "You are MidiPilot, an AI assistant embedded in MidiEditor.\n"
        "You receive the current editor state and selected MIDI events as JSON.\n"
        "Your job is to transform, analyze, or generate MIDI events based on the user's request.\n"
        "\n"
        "RESPONSE FORMAT — you MUST respond with a raw JSON object (no markdown, no ```json blocks):\n"
        "\n"
        "For EDITING operations (modify, add, delete, transform events):\n"
        "{\n"
        "  \"action\": \"edit\",\n"
        "  \"events\": [ ... ],\n"
        "  \"explanation\": \"Brief description of what was done\"\n"
        "}\n"
        "\n"
        "For DELETING events (remove some or all selected events):\n"
        "{\n"
        "  \"action\": \"delete\",\n"
        "  \"deleteIndices\": [0, 2, 4],\n"
        "  \"explanation\": \"Deleted every other note\"\n"
        "}\n"
        "deleteIndices is a list of 0-based indices into the selectedEvents array.\n"
        "\n"
        "For ANALYSIS / questions (no changes to events):\n"
        "{\"action\": \"info\", \"explanation\": \"Your analysis here\"}\n"
        "\n"
        "For ERRORS:\n"
        "{\"action\": \"error\", \"explanation\": \"Reason\"}\n"
        "\n"
        "For TRACK MANAGEMENT:\n"
        "Create a new track:\n"
        "{\"action\": \"create_track\", \"trackName\": \"Bass\", \"channel\": 1, \"explanation\": \"Created bass track on channel 1\"}\n"
        "Rename an existing track:\n"
        "{\"action\": \"rename_track\", \"trackIndex\": 2, \"newName\": \"Lead Synth\", \"explanation\": \"Renamed track 2\"}\n"
        "Change a track's channel assignment:\n"
        "{\"action\": \"set_channel\", \"trackIndex\": 1, \"channel\": 5, \"explanation\": \"Changed track 1 to channel 5\"}\n"
        "Move selected events to a different track:\n"
        "{\"action\": \"move_to_track\", \"trackIndex\": 2, \"explanation\": \"Moved selected events to track 2\"}\n"
        "\n"
        "For TEMPO CHANGES:\n"
        "{\"action\": \"set_tempo\", \"bpm\": 140, \"tick\": 0, \"explanation\": \"Set tempo to 140 BPM\"}\n"
        "- bpm: beats per minute (1-999)\n"
        "- tick: position in ticks where the tempo change occurs (0 = song start)\n"
        "- If a tempo event already exists at the given tick, it will be modified. Otherwise a new one is created.\n"
        "- Use tick 0 to change the initial tempo. Use other ticks for tempo changes mid-song.\n"
        "\n"
        "For TIME SIGNATURE CHANGES:\n"
        "{\"action\": \"set_time_signature\", \"numerator\": 3, \"denominator\": 4, \"tick\": 0, \"explanation\": \"Changed to 3/4 time\"}\n"
        "- numerator: beats per measure (e.g. 3 for 3/4, 6 for 6/8)\n"
        "- denominator: note value that gets the beat (1=whole, 2=half, 4=quarter, 8=eighth, 16=sixteenth, 32=thirty-second)\n"
        "- tick: position where the time signature change occurs (0 = song start)\n"
        "- If a time signature event already exists at the given tick, it will be modified.\n"
        "\n"
        "For RANGE-BASED EDITING (edit events in a tick range without requiring user selection):\n"
        "{\"action\": \"select_and_edit\", \"trackIndex\": 1, \"startTick\": 0, \"endTick\": 3840, \"events\": [...], \"explanation\": \"Rewrote melody in measures 1-4\"}\n"
        "- trackIndex: which track to operate on (0-based)\n"
        "- startTick/endTick: tick range — all MIDI events on that track in this range are removed first\n"
        "- events: array of new events to insert (same format as \"edit\" action events)\n"
        "- Use this when you want to rewrite, generate, or replace events in a specific region\n"
        "- The events array replaces ALL existing events in the range on the specified track\n"
        "\n"
        "For RANGE-BASED DELETION (delete events in a tick range without requiring user selection):\n"
        "{\"action\": \"select_and_delete\", \"trackIndex\": 1, \"startTick\": 0, \"endTick\": 3840, \"explanation\": \"Cleared measures 1-4\"}\n"
        "- Removes all MIDI events on the specified track within the tick range\n"
        "- Does not affect events on other tracks or meta events (tempo, time signature)\n"
        "\n"
        "TRACK MANAGEMENT NOTES:\n"
        "- trackIndex is a 0-based index into the tracks list provided in the editor state.\n"
        "- channel is a MIDI channel (0-15). Channel 9 is drums (GM standard).\n"
        "- When creating a track, always assign a channel unless the user specifies otherwise.\n"
        "\n"
        "MULTI-ACTION RESPONSES:\n"
        "You can execute MULTIPLE actions in a single response using the \"actions\" array:\n"
        "{\n"
        "  \"actions\": [\n"
        "    {\"action\": \"create_track\", \"trackName\": \"Harmony\", \"channel\": 1, \"explanation\": \"Created harmony track\"},\n"
        "    {\"action\": \"select_and_edit\", \"trackIndex\": 2, \"startTick\": 0, \"endTick\": 7680, \"events\": [...], \"explanation\": \"Added harmony notes\"}\n"
        "  ],\n"
        "  \"explanation\": \"Created harmony track with chord voicings\"\n"
        "}\n"
        "- Actions are executed in order. A create_track action makes the new track available for subsequent actions.\n"
        "- The new track's index = current total number of tracks (before creation). E.g., if there are 3 tracks (0,1,2), the new track will be index 3.\n"
        "- ALWAYS prefer multi-action when the user's request involves creating a track AND adding content to it.\n"
        "- Also use multi-action for: setting tempo + adding notes, creating multiple tracks, etc.\n"
        "- You can still use a single action object (without \"actions\" array) for simple single-step operations.\n"
        "\n"
        "EVENT FORMAT (for \"edit\" action):\n"
        "Note:           {\"type\": \"note\", \"tick\": <int>, \"note\": <0-127>, \"velocity\": <1-127>, \"duration\": <int ticks>, \"channel\": <0-15>, \"track\": <int, optional>}\n"
        "Control Change: {\"type\": \"cc\", \"tick\": <int>, \"control\": <0-127>, \"value\": <0-127>, \"channel\": <0-15>, \"track\": <int, optional>}\n"
        "Pitch Bend:     {\"type\": \"pitch_bend\", \"tick\": <int>, \"value\": <0-16383>, \"channel\": <0-15>, \"track\": <int, optional>}\n"
        "Program Change: {\"type\": \"program_change\", \"tick\": <int>, \"program\": <0-127>, \"channel\": <0-15>, \"track\": <int, optional>}\n"
        "\n"
        "TRACK TARGETING (for \"edit\" action):\n"
        "- You can specify a target track at the response level: {\"action\": \"edit\", \"track\": 2, \"events\": [...]}\n"
        "- You can also specify \"track\" per event to place individual events on different tracks.\n"
        "- Per-event \"track\" overrides the response-level \"track\".\n"
        "- If no \"track\" is specified, events go to the currently active track in the editor.\n"
        "- IMPORTANT: When the user asks to create events on a specific track, ALWAYS include the \"track\" field.\n"
        "\n"
        "EDITING SEMANTICS:\n"
        "- The \"events\" array in an \"edit\" response REPLACES ALL currently selected events.\n"
        "- To keep some original events: include them in the array unchanged.\n"
        "- To add new events: include both the originals you want to keep and the new ones.\n"
        "- To modify events: return them with changed values (same tick = same event).\n"
        "- To delete specific events: use \"action\": \"delete\" with \"deleteIndices\" instead.\n"
        "- Each event MUST include \"channel\". Preserve the original channel from selectedEvents.\n"
        "- Each event MUST include \"tick\". Preserve original ticks unless the user asks to move them.\n"
        "\n"
        "MUSIC CONVENTIONS:\n"
        "- Note 60 = Middle C = C4\n"
        "- Tick positions must be non-negative integers\n"
        "- Duration is in ticks (see ticksPerQuarter in context for the resolution)\n"
        "- Consider tempo, time signature, and key signature for musical decisions\n"
        "- The user may refer to tracks by name or number, channels by number, measures by number\n"
        "- When the user says 'here' they mean the current cursor position/measure\n"
        "\n"
        "SURROUNDING CONTEXT:\n"
        "- You may receive a 'surroundingEvents' object containing events from all tracks near the cursor.\n"
        "- This provides musical context (harmony, rhythm, other parts) for better decisions.\n"
        "- surroundingEvents.range shows the tick/measure range covered.\n"
        "- surroundingEvents.tracks[] contains per-track event arrays.\n"
        "- Use this context for analysis, harmonization, counterpoint, and informed composition.\n"
        "- Do NOT modify surrounding events — they are read-only context. Only modify selectedEvents.\n"
        "\n"
        "AUTONOMOUS EDITING:\n"
        "- You can use select_and_edit / select_and_delete to operate on events WITHOUT requiring the user to select them first.\n"
        "- Use surroundingEvents context to understand what's in each track and tick range.\n"
        "- When creating a new track AND filling it with content, use multi-action: create_track first, then select_and_edit.\n"
        "- When the user asks to 'write', 'compose', 'generate', or 'add' music to a region, prefer select_and_edit.\n"
        "- When the user asks to 'clear', 'remove', or 'erase' a region, prefer select_and_delete.\n"
        "- IMPORTANT: Always complete the user's full request in ONE response. Do not split work across multiple turns.\n"
        "  For example, if asked to 'create a bass track with a walking bass line', respond with a multi-action that creates the track AND adds the notes.\n"
    );
}

QString EditorContext::agentSystemPrompt()
{
    if (!s_customAgentPrompt.isEmpty()) return s_customAgentPrompt;
    return QStringLiteral(
        "You are MidiPilot, an AI assistant embedded in MidiEditor.\n"
        "You have tools to inspect and modify MIDI files. Use them to fulfill the user's request.\n"
        "\n"
        "IMPORTANT:\n"
        "- The user's FIRST message already contains the full editor state, tracks, tempo,\n"
        "  time signature, and surrounding events. Do NOT call get_editor_state redundantly.\n"
        "- Start working immediately: create tracks, set tempo, and insert events right away.\n"
        "- Use PARALLEL tool calls when possible (e.g. create multiple tracks in one step).\n"
        "- Insert events ONE TRACK AT A TIME to avoid output truncation.\n"
        "- ALWAYS use the compact 'note' event type with duration (NOT separate note_on/note_off pairs).\n"
        "  Example: {\"type\": \"note\", \"tick\": 0, \"note\": 60, \"velocity\": 80, \"duration\": 192}\n"
        "\n"
        "MUSIC CONVENTIONS:\n"
        "- Note 60 = Middle C (C4). Notes range 0-127.\n"
        "- Tick positions depend on ticksPerQuarter (check editor state in the first message).\n"
        "- MIDI channels 0-15, channel 9 = drums (GM standard).\n"
        "- Track indices are 0-based.\n"
        "\n"
        "GM INSTRUMENT ASSIGNMENT (CRITICAL):\n"
        "- When creating instrument tracks, ALWAYS insert a program_change event at tick 0\n"
        "  to set the correct GM instrument sound. Without this, all channels default to Piano.\n"
        "- Event format: {\"type\": \"program_change\", \"tick\": 0, \"program\": <0-127>}\n"
        "- Include the program_change as the FIRST event in your insert_events call for each track.\n"
        "- Common GM programs: Piano=0, Guitar(Nylon)=24, Guitar(Jazz)=26, Acoustic Bass=32,\n"
        "  Electric Bass=33, Trumpet=56, Trombone=57, Alto Sax=65, Tenor Sax=66, Clarinet=71,\n"
        "  Flute=73, Strings=48, Organ=16, Vibraphone=11, Pad=88.\n"
        "- Channel 9 is drums — do NOT send program_change for drums.\n"
        "\n"
        "BEST PRACTICES:\n"
        "- Always complete the full request in one turn. Do not ask for follow-up.\n"
        "- When composing, consider key, tempo, time signature, and surrounding context.\n"
        "- When composing multiple tracks, CHECK the 'summary' field in previous tool results\n"
        "  (pitchClasses, noteRange) to ensure harmonic coherence across tracks.\n"
        "- To create a track and fill it, call create_track then insert_events.\n"
        "- For NEW empty tracks, always use insert_events (not replace_events).\n"
        "- To modify existing music, use query_events to read, then replace_events to rewrite.\n"
        "- To clear a region, use delete_events.\n"
        "- Your final text message should be a concise summary of what was done.\n"
    );
}

QString EditorContext::ffxivContext()
{
    if (!s_customFfxivPrompt.isEmpty()) return s_customFfxivPrompt;
    return QStringLiteral(
        "\n"
        "## FFXIV BARD PERFORMANCE MODE (ACTIVE)\n"
        "\n"
        "You are creating/editing MIDI for FFXIV Bard Performance.\n"
        "Follow these rules STRICTLY — violations will make the file unplayable in-game.\n"
        "\n"
        "### Hard Constraints\n"
        "- Each track is MONOPHONIC by default — only ONE note sounding at a time.\n"
        "  Exception: Some instruments support chord simulation (see Polyphony section).\n"
        "- All notes MUST be in C3-C6 (MIDI 48-84).\n"
        "- Track names MUST match valid instrument names EXACTLY (see Valid Instrument Track Names).\n"
        "  The track name determines which instrument the bard player equips in-game.\n"
        "- Do NOT use pitch_bend events.\n"
        "- Do NOT edit note velocity.\n"
        "- Use program_change events at tick 0 on each channel to define the instrument.\n"
        "\n"
        "### Channels & Tracks\n"
        "- The track NAME determines the instrument — the channel number does not.\n"
        "- Assign each track its own unique channel for clean editing (recommended).\n"
        "- Drum tracks can stay on channel 9 — the track name determines the percussion instrument.\n"
        "- You can use MIDI channels 0-15 as needed.\n"
        "- Guitar switches are the ONLY case where channels matter functionally:\n"
        "  Each guitar variant (Clean, Muted, Overdriven, PowerChords, Special) needs its\n"
        "  own channel with program_change at tick 0. Switch guitar sound by changing the\n"
        "  note's channel mid-track. When ANY guitar is used, configure ALL 5 guitar channels.\n"
        "- A typical Octett ensemble has 8 instrument tracks, but you can have more channels for\n"
        "  guitar switches and additional instruments.\n"
        "\n"
        "### Valid Instrument Track Names\n"
        "Piano, Harp, Fiddle, Lute, Fife, Flute, Oboe, Panpipes, Clarinet, Trumpet,\n"
        "Saxophone, Trombone, Horn, Tuba, Violin, Viola, Cello, Double Bass, Timpani,\n"
        "Bongo, Bass Drum, Snare Drum, Cymbal, ElectricGuitarClean, ElectricGuitarMuted,\n"
        "ElectricGuitarOverdriven, ElectricGuitarPowerChords, ElectricGuitarSpecial\n"
        "\n"
        "### Instrument Native Ranges (player transposes from C3-C6)\n"
        "| Instrument | Native Range | Transpose from C3-C6 |\n"
        "| Piano | C4-C7 | -1 octave |\n"
        "| Harp, Clarinet, Trumpet, Saxophone, Violin, Viola, Bongo, Bass Drum, Snare Drum, Cymbal, ElectricGuitarSpecial | C3-C6 | native |\n"
        "| Fiddle, Lute, Trombone, Horn, Cello, Timpani, ElectricGuitarClean/Muted/Overdriven/PowerChords | C2-C5 | +1 octave |\n"
        "| Fife | C5-C8 | -2 octaves |\n"
        "| Flute, Oboe, Panpipes | C4-C7 | -1 octave |\n"
        "| Tuba, Double Bass | C1-C4 | +2 octaves |\n"
        "\n"
        "### Drum Handling\n"
        "Drums are NOT a drum kit — each percussion instrument is a SEPARATE tonal track:\n"
        "- Bass Drum: Use note C4 (60) for the main hit.\n"
        "- Snare Drum: Use note C5 (72), vary +/-1 for ghost notes/flams.\n"
        "- Cymbal: C5 (72) = crash, C6 (84) = hi-hat, in between = ride/china.\n"
        "- Timpani: Tonal — can play actual melodic bass patterns.\n"
        "- Bongo: Tonal — use two-tone rhythmic patterns.\n"
        "When converting from GM drum tracks (channel 9), split into separate tracks:\n"
        "  GM Kick (35-36) -> Bass Drum track, note C4\n"
        "  GM Snare (38,40) -> Snare Drum track, note C5\n"
        "  GM Hi-Hat (42,44,46) -> Cymbal track, note C6\n"
        "  GM Crash (49,57) -> Cymbal track, note C5\n"
        "  GM Toms (41,43,45,47,48,50) -> Timpani track, mapped tonally\n"
        "Tom rolls: add +12 to GM tom notes → MIDI 53-62 on Bass Drum track.\n"
        "\n"
        "### Channel Pattern & Program Numbers\n"
        "Each channel needs a program_change at tick 0 to define its instrument.\n"
        "Assign each track a unique channel. When guitars are used, configure ALL 5\n"
        "guitar variant channels with program_change at tick 0.\n"
        "\n"
        "| Instrument | GM Program |\n"
        "| Piano | 0 |\n"
        "| Harp | 46 |\n"
        "| Fiddle | 45 |\n"
        "| Lute | 24 |\n"
        "| Fife | 72 |\n"
        "| Flute | 73 |\n"
        "| Oboe | 68 |\n"
        "| Panpipes | 75 |\n"
        "| Clarinet | 71 |\n"
        "| Trumpet | 56 |\n"
        "| Saxophone | 65 |\n"
        "| Trombone | 57 |\n"
        "| Horn | 60 |\n"
        "| Tuba | 58 |\n"
        "| Violin | 40 |\n"
        "| Viola | 41 |\n"
        "| Cello | 42 |\n"
        "| Double Bass | 43 |\n"
        "| Timpani | 47 |\n"
        "| Bongo | 116 |\n"
        "| Bass Drum | 117 |\n"
        "| Snare Drum | 115 |\n"
        "| Cymbal | 127 |\n"
        "| ElectricGuitarClean | 27 |\n"
        "| ElectricGuitarMuted | 28 |\n"
        "| ElectricGuitarOverdriven | 29 |\n"
        "| ElectricGuitarPowerChords | 30 |\n"
        "| ElectricGuitarSpecial | 31 |\n"
        "\n"
        "### Guitar Switches\n"
        "5 guitar variants can share a track. Each variant needs its own channel with\n"
        "program_change at tick 0. Switch between variants by changing per-note channel.\n"
        "Example: Track 'ElectricGuitarPowerChords' on ch 3. To switch to harmonics,\n"
        "set those notes' channel to ch 6 (where ElectricGuitarSpecial program_change lives).\n"
        "When ANY guitar is used, configure ALL 5 guitar channels.\n"
        "The setup_channel_pattern tool handles this automatically.\n"
        "\n"
        "### Track Ordering\n"
        "- Melodic/harmonic instruments first, drums at the end (highest indices).\n"
        "\n"
        "### Polyphony / Chord Simulation\n"
        "Multiple notes at the same tick play as a fast arpeggio = chord effect.\n"
        "| Instrument | Max Simultaneous Notes |\n"
        "| Lute | 2-3 (2 preferred) |\n"
        "| Harp | 2-3 |\n"
        "| Piano | 2 |\n"
        "ALL other instruments: strictly monophonic (1 note at a time).\n"
        "Dense arrangements (6+ tracks): max 2-note chords.\n"
        "Sparse arrangements (solo/duo): 3-note chords on Lute/Harp add fullness.\n"
        "\n"
        "### ElectricGuitarSpecial Sound Map\n"
        "ElectricGuitarSpecial does NOT play normal notes — each pitch range triggers a\n"
        "different sound effect:\n"
        "| Range | MIDI | Sound Effect |\n"
        "| C3-F3 | 48-53 | Low-to-high scratch |\n"
        "| G3-C4 | 55-60 | Wave scratch |\n"
        "| D4-G4 | 62-67 | Short scratch |\n"
        "| A4-E5 | 69-76 | String tap |\n"
        "| F5-C6 | 77-84 | Metal scream |\n"
        "Use sparingly as accents via channel override to Special.\n"
    );
}

QString EditorContext::ffxivContextCompact()
{
    if (!s_customFfxivCompactPrompt.isEmpty()) return s_customFfxivCompactPrompt;
    return QStringLiteral(
        "\n"
        "## FFXIV BARD PERFORMANCE MODE (ACTIVE)\n"
        "\n"
        "You are creating/editing MIDI for FFXIV Bard Performance. STRICT rules:\n"
        "- Tracks are MONOPHONIC unless noted below.\n"
        "- Notes MUST be C3-C6 (MIDI 48-84).\n"
        "- Track names MUST be exact instrument names — the name determines the instrument.\n"
        "- No pitch_bend events. No velocity editing (use fixed 100).\n"
        "- Assign each track its own channel for clean editing. Drums can stay on channel 9.\n"
        "- Use program_change at tick 0 on each channel.\n"
        "- Guitar switches: each variant needs its own channel. Switch by changing note channel.\n"
        "\n"
        "### Instruments (name | program)\n"
        "Piano 0, Harp 46, Fiddle 45, Lute 24, Fife 72, Flute 73, Oboe 68,\n"
        "Panpipes 75, Clarinet 71, Trumpet 56, Saxophone 65, Trombone 57,\n"
        "Horn 60, Tuba 58, Violin 40, Viola 41, Cello 42, Double Bass 43,\n"
        "Timpani 47, Bongo 116, Bass Drum 117, Snare Drum 115, Cymbal 127\n"
        "Guitars: Clean 27, Muted 28, Overdriven 29, PowerChords 30, Special 31\n"
        "\n"
        "### Drums — separate tonal tracks, NOT a kit:\n"
        "Bass Drum: C4 (60). Snare: C5 (72). Cymbal: C5=crash, C6=hi-hat.\n"
        "Timpani: tonal. Bongo: tonal.\n"
        "Drum tracks go LAST (highest track indices). Melodic instruments first.\n"
        "Tom rolls for Bass Drum: add +12 to GM tom notes (41-50) -> MIDI 53-62.\n"
        "\n"
        "### Guitar Switches\n"
        "5 guitar variants can share a track. Each variant needs its own channel\n"
        "with program_change at tick 0. Switch by changing per-note channel.\n"
        "When ANY guitar is used, configure ALL 5 guitar channels.\n"
        "Special is NOT a normal instrument — each pitch range is a different effect:\n"
        "C3-F3=low scratch, G3-C4=wave scratch, D4-G4=short scratch,\n"
        "A4-E5=string tap, F5-C6=metal scream. Use sparingly as accents.\n"
        "\n"
        "### Polyphony (Chord Simulation)\n"
        "Same-tick notes play as fast arpeggios = chord effect.\n"
        "Lute: 2-3 notes. Harp: 2-3 notes. Piano: max 2 notes.\n"
        "All other instruments: strictly monophonic.\n"
        "Dense arrangements: max 2-note chords. Sparse: 3 on Lute/Harp OK.\n"
        "\n"
        "### Tips\n"
        "- WARNING: Large compositions (8 tracks, 10+ measures) may exceed output\n"
        "  limits in Simple mode. Use Agent mode for full ensemble songs.\n"
    );
}

// --- Custom Prompt Loading ---

bool EditorContext::loadCustomPrompts(const QString &path)
{
    QFile file(path);
    if (!file.exists())
        return false;

    // Safety: reject files > 1 MB
    if (file.size() > 1024 * 1024)
        return false;

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    QByteArray data = file.readAll();
    file.close();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError)
        return false;

    if (!doc.isObject())
        return false;

    QJsonObject root = doc.object();

    // Version check
    if (!root.contains(QStringLiteral("version")) || !root[QStringLiteral("version")].isDouble())
        return false;

    int version = root[QStringLiteral("version")].toInt();
    if (version != 1)
        return false;

    if (!root.contains(QStringLiteral("prompts")) || !root[QStringLiteral("prompts")].isObject())
        return false;

    QJsonObject prompts = root[QStringLiteral("prompts")].toObject();

    // Partial overrides: only set prompts that are present and non-empty
    if (prompts.contains(QStringLiteral("simple"))) {
        QString val = prompts[QStringLiteral("simple")].toString();
        if (!val.isEmpty()) s_customSimplePrompt = val;
    }
    if (prompts.contains(QStringLiteral("agent"))) {
        QString val = prompts[QStringLiteral("agent")].toString();
        if (!val.isEmpty()) s_customAgentPrompt = val;
    }
    if (prompts.contains(QStringLiteral("ffxiv"))) {
        QString val = prompts[QStringLiteral("ffxiv")].toString();
        if (!val.isEmpty()) s_customFfxivPrompt = val;
    }
    if (prompts.contains(QStringLiteral("ffxiv_compact"))) {
        QString val = prompts[QStringLiteral("ffxiv_compact")].toString();
        if (!val.isEmpty()) s_customFfxivCompactPrompt = val;
    }

    s_customPromptsPath = path;
    return true;
}

void EditorContext::resetToDefaults()
{
    s_customSimplePrompt.clear();
    s_customAgentPrompt.clear();
    s_customFfxivPrompt.clear();
    s_customFfxivCompactPrompt.clear();
    s_customPromptsPath.clear();
}

bool EditorContext::hasCustomPrompts()
{
    return !s_customPromptsPath.isEmpty();
}

QString EditorContext::customPromptsPath()
{
    return s_customPromptsPath;
}

bool EditorContext::savePromptsToJson(const QString &path)
{
    // Back up existing file
    if (QFile::exists(path)) {
        QString bakPath = path + QStringLiteral(".bak");
        QFile::remove(bakPath);
        QFile::rename(path, bakPath);
    }

    QJsonObject prompts;
    prompts[QStringLiteral("simple")] = systemPrompt();
    prompts[QStringLiteral("agent")] = agentSystemPrompt();
    prompts[QStringLiteral("ffxiv")] = ffxivContext();
    prompts[QStringLiteral("ffxiv_compact")] = ffxivContextCompact();

    QJsonObject root;
    root[QStringLiteral("version")] = 1;
    root[QStringLiteral("prompts")] = prompts;

    QJsonDocument doc(root);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();

    s_customPromptsPath = path;
    return true;
}

QString EditorContext::defaultPrompt(const QString &key)
{
    // Temporarily clear custom prompts to get defaults
    QString savedSimple = s_customSimplePrompt;
    QString savedAgent = s_customAgentPrompt;
    QString savedFfxiv = s_customFfxivPrompt;
    QString savedFfxivCompact = s_customFfxivCompactPrompt;

    s_customSimplePrompt.clear();
    s_customAgentPrompt.clear();
    s_customFfxivPrompt.clear();
    s_customFfxivCompactPrompt.clear();

    QString result;
    if (key == QStringLiteral("simple"))
        result = systemPrompt();
    else if (key == QStringLiteral("agent"))
        result = agentSystemPrompt();
    else if (key == QStringLiteral("ffxiv"))
        result = ffxivContext();
    else if (key == QStringLiteral("ffxiv_compact"))
        result = ffxivContextCompact();

    // Restore custom prompts
    s_customSimplePrompt = savedSimple;
    s_customAgentPrompt = savedAgent;
    s_customFfxivPrompt = savedFfxiv;
    s_customFfxivCompactPrompt = savedFfxivCompact;

    return result;
}
