# MidiEditor AI-Copilot — Roadmap & Feature-Plan

> Goal: Integrate an AI assistant ("MidiPilot") into MidiEditor that understands
> MIDI events, editor context, and can actively edit MIDI data via the OpenAI API.
> All UI and code must be in **English**.

---

## Architecture Overview

### UI Concept: Secondary Sidebar (VS Code-style)

```
┌──────────────────────────────────────────────────────────────────────────────┐
│  Menu: File | Edit | Tools | MIDI | View | Playback | Help                  │
├──────────────────────────────────────────────────────────────────────────────┤
│  Toolbar: [Tools] [Playback] [Zoom]                          [🤖 Toggle AI] │
├────────────────────────────────┬──────────────┬──────────────────────────────┤
│                                │  Tracks Tab  │  ┌─ MidiPilot ───────────┐  │
│                                │  Channels Tab│  │ ● Status: Connected   │  │
│  MatrixWidget (Piano Roll)     │──────────────│  │ Model: gpt-4o-mini    │  │
│                                │  Protocol Tab│  ├───────────────────────┤  │
│                                │  Event Tab   │  │ Context:              │  │
│                                │              │  │  Track 1: Piano       │  │
│                                │              │  │  Ch 0 | Measure 5     │  │
│                                │              │  │  3 notes selected     │  │
├────────────────────────────────┤              │  ├───────────────────────┤  │
│  MiscWidget (Velocity/CC)      │              │  │ [Chat History]        │  │
├────────────────────────────────┤              │  │ User: Add thirds      │  │
│  Scrollbar                     │              │  │ AI: Added E4 above... │  │
│                                │              │  ├───────────────────────┤  │
│                                │              │  │ [Type message...]  ➤  │  │
└────────────────────────────────┴──────────────┴──┴───────────────────────┘  │
```

- Toggle button in toolbar (like VS Code's secondary sidebar toggle)
- Shortcut: `Ctrl+I` to open/close
- Panel has 3 sections: **Status/Config**, **Context Display**, **Chat**

### Context System: What the AI Knows

The AI receives a **full editor snapshot** with every request:

```json
{
  "editorState": {
    "cursorTick": 1920,
    "cursorMs": 4000,
    "currentMeasure": 5,
    "measureStartTick": 1536,
    "measureEndTick": 2304,
    "activeTrack": { "index": 1, "name": "Piano", "channel": 0 },
    "activeChannel": 0,
    "viewport": { "startTick": 960, "endTick": 3840 },
    "file": {
      "path": "song.mid",
      "ticksPerQuarter": 192,
      "totalTracks": 4,
      "totalMeasures": 32,
      "durationMs": 64000
    },
    "tempo": { "bpm": 120, "microsecondsPerQuarter": 500000 },
    "timeSignature": { "numerator": 4, "denominator": 4 },
    "keySignature": { "key": "C", "scale": "major" }
  },
  "selectedEvents": [ ... ],
  "instruction": "add harmony in thirds"
}
```

**How we get this context** (from existing APIs):
| Data                  | API                                         |
|-----------------------|---------------------------------------------|
| Cursor position       | `MidiFile::cursorTick()`                    |
| Cursor in ms          | `MidiFile::msOfTick(cursorTick)`            |
| Current measure       | `MidiFile::measure(tick, &start, &end)`     |
| Active track          | `NewNoteTool::editTrack()` → `file->track(i)` |
| Active channel        | `NewNoteTool::editChannel()`                |
| Viewport range        | `MatrixWidget::minVisibleMidiTime()` / `max` |
| Selected events       | `Selection::instance()->selectedEvents()`   |
| Tempo                 | `MidiFile::tempoEvents()`                   |
| Time signature        | `MidiFile::timeSignatureEvents()`           |
| File metadata         | `MidiFile::path()`, `numTracks()`, etc.     |
| Track info            | `MidiTrack::name()`, `assignedChannel()`    |
| Meter at position     | `MidiFile::meterAt(tick, &num, &denom)`     |

### Conversation Memory

Simple in-memory conversation history per session:

```cpp
struct ConversationEntry {
    QString role;          // "user" or "assistant"
    QString message;       // The text message
    QJsonObject context;   // Editor snapshot at time of message
    QJsonArray events;     // Events that were modified (for undo reference)
    QDateTime timestamp;
};

QList<ConversationEntry> _conversationHistory;  // Per session, not persisted
```

**Why in-memory is enough (Phase 1):**
- Undo/Redo is already handled by the Protocol system — no need to duplicate
- Each AI action = one Protocol action → standard Ctrl+Z undoes it
- Conversation history provides multi-turn context for the LLM
- No database needed — keep it simple

**Later (Phase 4):** Optional SQLite for persistent history across sessions.

---

## Phase 1: Core Infrastructure

### 1.1 — OpenAI API Client ✅
- **Files:** `src/ai/AiClient.h/cpp`
- ✅ HTTP POST to `https://api.openai.com/v1/chat/completions`
- ✅ Uses `QNetworkAccessManager` (see `UpdateChecker.cpp` as template)
- ✅ Async requests with signal/slot pattern
- ✅ Signals: `responseReceived(QJsonObject)`, `errorOccurred(QString)`
- ✅ Error handling: rate limits, timeouts, network errors, invalid JSON
- ✅ Model parameter configurable (default: `gpt-4o-mini`)
- ✅ **Bonus:** Reasoning model detection (`isReasoningModel()`), `reasoning_effort` support, `testConnection()`, API debug logging (`midipilot_api.log`), GPT-5/o-series compatibility

### 1.2 — Editor Context Collector ✅
- **Files:** `src/ai/EditorContext.h/cpp`
- ✅ Captures full editor state snapshot (see table above)
- ✅ Serializes to `QJsonObject` for inclusion in API request
- ✅ Methods:
  - ✅ `QJsonObject captureState()` — full snapshot (incl. viewport via MatrixWidget)
  - ~~`QJsonObject captureSelection()`~~ — done inline in MidiPilotWidget instead
  - ✅ `QJsonObject captureFileInfo()` — file metadata (incl. totalMeasures)
- ✅ **Bonus:** Track list with `assignedChannel`, viewport `startTick`/`endTick`, system prompt with comprehensive action definitions

### 1.3 — Event Serializer (MIDI ↔ JSON) ✅
- **Files:** `src/ai/MidiEventSerializer.h/cpp`
- ✅ `QJsonArray serialize(QList<MidiEvent*>)` — events to JSON
- ✅ `QList<MidiEvent*> deserialize(QJsonArray, MidiFile*, MidiTrack*)` — JSON to events
- ✅ Compact format (token-efficient): only relevant fields per event type
- ✅ Validates all values (note 0-127, velocity 0-127, tick >= 0)
- ✅ Supported types: note, cc, pitch_bend, program_change

### 1.4 — AI Settings ✅
- **Files:** `src/gui/AiSettingsWidget.h/cpp`
- ✅ New tab in Settings dialog
- ✅ Fields: API Key (masked `QLineEdit`), Model selection (dropdown)
- ✅ Stored via `QSettings`: `"AI/api_key"`, `"AI/model"`, `"AI/enabled"`
- ~~Inline setup also available from the MidiPilot sidebar (first-run experience)~~ — redirects to Settings dialog instead
- ✅ **Bonus:** Thinking/reasoning checkbox + effort dropdown, test connection button, 8+ model presets

---

## Phase 2: MidiPilot Sidebar (UI)

### 2.1 — MidiPilot Panel ✅
- **Files:** `src/gui/MidiPilotWidget.h/cpp`
- ✅ Inherits `QDockWidget` — dockable as secondary sidebar
- ✅ **Three sections:**
  1. ✅ **Header:** Status indicator (connected/disconnected), model name, gear icon for settings
  2. ✅ **Context bar:** Shows active track, channel, measure, selection count (auto-updates)
  3. ✅ **Chat area:** Scrollable message list + input field + send button
- ⬜ Loading spinner during API requests — *uses text-based "Thinking..." instead*
- ✅ Error messages displayed inline

### 2.2 — Toolbar & Menu Integration ✅
- ✅ Toggle button in toolbar: 🤖 icon (`midipilot.png`) to show/hide MidiPilot panel
- ✅ Menu: `View → MidiPilot` (checkable, with `Ctrl+I` shortcut)
- ✅ Menu: `Edit → Ask MidiPilot...` (opens panel and focuses input)
- ⬜ Context menu on right-click in MatrixWidget: `"Ask MidiPilot..."` — *only in Edit menu for now*

### 2.3 — First-Run Setup ✅ (partial)
- ✅ If no API key configured: show setup prompt in the panel
- ✅ "Enter your OpenAI API key to get started" text
- ✅ "Open Settings" button → redirects to Settings dialog
- ⬜ Inline API key input + "Save & Test" button in the panel itself
- ⬜ Link to API key page

---

## Phase 3: MIDI Operations

### 3.1 — Request/Response Pipeline ✅

```
User types message in chat
        ↓
EditorContext::captureState()  →  Snapshot editor state         ✅
MidiEventSerializer::serialize()  →  Serialize selected events  ✅
        ↓
Build prompt:  system_prompt + context + events + user_message   ✅
        ↓
AiClient::sendRequest(prompt)                                    ✅
        ↓
Response received (JSON)                                         ✅
        ↓
Strip markdown fencing (```json ... ```)                         ✅
Validate JSON schema                                             ✅
        ↓
MidiEventSerializer::deserialize()  →  Create MidiEvent objects  ✅
        ↓
Protocol::startNewAction("MidiPilot: <description>")             ✅
Apply changes (insert/remove/modify events)                      ✅
Protocol::endAction()                                            ✅
        ↓
Display result in chat + update MatrixWidget                     ✅
```

### 3.2 — Supported Operations (Examples) ✅

All operations below are supported via the AI action system (edit/delete/info/error/create_track/rename_track/set_channel):

| User says                          | AI action                                | Status |
|------------------------------------|------------------------------------------|--------|
| "Delete every other note"          | Remove alternating events from selection | ✅ via `delete` action |
| "Transpose up a third"            | Note + 4 semitones (or diatonic third)   | ✅ via `edit` action |
| "Double the velocity"             | Velocity × 2, clamped to 127             | ✅ via `edit` action |
| "Add harmony in thirds"           | Duplicate notes at +4 semitones          | ✅ via `edit` action |
| "Make an arpeggio"                | Spread chord notes across time           | ✅ via `edit` action |
| "Invert the melody"              | Mirror notes around axis                  | ✅ via `edit` action |
| "Quantize to 1/8"                | Snap timing to eighth-note grid           | ✅ via `edit` action |
| "Humanize slightly"              | Random tick/velocity variation            | ✅ via `edit` action |
| "Create a bass line"             | Generate new events based on harmony      | ✅ via `edit` action |
| "What chords are these?"         | Analyze and respond (no editing)          | ✅ via `info` action |
| "Move these to track 2"          | Change track assignment                   | ✅ via `move_to_track` action |
| "Fill measure 5 with a pattern"  | Generate events in specific measure       | ✅ via `edit` action |
| "Create a drum track"            | Create new track with channel assignment  | ✅ via `create_track` |
| "Rename track 2 to Lead Synth"   | Rename existing track                     | ✅ via `rename_track` |
| "Set track 1 to channel 5"       | Change track channel assignment           | ✅ via `set_channel` |

### 3.3 — Undo/Redo (Automatic) ✅
- ✅ Every AI edit is wrapped in `Protocol::startNewAction()` / `endAction()`
- ✅ Shows as "MidiPilot: \<description\>" in the Protocol widget
- ✅ Standard `Ctrl+Z` undoes the entire AI action
- ✅ No separate undo system needed — leverages existing infrastructure

### 3.4 — Multi-Turn Conversation ✅
- ✅ Conversation history is sent as prior messages to the API
- ✅ User can refine: "No, make those sixths instead of thirds"
- ✅ Context re-captured on each message (reflects latest editor state)
- ✅ Conversation resets when user clicks "New Chat" (➕ button)
- ⬜ Conversation resets on file change — *not implemented, only refreshes context*

---

## Phase 4: Advanced Features (Later)

### 4.1 — Extended Context ✅
- ✅ Send surrounding events (not just selection) for better musical context — *±N measures around cursor, per-track*
- ✅ Include all tracks as high-level summary (track names, instruments, note ranges) — *track names + channels included*
- ✅ Configurable context window: ±N measures around cursor — *QSpinBox in Settings, default ±5, range 0-50*
- ✅ Token budget warning when surrounding context exceeds ~500 events

### 4.2 — Pattern Generation ✅ (via prompt)
- ✅ "Create a 4-bar drum pattern in 4/4" — *supported via edit action + create_track*
- ✅ "Add a walking bass line" — *supported via edit action*
- ✅ "Create a variation of these 8 bars" — *supported via edit action*
- ✅ User selects target track/channel for new events — *track/channel context sent*

### 4.3 — Analysis Mode (Read-Only) ✅
- ✅ "What chords are in measures 1-8?" — *via info action*
- ✅ "What key is this in?" — *via info action*
- ✅ "Are there any timing issues?" — *via info action*
- ✅ Informational output only — no event modification

### 4.4 — Tempo & Time Signature Actions ✅
- ✅ `set_tempo` action: AI can change BPM (create/modify TempoChangeEvent)
  - Supports setting BPM at any tick position (tempo changes mid-song)
  - Modifies existing tempo event at tick, or creates new one
  - Handle in `onResponseReceived()` → `applyTempoAction()`
- ✅ `set_time_signature` action: AI can change time signature (e.g. 4/4 → 3/4 → 6/8)
  - Supports setting time signature at any tick position
  - Converts musical denominator (4=quarter, 8=eighth) to MIDI power-of-2
  - Handle in `onResponseReceived()` → `applyTimeSignatureAction()`
- ✅ Update system prompt with new action definitions
- ✅ Fixed `captureTimeSignature` to send actual musical denominator instead of MIDI power-of-2
- **Context already available:** BPM and time signature are already sent to the AI (read-only)

### 4.5 — AI Self-Selection (Query & Modify by Range) ✅
- ✅ `select_and_edit` action: AI can specify a tick range + track to query events, then replace them
  - AI sends: `{"action": "select_and_edit", "trackIndex": 1, "startTick": 0, "endTick": 3840, "events": [...]}`
  - Removes all MIDI events on the track in the range, then inserts new events
  - Enables: "transpose measures 3-5 on track 1 up a fifth" without user pre-selecting
  - Newly created events are auto-selected
- ✅ `select_and_delete` action: AI can delete events in a specified range without user selection
  - AI sends: `{"action": "select_and_delete", "trackIndex": 1, "startTick": 1920, "endTick": 3840}`
  - Deletes all MIDI events on the specified track within the range
  - Skips OffEvents and meta channel events (tempo, time sig)
- ✅ Enables multi-step autonomous workflows (AI reasons about context, then acts on specific regions)
- ✅ Update system prompt with new action definitions + autonomous editing guidance

### 4.6 — Persistent History (SQLite) ⬜
- ⬜ Store conversation history across sessions
- ⬜ Associate conversations with MIDI files
- ⬜ Searchable history
- ⬜ Export/import conversation logs

### 4.7 — Model Configuration ✅ (partial)
- ✅ Simple operations → cheaper model (gpt-4o-mini)
- ✅ Complex generation → stronger model (gpt-4o, gpt-5, o4-mini, etc.)
- ✅ Token count display in status bar
- ⬜ Local LLM support (Ollama) as alternative backend

---

## Phase 5: Dual-Mode Architecture (Simple + Agent)

> **Goal:** Add a new "Agent" mode alongside the existing "Simple" mode — selectable
> via a dropdown in the chat footer, similar to VS Code Copilot's Ask/Agent modes.
> Simple Mode stays untouched. Agent Mode reuses the same action handlers as tools.

### Design Principle: Reuse, Don't Replace

```
┌─────────────────────────────────────────────────────────────────────┐
│                     Existing Action Handlers                        │
│  applyAiEdits() · applyTrackAction() · applyTempoAction() · ...   │
│  applySelectAndEdit() · applySelectAndDelete() · dispatchAction()  │
└──────────────────────────┬──────────────────────────────────────────┘
                           │
              ┌────────────┴────────────┐
              │                         │
     ┌────────▼─────────┐    ┌─────────▼──────────┐
     │   Simple Mode     │    │   Agent Mode        │
     │   (current)       │    │   (new)              │
     │                   │    │                      │
     │ • JSON response   │    │ • OpenAI tools API   │
     │ • dispatchAction()│    │ • tool_calls loop    │
     │ • Multi-action    │    │ • executeTool() →    │
     │ • One-shot        │    │   same handlers      │
     │ • No tool results │    │ • Returns results    │
     │ • System prompt   │    │ • Agent prompt       │
     │   with JSON format│    │   (shorter, tools    │
     │   documentation   │    │    are self-doc)     │
     └──────────────────┘    └──────────────────────┘
```

**Key decisions:**
- ✅ Simple Mode is **never modified** — it keeps working exactly as today
- ✅ All existing handler methods stay in `MidiPilotWidget` — Agent Mode wraps them
- ✅ `ToolExecutor` calls the same `applyTrackAction()`, `applyTempoAction()`, etc.
- ✅ Mode is selected per-chat via dropdown, remembered in QSettings
- ✅ Conversation history format differs per mode, but both use `_conversationHistory`

### Mode Comparison

| Aspect | Simple Mode ✅ | Agent Mode ✅ |
|--------|---------------|--------------|
| API format | JSON in `content` | OpenAI `tools` parameter |
| Response | Single/multi `action` objects | `tool_calls` array |
| Execution | `dispatchAction()` per action | `executeTool()` → same handlers |
| Planning | LLM plans everything upfront | LLM plans step-by-step |
| Error handling | No recovery | LLM retries on error |
| Context | Initial snapshot only | Can query mid-task |
| Track indices | Must guess new index | Receives actual index |
| System prompt | Large (all JSON format docs) | Small (music rules only) |
| Progress UI | "Thinking..." | Step-by-step indicators |
| Undo | One action per response | One action per agent run |
| Best for | Quick edits, analysis | Complex multi-step tasks |
| Models | All models | Models with function calling |

### UI: Mode Selector

```
┌─────────────────────────────────────────┐
│  📋 Context: Track 1 · Ch 0 · Meas 2   │
├─────────────────────────────────────────┤
│                                         │
│  Chat messages...                       │
│                                         │
├─────────────────────────────────────────┤
│  ┌─────────────────────────────────┐    │
│  │ Type your message...            │    │
│  │                                 │    │
│  └─────────────────────────────────┘    │
│  [Simple ▾]              [Send] [➕]    │
│                                         │
│  ● Connected · gpt-5 · ⚙                │
└─────────────────────────────────────────┘
```

- `QComboBox` in the input row: **Simple** / **Agent**
- Default: Simple (current behavior, no change for existing users)
- Setting persisted: `QSettings("AI/mode", "simple")`
- Mode change mid-conversation: starts a new chat (different prompt format)
- Tooltip: "Simple: single-shot edits · Agent: multi-step autonomous editing"

### 5.1 — Mode Selector UI ✅

Add mode dropdown to MidiPilotWidget footer:

```cpp
// In setupUi(), input row:
_modeCombo = new QComboBox();
_modeCombo->addItem("Simple", "simple");
_modeCombo->addItem("Agent", "agent");
// Place left of Send button, same row as input
```

- Mode selection triggers `onModeChanged()` → new chat if conversation exists
- `currentMode()` returns `"simple"` or `"agent"`
- `onSendMessage()` checks mode → routes to existing pipeline or agent pipeline

**Files:** Modify `MidiPilotWidget.h/cpp`

### 5.2 — Tool Definitions (Wrapping Existing Handlers) ✅

Create tool schemas that map 1:1 to existing action handlers:

```cpp
// src/ai/ToolDefinitions.h
class ToolDefinitions {
public:
    // Returns the OpenAI tools array for the API request
    static QJsonArray toolSchemas();

    // Executes a tool call by delegating to existing handlers
    // Returns a JSON result object to send back to the LLM
    static QJsonObject executeTool(const QString &toolName,
                                   const QJsonObject &args,
                                   MidiFile *file,
                                   MidiPilotWidget *widget);
};
```

**Mapping — existing handlers → tools:**

| Tool name | Delegates to | Return value |
|-----------|-------------|-------------|
| `query_events` | `MidiEventSerializer::serialize()` | `{events: [...], count}` |
| `get_editor_state` | `EditorContext::captureState()` | Full state JSON |
| `get_track_info` | `EditorContext::captureState()` (filtered) | `{name, channel, eventCount}` |
| `create_track` | `applyTrackAction()` (existing) | `{trackIndex, success}` |
| `rename_track` | `applyTrackAction()` (existing) | `{success}` |
| `set_channel` | `applyTrackAction()` (existing) | `{success}` |
| `insert_events` | `MidiEventSerializer::deserialize()` | `{inserted, success}` |
| `replace_events` | `applySelectAndEdit()` (existing) | `{removed, inserted, success}` |
| `delete_events` | `applySelectAndDelete()` (existing) | `{deleted, success}` |
| `set_tempo` | `applyTempoAction()` (existing) | `{success, previousBpm}` |
| `set_time_signature` | `applyTimeSignatureAction()` (existing) | `{success}` |
| `move_events_to_track` | `applyMoveToTrack()` (existing) | `{moved, success}` |

**Read-only tools (new, no existing handler):**
```
query_events(trackIndex, startTick, endTick)
  → MidiEventSerializer::serialize() on filtered events
  → {events: [...], count: int}

get_editor_state()
  → EditorContext::captureState()
  → Full editor state as JSON

get_track_info(trackIndex)
  → Track-specific subset of captureState()
  → {name, channel, eventCount, noteRange}
```

**Write tools wrap existing handlers** but return result JSON instead of showing chat bubbles.
The existing handler methods need minor refactoring to return success/failure info:

```cpp
// Before (void, shows chat bubble on error):
void MidiPilotWidget::applyTrackAction(const QJsonObject &response);

// After (returns result, handler works for both modes):
QJsonObject MidiPilotWidget::applyTrackAction(const QJsonObject &response);
// Returns: {"success": true, "trackIndex": 3} or {"success": false, "error": "..."}
// Simple Mode: caller shows chat bubble based on result
// Agent Mode: caller sends result JSON back to LLM
```

**Files:** `src/ai/ToolDefinitions.h/cpp`

### 5.3 — Agent Loop Engine ✅

The agent loop that replaces the single-shot flow **only when Agent Mode is active**:

```cpp
// src/ai/AgentRunner.h
class AgentRunner : public QObject {
    Q_OBJECT
public:
    explicit AgentRunner(AiClient *client, QObject *parent = nullptr);

    void run(const QString &systemPrompt,
             const QJsonArray &conversationHistory,
             const QString &userMessage,
             MidiFile *file,
             MidiPilotWidget *widget);

    void cancel();
    bool isRunning() const;

signals:
    void stepStarted(int stepNumber, const QString &toolName);
    void stepCompleted(int stepNumber, const QString &toolName, const QJsonObject &result);
    void finished(const QString &finalMessage);
    void errorOccurred(const QString &error);

private:
    void onApiResponse(const QString &content, const QJsonObject &fullResponse);
    void processToolCalls(const QJsonArray &toolCalls);
    void sendNextRequest();

    AiClient *_client;
    MidiFile *_file;
    MidiPilotWidget *_widget;
    QJsonArray _messages;      // Running conversation (incl. tool results)
    int _maxSteps = 15;
    int _currentStep = 0;
    bool _cancelled = false;
};
```

**Flow in MidiPilotWidget::onSendMessage():**
```cpp
if (currentMode() == "agent") {
    // Agent Mode: use AgentRunner
    _file->protocol()->startNewAction("MidiPilot: " + userMessage);
    _agentRunner->run(agentSystemPrompt, _conversationHistory, userMessage, _file, this);
    // ... stepStarted/stepCompleted signals update UI ...
    // ... finished signal → endAction() + show final message
} else {
    // Simple Mode: existing pipeline (unchanged)
    _client->sendRequest(systemPrompt, _conversationHistory, userMessage);
}
```

**API request includes tools:**
```json
{
  "model": "gpt-5",
  "messages": [...],
  "tools": [
    {
      "type": "function",
      "function": {
        "name": "create_track",
        "description": "Create a new MIDI track with the specified name and channel",
        "parameters": {
          "type": "object",
          "properties": {
            "name": {"type": "string", "description": "Track name"},
            "channel": {"type": "integer", "minimum": 0, "maximum": 15}
          },
          "required": ["name", "channel"]
        }
      }
    },
    ...
  ]
}
```

**Response with tool_calls (agent iterates):**
```json
{
  "choices": [{
    "message": {
      "role": "assistant",
      "tool_calls": [{
        "id": "call_abc123",
        "type": "function",
        "function": {
          "name": "create_track",
          "arguments": "{\"name\": \"Bass\", \"channel\": 1}"
        }
      }]
    },
    "finish_reason": "tool_calls"
  }]
}
```

**Tool result sent back to LLM:**
```json
{
  "role": "tool",
  "tool_call_id": "call_abc123",
  "content": "{\"trackIndex\": 3, \"success\": true}"
}
```

**Loop terminates when:**
1. LLM responds with `content` (text message, no tool_calls) → show as assistant message
2. Max steps reached → show warning + partial results
3. User cancels → abort + undo
4. API error → show error + undo

**Files:** `src/ai/AgentRunner.h/cpp`

### 5.4 — Agent System Prompt ✅

Separate system prompt for Agent Mode (shorter — tools are self-documenting):

```
You are MidiPilot, an AI assistant embedded in MidiEditor.
You have tools to query and modify MIDI data. Use them step by step.

Workflow:
1. Use get_editor_state or query_events to understand the current state
2. Use write tools (create_track, insert_events, etc.) to make changes
3. Respond with a summary of what you did

Music conventions:
- Note 60 = Middle C = C4
- Velocity 0-127, Note 0-127
- Ticks depend on ticksPerQuarter (see editor state)
- Channel 9 = drums (GM standard)
- Always create a track before inserting events into it
```

**No JSON format documentation needed** — tool schemas define the format.

**Files:** New method `EditorContext::agentSystemPrompt()` (alongside existing `systemPrompt()`)

### 5.5 — UI: Step-by-Step Progress ✅

In Agent Mode, replace "Thinking..." with live step indicators:

```
┌─────────────────────────────────────┐
│ You: Create a bass track with a     │
│      walking bass line              │
│                                     │
│  ╭──────────────────────────────╮   │
│  │ 🔍 Step 1: get_editor_state  │   │
│  │ ✅ Step 2: create_track      │   │
│  │   → Created "Bass" (ch 1)   │   │
│  │ 🔄 Step 3: insert_events    │   │
│  │   → Generating...           │   │
│  ╰──────────────────────────────╯   │
│                                     │
│ MidiPilot: Done! Created a walking  │
│ bass line on Track 3 following the  │
│ chord progression Cmaj7-Am7-Dm7-G7  │
└─────────────────────────────────────┘
```

- Steps widget is a collapsible section in the chat
- Each step shows: icon (🔍/✅/❌/🔄) + tool name + brief result
- Final LLM text message shown below as normal assistant bubble
- Status bar shows: "Agent: Step 2/15..."
- In Simple Mode: no change — still shows "Thinking..." as before

**Signals from AgentRunner:**
```cpp
connect(_agentRunner, &AgentRunner::stepStarted, this, &MidiPilotWidget::onAgentStepStarted);
connect(_agentRunner, &AgentRunner::stepCompleted, this, &MidiPilotWidget::onAgentStepCompleted);
connect(_agentRunner, &AgentRunner::finished, this, &MidiPilotWidget::onAgentFinished);
```

**Files:** Modify `MidiPilotWidget.h/cpp` — new slots, step widget

### 5.6 — Protocol Integration (Single Undo) ✅

All tool calls in one Agent Mode run → one undo action:

```cpp
// In MidiPilotWidget::onSendMessage() (agent branch):
_file->protocol()->startNewAction("MidiPilot Agent: " + userMessage);

// AgentRunner executes tools inside this action block...
// Each tool call modifies MIDI data within the same protocol action

// In onAgentFinished():
_file->protocol()->endAction();
// → Single Ctrl+Z undoes the ENTIRE multi-step agent operation
```

Simple Mode continues to use per-action protocol wrapping (unchanged).

### 5.7 — Handler Refactoring (Return Results) ✅

Minor refactoring of existing handler methods to return result info:

```cpp
// Current (Simple Mode — void, shows bubbles):
void applyTrackAction(const QJsonObject &response);

// Refactored (returns result, both modes use it):
QJsonObject applyTrackAction(const QJsonObject &response, bool showBubbles = true);
```

- `showBubbles = true` (Simple Mode, default) → existing behavior, shows chat bubbles
- `showBubbles = false` (Agent Mode) → silent execution, result returned as JSON
- **Minimal change** — add return type + optional parameter, keep all logic intact
- `ToolExecutor::executeTool()` calls handlers with `showBubbles = false`

**Affected methods:**
- `applyTrackAction()` → returns `{success, trackIndex?}`
- `applyTempoAction()` → returns `{success, previousBpm}`
- `applyTimeSignatureAction()` → returns `{success}`
- `applySelectAndEdit()` → returns `{removed, inserted, success}`
- `applySelectAndDelete()` → returns `{deleted, success}`
- `applyMoveToTrack()` → returns `{moved, success}`
- `applyAiEdits()` → returns `{inserted, success}`

### Architecture Example: Full Agent Run

```
User: "Create a bass track with a walking bass line that follows the chords"

┌─ Agent Loop (all inside one Protocol action) ──────────────────────┐
│                                                                     │
│  Step 1: LLM Call (with tool definitions)                          │
│  ← LLM: tool_call: get_editor_state()                             │
│  → ToolExecutor::executeTool("get_editor_state", {}, file, widget) │
│  → Result: {tracks: [...], tempo: 120, timeSignature: "4/4", ...}  │
│  ────────────────────────────────────────────────────────────────── │
│  Step 2: LLM Call (sees editor state)                              │
│  ← LLM: tool_call: query_events(track=1, start=0, end=7680)      │
│  → ToolExecutor: serialize events from track 1                     │
│  → Result: {events: [42 notes from melody], count: 42}             │
│  ────────────────────────────────────────────────────────────────── │
│  Step 3: LLM Call (analyzed melody, knows chords)                  │
│  ← LLM: tool_call: create_track(name="Bass", channel=1)           │
│  → ToolExecutor → applyTrackAction({...}, showBubbles=false)       │
│  → Result: {trackIndex: 6, success: true}                          │
│  ────────────────────────────────────────────────────────────────── │
│  Step 4: LLM Call (knows track index = 6)                          │
│  ← LLM: tool_call: insert_events(trackIndex=6, events=[...48...]) │
│  → ToolExecutor → MidiEventSerializer::deserialize()               │
│  → Result: {inserted: 48, success: true}                           │
│  ────────────────────────────────────────────────────────────────── │
│  Step 5: LLM Call (all done)                                       │
│  ← LLM: content: "Done! Created a walking bass line on Track 6..."│
│  → Show as assistant message in chat                               │
│                                                                     │
└──── Ctrl+Z undoes ALL of steps 3+4 in one action ──────────────────┘
```

### Implementation Order

```
Phase 5.1  Mode Selector UI (dropdown)                  ✅ DONE
Phase 5.2  Tool Definitions (schemas + executor)        ✅ DONE
Phase 5.3  Agent Loop Engine (AgentRunner)              ✅ DONE
Phase 5.4  Agent System Prompt                          ✅ DONE
Phase 5.5  UI: Step progress display                    ✅ DONE
Phase 5.6  Protocol integration (single undo)           ✅ DONE
Phase 5.7  Handler refactoring (return results)         ✅ DONE
```

### Estimated Complexity

| Sub-phase | New Code | Modified Code | Risk |
|-----------|----------|---------------|------|
| 5.1 Mode Selector UI | ~30 lines | MidiPilotWidget (~20 lines) | Low |
| 5.2 Tool Definitions | ~350 lines | — | Low — schemas + delegation |
| 5.3 Agent Loop Engine | ~250 lines | AiClient (~30 lines, add tools param) | Medium — async loop |
| 5.4 Agent System Prompt | ~60 lines | EditorContext (~10 lines, new method) | Low |
| 5.5 UI Step Progress | ~100 lines | MidiPilotWidget (~60 lines) | Low |
| 5.6 Protocol Integration | ~15 lines | MidiPilotWidget | Low |
| 5.7 Handler Refactoring | ~0 lines | MidiPilotWidget (~80 lines, add returns) | Low — backward compatible |

---

## Implementation Order

```
Phase 1.1  AiClient (HTTP + OpenAI API)                ✅ DONE
Phase 1.2  EditorContext (state snapshot)               ✅ DONE
Phase 1.3  MidiEventSerializer (MIDI ↔ JSON)           ✅ DONE
Phase 1.4  AiSettingsWidget (API key config)            ✅ DONE
Phase 2.1  MidiPilotWidget (sidebar UI)                 ✅ DONE
Phase 2.2  Toolbar & menu integration                   ✅ DONE
Phase 2.3  First-run setup flow                         ✅ DONE (partial — redirects to Settings)
Phase 3.1  Request/Response pipeline                    ✅ DONE
Phase 3.2  Test with first operations                   ✅ DONE
Phase 3.3  Undo/Redo verification                       ✅ DONE
Phase 3.4  Multi-turn conversation                      ✅ DONE
Phase 4.1  Extended context                             ✅ DONE (±N measures, configurable in Settings)
Phase 4.2  Pattern generation                           ✅ DONE (via prompt)
Phase 4.3  Analysis mode                                ✅ DONE
Phase 4.4  Tempo & time signature actions               ✅ DONE (set_tempo, set_time_signature)
Phase 4.5  AI self-selection (query & modify by range)  ✅ DONE (select_and_edit, select_and_delete)
Phase 4.7  Model configuration                         ✅ DONE (partial — no Ollama/token count)
Phase 5.1  Mode Selector UI (dropdown)                  ✅ DONE
Phase 5.2  Tool Definitions (schemas + executor)        ✅ DONE
Phase 5.3  Agent Loop Engine (AgentRunner)              ✅ DONE
Phase 5.4  Agent System Prompt                          ✅ DONE
Phase 5.5  UI: Step progress display                    ✅ DONE
Phase 5.6  Protocol integration (single undo)           ✅ DONE
Phase 5.7  Handler refactoring (return results)         ✅ DONE
Phase 6    Post-launch hardening & polish               ✅ DONE (6.1-6.9)
Phase 7    FFXIV Bard Performance Mode                   ✅ DONE (7.1-7.5)
Phase 4.6  Persistent history (SQLite)                  ⬜ TODO  (low priority)
Phase 8    Multi-provider & free API access              ✅ DONE (8.1, 8.2, 8.5 — providers + tokens + model lists)
Phase 9    Editable system prompts (JSON + dialog)       ⬜ TODO  (planned)
```

### Bonus Features (not in original plan)
- ✅ Reasoning model support (GPT-5, o4-mini, o3-mini, etc.)
- ✅ `reasoning_effort` parameter for Chat Completions API
- ✅ API debug logging (`midipilot_api.log`)
- ✅ Markdown JSON fencing strip (`\`\`\`json ... \`\`\``)
- ✅ Track management actions (`create_track`, `rename_track`, `set_channel`, `move_to_track`)
- ✅ Per-event and response-level `track` targeting for `edit` action
- ✅ `delete` action with `deleteIndices` (targeted deletion)
- ✅ Test connection with `testConnection()` in settings
- ✅ `focusInput()` for Ctrl+I auto-focus
- ✅ Surrounding events context with configurable ±N measures range
- ✅ Token budget warning for large context- ✅ Model/effort selector in chat footer (quick switching without opening Settings)
- ✅ Steps pre-display (show all planned tool calls before execution)
- ✅ Transfer timeout 600s for reasoning models
- ✅ Strict mode on all 12 tool schemas- ✅ Tempo & time signature modification actions (`set_tempo`, `set_time_signature`)
- ✅ Range-based autonomous editing (`select_and_edit`, `select_and_delete`)
- ✅ Fixed time signature display (actual denominator instead of MIDI power-of-2)
- ✅ Multi-provider support (OpenAI, OpenRouter, Google Gemini, Custom URL)
- ✅ Google Gemini native API integration (not just via OpenRouter)
- ✅ Token usage display (last request / session totals)
- ✅ Provider-specific model dropdowns (per-provider model lists)
- ✅ FFXIV Bard Performance mode (toggle, prompts, validation, drum conversion)

---

## Phase 6: Post-Launch Hardening & Polish ✅

Iterative improvements based on real-world testing after Phase 5 completion.

### 6.1 — Agent Mode Bug Fixes ✅
- ✅ **Duplicate user message:** Fixed agent mode sending user message twice (once in history, once appended)
- ✅ **Empty events validation:** Added recoverable error with `"recoverable": true` flag when model omits events array — enables automatic retry instead of hard failure
- ✅ **HTTP 400 on re-send:** Stripped response-only fields (`annotations`, `refusal`) from assistant messages before appending to conversation history — API rejected extra fields
- ✅ **Selection leak:** Agent tool results no longer leave stale MidiEvent selection in the editor

### 6.2 — Musical Quality Improvements ✅
- ✅ **Channel derivation:** `insert_events` now derives channel from track's `assignedChannel()` when not specified, falling back to `NewNoteTool::editChannel()`
- ✅ **Instrument display:** Fixed instruments showing as "Acoustic Grand Piano" for all tracks — now correctly reads `ProgChangeEvent` from track data
- ✅ **Compact note format:** Introduced `note` event type with `duration` field (replaces paired `note_on`/`note_off`), dramatically reducing token usage
- ✅ **`note_on`/`note_off` backward compat:** Deserializer still accepts legacy paired format, but schema exclusively promotes compact `note` type
- ✅ **Musical coherence in agent mode:** Added `buildMusicalSummary()` — tool results now include `pitchClasses`, `noteRange`, `tickRange`, `gmProgram`, `noteCount`, `ccCount` so subsequent agent steps can compose harmonically coherent parts
- ✅ **System prompt guidance:** Added best practice instruction: "When composing multiple tracks, CHECK the 'summary' field in previous tool results"

### 6.3 — API Robustness ✅
- ✅ **`reasoning_effort` parameter:** Correctly sent for reasoning models (gpt-5, o-series) — configurable in settings (low/medium/high)
- ✅ **Better error messages:** Error responses from the API now include `recoverable` flag — UI shows ⚠️ warning for recoverable errors vs ❌ for fatal ones
- ✅ **Connection test limits:** `testConnection()` uses minimal `max_completion_tokens` (64 normal, 16 reasoning) to avoid wasting tokens; normal requests send NO token limit

### 6.4 — Incremental Visual Updates ✅
- ✅ **Live editor refresh during agent runs:** Fixed MatrixWidget not updating during agent tool execution — was missing `registerRelayout()` call to invalidate cached pixmap. Now `requestRepaint` handler calls `mw_matrixWidget->registerRelayout()` + updates all three widget areas (matrix, misc, track sidebar)

### 6.5 — Strict Mode (Structured Outputs) ✅
- ✅ **`strict: true` on all 12 tool schemas:** Guarantees the model's output always conforms to the JSON schema — the `events` array can physically never be omitted again
- ✅ **`additionalProperties: false`** on all parameter objects and event sub-schemas
- ✅ **Discriminated union for events:** `anyOf` schema with 4 variants (`note`, `cc`, `pitch_bend`, `program_change`), each fully typed with all properties required
- ✅ **Removed `minimum`/`maximum` constraints:** Not supported in strict mode — range info moved to description text (e.g., "MIDI channel (0-15)")
- ✅ **Previously optional params now required:** `insert_events.channel`, `set_tempo.tick`, `set_time_signature.tick` — all must be explicitly provided (eliminates ambiguity)

### 6.6 — Timeout & Transfer Robustness ✅
- ✅ **Transfer timeout 600s for reasoning models:** Prevents `OperationCanceledError` on GPT-5 with high reasoning effort (was 120s → 600s)
- ✅ **Timeout error message:** Shows "Request was cancelled or timed out. Try a lower reasoning effort." instead of silent failure

### 6.7 — Model/Effort Selector in Chat Footer ✅
- ✅ **Model combo in footer:** `QComboBox` with 8 presets (GPT-5, GPT-4o, o4-mini, etc.) + custom entry, synced bidirectionally with Settings
- ✅ **Effort combo in footer:** Low/Medium/High reasoning effort selector, only visible for reasoning models
- ✅ **Auto-enable thinking:** Changing effort level automatically enables thinking for reasoning models
- ✅ **Settings sync:** Footer combos and Settings dialog stay in sync — changes in either propagate

### 6.8 — SSE Streaming Removed ✅
- ✅ **Removed all SSE streaming code:** Streaming caused 3 rounds of crashes (race conditions, use-after-free) and GPT-5 reasoning tokens are not visible via Chat Completions API anyway
- ✅ **Stripped from 6 files:** AiClient.h/.cpp, MidiPilotWidget.h/.cpp, AiSettingsWidget.h/.cpp
- ✅ **Kept:** Model/effort footer selector (works perfectly without streaming)

### 6.9 — Steps Pre-Display ✅
- ✅ **`stepsPlanned` signal:** AgentRunner extracts all tool_calls from an API response and emits their names upfront before executing any
- ✅ **Pre-display in UI:** All planned steps appear at once as ⏳ (pending), then each turns 🔄 (active, blue) while executing, then ✅/⚠/❌ on completion
- ✅ **Batch-aware:** Each API round-trip can add a new batch of planned steps

---

## File Structure (New Files) ✅

```
src/
├── ai/                              ✅ CREATED
│   ├── AiClient.h/cpp              # OpenAI API communication
│   ├── EditorContext.h/cpp          # Editor state capture & serialization
│   ├── MidiEventSerializer.h/cpp   # MIDI events ↔ JSON conversion
│   ├── ToolDefinitions.h/cpp       # ✅ Phase 5.2 — Tool schemas + executor (wraps existing handlers)
│   └── AgentRunner.h/cpp           # ✅ Phase 5.3 — Agent loop engine (tool-calling loop)
└── gui/
    ├── MidiPilotWidget.h/cpp        # ✅ Sidebar chat panel
    └── AiSettingsWidget.h/cpp       # ✅ Settings tab for AI config

run_environment/graphics/tool/
    └── midipilot.png                # ✅ Toolbar icon (21×21 robot)
```

---

## Technical Notes

### System Prompt (Draft)

```
You are MidiPilot, an AI assistant embedded in MidiEditor.
You receive the current editor state and selected MIDI events as JSON.
Your job is to transform, analyze, or generate MIDI events based on the user's request.

Rules:
- For editing operations: respond with ONLY a JSON object matching the response schema
- For analysis/questions: respond with a plain text explanation
- Note values: 0-127 (middle C = 60, C4)
- Velocity: 0-127
- Tick positions must align with the ticksPerQuarter resolution
- If deleting events: return only the events to KEEP
- If adding events: include both original and new events
- Use the provided context (track, channel, measure, tempo) to make musically sensible decisions
- The user may refer to tracks by name or number, channels by number, measures by number
- When the user says "here" they mean the current cursor position / measure
```

### JSON Response Schema

```json
{
  "action": "edit",
  "events": [
    {
      "id": 0,
      "type": "note",
      "tick": 480,
      "note": 60,
      "velocity": 100,
      "duration": 192,
      "channel": 0
    }
  ],
  "explanation": "Brief description of what was done"
}
```

**Action types:**
- ✅ `"edit"` — modify/add/remove events (returns full event list)
- ✅ `"delete"` — delete specific events by index (`deleteIndices` array)
- ✅ `"info"` — analysis only, no event changes (explanation field only)
- ✅ `"error"` — could not process request (explanation describes why)
- ✅ `"create_track"` — create a new track with name and optional channel
- ✅ `"rename_track"` — rename an existing track by index
- ✅ `"set_channel"` — change a track's channel assignment
- ✅ `"move_to_track"` — move selected events to a different track

### Risks & Mitigations

| Risk | Mitigation |
|------|-----------|
| Invalid JSON from LLM | JSON validation + retry with error feedback |
| Invalid note/velocity values | Clamp to valid ranges, reject impossible values |
| High API latency | Async requests, loading indicator, timeout handling |
| Token cost with many events | Limit events per request, compact format, model selection |
| API key security | Stored locally via QSettings (OS keychain), masked in UI |
| Network errors | Retry logic, offline detection, clear error messages |
| LLM modifies wrong events | ID-based event matching, verify before applying |

---

## Phase 7: FFXIV Bard Performance Mode ✅

> **Goal:** Add a "FFXIV Mode" toggle to MidiPilot that injects FFXIV Bard Performance
> constraints into the AI context. When enabled, the AI automatically follows the game's
> music system rules — correct instrument ranges, monophonic voicing, drum separation,
> track naming, and ensemble limits.

### Background

FFXIV's Bard Performance system has strict technical constraints:
- **Max 8 tracks** for a full ensemble (octet)
- **Monophonic per track** — one note at a time (chords technically work but sound bad with most instruments)
- **Universal safe range: C3-C6** — all instruments accept notes in C3-C6 (MIDI 48-84). MidiBard2 auto-transposes to each instrument's native range. This is the **recommended default approach**.
- **Extended ranges via track name suffix** — if you need an instrument's full native range (e.g., Tuba C1-C4), append `+N` or `-N` to the track name (e.g., `Tuba+2`). MidiBard2 shifts the octave accordingly. This is optional and only needed for advanced arrangements.
- **No drum kit** — percussion instruments are individual tonal tracks
- **Track names must match MidiBard2 naming** for automatic instrument switching

### Instrument Ranges (from MidiBard2)

| Instrument | Track Name | MIDI Range | Transpose |
|------------|-----------|------------|-----------|
| Piano | Piano | C4-C7 (60-96) | -1 octave |
| Harp | Harp | C3-C6 (48-84) | native |
| Fiddle | Fiddle | C2-C5 (36-72) | +1 octave |
| Lute | Lute | C2-C5 (36-72) | +1 octave |
| Fife | Fife | C5-C8 (72-108) | -2 octaves |
| Flute | Flute | C4-C7 (60-96) | -1 octave |
| Oboe | Oboe | C4-C7 (60-96) | -1 octave |
| Panpipes | Panpipes | C4-C7 (60-96) | -1 octave |
| Clarinet | Clarinet | C3-C6 (48-84) | native |
| Trumpet | Trumpet | C3-C6 (48-84) | native |
| Saxophone | Saxophone | C3-C6 (48-84) | native |
| Trombone | Trombone | C2-C5 (36-72) | +1 octave |
| Horn | Horn | C2-C5 (36-72) | +1 octave |
| Tuba | Tuba | C1-C4 (24-60) | +2 octaves |
| Violin | Violin | C3-C6 (48-84) | native |
| Viola | Viola | C3-C6 (48-84) | native |
| Cello | Cello | C2-C5 (36-72) | +1 octave |
| Double Bass | Double Bass | C1-C4 (24-60) | +2 octaves |
| Timpani | Timpani | C2-C5 (36-72) | +1 octave |
| Bongo | Bongo | C3-C6 (48-84) | native |
| Bass Drum | Bass Drum | C3-C6 (48-84) | native |
| Snare Drum | Snare Drum | C3-C6 (48-84) | native |
| Cymbal | Cymbal | C3-C6 (48-84) | native |
| ElectricGuitarOverdriven | ElectricGuitarOverdriven | C2-C5 (36-72) | +1 octave |
| ElectricGuitarClean | ElectricGuitarClean | C2-C5 (36-72) | +1 octave |
| ElectricGuitarMuted | ElectricGuitarMuted | C2-C5 (36-72) | +1 octave |
| ElectricGuitarPowerChords | ElectricGuitarPowerChords | C2-C5 (36-72) | +1 octave |
| ElectricGuitarSpecial | ElectricGuitarSpecial | C3-C6 (48-84) | native |

### Drum Handling in FFXIV

Drums are NOT a drum kit — each percussion instrument is a separate tonal track:
- **Bass Drum** track: Single note on C4 (MIDI 60) — low thud
- **Snare Drum** track: Single note around C5 (MIDI 72) ±1 note for variation
- **Cymbal** track: Range C5-C6 — big crash ≈ C5, hi-hat/ride ≈ C6, china ≈ middle
- **Timpani** track: Tonal — can play actual melodic patterns
- **Bongo** track: Tonal — two-tone patterns common

When converting from GM drum tracks (channel 9), the AI must:
1. Separate kick/snare/cymbal/tom hits into individual instrument tracks
2. Map GM drum notes to appropriate FFXIV pitch on each track
3. Create separate tracks named e.g. `Bass Drum`, `Snare Drum`, `Cymbal`

### 7.1 — FFXIV Mode Toggle ✅

UI addition to MidiPilot chat panel:

```
┌─────────────────────────────────────┐
│  [Type message...]           [Send] │
│  [Agent ▾]              [☐ FFXIV]  │
│  ● Ready · gpt-5 · Med · ⚙        │
└─────────────────────────────────────┘
```

- `QCheckBox` labeled "FFXIV" in the footer bar (near mode selector)
- Stored: `QSettings("AI/ffxiv_mode", false)`
- Also configurable in AiSettingsWidget (with more detailed options)
- Tooltip: "Enable FFXIV Bard Performance mode — constrains output to game rules"

**Files:** Modify `MidiPilotWidget.h/cpp`, `AiSettingsWidget.h/cpp`

### 7.2 — FFXIV System Prompt Injection ✅

When FFXIV mode is enabled, append FFXIV-specific rules to both Simple and Agent system prompts:

```cpp
// EditorContext.cpp
QString EditorContext::ffxivContext() {
    return QStringLiteral(R"(
## FFXIV Bard Performance Mode (ACTIVE)

You are creating/editing MIDI for Final Fantasy XIV's Bard Performance system.
Follow these rules STRICTLY:

### Constraints
- Maximum 8 tracks for a full ensemble
- Each track is MONOPHONIC — only one note at a time, never overlap notes
- All notes MUST be in C3-C6 (MIDI 48-84) — MidiBard2 auto-transposes to each instrument's native range
- Track names MUST match MidiBard2 instrument names exactly
- For extended range: append +N/-N to track name (e.g., 'Tuba+2' for C1-C4 native range)

### Drum Rules
- There is NO drum kit or channel 9 mapping
- Each percussion instrument is a SEPARATE track (Bass Drum, Snare Drum, Cymbal, etc.)
- Drums are TONAL — use specific pitches:
  - Bass Drum: C4 (note 60) for the main hit
  - Snare Drum: C5 (note 72), vary ±1 for ghost notes/flams
  - Cymbal: C5 (72) = crash, C6 (84) = hi-hat, between = china/ride
  - Timpani: tonal, can play melodic bass lines
  - Bongo: tonal, use for rhythmic patterns

### Track Naming
Use EXACTLY these names: Piano, Harp, Fiddle, Lute, Fife, Flute, Oboe,
Panpipes, Clarinet, Trumpet, Saxophone, Trombone, Horn, Tuba, Violin,
Viola, Cello, Double Bass, Timpani, Bongo, Bass Drum, Snare Drum, Cymbal,
ElectricGuitarClean, ElectricGuitarMuted, ElectricGuitarOverdriven,
ElectricGuitarPowerChords, ElectricGuitarSpecial

### Composing Tips
- Avoid chords (stacked notes) — they sound bad in FFXIV's monophonic engine
- Use arpeggios instead of chords for harmonic richness
- Keep drum patterns simple — one hit at a time per drum track
- When converting songs: split GM drums into separate tracks
- Prioritize clarity over complexity — 4-5 well-voiced tracks > 8 muddy tracks
)");
}
```

- Appended to `systemPrompt()` and `agentSystemPrompt()` when `ffxivMode()` returns true
- Context is automatically included in every request

**Files:** Modify `EditorContext.h/cpp`

### 7.3 — FFXIV Validation Tool (Agent Mode) ✅

New agent tool: `validate_ffxiv` — checks if the current MIDI file conforms to FFXIV rules:

```json
{
  "name": "validate_ffxiv",
  "description": "Check if the MIDI file meets FFXIV Bard Performance constraints",
  "parameters": {}
}
```

Returns a report:
```json
{
  "valid": false,
  "issues": [
    {"track": 1, "issue": "polyphonic", "details": "3 overlapping notes at tick 1920"},
    {"track": 3, "issue": "out_of_range", "details": "Note B2 (47) below universal range C3-C6 (MIDI 48-84)"},
    {"track": 0, "issue": "track_name", "details": "Track name 'Lead' doesn't match any FFXIV instrument"},
    {"issue": "too_many_tracks", "details": "9 tracks exceed maximum of 8"}
  ],
  "summary": "4 issues found: 1 polyphonic, 1 out of range, 1 naming, 1 track count"
}
```

The AI can then use this tool to diagnose issues and fix them autonomously.

**Files:** Modify `ToolDefinitions.h/cpp`

### 7.4 — GM Drum Conversion Tool (Agent Mode) ✅

New agent tool: `convert_drums_ffxiv` — splits a GM drum track into separate FFXIV drum tracks:

```json
{
  "name": "convert_drums_ffxiv",
  "description": "Convert a GM drum track (channel 9) into separate FFXIV drum instrument tracks",
  "parameters": {
    "trackIndex": { "type": "integer", "description": "Source drum track index" }
  }
}
```

Mapping strategy:
- GM Kick (35-36) → `Bass Drum` track, note C4
- GM Snare (38, 40) → `Snare Drum` track, note C5
- GM Hi-Hat (42, 44, 46) → `Cymbal` track, note C6
- GM Crash (49, 57) → `Cymbal` track, note C5
- GM Ride (51, 59) → `Cymbal` track, note C5+1 or C5+2
- GM Toms (41, 43, 45, 47, 48, 50) → `Timpani` track, mapped tonally

**Files:** Modify `ToolDefinitions.h/cpp`

### 7.5 — FFXIV-Aware Compose Mode ✅

When FFXIV mode is active and user asks to create a song:
- AI automatically uses FFXIV instrument names for tracks
- Notes are placed within each instrument's range
- Voices are monophonic (no overlapping notes)
- Drum parts are split into separate tonal tracks
- Ensemble limited to 8 tracks

Example: "Create a jazz song for FFXIV bard ensemble" →
- Track 1: Trumpet (melody, C3-C6)
- Track 2: Saxophone (counter-melody, C3-C6)
- Track 3: Piano (comping arpeggios, C3-C6)
- Track 4: Double Bass (walking bass, C3-C6, auto-transposed to C1-C4)
- Track 5: Snare Drum (brushes, C5)
- Track 6: Cymbal (ride pattern, C5-C6)
- Track 7: Bass Drum (kick, C4)

This is handled entirely by the system prompt — no additional code needed beyond 7.2.

### Implementation Order

```
Phase 7.1  FFXIV Mode toggle (checkbox in UI)           ✅ DONE
Phase 7.2  System prompt injection (rules + ranges)      ✅ DONE
Phase 7.3  Validation tool (check FFXIV compliance)      ✅ DONE
Phase 7.4  GM Drum conversion tool                       ✅ DONE
Phase 7.5  FFXIV-aware compose (via prompt only)         ✅ DONE (free with 7.2)
```

### Estimated Complexity

| Sub-phase | New Code | Modified Code | Risk |
|-----------|----------|---------------|------|
| 7.1 Toggle UI | ~20 lines | MidiPilotWidget (~15), AiSettingsWidget (~10) | Low |
| 7.2 Prompt injection | ~80 lines | EditorContext (~10 lines, conditional append) | Low |
| 7.3 Validation tool | ~120 lines | ToolDefinitions (~30 lines) | Medium |
| 7.4 Drum conversion | ~150 lines | ToolDefinitions (~30 lines) | Medium |
| 7.5 Compose mode | 0 lines | — (handled by prompt) | None |

---

## Phase 8: Multi-Provider Support & Free API Access ✅

> **Goal:** Allow users to choose between multiple API providers — including free options —
> so MidiPilot is accessible to users who don't want to pay for an API key.
> Add token usage tracking, rate limit awareness, and provider-specific configuration.

### Background & Research

**OpenAI Rate Limits (direct API):**
- **Tiers:** Free → Tier 1 ($5 paid) → Tier 2 ($50) → Tier 3 ($100) → Tier 4 ($250) → Tier 5 ($1000+)
- **Free tier:** $100/month usage limit, geography-restricted, limited RPM/TPM
- **Measured by:** RPM (requests/min), RPD (requests/day), TPM (tokens/min), TPD (tokens/day)
- **Response headers:** `x-ratelimit-remaining-requests`, `x-ratelimit-remaining-tokens`, `x-ratelimit-reset-requests`, `x-ratelimit-reset-tokens`
- **HTTP 429** on rate limit exceeded (already caught, but no retry logic)

**Puter.com (free unlimited access):**
- Free access to OpenAI models (GPT-5.4, GPT-5.3, GPT-5.2, etc.) via "User-Pays" model
- Supports: text generation, tool/function calling, streaming, temperature, max_tokens
- **SDK:** JavaScript-only (Puter.js) — designed for web apps
- **For C++ desktop app:** Would need to reverse-engineer the REST API behind Puter.js, or embed a lightweight JavaScript bridge
- **Models available:** gpt-5.4, gpt-5.4-mini, gpt-5.4-nano, gpt-5.4-pro, gpt-5.3-chat, gpt-5.2, gpt-5.2-chat, gpt-5.2-pro, gpt-5.1, gpt-5.3-codex, gpt-oss-120b
- **Limitation:** "User-Pays" model means each user covers their own usage — great for us since each MidiEditor user would be their own "user"

**OpenAI-compatible APIs (custom base URL approach):**
Many providers expose an OpenAI-compatible endpoint — just change the base URL:
- **Ollama** (local): `http://localhost:11434/v1/chat/completions`
- **LM Studio** (local): `http://localhost:1234/v1/chat/completions`
- **OpenRouter**: `https://openrouter.ai/api/v1/chat/completions` (aggregator with free models)
- **Groq**: `https://api.groq.com/openai/v1/chat/completions` (fast inference, free tier)
- **Together AI**: `https://api.together.xyz/v1/chat/completions`
- **Mistral**: `https://api.mistral.ai/v1/chat/completions`
- **DeepSeek**: `https://api.deepseek.com/v1/chat/completions`

**Native APIs (different format, need adapter or use via OpenRouter):**
- **Anthropic Claude**: `https://api.anthropic.com/v1/messages` — NOT OpenAI-compatible (different request/response format: `messages` API with `anthropic-version` header, `content` blocks instead of `choices`). Best accessed via OpenRouter for compatibility.
- **Google Gemini**: `https://generativelanguage.googleapis.com/v1beta/` — NOT OpenAI-compatible (uses `generateContent` endpoint, different auth via API key in URL). Best accessed via OpenRouter or Google's OpenAI-compatible endpoint `https://generativelanguage.googleapis.com/v1beta/openai/` (experimental).

### 8.1 — Provider Abstraction (Custom Base URL) ✅

Simplest first step: make the API base URL configurable instead of hardcoded.

```cpp
// Current (hardcoded):
static const QString API_URL = "https://api.openai.com/v1/chat/completions";

// New (configurable):
// QSettings: "AI/api_base_url" (default: "https://api.openai.com/v1")
// Endpoint constructed: baseUrl + "/chat/completions"
```

**Provider presets in Settings dropdown:**

| Provider | Base URL | API Key Required | Free Tier |
|----------|----------|-----------------|-----------|
| OpenAI (default) | `https://api.openai.com/v1` | Yes | Limited free |
| OpenRouter | `https://openrouter.ai/api/v1` | Yes | Free models available |
| Groq | `https://api.groq.com/openai/v1` | Yes | Free tier |
| Ollama (local) | `http://localhost:11434/v1` | No | Unlimited (local) |
| LM Studio (local) | `http://localhost:1234/v1` | No | Unlimited (local) |
| Custom | User-specified URL | User-specified | Varies |

**Settings UI:**
```
┌─ AI Provider ──────────────────────────────────────┐
│ Provider:  [OpenAI          ▾]                     │
│ Base URL:  [https://api.openai.com/v1          ]   │
│ API Key:   [sk-...                        ] [Test] │
│ Model:     [gpt-5.4                            ▾]  │
└────────────────────────────────────────────────────┘
```

- Provider dropdown auto-fills base URL and suggests models
- "Custom" option enables free-text base URL input
- Local providers (Ollama, LM Studio) hide API key field
- Model list updates per provider (known models per provider)

**Files:** Modify `AiClient.h/cpp`, `AiSettingsWidget.h/cpp`

### 8.2 — Token Usage Tracking & Display ✅

Parse token usage from API responses and display in the UI.

**OpenAI includes usage in every response:**
```json
{
  "usage": {
    "prompt_tokens": 1250,
    "completion_tokens": 340,
    "total_tokens": 1590
  }
}
```

**Implementation:**
- Parse `usage` object from every API response in `AiClient`
- Emit `tokenUsageReceived(int prompt, int completion, int total)` signal
- Track cumulative session totals: `_sessionPromptTokens`, `_sessionCompletionTokens`
- Display in MidiPilot status bar: `"Tokens: 1.6k (session: 24.3k)"`
- Optional: estimated cost display based on known model pricing

**Status bar update:**
```
● Ready · gpt-5.4 · Med · Tokens: 1.6k · ⚙
```

**Files:** Modify `AiClient.h/cpp`, `MidiPilotWidget.h/cpp`

### 8.3 — Rate Limit Awareness ⬜

Parse rate limit headers from OpenAI responses and react intelligently.

**Headers to parse:**
```
x-ratelimit-limit-requests: 60
x-ratelimit-remaining-requests: 45
x-ratelimit-limit-tokens: 150000
x-ratelimit-remaining-tokens: 120000
x-ratelimit-reset-requests: 1s
x-ratelimit-reset-tokens: 6m0s
```

**Implementation:**
- Parse rate limit headers in `AiClient::onReplyFinished()`
- Store current rate limit state: `_remainingRequests`, `_remainingTokens`
- Warning when approaching limits (e.g., < 10% remaining): yellow status indicator
- On HTTP 429: auto-retry with exponential backoff (1s → 2s → 4s → max 16s, up to 3 retries)
- Show retry count in status: `"Rate limited — retrying in 4s (2/3)"`
- After max retries: show error with suggestion to wait or reduce request size

**Files:** Modify `AiClient.h/cpp`, `MidiPilotWidget.h/cpp`

### 8.4 — Free Provider Integration (Puter.com) ⬜

> **Investigation needed:** Puter.js is a JavaScript SDK — need to determine if there's a
> REST API endpoint we can call from C++ directly. Options:
>
> **Option A — REST API (preferred):** Puter likely has an internal REST API that the JS SDK
> calls. If we can identify the endpoint and auth mechanism, we can call it directly from
> `QNetworkAccessManager`. Needs reverse-engineering of the Puter.js SDK.
>
> **Option B — Embedded WebView bridge:** Use `QWebEngineView` to load a minimal HTML page
> with Puter.js, send prompts via JavaScript bridge, receive responses. More complex but
> guaranteed to work since it uses the official SDK.
>
> **Option C — Skip Puter, focus on OpenRouter:** OpenRouter has genuinely free models
> (some open-source models at $0 cost) with an OpenAI-compatible API. This works with
> Phase 8.1 (custom base URL) with zero additional code.

**Recommended approach:** Start with **Option C** (OpenRouter free models via custom base URL)
for immediate free access, then investigate Option A (Puter REST API) later.

### 8.5 — Provider-Specific Model Lists ✅

Each provider supports different models. Populate model dropdown based on selected provider.

**Known model lists per provider:**

| Provider | Notable Models |
|----------|---------------|
| OpenAI | gpt-5.4, gpt-5, gpt-4o, gpt-4o-mini, o4-mini, o3-mini |
| Anthropic Claude | claude-sonnet-4-20250514, claude-3.5-haiku, claude-3-opus |
| Google Gemini | gemini-2.5-pro, gemini-2.5-flash, gemini-2.0-flash (free tier: 15 RPM, 1M TPM) |
| OpenRouter | All OpenAI + Claude + Gemini + Llama + Mistral + free models |
| Groq | llama-3.3-70b, gemma2-9b, mixtral-8x7b (fast inference) |
| Ollama | User's locally installed models (query `/v1/models` endpoint) |
| LM Studio | User's locally loaded model (query `/v1/models` endpoint) |

**For local providers:** query `GET /v1/models` to discover available models dynamically.

**Files:** Modify `AiSettingsWidget.h/cpp`, `AiClient.h/cpp`

### Implementation Order

```
Phase 8.1  Custom base URL (provider presets)           ✅ DONE — OpenAI, OpenRouter, Gemini, Custom
Phase 8.2  Token usage tracking & display               ✅ DONE — last request + session totals
Phase 8.3  Rate limit awareness + auto-retry            ⬜ TODO — important for free tiers
Phase 8.4  Puter.com / free provider integration        ⬜ TODO — needs investigation
Phase 8.5  Provider-specific model lists                ✅ DONE — per-provider model dropdowns
```

### Estimated Complexity

| Sub-phase | New Code | Modified Code | Risk |
|-----------|----------|---------------|------|
| 8.1 Custom base URL | ~20 lines | AiClient (~30), AiSettingsWidget (~80) | Low |
| 8.2 Token tracking | ~40 lines | AiClient (~20), MidiPilotWidget (~30) | Low |
| 8.3 Rate limit handling | ~80 lines | AiClient (~60) | Medium — retry logic |
| 8.4 Puter integration | ~100-200 lines | AiClient (~50) | High — unknown API |
| 8.5 Model lists | ~60 lines | AiSettingsWidget (~40) | Low |

### Key Considerations

- **Phase 8.1 alone** unlocks Ollama, LM Studio, Groq, OpenRouter, DeepSeek, Mistral, etc.
  This is the highest-value change — one feature enables dozens of providers.
- **Claude & Gemini via OpenRouter:** Easiest path — OpenRouter proxies both Anthropic and
  Google APIs with an OpenAI-compatible format. Users get Claude and Gemini support for free
  through 8.1 without any native adapter code.
- **Google Gemini free tier is generous:** 15 RPM, 1M tokens/min, 1500 RPD — enough for
  casual MidiPilot use without paying anything. Gemini also has an experimental
  OpenAI-compatible endpoint that may work with just a base URL change.
- **Anthropic Claude has a different API format:** `messages` API with `anthropic-version`
  header, `content` blocks array instead of plain string, no `choices` wrapper. Native
  support would need an adapter in `AiClient`. Via OpenRouter, it just works.
- **Local AI (Ollama/LM Studio)** is the ultimate free option — no API key, no rate limits,
  no internet needed. Quality depends on the model and hardware but works great for simple
  edits with models like Llama 3.3 70B or Mistral Large.
- **Token tracking (8.2)** helps users understand their usage, especially important for
  free tier users who have limits.
- **Rate limit handling (8.3)** is critical for free tiers with low RPM/TPM limits.
  Auto-retry with backoff makes the experience smooth instead of showing errors.
- **Not all providers support `tools`/function calling** — Agent Mode may need a graceful
  fallback or warning for providers that don't support tools.
- **Responses API** (`/v1/responses`) is OpenAI-specific — custom providers should always
  use Chat Completions API (`/v1/chat/completions`).

---

## Phase 9: Editable System Prompts ⬜

> **Goal:** Allow users to customize the system prompts sent to the AI without recompiling.
> Hardcoded defaults remain as fallback. User edits are saved as a JSON file.
> This enables power users to tweak AI behavior, fix prompt issues, or adapt to niche
> workflows without waiting for a code update.

### Concept

```
┌─────────────────────────────────────────────────────────┐
│  Startup                                                 │
│  ├── Check for system_prompts.json next to exe           │
│  ├── Found & valid? → Use custom prompts                 │
│  ├── Found & invalid? → Warn user, use hardcoded defaults│
│  └── Not found? → Use hardcoded defaults (current)       │
└─────────────────────────────────────────────────────────┘
```

### JSON Format

```json
{
  "version": 1,
  "prompts": {
    "simple": "You are MidiPilot, an AI assistant embedded in MidiEditor...",
    "agent": "You are MidiPilot, an AI assistant embedded in MidiEditor...",
    "ffxiv": "## FFXIV BARD PERFORMANCE MODE (ACTIVE)...",
    "ffxiv_compact": "## FFXIV BARD PERFORMANCE MODE (ACTIVE)..."
  }
}
```

- **Partial overrides:** Only prompts present in the JSON are overridden. Missing keys
  fall back to hardcoded defaults. E.g., a file with only `"simple"` overrides just the
  Simple mode prompt while Agent/FFXIV stay default.
- **version:** Integer for forward compatibility. Current version = 1.
- **Unknown keys ignored** (forward compat).

### File Location

- Primary: `<exe_directory>/system_prompts.json` (portable, next to MidiEditor.exe)
- Fallback: `<AppData>/MidiEditor/system_prompts.json` (per-user config)

### 9.1 — Prompt Loading from JSON ⬜

Modify `EditorContext` to check for external prompts before returning hardcoded ones:

```cpp
// EditorContext.h — new methods:
static bool loadCustomPrompts(const QString &path);
static void resetToDefaults();

// EditorContext.cpp — static storage:
static QString s_customSimplePrompt;
static QString s_customAgentPrompt;
static QString s_customFfxivPrompt;
static QString s_customFfxivCompactPrompt;

// Modified existing methods:
QString EditorContext::systemPrompt() {
    if (!s_customSimplePrompt.isEmpty()) return s_customSimplePrompt;
    return QStringLiteral("...");  // existing hardcoded default
}
// Same pattern for agentSystemPrompt(), ffxivContext(), ffxivContextCompact()
```

**Validation rules:**
- Must be valid JSON (parse with `QJsonDocument`)
- Must have `"version"` key (integer, currently must be 1)
- Must have `"prompts"` object
- Each prompt value must be a non-empty string
- File size limit: 1 MB (prevent accidental huge files)

**Files:** Modify `EditorContext.h/cpp`

### 9.2 — System Prompt Editor Dialog ⬜

New dialog accessible from Settings → "Edit System Prompts..." button:

```
┌─ System Prompt Editor ──────────────────────────────────────────┐
│                                                                  │
│  ┌──────────┬──────────┬──────────┬────────────────┐            │
│  │  Simple  │  Agent   │  FFXIV   │  FFXIV Compact │            │
│  └──────────┴──────────┴──────────┴────────────────┘            │
│  ┌─────────────────────────────────────────────────────────┐    │
│  │ You are MidiPilot, an AI assistant embedded in          │    │
│  │ MidiEditor.                                              │    │
│  │ You receive the current editor state and selected MIDI   │    │
│  │ events as JSON.                                          │    │
│  │ Your job is to transform, analyze, or generate MIDI      │    │
│  │ events based on the user's request.                      │    │
│  │ ...                                                      │    │
│  │                                                          │    │
│  └─────────────────────────────────────────────────────────┘    │
│                                                                  │
│  [Load Default]  [Export to JSON]  [Import from JSON]           │
│                                                                  │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │ ℹ Custom prompts are saved to system_prompts.json next   │   │
│  │   to the application. Delete the file to restore defaults.│   │
│  └──────────────────────────────────────────────────────────┘   │
│                                                                  │
│                              [Reset All to Defaults]  [Save]    │
│                              [Cancel]                            │
└──────────────────────────────────────────────────────────────────┘
```

**Features:**
- `QTabWidget` with 4 tabs (Simple, Agent, FFXIV, FFXIV Compact)
- Each tab has a `QPlainTextEdit` (monospaced font, syntax-aware line numbers optional)
- **Load Default:** Restores the hardcoded default for the active tab
- **Export to JSON:** Saves current prompts to `system_prompts.json`
- **Import from JSON:** Loads prompts from a user-selected JSON file
- **Reset All to Defaults:** Restores all 4 prompts to hardcoded defaults + deletes JSON file
- **Save:** Writes `system_prompts.json` and applies changes immediately (no app restart)
- **Cancel:** Discards unsaved changes
- Validation on Save: non-empty check, valid UTF-8

**Files:** New `src/gui/SystemPromptDialog.h/cpp`

### 9.3 — Settings Integration ⬜

Add button in `AiSettingsWidget` to open the System Prompt Editor:

```
┌─ MidiPilot AI Settings ────────────────────────────┐
│ ...existing settings...                             │
│                                                     │
│ System Prompts:  [Edit System Prompts...]           │
│                  Custom prompts: ● Active / ○ Default│
│                                                     │
└─────────────────────────────────────────────────────┘
```

- Button opens `SystemPromptDialog`
- Status indicator shows whether custom prompts are active or defaults are in use
- If custom JSON exists but is invalid, show ⚠ warning status

**Files:** Modify `AiSettingsWidget.h/cpp`

### 9.4 — App Startup Loading ⬜

Call `EditorContext::loadCustomPrompts()` on application startup:

```cpp
// In MainWindow constructor or main.cpp:
QString promptsPath = QCoreApplication::applicationDirPath() + "/system_prompts.json";
if (QFile::exists(promptsPath)) {
    if (!EditorContext::loadCustomPrompts(promptsPath)) {
        qWarning() << "Invalid system_prompts.json — using defaults";
    }
}
```

**Files:** Modify `main.cpp` or `MainWindow.cpp`

### Implementation Order

```
Phase 9.1  Prompt loading from JSON                     ⬜ NEXT
Phase 9.2  System Prompt Editor dialog                  ⬜
Phase 9.3  Settings button + status indicator           ⬜
Phase 9.4  App startup auto-loading                     ⬜
```

### Estimated Complexity

| Sub-phase | New Code | Modified Code | Risk |
|-----------|----------|---------------|------|
| 9.1 JSON loading | ~80 lines | EditorContext (~20 lines) | Low |
| 9.2 Editor dialog | ~200 lines | — (new file) | Low |
| 9.3 Settings button | ~20 lines | AiSettingsWidget (~15 lines) | Low |
| 9.4 Startup loading | ~10 lines | main.cpp or MainWindow (~10 lines) | Low |

### Safety

- Hardcoded defaults are NEVER removed from the source code
- Invalid JSON → warning + fallback to defaults (never crash)
- Backup: before overwriting existing JSON, rename to `.json.bak`
- File size limit prevents accidental 100MB file from causing OOM

---

## Remaining Work

### Not Yet Implemented
- ⬜ **Context menu in MatrixWidget:** Right-click on selected notes → "Ask MidiPilot..." (Phase 2.2)
- ⬜ **Inline API key setup:** Enter API key directly in MidiPilot panel (Phase 2.3)
- ⬜ **Conversation reset on file change** (Phase 3.4)
- ⬜ **Local LLM support** (Ollama/llama.cpp) as offline alternative (Phase 4.7)
- ⬜ **Persistent history** (SQLite) across sessions (Phase 4.6)
- ⬜ **Loading spinner** animation instead of "Thinking..." text (Phase 2.1)
- ⬜ **Rate limit awareness** + auto-retry for free tiers (Phase 8.3)
- ⬜ **Editable system prompts** via JSON + dialog (Phase 9)

### Open Questions
- [ ] Max events per request? (Token budget consideration)
- [x] Should MidiPilot work without selection (on entire visible area)? — *Yes, AI can generate into current track/channel without selection*
- [ ] Auto-suggest mode? (AI proactively offers suggestions?)
- [ ] Keyboard shortcut for "apply last AI suggestion"?
- [ ] Support for batch operations (multiple prompts queued)?
