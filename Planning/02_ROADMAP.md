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
- ✅ Local LLM support (Ollama, LM Studio) — via custom base URL (Phase 8.1)

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
Phase 8    Multi-provider & free API access              ✅ DONE (8.1, 8.2, 8.5 — providers + tokens + model lists)
Phase 9    Editable system prompts (JSON + dialog)       ✅ DONE (9.1-9.4)
Phase 10   Independent repo & rebranding                 ✅ DONE (10.1-10.7)
Phase 11   FFXIV Channel Fix (deterministic fixer + UI)   ✅ DONE (11.1-11.4)
Phase 11.5 FFXIV Channel Fix v2 — 3-Tier Detection       ✅ DONE (v1.1.0 + v1.1.2)
Phase 12   Prompt Architecture v2                         ✅ DONE (12.1-12.7, v1.1.3)
Phase 13   Auto-Save & Crash Recovery                     ✅ DONE (13.1-13.4, v1.1.3.1)
Phase 14   Split Channels to Tracks                       ✅ DONE (14.1-14.4, v1.1.4)
Phase 15   Auto-Updater                                   ✅ DONE (15.1-15.6, v1.1.5)
Phase 16   Guitar Pro Import (.gp3-.gp7)                  ✅ DONE (16.1-16.5)
Phase 17   Modern UI Facelift (Dark/Light QSS Themes)     ✅ DONE (17.1-17.13)
Phase 18   Mewo Upstream Feature Sync                     ✅ DONE (18.1-18.16, v1.1.8)
Phase 19   MidiPilot AI Improvements                      ✅ DONE (19.1-19.7, v1.1.9)
Phase 20   Audio Export & FluidSynth Hardening            ✅ DONE (20.1-20.8, v1.2.0)
Phase 21   Lyric Editor                                   ✅ DONE (all sub-phases 21.1–21.9 complete)
Phase 22   Lyric Visualizer (Karaoke Display)             ✅ DONE
Phase 23   MCP Server, Documentation & Prompt v3          ✅ DONE (23.1-23.5, v1.3.2)
Phase 24   MusicXML & MuseScore (.mscz) Import              ⬜ TODO
Phase 4.6  Persistent history (SQLite)                    ⬜ TODO (low priority)
```

#### Phase 11.5 — 3-Tier Smart Detection (Fix X|V v2)
- **Tier 1 — No FFXIV MIDI:** No track names match known FFXIV instruments → abort + warning
- **Tier 2 — Dirty FFXIV (Rebuild):** FFXIV track names present, but guitar notes on only 1 channel → full clean+rebuild (channels, program_changes, switches)
- **Tier 3 — Configured FFXIV (Preserve):** FFXIV names + guitar notes already on multiple guitar-variant channels → preserve channels/notes, only clean+rebuild program_changes (tick-0 + switch ticks), reserve missing guitar channels, rename track if tick-0 note ≠ track name variant
- **Bonus:** Running the fixer twice works cleanly (Tier 2 → manual edits → Tier 3 picks up switches)

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
- ✅ Guitar Pro import support (GP3, GP4, GP5, GP6/GPX, GP7/GP8 — all formats)
- ✅ Fixed time signature display (actual denominator instead of MIDI power-of-2)
- ✅ Multi-provider support (OpenAI, OpenRouter, Google Gemini, Custom URL)
- ✅ Google Gemini native API integration (not just via OpenRouter)
- ✅ Token usage display (last request / session totals)
- ✅ Provider-specific model dropdowns (per-provider model lists)
- ✅ FFXIV Bard Performance mode (toggle, prompts, validation, drum conversion)
- ✅ MP3 export with built-in LAME encoder (static library, 3.100)
- ✅ Export completion dialog (Open File / Open Folder / Close)
- ✅ Guitar Pro file audio export fix (temp MIDI conversion for non-MIDI formats)
- ✅ SoundFont enable/disable checkboxes (per-font toggle without removing)
- ✅ FluidSynth audio driver fallback chain (wasapi → dsound → waveout → sdl3 → sdl2)
- ✅ FFXIV SoundFont Mode auto-toggle (detect "ff14"/"ffxiv" in font filename)
- ✅ FluidSynth settings always accessible (configure SoundFonts without switching output)
- ✅ FluidSynth error feedback dialog (init failure shows QMessageBox)
- ✅ Proper drum channel reset on GM restore (bank_select 128 + program_change on ch9)
- ✅ Lyric Timeline Widget with playback pop effects, label panel, dynamic font, auto-show
- ✅ Configurable lyric color (fixed pinkish default or track color) in Appearance settings
- ✅ MIDI text encoding fix — UTF-8 with Latin-1 fallback for TextEvents (fixes umlauts in ISO-8859-1 files)

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
│   ├── AgentRunner.h/cpp           # ✅ Phase 5.3 — Agent loop engine (tool-calling loop)
│   └── FFXIVChannelFixer.h/cpp     # ✅ Phase 11.1 — Deterministic FFXIV channel fixer
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

### 8.4 — Free Provider Integration (Puter.com) ❌ DEFERRED

> **Status:** Deferred indefinitely. Puter.js is a JavaScript-only SDK with no documented
> REST API for C++ desktop apps. The effort to reverse-engineer their API or embed a WebView
> bridge is not justified given the alternatives.
>
> **What we did instead:** Phase 8.1 (custom base URL) + Google Gemini native support
> already provide free/low-cost access:
> - **Google Gemini free tier:** 15 RPM, 1M TPM — generous enough for casual use
> - **OpenRouter free models:** $0 open-source models via OpenAI-compatible API
> - **Ollama / LM Studio:** Unlimited local AI, no API key needed
>
> These three options cover the "free access" goal without Puter.com.

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
Phase 8.3  Rate limit awareness + auto-retry            ⬜ TODO — nice-to-have for free tiers
Phase 8.4  Puter.com / free provider integration        ❌ DEFERRED — covered by Gemini/OpenRouter/Ollama
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

## Phase 9: Editable System Prompts ✅

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

### 9.1 — Prompt Loading from JSON ✅

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

### 9.2 — System Prompt Editor Dialog ✅

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

### 9.3 — Settings Integration ✅

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

### 9.4 — App Startup Loading ✅

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
Phase 9.1  Prompt loading from JSON                     ✅ DONE
Phase 9.2  System Prompt Editor dialog                  ✅ DONE
Phase 9.3  Settings button + status indicator           ✅ DONE
Phase 9.4  App startup auto-loading                     ✅ DONE
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

### Not Yet Implemented (Nice-to-Have)
- ⬜ **Context menu in MatrixWidget:** Right-click on selected notes → "Ask MidiPilot..." (Phase 2.2)
- ⬜ **Inline API key setup:** Enter API key directly in MidiPilot panel (Phase 2.3) — *low priority, Settings redirect works fine*
- ⬜ **Conversation reset on file change** (Phase 3.4)
- ⬜ **Persistent history** (SQLite) across sessions (Phase 4.6) — *low priority*
- ⬜ **Loading spinner** animation instead of "Thinking..." text (Phase 2.1) — *cosmetic*
- ⬜ **Rate limit awareness** + auto-retry for free tiers (Phase 8.3) — *nice-to-have*

### Completed Since Last Update
- ✅ **Lyric Timeline Widget (Phase 21.1)** — new LyricTimelineWidget displaying lyric/text events as colored blocks synced with MatrixWidget scroll/zoom, label panel, playback pop effects (expand, glow, shadow, bold), View menu toggle (Ctrl+L), auto-show setting, dynamic font sizing, centered text, configurable lyric color (fixed pinkish default or track color) in Appearance settings
- ✅ **Lyric UI Integration (Phase 21.8 partial)** — View → Lyric Timeline toggle, Ctrl+L shortcut, auto-show setting in Appearance, lyric color mode (Fixed Color / Track Color) + color picker in Settings
- ✅ **MIDI text encoding fix** — UTF-8 with Latin-1 fallback for text events (fixes German umlauts in MIDI files encoded as ISO-8859-1)
- ✅ **Prompt Architecture v2 (Phase 12.1-12.7)** — priority rule, validation block, timing reference, truncation fallback, event error feedback, schema unification, unused variable removal (v1.1.3)
- ✅ **Provider selector in MidiPilot footer** — switch provider/model directly in chat panel (v1.1.3)
- ✅ **Crash fix: New File during playback** — use-after-free in PlayerThread (v1.1.3)
- ✅ **FFXIV Channel Fix v2** — 3-Tier Smart Detection shipped in v1.1.0 + v1.1.2 (Tier 1 Not-FFXIV, Tier 2 Rebuild, Tier 3 Preserve)
- ✅ **Velocity normalization** — Tier 2 & 3 set all NoteOn velocities to 127 (v1.1.2)
- ✅ **FluidSynth integration** — built-in synthesizer merged from upstream (v1.1.2)
- ✅ **FFXIV SoundFont Mode** — per-note program changes on CH9 for drum instruments (v1.1.2)
- ✅ **FFXIV Channel Fix** — deterministic `FFXIVChannelFixer` class, toolbar button (Fix X|V Channels), confirmation popup, QSettings bug fix (Phase 11.1-11.4)
- ✅ **QSettings fix** — FFXIV tools were never sent to LLM due to constructor mismatch; both now use `QSettings("MidiEditor", "NONE")`
- ✅ **Editable system prompts** via JSON + dialog (Phase 9.1-9.4)
- ✅ **Manual / Wiki** — GitHub Pages with dark theme + MidiPilot page (Phase 10.6)
- ✅ **Independent repo & rebranding** — all sub-phases complete (Phase 10.1-10.7)
- ✅ **v1.0.0–v1.1.0 released** on GitHub with automated CI/CD builds

---

## Phase 11: FFXIV Channel Fix — Automated Channel & Program Management ✅

> **Goal:** Remove channel/program_change complexity from the AI by providing a deterministic
> tool that auto-fixes channel assignments and program_change events for FFXIV MIDI files.
> Available both as a **one-click UI button** (no AI needed) and as an **AI tool call**.
> This eliminates the #1 source of LLM errors in FFXIV mode: incorrect channel assignments
> and missing/wrong program_change events.

### Background & Problem

The current `setup_channel_pattern` tool has several issues:
1. It skips channel 9 for all tracks, but FFXIV percussion **should** use CH9
2. It only sets `MidiTrack::assignChannel()` (metadata) — it does NOT move existing
   note/CC/program_change events to the new channel
3. Guitar channel reservation is overly complex (reserves a block after non-guitar tracks)
4. LLMs frequently get channel assignments wrong despite detailed system prompts

### Design: Simple, Deterministic Rules

**Rule 1 — Track-to-Channel Mirror:**
Each track maps to the channel matching its index: T0→CH0, T1→CH1, T2→CH2, etc.

**Rule 2 — Percussion Exception:**
If the track name is one of `Bass Drum`, `Snare Drum`, `Cymbal`, `Bongo`,
all events stay on / move to CH9 (GM Drumkit channel).
Exception: `Timpani` follows Rule 1 (it's a tonal instrument that can play melodic patterns).

**Rule 3 — Guitar Tracks:**
Guitar tracks (`ElectricGuitarClean`, `ElectricGuitarMuted`, `ElectricGuitarOverdriven`,
`ElectricGuitarPowerChords`, `ElectricGuitarSpecial`) also follow Rule 1 — their track
number determines their channel. This naturally gives each guitar variant its own channel
without needing a reserved block.

**General: Suffix Stripping (`[+-]\d+$`):**
All instrument name matching strips a trailing `[+-]\d+` suffix before lookup.
This applies to **every** track, not just guitars. Examples:
- `Flute+1` → recognized as `Flute` (program 73)
- `Piano+2` → recognized as `Piano` (program 0)
- `ElectricGuitarClean+1` → recognized as `ElectricGuitarClean` (program 27)
- `Snare Drum` → recognized as `Snare Drum` (percussion, CH9)

This allows multiple tracks of the same instrument (e.g., two flutes) while still
correctly identifying the instrument name and program number.

**Guitar variants without their own track:** If any guitar track exists but not all 5 variants
have a track, the missing variants get program_change events on free channels (first unused
channels after all tracks are assigned). These program_changes are attached to the first
guitar track as carrier.

**Rule 4 — Program Changes:**
After all channels are assigned, every track gets program_change events at tick 0 for
**all** used channels (not just its own). This ensures MIDI players like MidiBard2 know the
complete instrument→channel mapping.

**Rule 5 — Event Migration:**
All existing events (notes, CC, program_change, etc.) on each track are moved to the
track's assigned channel. For percussion tracks, events move to CH9. For guitar tracks
with switch notes on different channels, those notes move to the correct guitar variant channel.

### Algorithm

```
1. ANALYZE — Scan all tracks:
   - Classify each track: melodic / percussion / guitar
   - Build trackIndex → targetChannel map using Rules 1-3

2. CLEAN — Remove all existing program_change events at tick 0
   (they'll be re-created correctly)

3. MIGRATE — For each track:
   a. Get all events on this track (across all channels)
   b. If percussion: moveToChannel(9)
   c. If melodic/guitar: moveToChannel(trackIndex)
   d. Special: guitar switch notes — if the note was on a channel
      that maps to a different guitar variant, keep it on that variant's channel

4. PROGRAM — Insert program_change at tick 0:
   - For each assigned channel, insert the correct program number
   - Attach all program_changes to respective tracks
   - For reserved guitar channels (no track), attach to first guitar track

5. METADATA — Set track.assignChannel() for each track

6. REPORT — Return channel map + changes made
```

### 11.1 — Core Logic: `FFXIVChannelFixer` Class ✅

New utility class with pure logic (no UI dependency):

```cpp
// src/ai/FFXIVChannelFixer.h

#pragma once
#include <QJsonObject>
#include <QJsonArray>

class MidiFile;
class MidiPilotWidget;

class FFXIVChannelFixer {
public:
    // Static entry point — fixes all channels + program_changes
    // Returns JSON report of changes made
    static QJsonObject fixChannels(MidiFile *file, MidiPilotWidget *widget);

    // Read-only analysis — returns proposed channel map without modifying anything
    static QJsonObject analyzeChannels(MidiFile *file);

private:
    // Classification helpers
    static bool isPercussion(const QString &trackName);  // Bass Drum, Snare Drum, Cymbal, Bongo
    static bool isGuitar(const QString &trackName);       // starts with "ElectricGuitar"
    static bool isTimpani(const QString &trackName);      // Timpani (tonal, NOT percussion)
    static int programNumber(const QString &trackName);   // FFXIV GM program lookup

    // Core steps
    static QHash<int, int> buildChannelMap(MidiFile *file);
    static void cleanProgramChanges(MidiFile *file);
    static void migrateEvents(MidiFile *file, const QHash<int, int> &channelMap);
    static void insertProgramChanges(MidiFile *file, MidiPilotWidget *widget,
                                      const QHash<int, int> &channelMap);
};
```

**Classification logic:**

```cpp
static bool isPercussion(const QString &name) {
    // Strip [+-]\d+$ suffix
    QString base = name;
    base.remove(QRegularExpression("[+-]\\d+$"));
    return base == "Bass Drum" || base == "Snare Drum"
        || base == "Cymbal"    || base == "Bongo";
}

static bool isTimpani(const QString &name) {
    QString base = name;
    base.remove(QRegularExpression("[+-]\\d+$"));
    return base == "Timpani";
}

static bool isGuitar(const QString &name) {
    QString base = name;
    base.remove(QRegularExpression("[+-]\\d+$"));
    return base.startsWith("ElectricGuitar");
}
```

**Channel map builder:**

```cpp
QHash<int, int> FFXIVChannelFixer::buildChannelMap(MidiFile *file) {
    QHash<int, int> map; // trackIndex → channel

    for (int t = 0; t < file->numTracks(); t++) {
        QString name = file->track(t)->name();
        if (isPercussion(name)) {
            map[t] = 9;  // All percussion → CH9
        } else {
            map[t] = t;  // Track N → Channel N (melodic, guitar, timpani)
        }
    }
    return map;
}
```

**Event migration (key new functionality):**

```cpp
void FFXIVChannelFixer::migrateEvents(MidiFile *file,
                                       const QHash<int, int> &channelMap) {
    for (int t = 0; t < file->numTracks(); t++) {
        int targetCh = channelMap.value(t, t);
        MidiTrack *track = file->track(t);

        // Collect all events belonging to this track across all channels
        for (int ch = 0; ch < 16; ch++) {
            QMultiMap<int, MidiEvent*> *events = file->channel(ch)->eventMap();
            QList<MidiEvent*> toMove;

            for (auto it = events->begin(); it != events->end(); ++it) {
                if (it.value()->track() == track && ch != targetCh) {
                    toMove.append(it.value());
                }
            }

            for (MidiEvent *ev : toMove) {
                ev->moveToChannel(targetCh);
            }
        }
    }
}
```

**Files:** New `src/ai/FFXIVChannelFixer.h`, `src/ai/FFXIVChannelFixer.cpp`
**Estimated:** ~200 lines

### 11.2 — Replace `setup_channel_pattern` AI Tool ✅

Replace the current `setup_channel_pattern` implementation in `ToolDefinitions.cpp`
with a call to `FFXIVChannelFixer::fixChannels()`.

```cpp
// ToolDefinitions.cpp — execSetupChannelPattern() becomes:
QJsonObject ToolDefinitions::execSetupChannelPattern(MidiFile *file,
                                                      MidiPilotWidget *widget) {
    return FFXIVChannelFixer::fixChannels(file, widget);
}
```

The tool schema stays the same (no parameters needed). The AI just calls
`setup_channel_pattern` and the deterministic fixer handles everything.

**Update tool description** in `toolSchemas()` to clarify what it does:
```
"Automatically fix all FFXIV channel assignments and program_change events.
 Maps each track to its matching channel (T0→CH0, T1→CH1), percussion to CH9,
 and inserts correct program_change events at tick 0 for all channels.
 Moves all existing events to their correct channels.
 Call this after creating/modifying tracks to ensure correct FFXIV channel setup."
```

**Files:** Modify `ToolDefinitions.cpp` (~5 lines changed)

### 11.3 — UI Button: "Fix X|V Channels" ✅

Add a menu item and optional toolbar button in `MainWindow.cpp`:

```
Tools menu:
  ...existing items...
  ─────────────
  Fix FFXIV Channels    (Ctrl+Shift+F)
```

**Implementation in MainWindow:**

```cpp
// In MainWindow::setupActions():
toolsMB->addSeparator();
QAction *ffxivChannelFixAction = new QAction(tr("Fix FFXIV Channels"), this);
ffxivChannelFixAction->setShortcut(
    QKeySequence(QKeyCombination(Qt::CTRL | Qt::SHIFT, Qt::Key_F)));
connect(ffxivChannelFixAction, &QAction::triggered, this, &MainWindow::fixFFXIVChannels);
toolsMB->addAction(ffxivChannelFixAction);

// New slot:
void MainWindow::fixFFXIVChannels() {
    MidiFile *file = this->file();
    if (!file) return;

    // Show confirmation dialog with preview
    QJsonObject analysis = FFXIVChannelFixer::analyzeChannels(file);
    // ... show dialog with proposed changes ...

    file->protocol()->startNewAction("Fix FFXIV Channels");
    QJsonObject result = FFXIVChannelFixer::fixChannels(file, _midiPilotWidget);
    file->protocol()->endAction();

    // Show summary
    int tracksFixed = result["tracksModified"].toInt();
    QMessageBox::information(this, tr("FFXIV Channel Fix"),
        tr("Fixed %1 track(s). All channels and program changes updated.")
            .arg(tracksFixed));

    updateTrackMenu();
    emit repaintAllRequested();
}
```

**Key UX points:**
- Works without AI / without API key — pure local operation
- Wrapped in protocol action → fully undoable with Ctrl+Z
- Confirmation dialog shows what will change before applying
- Status message shows summary after completion

**Files:** Modify `MainWindow.h` (~3 lines), `MainWindow.cpp` (~30 lines)

### 11.4 — Simplify FFXIV System Prompts ✅

With the channel fixer handling all channel logic, the FFXIV system prompts can be
dramatically simplified. Remove all channel/program_change rules and replace with:

```
### Channels & Program Changes
Do NOT worry about channel assignments or program_change events.
After creating or modifying tracks, call the setup_channel_pattern tool.
It will automatically:
- Map each track to the correct channel (T0→CH0, T1→CH1, percussion→CH9)
- Insert all required program_change events at tick 0
- Move any misplaced events to the correct channel

Just focus on creating the right tracks with correct instrument names and notes.
```

This removes ~40 lines of complex channel rules from both the full and compact FFXIV
prompts, replacing them with ~5 lines. The AI only needs to:
1. Create tracks with valid FFXIV instrument names
2. Put notes in the correct range (C3-C6)
3. Call `setup_channel_pattern` when done

**Files:** Modify `EditorContext.cpp` (~40 lines removed, ~5 added)

### Implementation Order

```
Phase 11.1  Core logic (FFXIVChannelFixer class)         ✅ DONE
Phase 11.2  Replace setup_channel_pattern tool            ✅ DONE
Phase 11.3  UI button (Fix X|V Channels)                  ✅ DONE — toolbar + menu + confirmation popup
Phase 11.4  Simplify FFXIV system prompts                 ✅ DONE
```

### Estimated Complexity

| Sub-phase | New Code | Modified Code | Risk |
|-----------|----------|---------------|------|
| 11.1 FFXIVChannelFixer class | ~200 lines | CMakeLists.txt (~1 line) | Medium — event migration |
| 11.2 Replace setup_channel_pattern | ~5 lines | ToolDefinitions.cpp (~100 lines replaced) | Low |
| 11.3 UI button | ~30 lines | MainWindow.h (~3), MainWindow.cpp (~30) | Low |
| 11.4 Simplify prompts | ~5 lines | EditorContext.cpp (~40 lines removed) | Low |

### Key Considerations

- **Undoable:** The UI button wraps everything in `protocol()->startNewAction()` /
  `endAction()`, so Ctrl+Z reverts all channel changes at once.
- **Idempotent:** Running the fixer twice produces the same result. Safe to re-run.
- **No AI dependency:** The UI button works without any API key or AI connection.
  Users can manually create FFXIV tracks and fix channels with one click.
- **Backward compatible:** The AI tool name `setup_channel_pattern` stays the same.
  Existing custom prompts referencing it will still work.
- **Track limit not enforced here:** The 8-track limit stays in the system prompt.
  The channel fixer works with any number of tracks (up to 16 channels).
  If there are more than 16 non-percussion tracks, channels wrap or clamp to 15.
- **Existing events preserved:** Notes, CC, and other events are moved — not deleted
  and re-created. Timing, velocity, duration all stay intact.
- **Guitar switch notes:** If a guitar track has notes on multiple channels (for
  switching between variants), those notes are kept on their respective variant
  channels, not all moved to the track's primary channel.

---

## Phase 10: Independent Repo & Rebranding ✅

> **Goal:** Establish MidiEditor AI as a standalone project with its own GitHub repository,
> proper credits to upstream projects, automated builds, and a publishing strategy.

### 10.1 — App Rebranding ✅

Rename the application from "MidiEditor" / "MeowMidiEditor" to **"MidiEditor AI"**.

**Locations to update:**

| File | Current | New |
|------|---------|-----|
| `src/main.cpp` | `a.setApplicationName("MeowMidiEditor")` | `"MidiEditor AI"` |
| `CMakeLists.txt` | `project(MidiEditor ...)` | `project(MidiEditorAI ...)` |
| `midieditor.pro` | `TARGET = MidiEditor` | `TARGET = MidiEditorAI` |
| `scripts/packaging/windows/config.xml` | `<Name>MidiEditor</Name>` | `<Name>MidiEditor AI</Name>` |
| `scripts/packaging/debian/control` | `Package: midieditor` | `Package: midieditor-ai` |
| `scripts/packaging/debian/MidiEditor.desktop` | `Name=MidiEditor` | `Name=MidiEditor AI` |
| Window title | Auto from `applicationName()` | Automatic |
| About dialog | Auto from `applicationName()` | Automatic |

**⚠ QSettings key:** Currently uses `QSettings("MidiEditor", "NONE")`. Consider keeping `"MidiEditor"` as the organization name for backward compatibility (existing users keep their API keys and settings), or migrate settings on first launch.

### 10.2 — Credits & About Dialog ✅

Update `AboutDialog.cpp` to properly credit the upstream chain:

- **MidiEditor AI** — AI-powered fork by happytunesai
- **Based on:** [Meowchestra/MidiEditor](https://github.com/Meowchestra/MidiEditor) — maintained by Meowchestra
- **Which is based on:** [jingkaimori/midieditor](https://github.com/jingkaimori/midieditor/) fork of [ProMidEdit](https://github.com/PROPHESSOR/ProMidEdit)
- **Original project:** [MidiEditor](https://github.com/markusschwenk/midieditor) by Markus Schwenk
- Keep all existing contributor credits (CONTRIBUTORS file)
- Keep all third-party credits (icons, metronome sound, RtMidi)
- Update Ko-fi/funding link if applicable
- Update GitHub link to new repo URL

### 10.3 — Update Checker ✅

Redirect the update checker to the new repo.

**Current:** `https://api.github.com/repos/Meowchestra/MidiEditor/releases/latest`
**New:** `https://api.github.com/repos/happytunesai/MidiEditor_AI/releases/latest` (or final repo name)

**File:** `src/gui/UpdateChecker.cpp`
- Change the GitHub API URL
- Change the User-Agent header from `"MidiEditor"` to `"MidiEditor AI"`
- Version scheme: continue from current `4.3.1` or start fresh (e.g., `1.0.0`) — decide

### 10.4 — GitHub Actions (CI/CD) ✅

Re-enable and adapt the disabled workflows:

1. **`xmake.yml.disabled` → `cmake.yml`** — Adapt to CMake build (current build system)
   - Qt 6.5.3, MSVC 2019/2022
   - Build on push to main + PRs
   - Produce Release artifact (zip/installer)
   - Consider adding Qt 6.6+ compatibility test

2. **`deploy-pages.yml.disabled`** — Re-enable if a manual/wiki site is wanted
   - Point to new repo's GitHub Pages

3. **New: `release.yml`** — Automated release workflow
   - Trigger on tag push (`v*`)
   - Build Release binary
   - Run `windeployqt`
   - Zip the output
   - Create GitHub Release with zip attached
   - Generate changelog from commits

### 10.5 — Automated Release Build ✅

Create a publish pipeline that builds and packages the latest version:

**Existing asset:** `build.bat` in repo root (MSVC 2019 + Qt 6.5.3 + CMake + windeployqt)

**Steps for a release batch/script:**
1. Build Release binary (`cmake --build build --config Release`)
2. Run `windeployqt` to gather Qt DLLs
3. Copy `run_environment/` assets (graphics, metronome, MIDI)
4. Zip everything into `MidiEditorAI-v<version>-win64.zip`
5. (Optional) Build Qt Installer Framework package

**Local script:** Create `release.bat` or adapt existing `build.bat`
**CI script:** GitHub Actions workflow (see 10.4)

### 10.6 — Manual / Wiki ✅

The `manual/` folder is deployed to GitHub Pages via `deploy-pages.yml` workflow.

**Completed:**
- ✅ Dark-themed CSS stylesheet (`manual/style.css`) — GitHub-dark inspired
- ✅ All 14 HTML pages wrapped with proper `<!DOCTYPE>`, responsive `<head>`, nav bar, footer
- ✅ Nav bar on every page with links to all sections including MidiPilot
- ✅ **New `midipilot.html` page** — full MidiPilot documentation with:
  - 7 screenshots (overview, settings, connection test, system prompts, chat panels, input bar)
  - Feature cards grid (Agent Mode, Simple Mode, FFXIV, Multi-Provider, Reasoning, Custom Prompts)
  - AI Tools reference table (13 tools)
  - Supported Providers table
  - Getting Started guide
- ✅ Screenshots stored in `manual/screenshots/midipilot-*.png`
- ✅ GitHub Pages auto-deploys on push to main (when `manual/**` changes)
- ✅ Live at: `https://happytunesai.github.io/MidiEditor_AI/`

### 10.7 — README Update for New Repo ✅

Update README.md header for the standalone project:
- Change title to "MidiEditor AI"
- Update badges to point to new repo
- Update download/release links
- Add "Credits & Acknowledgments" section with upstream chain
- Keep MidiPilot feature docs and examples

### Implementation Order

```
Phase 10.1  App rebranding (name, CMake, .pro)           ✅ DONE
Phase 10.2  Credits & About Dialog                       ✅ DONE
Phase 10.3  Update Checker redirect                      ✅ DONE
Phase 10.4  GitHub Actions (CI/CD)                       ✅ DONE
Phase 10.5  Automated release build                      ✅ DONE
Phase 10.6  Manual / Wiki (GitHub Pages + MidiPilot)     ✅ DONE
Phase 10.7  README update for new repo                   ✅ DONE
```

### Open Questions
- [x] Max events per request? — *Handled via token budget warning (Phase 4.1) + configurable context range*
- [x] Should MidiPilot work without selection (on entire visible area)? — *Yes, AI can generate into current track/channel without selection*
- [ ] Auto-suggest mode? (AI proactively offers suggestions?) — *deferred, not planned*
- [ ] Keyboard shortcut for "apply last AI suggestion"? — *deferred*
- [ ] Support for batch operations (multiple prompts queued)? — *deferred*

---

## Phase 12: Prompt Architecture v2 ✅ DONE (v1.1.3)

> **Goal:** Improve the reliability and consistency of LLM responses by resolving prompt
> conflicts, tightening the JSON schema, restructuring prompt priority, and adding both
> prompt-level and code-level validation. Based on a systematic external review.
>
> **Source:** `MidiEditor_AI_Prompt_Review.md` (external review, March 2026)
>
> **Scope:** Changes to `EditorContext.cpp` (prompt text) + `MidiEventSerializer.cpp` /
> `MidiPilotWidget.cpp` (validation code). No UI changes, no new features.

### Background & Motivation

The current prompt architecture is functional and above-average for an embedded AI assistant.
However, several issues reduce reliability, especially in FFXIV mode and for complex
multi-step compositions:

1. **Conflicting rules** — `agentSystemPrompt()` says "ALWAYS insert program_change at tick 0"
   but `ffxivContext()` says "Do NOT manually set channels or insert program_change".
   LLMs don't reliably prioritize mode-specific rules over general ones.
2. **No final validation block** — The system prompt describes rules throughout the text
   but lacks a compact checklist the LLM reviews before responding.
3. **Implicit timing** — Only "see ticksPerQuarter" is mentioned, no concrete tick values
   for common note durations. LLMs frequently miscalculate rhythms.
4. **No truncation prevention** — The prompt asks for complete output but gives no
   instruction to gracefully degrade if the request is too large.
5. **Silent event rejection** — Invalid events in `deserialize()` are skipped with `continue`.
   In Agent mode, the LLM never learns that events were rejected.
6. **Dual top-level schema** — Simple mode accepts both `{"action": ...}` and
   `{"actions": [...]}`. This duality increases the chance of malformed responses.
7. **Unused variable** — `tsEvents` in `captureKeySignature()` is fetched but never used.

### What Was Verified As NOT A Problem

- **Off-by-one in `captureSurroundingEvents()`:** Both `measure()` and `startTickOfMeasure()`
  are 1-based. The `qMax(1, cursorMeasure - measures)` is correct.
- **Missing event IDs:** The serializer already emits sequential `"id": 0, 1, 2, ...`
  per event. `deleteIndices` works against these indices. No `sourceIndex` needed.
- **`schemaVersion` in responses:** Not useful. Prompt defines the schema; versionizing
  the response output adds token cost for no benefit.

### 12.1 — Priority Rule for Mode Conflicts ✅ DONE (v1.1.3)

Add a clear override rule at the very top of both `systemPrompt()` and `agentSystemPrompt()`:

```text
PRIORITY RULE:
If a mode-specific prompt (e.g. FFXIV mode) conflicts with a general rule,
the mode-specific rule ALWAYS overrides the general rule.
```

This is the single highest-impact change. Two lines of text that eliminate the #1 source
of FFXIV agent errors (inserting `program_change` when the mode says not to).

**Files:** `EditorContext.cpp` — `systemPrompt()`, `agentSystemPrompt()`
**Effort:** 5 min

### 12.2 — Final Validation Block ✅ DONE (v1.1.3)

Append a compact validation checklist to `systemPrompt()` (last thing the LLM reads
before responding):

```text
FINAL VALIDATION BEFORE RESPONDING:
- Return raw JSON only — no markdown, no code fences
- Every event must include all required fields (type, tick, note, velocity, duration, channel)
- tick must be integer >= 0
- duration must be integer > 0
- note must be 0-127
- velocity must be 1-127
- channel must be 0-15
- trackIndex must refer to an existing track or one created earlier in this response
```

For FFXIV mode, append additional rules:

```text
FFXIV VALIDATION:
- Never exceed 8 total tracks
- Notes must be MIDI 48-84 (C3-C6)
- Track names must be exact valid instrument names
- Do not insert pitch_bend events
- Do not manually set channels or program_change — use setup_channel_pattern
```

**Files:** `EditorContext.cpp` — `systemPrompt()`, `ffxivContext()`
**Effort:** 10 min

### 12.3 — Timing Reference ✅ DONE (v1.1.3)

Add an explicit timing helper block to `systemPrompt()` and `agentSystemPrompt()`:

```text
TIMING REFERENCE (at current ticksPerQuarter):
quarter note = ticksPerQuarter
eighth note  = ticksPerQuarter / 2
sixteenth    = ticksPerQuarter / 4
half note    = ticksPerQuarter * 2
whole note   = ticksPerQuarter * 4
dotted quarter = ticksPerQuarter * 3 / 2
triplet quarter = ticksPerQuarter * 2 / 3
```

The concrete values (e.g., `quarter = 480`) are already available in the editor state JSON
via `ticksPerQuarter`. This reference helps the LLM compute durations without mistakes.

**Files:** `EditorContext.cpp` — `systemPrompt()`, `agentSystemPrompt()`
**Effort:** 5 min

### 12.4 — Truncation Fallback Rule ✅ DONE (v1.1.3)

Add to both system prompts:

```text
IMPORTANT: If the requested output would be too large to complete in one response,
produce the smallest complete musically coherent version that satisfies the request
instead of returning a partial or truncated result.
```

This instrution reduces half-finished outputs, especially in Simple mode where there is no
retry loop.

**Files:** `EditorContext.cpp` — `systemPrompt()`, `agentSystemPrompt()`
**Effort:** 2 min

### 12.5 — Invalid Event Feedback in Agent Mode ✅ DONE (v1.1.3)

Currently `MidiEventSerializer::deserialize()` silently skips invalid events (`continue`).
In Agent mode, the LLM never learns that events failed validation.

**Change:** Collect validation errors during deserialization and include them in the
tool result returned to the LLM:

```cpp
// In deserialize() — instead of just `continue`:
if (!validateEventJson(obj, errorMsg)) {
    skippedErrors.append(QString("Event %1: %2").arg(i).arg(errorMsg));
    continue;
}
```

Then in the tool result:
```json
{"success": true, "inserted": 45, "skipped": 3,
 "skippedErrors": ["Event 12: note must be 0-127", ...]}
```

This lets the LLM self-correct in subsequent tool calls.

**Files:** `MidiEventSerializer.cpp` (deserialize), `MidiPilotWidget.cpp` (tool result builder)
**Effort:** 30 min

### 12.6 — Unify Simple Mode Schema to `actions[]` ✅ DONE (v1.1.3)

Currently Simple mode accepts two top-level formats:
- Single: `{"action": "edit", "events": [...]}`
- Multi: `{"actions": [{"action": "edit", ...}, ...], "explanation": "..."}`

To reduce ambiguity, always require the array format:

```text
RESPONSE FORMAT — respond with a raw JSON object:
{
  "actions": [
    {"action": "edit", "events": [...], "explanation": "..."}
  ],
  "explanation": "Overall summary"
}
Always use the "actions" array, even for single operations.
```

The parser in `onResponseReceived` already handles the `actions` array path.
Keep backward compat for the `action` path but stop documenting it in the prompt.

**Risk:** Some models may still emit the single-action format from cached patterns.
Keep parsing both but only teach the array format going forward.

**Files:** `EditorContext.cpp` — `systemPrompt()`
**Effort:** 1h (prompt rewrite + testing)

### 12.7 — Remove Unused Variable ✅ DONE (v1.1.3)

Remove the unused `tsEvents` in `captureKeySignature()`:

```cpp
// Line 168 — DELETE:
QMap<int, MidiEvent *> *tsEvents = file->timeSignatureEvents();
```

**Files:** `EditorContext.cpp`
**Effort:** 1 min

### Implementation Order

```
Phase 12.1  Priority rule for mode conflicts              ✅ DONE (v1.1.3)
Phase 12.2  Final validation block                        ✅ DONE (v1.1.3)
Phase 12.3  Timing reference                              ✅ DONE (v1.1.3)
Phase 12.4  Truncation fallback rule                      ✅ DONE (v1.1.3)
Phase 12.5  Invalid event feedback (Agent mode)           ✅ DONE (v1.1.3)
Phase 12.6  Unify Simple mode schema to actions[]         ✅ DONE (v1.1.3)
Phase 12.7  Remove unused variable                        ✅ DONE (v1.1.3)
```

### Estimated Complexity

| Sub-phase | New Code | Modified Code | Risk |
|-----------|----------|---------------|------|
| 12.1 Priority rule | 0 lines | EditorContext (~4 lines, prompt text) | None |
| 12.2 Final validation block | 0 lines | EditorContext (~20 lines, prompt text) | None |
| 12.3 Timing reference | 0 lines | EditorContext (~10 lines, prompt text) | None |
| 12.4 Truncation fallback | 0 lines | EditorContext (~4 lines, prompt text) | None |
| 12.5 Event error feedback | ~20 lines | MidiEventSerializer (~15), MidiPilotWidget (~10) | Low |
| 12.6 Schema unification | 0 lines | EditorContext (~30 lines, prompt rewrite) | Low — backward compat |
| 12.7 Unused variable | 0 lines | EditorContext (~1 line removed) | None |

### Key Considerations

- **12.1-12.4 are pure prompt text changes** — zero risk, no code logic changes,
  immediately testable by sending a prompt to any model.
- **12.5 is the only code change** that affects runtime behavior. It's backward-compatible:
  the extra `skippedErrors` field in tool results is additive.
- **12.6 is cosmetic for the prompt** — the parser already handles both formats.
  The goal is to reduce LLM confusion, not break existing behavior.
- **NOT implementing full per-mode prompt variants** (e.g., separate `agentSystemPromptFFXIV()`).
  The priority rule (12.1) achieves 90% of the benefit at 10% of the maintenance cost.
  Separate per-mode prompts can be added later if conflicts persist.
- **NOT adding `schemaVersion` to responses** — versioning belongs in the prompt, not
  in the LLM output. No benefit for token cost.
- **Custom prompt users** (`system_prompts.json`) get the improvements automatically
  only if they reset to defaults. Existing custom prompts are not affected.

---

## Phase 13: Auto-Save ✅ DONE (v1.1.3.1)

> **Goal:** Prevent data loss by automatically saving the MIDI file at regular intervals
> and providing crash recovery for unsaved work. Currently there is zero backup/recovery
> infrastructure — if the app crashes, all unsaved edits are gone.
>
> **Motivation:** Users often edit for hours without saving. A single crash loses everything.

### Background & Code Analysis

**Current save path:**
- `MainWindow::save()` → `MidiFile::save(path)` — **synchronous**, writes raw MIDI binary
  to disk on the GUI thread. Fast for typical files (<100ms), but blocks the UI.
- `MidiFile::_saved` (bool) — set `false` by `Protocol::endAction()`, set `true` by `save()`.
- `MidiFile::_path` — empty string for untitled (never-saved) documents.
- No temp files, no backup directory, no crash recovery exists.

**Edit detection signals:**
- `Protocol::actionFinished()` → connected to `MainWindow::markEdited()` — emitted after
  every undo-able action. This is the ideal trigger to reset an auto-save countdown timer.

**Timer infrastructure:**
- `MainWindow` currently has no QTimers. Clean slate for adding one.

**Settings dialog:**
- 9 tabbed panels in `SettingsDialog`, added via `addSetting()`. Auto-save settings can
  go into a new "General" panel or be appended to `PerformanceSettingsWidget`.

### Design

**Two-tier approach:**

1. **Tier A — In-place auto-save** (for files that already have a path on disk):
   - Silently calls `file->save(file->path())` after N seconds of inactivity.
   - Only triggers if the file is dirty (`!file->saved()`).
   - Resets the timer on every edit (debounce pattern — saves after a quiet period,
     not mid-typing).

2. **Tier B — Backup auto-save** (for ALL files, including untitled):
   - Saves to a sidecar file: `<filepath>.autosave.mid` (or `<appdata>/autosave/<hash>.mid`
     for untitled documents).
   - The sidecar file is deleted on clean exit or manual save.
   - On startup, check for leftover `.autosave.mid` files → offer recovery dialog.

**Recommended approach: Start with Tier B only.** Tier B is safer because it never
overwrites the user's original file. Tier A can be added later as an opt-in preference.

### 13.1 — Auto-Save Timer & Backup File ✅

Add to `MainWindow`:

```cpp
// Header
QTimer *_autoSaveTimer;
void performAutoSave();

// Constructor
_autoSaveTimer = new QTimer(this);
_autoSaveTimer->setSingleShot(true);
connect(_autoSaveTimer, &QTimer::timeout, this, &MainWindow::performAutoSave);

// In markEdited() — reset the debounce timer
void MainWindow::markEdited() {
    setWindowModified(true);
    if (_settings->value("autosave_enabled", true).toBool()) {
        int intervalSec = _settings->value("autosave_interval", 120).toInt();
        _autoSaveTimer->start(intervalSec * 1000);
    }
}
```

Auto-save slot:

```cpp
void MainWindow::performAutoSave() {
    if (!file || file->saved()) return;

    QString backupPath;
    if (!file->path().isEmpty()) {
        // Named file → sidecar: "MySong.mid" → "MySong.mid.autosave"
        backupPath = file->path() + ".autosave";
    } else {
        // Untitled → AppData temp dir
        QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                    + "/autosave";
        QDir().mkpath(dir);
        backupPath = dir + "/untitled_autosave.mid";
    }

    if (file->save(backupPath)) {
        // Don't mark as saved — the *real* file is still dirty
        file->setSaved(false);
        setStatus("Auto-saved", "gray");   // brief status bar feedback
    }
}
```

**On clean exit / manual save:** Delete the `.autosave` sidecar:

```cpp
// After successful save():
QFile::remove(file->path() + ".autosave");

// In closeEvent() after user confirms discard:
QFile::remove(file->path() + ".autosave");
```

**Files:** `MainWindow.h`, `MainWindow.cpp`
**Effort:** 1-2h

### 13.2 — Crash Recovery Dialog ✅

On startup, before loading the initial file:

```cpp
void MainWindow::checkAutoSaveRecovery() {
    // Check 1: sidecar for the file being opened
    if (QFile::exists(filePath + ".autosave")) {
        // Ask user: "A backup from a previous session was found. Recover?"
    }

    // Check 2: untitled backups in AppData
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                + "/autosave";
    QDir autoDir(dir);
    QStringList backups = autoDir.entryList({"*.mid"}, QDir::Files);
    // Offer to recover each one
}
```

Show a simple `QMessageBox::question`:
- **Recover** → load the `.autosave` file, mark as dirty (user can then Save As)
- **Discard** → delete the `.autosave` file, continue normally

**Files:** `MainWindow.cpp`
**Effort:** 1h

### 13.3 — Settings UI ✅

Add auto-save options to settings. Either a new "General" tab or extend `PerformanceSettingsWidget`:

- **Enable auto-save** — checkbox (default: ON)
- **Auto-save interval** — spin box, 30-600 seconds (default: 120 = 2 minutes)
- **Save to backup file** — radio: "Backup file only" (safe default) / "Overwrite original"

QSettings keys:
- `autosave_enabled` (bool, default `true`)
- `autosave_interval` (int seconds, default `120`)
- `autosave_overwrite` (bool, default `false` — Tier A opt-in)

**Files:** New `AutoSaveSettingsWidget.cpp/.h` or extend `PerformanceSettingsWidget`
**Effort:** 30min

### 13.4 — Status Bar Feedback ✅

Brief non-intrusive feedback when auto-save fires:

- Show "Auto-saved" in the MidiPilot status bar or the main window status bar for 3 seconds
- Use the existing `setStatus()` pattern or add a temporary label

**Files:** `MainWindow.cpp`
**Effort:** 15min

### Implementation Order

```
Phase 13.1  Auto-save timer & backup file       ✅ DONE (v1.1.3.1)
Phase 13.2  Crash recovery dialog                ✅ DONE (v1.1.3.1)
Phase 13.3  Settings UI                          ✅ DONE (v1.1.3.1)
Phase 13.4  Status bar feedback                  ✅ DONE (v1.1.3.1)
```

### Estimated Complexity

| Sub-phase | New Code | Modified Code | Risk |
|-----------|----------|---------------|------|
| 13.1 Timer & backup | ~50 lines | MainWindow (~20 lines) | Low — writes to separate file |
| 13.2 Recovery dialog | ~40 lines | MainWindow (~10 lines) | Low — only on startup |
| 13.3 Settings UI | ~80 lines (new widget) | SettingsDialog (~2 lines) | None |
| 13.4 Status feedback | ~10 lines | MainWindow (~5 lines) | None |

### Key Considerations

- **Tier B (backup sidecar) is the safe default.** It never overwrites the user's file.
  The user explicitly saves when they want to commit changes. The `.autosave` sidecar
  is only a crash recovery net.
- **Tier A (overwrite original) is opt-in.** Some users want true auto-save (like VS Code).
  This requires the file to already have a path (not untitled). Risky if the user wants
  to revert — but undo history is in memory anyway, so losing the on-disk original is
  recoverable as long as the app doesn't crash.
- **Performance:** `MidiFile::save()` is synchronous and fast (<100ms for typical files).
  For very large files (100k+ events), consider `QTimer::singleShot` + worker thread
  in a future optimization. Not needed for v1.
- **Debounce, not interval:** The timer resets on every edit. If the user is actively
  editing, auto-save waits until they pause. This avoids saving mid-operation and
  prevents performance hiccups during rapid editing.
- **`file->setSaved(false)` after backup save:** Critical. The backup save must NOT mark
  the file as "saved" — otherwise the close-event dialog won't warn about unsaved changes.
- **NOT implementing full journaling/WAL** — overkill for a MIDI editor. Simple file-level
  backup is sufficient.

---

## Phase 14: Split Channels to Tracks ✅ (v1.1.4)

> **Goal:** One-click conversion of MIDI Format 0 files (single track, multiple channels)
> into Format 1 style (one track per channel). This is the most common layout for GM MIDI
> files downloaded from the internet — all instruments live on one track, distinguished
> only by MIDI channel. This makes editing painful because you can't hide/solo/recolor
> individual instruments.
>
> **Motivation:** Users frequently work with GM MIDI files where all 16 channels are
> crammed into a single track. To edit instrument parts individually (move, copy, delete,
> mute), they need each channel on its own track. Currently this requires tedious manual
> work: select all events from channel N → move to new track → repeat 15 times.

### Background & Code Analysis

**Current event storage model:**
- Events are stored **per-channel** in `MidiChannel::eventMap()` (`QMultiMap<int, MidiEvent*>`)
- Each event has a track pointer: `MidiEvent::track()` / `MidiEvent::setTrack()`
- `setTrack()` only changes the track pointer — events stay in their channel's eventMap
- This means splitting channels to tracks is a **track-pointer reassignment**, not a data move

**Existing infrastructure we can reuse:**
- `MidiFile::addTrack()` — creates a new track, appends to track list
- `MidiTrack::setName(QString)` — set track name
- `MidiTrack::assignChannel(int)` — assign display channel
- `MidiEvent::setTrack(MidiTrack*, bool toProtocol)` — reassign track (with undo support)
- `MidiFile::gmInstrumentName(int prog)` — full 128-entry GM instrument name table
- `ProgChangeEvent::program()` — read program number for auto-naming
- `Explode Chords to Tracks` — reference implementation for track creation + event moving
- `FFXIVChannelFixer` — reference for channel analysis + progress callback pattern

**Key insight:** Since events are stored by channel (not by track), splitting channels
to tracks only requires calling `setTrack()` on each event. No `moveToChannel()` needed.
The channel stays the same — we just give each channel its own track.

### Design

**Algorithm:**

```
1. Scan: For each channel 0-15, collect all events on the source track(s)
2. Filter: Skip channels with 0 events (nothing to split)
3. Create: For each active channel → new MidiTrack
4. Name:   Read first ProgChangeEvent on that channel →
           gmInstrumentName(prog) → track name
           Channel 9 → "Drums" (unless user opts to skip drums)
5. Move:   setTrack(newTrack) on all events of that channel
6. Clean:  Remove empty source track(s) if requested
```

**Dialog options:**
- Source: "All tracks" (default for Format 0) or "Selected track only"
- Drums: ☐ "Keep Channel 9 (Drums) on original track" (default: unchecked)
- Naming: Auto-name from Program Change (default: on)
- Position: Insert new tracks after source / at end
- Original: ☐ "Remove empty source track after split" (default: checked)
- Preview: Shows table of Channel → Program → Track Name before executing

### 14.1 — Channel Analysis & Split Engine ✅

Core logic in a new method `MainWindow::splitChannelsToTracks()`:

```cpp
void MainWindow::splitChannelsToTracks() {
    if (!file) return;

    // Phase 1: Analyze — count events per channel per track
    struct ChannelInfo {
        int channel;
        int eventCount;
        int programNumber;    // from first ProgChangeEvent, or -1
        QString instrumentName;
        MidiTrack *sourceTrack;
    };
    QList<ChannelInfo> activeChannels;

    for (int ch = 0; ch < 16; ++ch) {
        QMultiMap<int, MidiEvent*> *emap = file->channel(ch)->eventMap();
        int count = 0;
        int prog = -1;
        MidiTrack *srcTrack = nullptr;
        for (auto it = emap->begin(); it != emap->end(); ++it) {
            MidiEvent *ev = it.value();
            // Only count events on source track(s)
            if (!srcTrack) srcTrack = ev->track();
            count++;
            if (prog < 0) {
                ProgChangeEvent *pc = dynamic_cast<ProgChangeEvent*>(ev);
                if (pc) prog = pc->program();
            }
        }
        if (count > 0) {
            QString name = (ch == 9) ? tr("Drums")
                         : (prog >= 0) ? MidiFile::gmInstrumentName(prog)
                         : tr("Channel %1").arg(ch);
            activeChannels.append({ch, count, prog, name, srcTrack});
        }
    }

    // Phase 2: Show confirmation dialog (see 14.2)
    // Phase 3: Create tracks and move events
    file->protocol()->startNewAction(tr("Split channels to tracks"));

    for (const auto &info : activeChannels) {
        if (skipDrums && info.channel == 9) continue;

        file->addTrack();
        MidiTrack *dst = file->tracks()->last();
        dst->setName(info.instrumentName);
        dst->assignChannel(info.channel);

        // Move all events on this channel to the new track
        QMultiMap<int, MidiEvent*> *emap = file->channel(info.channel)->eventMap();
        for (auto it = emap->begin(); it != emap->end(); ++it) {
            MidiEvent *ev = it.value();
            if (ev->track() == info.sourceTrack) {
                ev->setTrack(dst);
            }
        }
    }

    file->protocol()->endAction();
    updateAll();
}
```

**Key points:**
- Only moves events from the source track — events on other tracks are untouched
- `setTrack()` with `toProtocol=true` (default) enables full undo with Ctrl+Z
- OffEvents (`NoteOff`) inherit the track from `setTrack()` — no special handling needed
  because `OffEvent` is also a `MidiEvent` and iterating the channel's eventMap picks
  them up too
- Performance: O(N) where N = total events. Even 100k events should complete in <100ms

**Files:** `MainWindow.h`, `MainWindow.cpp`
**Effort:** 2-3h

### 14.2 — Split Channels Dialog ✅

A dialog similar to `ExplodeChordsDialog` that shows a preview and options:

```
┌─────────────────────────────────────────────────────┐
│  Split Channels to Tracks                            │
├─────────────────────────────────────────────────────┤
│                                                      │
│  Source: Track 0 "Untitled track" (1 track, 12 ch)  │
│                                                      │
│  ┌──────┬────────┬──────────────────────┬──────┐    │
│  │  CH  │  Prog  │  Track Name          │ Notes│    │
│  ├──────┼────────┼──────────────────────┼──────┤    │
│  │   0  │    0   │  Acoustic Grand Piano│  342 │    │
│  │   1  │   25   │  Acoustic Guitar     │  128 │    │
│  │   2  │   48   │  String Ensemble 1   │  256 │    │
│  │   3  │   73   │  Flute               │   64 │    │
│  │   9  │   —    │  Drums               │  512 │    │
│  └──────┴────────┴──────────────────────┴──────┘    │
│                                                      │
│  ☐ Keep Channel 9 (Drums) on original track         │
│  ☑ Auto-name tracks from GM program                  │
│  ☑ Remove empty source track after split             │
│  ○ Insert after source track                         │
│  ○ Insert at end                                     │
│                                                      │
│              [ Cancel ]        [ Split ]              │
└─────────────────────────────────────────────────────┘
```

**Files:** New `src/gui/SplitChannelsDialog.h/cpp`
**Effort:** 1-2h

### 14.3 — Menu & Toolbar Integration ✅

- Menu entry: `Tools → Split Channels to Tracks` (shortcut: `Ctrl+Shift+E`)
- Optional toolbar button (reuse existing toolbar migration pattern)
- Disabled when file has only 1 channel with events (nothing to split)
- Protocol action name: "Split channels to tracks"
- Status bar feedback: "Split N channels into N tracks"

**Files:** `MainWindow.cpp` (menu setup)
**Effort:** 30min

### 14.4 — Smart Detection & Auto-Prompt ✅

Optional quality-of-life enhancement:

- On file open, detect if the file is Format 0 (single track, multiple channels)
- Show a non-modal info bar: "This file has N instruments on 1 track.
  [Split to Tracks] [Dismiss]"
- Only shows once per file (remember in QSettings by file hash or path)

**Files:** `MainWindow.cpp` (in `openFile()`)
**Effort:** 1h

### Implementation Order

```
Phase 14.1  Channel analysis & split engine      ✅
Phase 14.2  Split channels dialog (preview + options) ✅
Phase 14.3  Menu & toolbar integration            ✅
Phase 14.4  Smart detection & auto-prompt         ✅
```

### Estimated Complexity

| Sub-phase | New Code | Modified Code | Risk |
|-----------|----------|---------------|------|
| 14.1 Split engine | ~80 lines | MainWindow (~5 lines) | Low — uses existing setTrack() |
| 14.2 Dialog | ~150 lines (new widget) | — | Low — display only |
| 14.3 Menu integration | ~15 lines | MainWindow (~10 lines) | None |
| 14.4 Auto-detection | ~30 lines | MainWindow (~15 lines) | Low — non-modal, optional |

### Key Considerations

- **`setTrack()` is protocol-aware** — every event move is recorded for undo. A file
  with 10,000 events creates 10,000 protocol entries. For very large files, consider
  wrapping in a single `startNewAction()` / `endAction()` (already planned).
- **OffEvents:** When iterating `channelEvents`, both NoteOn and OffEvent are present.
  `setTrack()` on OffEvent works normally — no need for special pairing logic.
  However, `OffEvent` shares the same channel as its `NoteOnEvent`, so iterating by
  channel naturally picks up both.
- **Program Changes at tick 0:** If a channel has a `ProgChangeEvent` at tick 0, it
  stays with the new track (moved via `setTrack()`). This is the desired behavior — each
  track gets its own program change.
- **Meta events (tempo, time sig):** These are on channel 17 (special channel) and
  track 0. They are NOT affected by the split because we only iterate channels 0-15.
- **Multi-track source files:** If the file already has multiple tracks with events on
  different channels, the dialog should handle this gracefully — either split per-track
  or combine all events per-channel across tracks.

---

## Phase 15: Auto-Updater ✅ DONE (v1.1.5)

> **Goal:** Replace the current "open browser to download" update flow with a fully
> integrated in-app auto-updater that downloads, extracts, and installs new versions
> automatically — without requiring user intervention beyond a single confirmation click.
>
> **Motivation:** The current `UpdateChecker` detects new releases via GitHub API and
> shows a dialog, but clicking "Yes" merely opens the browser to the release page. The
> user must then manually download the ZIP, close the app, extract over the old folder,
> and restart. This friction discourages frequent updates. A seamless auto-updater keeps
> users on the latest version with zero effort.
>
> **Constraint:** MidiEditor AI is a **portable app** (no installer, no admin rights,
> no registry entries). The app lives in a self-contained folder. The updater must work
> within these constraints — no MSI, no elevated permissions, no external update
> frameworks. The running EXE cannot overwrite itself, so file replacement must happen
> via an external batch/PowerShell script after the app exits.

### Background & Code Analysis

**Current update infrastructure (`UpdateChecker.h/cpp`):**
- `checkForUpdates()` → GitHub API `GET /repos/happytunesai/MidiEditor_AI/releases/latest`
- Parses `tag_name` → `QVersionNumber::fromString()` → compares with `applicationVersion()`
- Emits `updateAvailable(version, url)` → `MainWindow` shows QMessageBox → `QDesktopServices::openUrl()`
- Already has error handling and silent mode (startup check = silent, Help menu = verbose)
- Runs on startup with 2-second delay: `QTimer::singleShot(2000, ...)`

**Existing download infrastructure (`DownloadSoundFontDialog`):**
- Full HTTP download with `QNetworkAccessManager` + redirect policy
- `QProgressDialog` with cancel support
- Streaming write via `readyRead` → `QFile::write()`
- ZIP extraction using `tar -xf` (system command, Win10+)
- Error handling, partial file cleanup, success notification
- **This pattern can be reused almost 1:1 for the update download.**

**Release packaging (`release.bat`):**
- Produces `MidiEditorAI-v{VERSION}-win64.zip` containing flat app directory
- Contents: `MidiEditorAI.exe`, Qt DLLs, `metronome/`, `graphics/`, `midieditor.ico`, etc.
- ZIP built with PowerShell `Compress-Archive`

**GitHub API release assets:**
- `GET /repos/happytunesai/MidiEditor_AI/releases/latest` returns JSON with `assets[]`
- Each asset has `name` (e.g. `MidiEditorAI-v1.2.0-win64.zip`) and `browser_download_url`
- We can find the ZIP asset by matching pattern `MidiEditorAI-v*-win64.zip`

**Self-update constraint:**
- A running Windows EXE cannot be deleted or overwritten
- Solution: Launch external `updater.bat` that waits for app exit, then replaces files
- PowerShell `Expand-Archive` extracts ZIP contents
- Batch script restarts the app after replacement

**Save & restore state:**
- `MainWindow::closeEvent()` already handles save-before-close via `saveBeforeClose()`
- Currently loaded file path: `file->path()` (can pass as command-line arg on restart)
- `QSettings` stores all preferences, survives app restart

### Design

**User flow — "Update Now":**

```
1. UpdateChecker detects new version → emits updateAvailable(version, downloadUrl)
2. Dialog: "Version X.Y.Z available. Update now or after exit?"
   [Update Now]  [After Exit]  [Skip]
3. User clicks [Update Now]
4. Download ZIP with progress bar (reuse DownloadSoundFontDialog pattern)
5. Download complete → Confirmation: "Update ready. The app will save your work,
   restart, and reopen your current file. Continue?"  [OK] [Cancel]
6. Auto-save current file if modified (file->save())
7. Remember current file path → write to temp marker file or QSettings
8. Launch updater.bat with args: zipPath, appDir, exePath, midiFilePath
9. App exits (QApplication::quit())
10. updater.bat:
    a. Wait for MidiEditorAI.exe to exit (tasklist loop)
    b. Backup current exe → MidiEditorAI.exe.bak
    c. Extract ZIP over app directory (overwrite)
    d. Delete ZIP and backup
    e. Restart MidiEditorAI.exe with --open <midiFilePath> arg
```

**User flow — "After Exit":**

```
1. Same download as above (steps 1-4)
2. Store update info: zipPath saved in QSettings ("pending_update_zip")
3. User continues working normally
4. On MainWindow::closeEvent():
   a. Normal save-before-close flow
   b. If pending_update_zip exists in QSettings:
      - Remember current file path
      - Launch updater.bat with args
      - Clear QSettings key
5. updater.bat runs after exit (same as steps 10a-10e above)
```

**updater.bat pseudo-code:**

```batch
@echo off
REM === MidiEditor AI Auto-Updater ===
REM Args: %1=zipPath  %2=appDir  %3=exeName  %4=midiFilePath (optional)

:wait_loop
tasklist /FI "IMAGENAME eq %3" 2>NUL | find /I "%3" >NUL
if %ERRORLEVEL%==0 (
    timeout /t 1 /nobreak >NUL
    goto wait_loop
)

REM Backup current exe
if exist "%2\%3" rename "%2\%3" "%3.bak"

REM Extract update (overwrites existing files)
powershell -Command "Expand-Archive -Path '%1' -DestinationPath '%2' -Force"

REM The ZIP may contain a subfolder — move contents up if needed
for /d %%D in ("%2\MidiEditorAI-*") do (
    xcopy /s /e /y "%%D\*" "%2\" >NUL
    rmdir /s /q "%%D"
)

REM Cleanup
if exist "%1" del "%1"
if exist "%2\%3.bak" del "%2\%3.bak"

REM Restart app
if "%4"=="" (
    start "" "%2\%3"
) else (
    start "" "%2\%3" --open "%4"
)
```

### 15.1 — Extend UpdateChecker to Provide Download URL ⬜

**Changes to `UpdateChecker.h/cpp`:**

- Parse `assets` array from GitHub API response
- Find asset matching `MidiEditorAI-v*-win64.zip`
- Emit new signal: `updateAvailable(QString version, QString releaseUrl, QString zipDownloadUrl)`
- Store asset `size` for progress dialog

**New fields in signal:**
```cpp
signals:
    void updateAvailable(QString version, QString releaseUrl, QString zipDownloadUrl, qint64 zipSize);
```

**Estimated:** ~20 lines changed in `UpdateChecker.cpp`

### 15.2 — AutoUpdater Class (Download + Extract) ⬜

**New files: `src/gui/AutoUpdater.h` + `src/gui/AutoUpdater.cpp`**

Core class handling download and update orchestration:

```cpp
class AutoUpdater : public QObject {
    Q_OBJECT
public:
    explicit AutoUpdater(QWidget *parent = nullptr);

    // Start downloading the update ZIP
    void downloadUpdate(const QString &zipUrl, qint64 expectedSize);

    // Schedule update for after app exit (stores in QSettings)
    void scheduleUpdateOnExit();

    // Execute update now (saves file, launches updater, quits app)
    void executeUpdateNow(const QString &currentMidiPath);

    // Check if a pending update exists (called in closeEvent)
    bool hasPendingUpdate() const;

    // Launch the updater script for pending update (called in closeEvent)
    void launchPendingUpdate(const QString &currentMidiPath);

    // Cancel and clean up a pending download
    void cancelDownload();

signals:
    void downloadProgress(qint64 bytesReceived, qint64 bytesTotal);
    void downloadComplete(const QString &zipPath);
    void downloadFailed(const QString &error);
    void updateReady();  // ZIP downloaded, ready to apply

private:
    QNetworkAccessManager *_networkManager;
    QNetworkReply *_downloadReply;
    QFile *_downloadFile;
    QProgressDialog *_progressDialog;
    QString _downloadedZipPath;
    QString _newVersion;
};
```

**Key implementation details:**
- Download target: `QDir::temp()` / `MidiEditorAI-update.zip` (temp directory, not app dir)
- Progress dialog: Modal `QProgressDialog` with cancel
- `executeUpdateNow()`:
  1. Writes `updater.bat` to app directory (from embedded resource or generates at runtime)
  2. Calls `QProcess::startDetached("cmd.exe", {"/c", updaterPath, zipPath, appDir, exeName, midiPath})`
  3. Calls `QApplication::quit()`
- `scheduleUpdateOnExit()`: Stores zipPath in `QSettings("pending_update_zip")`
- `launchPendingUpdate()`: Same as executeUpdateNow but called from closeEvent

**Estimated:** ~200 lines (AutoUpdater.h ~50, AutoUpdater.cpp ~150)

### 15.3 — Update Decision Dialog ⬜

**New dialog shown when update is available:**

Three-button dialog replacing the current simple QMessageBox:

```
┌──────────────────────────────────────────────────────────┐
│  🔄 Update Available                                     │
│                                                          │
│  Version 1.2.0 is available!                             │
│  Current: 1.1.4.1                                        │
│                                                          │
│  📋 Changelog   📖 Manual                                │
│                                                          │
│  ┌───────────┐ ┌──────────┐ ┌───────────────┐ ┌────────┐│
│  │Update Now │ │After Exit│ │Download Manual│ │ Skip   ││
│  └───────────┘ └──────────┘ └───────────────┘ └────────┘│
│                                                          │
│  ℹ️ The app will save your work and                      │
│    restart automatically.                                │
└──────────────────────────────────────────────────────────┘

- **📋 Changelog:** Clickable link → opens `https://github.com/happytunesai/MidiEditor_AI/blob/main/CHANGELOG.md` in browser
- **📖 Manual:** Clickable link → opens `https://happytunesai.github.io/MidiEditor_AI/` in browser
- **Update Now:** Downloads → shows progress → confirmation ("App will restart and save. Continue?") → executes
- **After Exit:** Downloads in background → stores pending → applies on close
- **Download Manual:** Opens the GitHub release page in the browser (old behavior via `QDesktopServices::openUrl()`)
- **Skip:** Dismisses dialog, does nothing (same as current "No")

Both links are `QLabel` with `setOpenExternalLinks(true)` using `<a href="...">` — zero extra code, Qt handles the click.

**Estimated:** ~45 lines (can be inline in MainWindow or a small helper)

### 15.4 — MainWindow Integration ⬜

**Changes to `MainWindow.h/cpp`:**

1. Replace current `updateAvailable` lambda with new flow:
   - Show 3-button dialog (15.3)
   - On "Update Now": `autoUpdater->downloadUpdate()` → on complete → save + quit
   - On "After Exit": `autoUpdater->downloadUpdate()` → on complete → `scheduleUpdateOnExit()`

2. Modify `closeEvent()`:
   - After existing save logic, check `autoUpdater->hasPendingUpdate()`
   - If yes: `autoUpdater->launchPendingUpdate(file ? file->path() : "")`

3. Handle `--open` command-line argument on startup:
   - In `main.cpp` or `MainWindow` constructor, check for `--open <path>`
   - Call `loadFile(path)` if present

**Estimated:** ~60 lines changed in MainWindow + ~10 lines in main.cpp

### 15.5 — updater.bat Script ⬜

**File: `run_environment/updater.bat`** (shipped with the app)

The batch script that runs after the app exits:

- Waits for `MidiEditorAI.exe` to terminate (polling `tasklist`)
- Creates backup of current EXE (`.bak`)
- Extracts ZIP using PowerShell `Expand-Archive -Force`
- Handles nested folder in ZIP (release.bat creates `MidiEditorAI-v{ver}-win64/` subfolder)
- Cleans up: deletes ZIP, backup, and temp extraction artifacts
- Restarts the app, optionally with `--open <midi_path>`
- Includes error handling: if extraction fails, restores backup

**Estimated:** ~40 lines

### 15.6 — Testing & Edge Cases ⬜

**Test scenarios:**
1. ✅ Update Now with no file open → download, extract, restart (no --open arg)
2. ✅ Update Now with unsaved file → auto-save, restart with --open
3. ✅ Update Now with saved file → restart with --open
4. ✅ After Exit with pending update → close app → updater runs → restart
5. ✅ Cancel during download → cleanup partial files
6. ✅ Network error during download → error dialog, no state corruption
7. ✅ User clicks Skip → no action
8. ✅ No assets in release (source-only release) → graceful fallback
9. ✅ ZIP has nested subfolder → updater handles flattening
10. ✅ App crash before updater finishes → .bak files remain, manual recovery

**Edge cases handled:**
- GitHub rate limit (403) → error dialog with retry suggestion
- Download interrupted → partial ZIP deleted
- Updater.bat fails → backup EXE restored
- `--open` with non-existent file → app starts normally, ignores bad path
- Multiple instances → updater waits for all instances of MidiEditorAI.exe

### Implementation Order

```
Phase 15.1  Extend UpdateChecker (ZIP URL parsing)    ✅ DONE (v1.1.5)
Phase 15.2  AutoUpdater class (download + orchestrate) ✅ DONE (v1.1.5)
Phase 15.3  Update decision dialog (3-button)          ✅ DONE (v1.1.5)
Phase 15.4  MainWindow integration (closeEvent, --open) ✅ DONE (v1.1.5)
Phase 15.5  updater.bat script                         ✅ DONE (v1.1.5)
Phase 15.6  Testing & edge cases                       ✅ DONE (v1.1.5)
```

### Estimated Complexity

| Sub-phase | New Code | Modified Code | Risk |
|-----------|----------|---------------|------|
| 15.1 UpdateChecker extension | ~20 lines | UpdateChecker.h/cpp | Low — additive change |
| 15.2 AutoUpdater class | ~200 lines (new) | CMakeLists.txt (~2 lines) | Medium — download + process mgmt |
| 15.3 Update dialog | ~30 lines | — | None — UI only |
| 15.4 MainWindow integration | — | MainWindow.h/cpp (~60 lines), main.cpp (~10 lines) | Low — replaces existing lambda |
| 15.5 updater.bat | ~40 lines (new) | release.bat (~1 line to copy) | Medium — OS-level file ops |
| 15.6 Testing | — | — | — |
| **Total** | **~290 lines new** | **~90 lines modified** | **Medium overall** |

### Key Considerations

- **No new dependencies:** Uses only Qt's existing `QNetworkAccessManager`, `QProcess`,
  `QProgressDialog`. ZIP extraction via PowerShell `Expand-Archive` (built into Windows 10+).
- **Portable app safe:** No registry, no admin rights, no service. Just file copy + restart.
- **Rollback:** `.bak` file allows manual recovery if updater.bat fails mid-extraction.
- **GitHub API:** Same endpoint already used by UpdateChecker. Asset URL uses
  `browser_download_url` which follows redirects automatically.
- **Security:** ZIP comes from the project's own GitHub releases. The `browser_download_url`
  is HTTPS and authenticated by GitHub's CDN. No code execution from ZIP — only file extraction.
- **`run_environment/updater.bat`**: Shipped alongside the app in release builds. Added to
  `release.bat` asset copy section.

---

## Phase 16: Guitar Pro Import (.gp3 through .gp7/.gp8) ✅ DONE

> **Goal:** Add native Guitar Pro file import to MidiEditor AI, supporting all major
> Guitar Pro formats: GP3, GP4, GP5 (binary), GP6/GPX (BCFZ compressed XML), and
> GP7/GP8 (ZIP-packaged XML). Guitar Pro is the most popular guitar tablature format
> with millions of files available online. Importing these directly gives users instant
> access to a huge library of songs.
>
> **Motivation:** Guitar Pro files (.gp3, .gp4, .gp5, .gpx, .gp) are extremely common
> in the guitar/music community. Users previously had to convert these to MIDI using
> external tools before opening them in MidiEditor. Native import eliminates this friction.
>
> **Origin:** The Meowchestra/MidiEditor upstream fork contained an **unfinished Guitar Pro
> parser** (`src/midi/GuitarPro/`) with a basic structure for GP3-GP7 reading, but it was
> non-functional — binary parsers had field-ordering bugs and missing overrides, BCFZ
> decompression used the wrong algorithm (zlib instead of custom LZ77), ZIP extraction
> failed on files with data descriptors, and XML node lookups returned wrong elements.
> We took this unfinished upstream code as a starting point and fixed all the bugs to
> make every format actually work with real-world files.

### Background & Format Overview

Guitar Pro files come in two families:

**Binary formats (GP3, GP4, GP5):**
- Sequential binary data: header → global info → tracks → measures → beats → notes
- Version identified by magic string: `FICHIER GUITAR PRO vX.YY`
- GP3 (v3.00): basic tabs, no effects
- GP4 (v4.00-4.06): added lyrics, RSE, key signatures
- GP5 (v5.00-5.10): added RSE2, extended note effects, alternate endings
- Each version adds fields — parsers must skip/read the correct number of bytes

**XML formats (GP6, GP7/GP8):**
- GP6 (.gpx): BCFZ-compressed GPIF XML (custom bit-level LZ77 compression)
- GP7/GP8 (.gp): Standard ZIP archive containing `score.gpif` XML file
- Both share the same GPIF XML schema: Score, MasterTrack, Tracks, MasterBars,
  Bars, Voices, Beats, Notes, Rhythms
- Conversion: GPIF XML → synthetic GP5 in-memory structure → MIDI

### Architecture

```
GpImporter (entry point)
├── Gp345Parser (binary formats)
│   ├── Gp3Parser : Gp345Parser   → .gp3 files
│   ├── Gp4Parser : Gp3Parser     → .gp4 files
│   └── Gp5Parser : Gp4Parser     → .gp5 files
└── Gp678Parser (XML formats)
    ├── Gp6Parser : Gp678Parser   → .gpx files (BCFZ)
    └── Gp7Parser : Gp678Parser   → .gp files (ZIP)

GpUnzip  — ZIP extraction for .gp files (central directory parser)
GpBitStream — bit-level reader for BCFZ decompression
```

**GpImporter** detects format from file header:
- `FICHIER GUITAR PRO v3` → Gp3Parser
- `FICHIER GUITAR PRO v4` → Gp4Parser
- `FICHIER GUITAR PRO v5` → Gp5Parser
- `BCFZ` header (0x42434653) → Gp6Parser (BCFZ decompression → GPIF XML)
- `PK` header (0x504B) → Gp7Parser (ZIP extraction → GPIF XML)

### 16.1 — GP3/GP4/GP5 Binary Parser Fixes ✅

The upstream Meowchestra code had a basic `Gp345Parser` skeleton but it failed on
every real-world GP3/GP4/GP5 file due to several critical bugs:

**Fixes applied to `Gp345Parser.cpp`:**
- **readNoteEffects override (GP5):** GP5 files have extended note effects with
  additional bytes. Added `Gp5Parser::readNoteEffects()` override that reads the
  GP5-specific effect flags and associated data blocks
- **readNote field order:** Fixed incorrect field ordering — `accentuatedNote` and
  `ghostNote` flags must be read in the specific order defined by each GP version
- **EOF guard:** Added boundary checking to prevent reads past end of data buffer,
  gracefully stopping parse instead of crashing on truncated files
- **Removed stray brace and unused includes** after debug cleanup

**Test results:**
- GP3: U2 - Lemon.gp3 → 9 tracks, 199 measures ✅
- GP4: Sakuran - Sakuran.gp4 → 5 tracks, 137 measures ✅
- GP5: Float - Flogging-Molly.gp5 → 5 tracks, 74 measures ✅

**Files:** `src/midi/GuitarPro/Gp345Parser.cpp`, `src/midi/GuitarPro/Gp345Parser.h`

### 16.2 — GP6 (.gpx) BCFZ Decompression ✅

GP6 files use a custom bit-level LZ77 compression called **BCFZ** (not zlib!).
The upstream Meowchestra code incorrectly attempted zlib `inflate()` on BCFZ data,
causing hangs and crashes. The decompression was completely rewritten.

**BCFZ algorithm (ported from C# BardMusicPlayer/LightAmp):**
```
Input: raw BCFZ data (after 4-byte "BCFZ" magic + 4-byte uncompressed size)
Output: decompressed byte array containing GPIF XML

Loop until input exhausted:
  1. Read 1 bit (compressed flag)
  2. If compressed (bit = 1):
     a. Read 4 bits big-endian → wordSize (minimum 2)
     b. Read wordSize bits little-endian → offset
     c. Read wordSize bits little-endian → length
     d. Back-reference copy: copy (length) bytes from output at (output.size - offset)
  3. If literal (bit = 0):
     a. Read 2 bits little-endian → byteCount (add 1, so 1-4 bytes)
     b. Read (byteCount) raw bytes from input
     c. Append bytes to output

After decompression:
  - Search output for "<GPIF" or "<?xml" marker
  - Extract from marker to "</GPIF>" closing tag
  - Return as XML string for GPIF parsing
```

**Key classes used:**
- `GpBitStream`: Provides `getBit()`, `getBitsBE(n)`, `getBitsLE(n)`, `getByte()`
  for reading individual bits from the compressed data stream

**Test results:**
- GP6: Guns N' Roses - Sweet Child O Mine.gpx → 6 tracks, 180 measures, 67692 bytes MIDI ✅

**Files:** `src/midi/GuitarPro/Gp678Parser.cpp` (`decompressGPX()` completely rewritten)

### 16.3 — GP7/GP8 (.gp) ZIP Extraction ✅

GP7/GP8 files are standard ZIP archives containing a `score.gpif` XML file
(plus optional binary resources for RSE sounds, images, etc.).

**ZIP parsing approach (central directory):**
- The upstream `GpUnzip` used local file headers, which have unreliable sizes
  when data descriptors are present (common in GP7 files)
- Solution: Rewrote to parse the **ZIP central directory** from the end of the file:
  1. Scan backwards for EOCD signature (`0x06054b50`)
  2. Read central directory offset and count from EOCD
  3. Parse central directory entries — each has correct compressed/uncompressed sizes
  4. Use these sizes to extract the correct file data from local entries
- `inflateData()` uses Qt6::ZlibPrivate for zlib decompression of DEFLATE entries

**Extraction flow:**
```
.gp file → GpUnzip::parseEntries() (central directory)
         → Find "score.gpif" entry
         → GpUnzip::inflateData() (zlib decompress)
         → GPIF XML string
         → Gp7Parser::readSong() (shared GPIF parser)
```

**Build integration:**
- `CMakeLists.txt`: Added `Qt6::ZlibPrivate` as fallback when system ZLIB not found
- `GP678_ENABLED` CMake variable controls inclusion of GP678 source files

**Test results:**
- GP7: The Mirror.gp → 6 tracks, 147 measures, 52939 bytes MIDI ✅

**Files:** `src/midi/GuitarPro/GpUnzip.h`, `src/midi/GuitarPro/GpUnzip.cpp`,
`CMakeLists.txt`

### 16.4 — GPIF XML → GP5 Conversion ✅

Both GP6 and GP7/GP8 share the same GPIF XML format. The parser converts GPIF
nodes into a synthetic GP5 in-memory structure, which then converts to MIDI:

**GPIF XML structure:**
```xml
<GPIF>
  <Score>           → title, subtitle, artist, album
  <MasterTrack>     → track IDs, automations
  <Tracks>          → track definitions (name, instrument, channel, tuning)
  <MasterBars>      → measure structures (time signature, key, repeats)
  <Bars>            → bar content per track
  <Voices>          → voice content within bars
  <Beats>           → beat durations and note references
  <Notes>           → individual note properties (fret, string, effects)
  <Rhythms>         → rhythm definitions (note value, dots, tuplets)
</GPIF>
```

**Critical fix — `directOnly` node lookup:**
The upstream code used `getSubnodeByName()` without depth restriction.
All top-level GPIF node lookups (`Score`, `MasterTrack`, `Tracks`, `MasterBars`,
`Bars`, `Voices`, `Beats`, `Notes`, `Rhythms`) now use `directOnly=true` parameter.
Without this, `getSubnodeByName("Bars")` could find a nested `<Bars>` element
inside a `<MasterBar>` node (with 0 subnodes) before finding the top-level
`<Bars>` collection (with 882 subnodes), causing silent data loss.

**Conversion pipeline:**
```
GPIF XML → parse nodes → build GP5 tracks/measures/beats/notes
         → Gp678Parser::readSong() populates self
         → GpImporter transfers to MidiFile
```

**Files:** `src/midi/GuitarPro/Gp678Parser.cpp` (`gp6NodeToGP5File()`),
`src/midi/GuitarPro/Gp678Parser.h`

### 16.5 — Debug Logging Cleanup ✅

During development, file-based debug logging (`gpLog()`) was added to diagnose
parser issues (Qt's `qWarning`/`qDebug` doesn't output to stderr on Windows GUI
apps). All debug logging was removed after all formats were verified working:

- Removed `gpLog()` function definition and all call sites from `GpImporter.cpp`
- Removed `gpParserLog()` function and all call sites from `Gp345Parser.cpp`
- Removed `#include <cstdio>` and `#include <fstream>` debug includes
- Removed hit/miss counters and debug-only variables from `Gp678Parser.cpp`
- Production code uses only `qWarning` for genuine error conditions

**Files:** `src/midi/GuitarPro/GpImporter.cpp`, `src/midi/GuitarPro/Gp345Parser.cpp`,
`src/midi/GuitarPro/Gp678Parser.cpp`

### 16.6 — GP1/GP2 (.gtp) Legacy Format Support ✅

Added support for the oldest Guitar Pro formats (v1.0–v2.21, `.gtp` extension).
These files were created by the original DOS-era Guitar Pro and use a simple
binary layout with no compression.

**Implementation:**
- **Gp1Parser** — reads GP v1.0–v1.04 files (identified by French header "GUITARE").
  Parses title, artist, tempo, measures, tracks with note/duration/string data.
  Duration decoding uses a lookup table (whole → 64th note).
- **Gp2Parser** — extends `Gp1Parser` for GP v2.20–v2.21. Adds triplet-feel flag,
  per-measure repeat markers, and per-track capo/string-count fields.
- **Header-based detection** in `GpImporter`: first bytes checked for "GUITARE"
  (→ GP1) or "FICHIER GUITAR PRO" (→ GP2). Falls back to GP2 for any `.gtp`
  file with an unrecognized header.
- Ported from TuxGuitar's `GP1InputStream.java` / `GP2InputStream.java` reference
  implementation, adapted to the existing C++ parser architecture.

**Files:** `src/midi/GuitarPro/Gp12Parser.h`, `src/midi/GuitarPro/Gp12Parser.cpp`,
`src/midi/GuitarPro/GpImporter.cpp`

### Implementation Order

```
Phase 16.1  GP3/GP4/GP5 binary parser fixes             ✅ DONE
Phase 16.2  GP6 (.gpx) BCFZ decompression               ✅ DONE
Phase 16.3  GP7/GP8 (.gp) ZIP extraction                ✅ DONE
Phase 16.4  GPIF XML → GP5 conversion + node lookup fix  ✅ DONE
Phase 16.5  Debug logging cleanup                        ✅ DONE
Phase 16.6  GP1/GP2 (.gtp) legacy format support         ✅ DONE
```

### Regression Test Results

| Format | File | Tracks | Measures | Status |
|--------|------|--------|----------|--------|
| GP1 | You've Got Something There.gtp | 8 | 12 | ✅ |
| GP3 | U2 - Lemon.gp3 | 9 | 199 | ✅ |
| GP4 | Sakuran - Sakuran.gp4 | 5 | 137 | ✅ |
| GP5 | Float - Flogging-Molly.gp5 | 5 | 74 | ✅ |
| GP6 | Guns N' Roses - Sweet Child O Mine.gpx | 6 | 180 | ✅ |
| GP7 | The Mirror.gp | 6 | 147 | ✅ |

### Key Considerations

- **BCFZ ≠ zlib:** This was the #1 bug. GP6's BCFZ compression is a custom bit-level
  LZ77 algorithm, NOT zlib/DEFLATE. The original code used `inflate()` which hung/crashed.
  Algorithm was reverse-engineered from the C# BardMusicPlayer/LightAmp source code.
- **Central directory for ZIP:** GP7 ZIP files use data descriptors after local entries,
  making local file header sizes unreliable. Parsing the central directory (from the end
  of the file) provides correct sizes for every entry.
- **Node lookup depth matters:** GPIF XML has nested elements with the same name as
  top-level collections (e.g., `<Bars>` inside `<MasterBar>`). Using `directOnly=true`
  ensures we find the correct top-level collection nodes.
- **Qt6::ZlibPrivate:** Qt6 bundles zlib internally. Using `Qt6::ZlibPrivate` as a
  CMake fallback avoids requiring a separate zlib installation on the build system.
- **File detection is header-based:** Format detected from first bytes, not file extension.
  This handles misnamed files correctly.
- **All parsers share common base:** GP3→GP4→GP5 use C++ inheritance chain.
  GP6/GP7 share `Gp678Parser` base with format-specific decompression/extraction.
- **GP1/GP2 are the oldest formats:** Simple binary layout, no compression. GP1 uses
  French header "GUITARE", GP2 uses "FICHIER GUITAR PRO". Gp2Parser extends Gp1Parser
  via inheritance, matching the GP3→GP4→GP5 pattern.

---

## Phase 17: Modern UI Facelift (Dark/Light QSS Themes) ✅ DONE

> **Goal:** Give MidiEditor AI a polished, modern look with proper Dark and Light themes
> using Qt Style Sheets (QSS), inspired by the project's manual website CSS colour
> palette. Additionally preserve the original system-native appearance as "Classic" theme.
>
> **Motivation:** The original MidiEditor has a purely system-native look with no theming
> support. Modern DAWs and music production tools universally offer dark themes for
> reduced eye strain during long editing sessions. A cohesive visual design with rounded
> corners, subtle borders, and accent colors makes the app feel professional and current.
>
> **Key constraint:** Every theme must keep the editing surface (piano roll, velocity,
> misc widgets) fully readable and fluid. The custom-painted widgets (MatrixWidget,
> MiscWidget, ClickButton, etc.) get their colors from `Appearance::*Color()` methods,
> so they automatically adapt when `shouldUseDarkMode()` returns the correct value.

### Background & Code Analysis

**UI framework:** Qt6 QWidgets (NOT QML). All standard widgets are QSS-styleable.
Custom-painted widgets (~10) use `Appearance` color methods and `QPainter` directly.

**Existing infrastructure (pre-Phase 17):**
- `Appearance` class: Static singleton managing all colors, styles, dark mode detection
- `shouldUseDarkMode()`: System dark mode detection via `QPlatformTheme` / style name
- `adjustIconForDarkMode()`: Recolors black PNG icons to light gray via `CompositionMode_SourceAtop`
- `refreshAllIcons()` / `forceColorRefresh()`: Propagates theme changes to all widgets
- Inline `setStyleSheet()` calls on toolbars and list widgets (hardcoded colors)

**Color palette (from manual website CSS):**

| Token | Dark | Light |
|-------|------|-------|
| Background | `#0d1117` | `#ffffff` |
| Secondary bg | `#161b22` | `#f6f8fa` |
| Card bg | `#1c2129` | `#ffffff` |
| Text | `#e6edf3` | `#1f2328` |
| Muted text | `#8b949e` | `#656d76` |
| Accent | `#58a6ff` | `#0969da` |
| Border | `#30363d` | `#d0d7de` |

### Sub-phases

#### Phase 17.1 — QSS Theme Files & Infrastructure ✅ DONE

Create the core QSS stylesheets and wire them into the `Appearance` class.

**Files created:**
- `src/gui/themes/dark.qss` (~580 lines) — Complete dark theme
- `src/gui/themes/light.qss` (~570 lines) — Complete light theme

**Files modified:**
- `resources.qrc` — Added both QSS files as Qt resources
- `Appearance.h` — Added `Theme` enum (`ThemeSystem`, `ThemeDark`, `ThemeLight`, `ThemeNone`),
  `theme()`, `setTheme()` methods, `_theme` static member
- `Appearance.cpp`:
  - `init()` loads theme from QSettings
  - `writeSettings()` persists theme
  - `applyStyle()` loads QSS from `:/src/gui/themes/*.qss` based on active theme
  - `shouldUseDarkMode()` respects Theme enum before falling back to legacy detection
  - `setTheme()` with 500ms debounce + queued `forceColorRefresh()`

**QSS coverage:** QMainWindow, QMenuBar, QMenu, QToolBar, QToolButton, QTabWidget,
QTabBar, QDockWidget, QPushButton, QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox,
QCheckBox, QRadioButton, QSlider, QProgressBar, QScrollBar, QScrollArea, QGroupBox,
QSplitter, QStatusBar, QDialog, QToolTip, QListWidget, QTreeWidget, QTableWidget,
QHeaderView, QLabel, QFrame, QTextEdit, QPlainTextEdit

#### Phase 17.2 — Settings UI & Theme Selector ✅ DONE

Add the Theme dropdown to the Appearance settings page.

**Files modified:**
- `AppearanceSettingsWidget.h` — Added `themeChanged(int)` slot
- `AppearanceSettingsWidget.cpp`:
  - Theme QComboBox at row 8: "System (Auto)", "Dark", "Light", "Classic"
  - Application Style combo disabled (grayed out) when theme ≠ Classic
  - Lambda connection toggles style combo enabled state on theme change
  - `themeChanged()` calls `Appearance::setTheme()` + `refreshColors()`

#### Phase 17.3 — Inline Style Fixes ✅ DONE

Fix hardcoded inline `setStyleSheet()` calls that broke QSS cascade.

**Problem:** 9× `setStyleSheet("QToolBar { border: 0px }")` in MainWindow.cpp
overrode the app-level QSS for those toolbars, making button text black-on-dark.
Similarly, Track/Channel list widgets had hardcoded `lightGray` borders.

**Solution:** Added three helper methods to `Appearance`:
- `toolbarInlineStyle()` — Returns theme-aware toolbar + button QSS string
- `listBorderStyle()` — Returns theme-aware list item border QSS string
- Updated `toolbarBackgroundColor()` — Returns theme-matching colors for mini-toolbars

**Files modified:**
- `Appearance.h` / `Appearance.cpp` — Added helper methods
- `MainWindow.cpp` — All 9 inline toolbar styles → `Appearance::toolbarInlineStyle()`
- `TrackListWidget.cpp` — List border → `Appearance::listBorderStyle()`
- `ChannelListWidget.cpp` — List border → `Appearance::listBorderStyle()`
- `AppearanceSettingsWidget.cpp` — Color picker list borders → theme-aware

#### Phase 17.4 — Custom-Painted Widget Polish ✅ DONE

Fine-tune the ~10 custom-painted widgets that bypass QSS.

**Target widgets:**
- `MatrixWidget` / `OpenGLMatrixWidget` — Piano roll grid colors, selection highlight
- `MiscWidget` / `OpenGLMiscWidget` — Velocity/controller editor backgrounds
- `ClickButton` — Custom tool buttons (currently hardcoded colors)
- `ColoredWidget` — Track/channel color swatches (border color)
- `EventWidget` — Event property display
- Piano key column (left side of matrix)

**Approach:** All these widgets already call `Appearance::backgroundColor()`,
`foregroundColor()`, etc. Verify each one renders correctly in both themes.
Fix any remaining hardcoded `QColor(...)` literals that don't adapt.

#### Phase 17.5 — Icon Refinement ✅ DONE

Improve icon rendering for themed modes.

**Current state:** `adjustIconForDarkMode()` recolors black icons to `QColor(180,180,180)`.
Skip list: `load`, `new`, `redo`, `undo`, `save`, `saveas`, `stop_record`, `icon`, `midieditor`.

**Potential improvements:**
- Provide dedicated dark-mode icon variants (SVG or higher-quality PNGs)
- Adjust accent-colored icons for light theme (currently optimized for dark)
- Review skip list — some "colorful" icons may still be too dark on dark bg
- Consider SVG icons with CSS-driven fill colors for future-proofing

#### Phase 17.6 — Testing & Polish ✅ DONE

Final visual QA pass and edge case fixes.

**Test matrix:**
- All 4 themes: System, Dark, Light, Classic
- All 3 Application Styles: Windows, windowsvista, Fusion
- Key scenarios: Settings dialog, Track/Channel panels, About dialog,
  Instrument selector, MIDI setup wizard, MidiPilot panel, Protocol list
- Theme switching: Verify no crash/hang on rapid theme changes (debounce)
- Startup: Theme persisted correctly across sessions
- High-DPI: Verify icons and borders scale correctly at 125%/150%/200%

#### Phase 17.7 — Dark Title Bar (Windows DWM) ✅ DONE

Native Windows dark title bar using the DWM API.

**Completed steps:**
- ✅ `DwmSetWindowAttribute` with `DWMWA_USE_IMMERSIVE_DARK_MODE = 20` for dark title bar
- ✅ `DarkTitleBarFilter` event filter installed on `qApp` — catches all new windows (dialogs, popups)
- ✅ Applies to MainWindow and every child dialog automatically
- ✅ Respects theme: only active when `Appearance::shouldUseDarkMode()` returns true

**Files modified:** `Appearance.cpp`, `Appearance.h`

#### Phase 17.8 — MIDI Visualizer Widget ✅ DONE

Real-time 16-channel MIDI activity equalizer bars in the toolbar.

**Completed steps:**
- ✅ Created `MidiVisualizerWidget` — 16 vertical bars, one per MIDI channel
- ✅ Reads `MidiOutput::channelActivity[16]` atomic ints (written by player thread)
- ✅ Thread-safe via `atomic::exchange(0)` — read-and-clear in one operation
- ✅ Green-to-blue color interpolation based on velocity (dark & light themes)
- ✅ Smooth decay animation (DECAY_RATE=0.82, ~30fps refresh)
- ✅ Polls `MidiPlayer::isPlaying()` directly — resilient to broken signal connections
- ✅ Timer runs while widget is visible (`showEvent` starts, `hideEvent` stops)
- ✅ Right-aligned in toolbar via expanding spacer
- ✅ Appears in Customize Toolbar settings with checkbox toggle
- ✅ Custom `midi_visualizer.png` icon (black, auto-grayed for dark mode)
- ✅ Works in both single-row and double-row toolbar layouts
- ✅ Widget created fresh on each toolbar rebuild — avoids Qt ownership/destruction bugs
- ✅ Migration: auto-added for existing users who don't have it in saved settings

**Key design decision:** Does NOT use `QWidgetAction` (which transfers widget ownership
to the toolbar, causing destruction on toolbar rebuild). Instead, widget is created
directly via `toolbar->addWidget()` each time the toolbar is rebuilt.

**Files created:** `src/gui/MidiVisualizerWidget.h`, `src/gui/MidiVisualizerWidget.cpp`,
`run_environment/graphics/tool/midi_visualizer.png`

**Files modified:** `MainWindow.cpp` (4 toolbar build loops), `MainWindow.h`,
`LayoutSettingsWidget.cpp` (action catalog + all order lists), `Appearance.cpp`
(icon dark-mode adjustment), `resources.qrc`, `CMakeLists.txt`

#### Phase 17.9 — Note Bar Color Presets ✅ DONE

One-click color presets for Channel Colors and Track Colors in the editor.

**Completed steps:**
- ✅ Added `ColorPreset` enum: Default, Rainbow, Neon, Fire, Ocean, Pastel, Sakura, AMOLED, Emerald, Punk
- ✅ Rainbow: HSV color wheel, evenly spaced hues for all 17 channels
- ✅ Neon: Bright saturated neon colors (hot pink, electric blue, lime, etc.)
- ✅ Fire: Warm reds, oranges, yellows
- ✅ Ocean: Cool blues, teals, aquas
- ✅ Pastel: Soft low-saturation colors
- ✅ Sakura: Cherry blossom pinks, soft rose tones
- ✅ AMOLED: Warm orange/amber tones matching the AMOLED theme
- ✅ Emerald: Teal/green tones matching the Material Dark theme
- ✅ Punk: Hot pinks, electric purples, acid greens, neon yellow
- ✅ Dark-mode aware: presets auto-darken slightly for dark themes
- ✅ Settings UI: "Color Preset" combo in Appearance panel
- ✅ Preset selection persisted to QSettings (`_colorPreset` member + `color_preset` key)
- ✅ Combo box correctly shows saved preset when re-entering Settings
- ✅ Applies to both Channel Colors and Track Colors instantly

**Also fixed in this round:**
- ✅ Piano white keys brighter in dark mode (`QColor(170,170,170)` vs old 120)
- ✅ Icons now properly refresh on runtime Light→Dark theme switch
- ✅ `Appearance::refreshAllIcons()` called after every toolbar rebuild

**Files modified:** `Appearance.h`, `Appearance.cpp`,
`AppearanceSettingsWidget.cpp`, `MidiVisualizerWidget.h`, `MidiVisualizerWidget.cpp`,
`MainWindow.cpp`

#### Phase 17.10 — Sakura Theme ✅ DONE

Light cherry blossom theme with soft pink accents.

**Completed steps:**
- ✅ Rewrote `pink.qss` as a **light** Sakura theme (`#fff5f8` bg, `#db7093` accents, `#f0c0d0` borders)
- ✅ `ThemePink = 4` — `shouldUseDarkMode()` returns **false** (light theme)
- ✅ Renamed "Pink" → "Sakura" in Settings UI
- ✅ Sakura-specific editor colors: light pink strips, rose borders, pink toolbar inline style
- ✅ Piano keys slightly pink in Sakura theme (`#FFF0F5` lavender blush white keys, `#502837` dark rose black keys)
- ✅ Pink hover/selected states for piano keys
- ✅ Pink key line highlight (`#DB7093` pale violet red with alpha)

**Files created:** `src/gui/themes/pink.qss` (rewritten)

**Files modified:** `Appearance.h`, `Appearance.cpp`, `AppearanceSettingsWidget.cpp`,
`resources.qrc`

#### Phase 17.11 — AMOLED & Material Dark Themes ✅ DONE

Two additional dark themes inspired by GTRONICK/QSS (MIT License).

**Completed steps:**
- ✅ **AMOLED theme** (`ThemeAmoled = 5`) — pure black `#000000` backgrounds with orange `#e67e22` accents, ideal for OLED screens
- ✅ **Material Dark theme** (`ThemeMaterial = 6`) — dark charcoal `#1e1d23` backgrounds with teal `#04b97f` accents, Material Design aesthetic
- ✅ Created `amoled.qss` and `materialdark.qss` — full QSS files matching our dark.qss structure
- ✅ Theme-specific toolbar inline styles (matching accent colors)
- ✅ Theme-specific list border styles
- ✅ Both use `shouldUseDarkMode() == true` — all dark-mode color methods apply
- ✅ Dark title bar enabled for both themes
- ✅ Added to Settings → Appearance theme selector

**Files created:** `src/gui/themes/amoled.qss`, `src/gui/themes/materialdark.qss`

**Files modified:** `Appearance.h`, `Appearance.cpp`, `AppearanceSettingsWidget.cpp`,
`resources.qrc`

#### Phase 17.12 — Theme Change Restart Mechanism ✅ DONE

App restart on theme change to solve Qt icon cache issues (runtime icon refresh never fully solved).

**Completed steps:**
- ✅ `restartForThemeChange()` in MainWindow — saves current file, launches new process with `--open-settings` flag, terminates via `ExitProcess(0)`
- ✅ `--open-settings` CLI arg parsing in `main.cpp` — reopens Settings on Appearance tab after restart (300ms delay via QTimer)
- ✅ `setThemeValue()` in Appearance — saves theme to QSettings without applying style (for pre-restart persistence)
- ✅ `setCurrentTab()` in SettingsDialog — allows opening on a specific tab
- ✅ Confirmation dialog before restart: QMessageBox with "Restart" / "Cancel" buttons
- ✅ Cancel reverts combo box to current theme via `blockSignals()` pattern

**Files modified:** `main.cpp`, `Appearance.h`, `Appearance.cpp`,
`AppearanceSettingsWidget.cpp`, `MainWindow.h`, `MainWindow.cpp`,
`SettingsDialog.h`, `SettingsDialog.cpp`

#### Phase 17.13 — Color Preset Persistence Fix ✅ DONE

Bug fix: Color Preset combo always showed "Default" when re-entering settings.

**Completed steps:**
- ✅ Added `_colorPreset` static member to Appearance class
- ✅ Persisted via QSettings (`color_preset` key) in `init()` / `writeSettings()`
- ✅ Added `colorPreset()` getter
- ✅ Combo box calls `setCurrentIndex(Appearance::colorPreset())` on construction
- ✅ `applyColorPreset()` now updates `_colorPreset` member before applying colors

**Files modified:** `Appearance.h`, `Appearance.cpp`, `AppearanceSettingsWidget.cpp`

### Affected Files

**New files:**
- `src/gui/themes/dark.qss`
- `src/gui/themes/light.qss`
- `src/gui/themes/pink.qss`
- `src/gui/themes/amoled.qss`
- `src/gui/themes/materialdark.qss`

**Core modifications:**
- `src/gui/Appearance.h` / `Appearance.cpp`
- `src/gui/AppearanceSettingsWidget.h` / `AppearanceSettingsWidget.cpp`
- `src/gui/MainWindow.h` / `MainWindow.cpp`
- `src/gui/SettingsDialog.h` / `SettingsDialog.cpp`
- `src/main.cpp`
- `resources.qrc`

**Inline style fixes:**
- `src/gui/MainWindow.cpp` (9 toolbar inline styles)
- `src/gui/TrackListWidget.cpp`
- `src/gui/ChannelListWidget.cpp`

### Implementation Order

```
Phase 17.1  QSS theme files & Appearance infrastructure  ✅ DONE
Phase 17.2  Settings UI & theme selector                 ✅ DONE
Phase 17.3  Inline style fixes (toolbar/list readability) ✅ DONE
Phase 17.4  Custom-painted widget polish                  ✅ DONE (via Appearance color methods)
Phase 17.5  Icon refinement                               ✅ DONE (dark mode icon adjustment)
Phase 17.6  Testing & polish                              ✅ DONE
Phase 17.7  Dark title bar (Windows DWM)                  ✅ DONE
Phase 17.8  MIDI Visualizer widget                        ✅ DONE
Phase 17.9  Note bar color presets (10 presets)           ✅ DONE
Phase 17.10 Sakura theme (light cherry blossom)           ✅ DONE
Phase 17.11 AMOLED & Material Dark themes                 ✅ DONE
Phase 17.12 Theme change restart mechanism                ✅ DONE
Phase 17.13 Color preset persistence fix                  ✅ DONE
```

### Key Considerations

- **QSS has no `box-shadow`:** Unlike CSS, Qt Style Sheets don't support shadows.
  Depth is approximated with subtle border colors and background gradients.
- **Inline styles break cascade:** Qt's `widget->setStyleSheet()` overrides the
  app-level stylesheet for that widget's entire selector chain. Every inline style
  must re-declare colors/borders to stay theme-consistent. The helper methods
  `toolbarInlineStyle()` and `listBorderStyle()` centralize this.
- **Application Style vs Theme:** The base QStyle (Windows/Vista/Fusion) controls
  widget structure (how checkboxes, scrollbars, etc. are drawn). QSS paints over it.
  In themed mode, the style choice has minimal visual impact because QSS covers
  nearly everything — so the style combo is disabled for Dark/Light/System themes.
- **ThemeNone = Classic:** Preserves exact legacy behavior for users who prefer the
  original system-native look. No QSS loaded, style selector fully functional.
- **`shouldUseDarkMode()` priority chain:** ThemeDark/ThemeAmoled/ThemeMaterial→true, ThemeLight/ThemePink→false,
  ThemeSystem→`isDarkModeEnabled()`, ThemeNone→falls through to style-name-based
  legacy detection. This ensures all `Appearance::*Color()` methods return correct
  values regardless of which theme path is active.

---

## Phase 18: Mewo Upstream Feature Sync ✅ DONE

> **Goal:** Cherry-pick valuable features from the Meowchestra/MidiEditor upstream fork
> (commits after our fork point `25ebba3` / tag 4.3.1) that improve the editor experience.
> Mewo added **62 new commits** after our fork-point, covering UI improvements, new tools,
> and bug fixes. Features already ported (FluidSynth, GuitarPro) are excluded from this phase.
>
> **Motivation:** Mewo has active development with useful features we don't need to
> reinvent — context menus, note duration presets, smooth scrolling, timeline markers,
> status bar with chord detection, MML import, drum presets, and resize-tool improvements.
> Selectively porting these gives our users a better experience with minimal effort.
>
> **Source reference:** Meowchestra/MidiEditor.git — commits `28eb14c..8ec534f` (after fork-point `25ebba3`)

### Implementation Order

```
Phase 18.1   Null-Byte Terminator Fix (TextEvents)           ✅ DONE  (~6 lines)
Phase 18.2   Context Menu on MatrixWidget                     ✅ DONE  (~260 lines)
Phase 18.3   Note Duration Presets                            ✅ DONE  (~200 lines)
Phase 18.4   Smooth Playback Scrolling                        ✅ DONE  (~60 lines)
Phase 18.5   Timeline Markers (visual CC/PC/Text markers)     ✅ DONE  (~300 lines)
Phase 18.6   Status Bar with Chord Detection                  ✅ DONE  (~250 lines)
Phase 18.7   MML Importer (Music Macro Language)              ✅ DONE  (~600 lines)
Phase 18.8   DrumKit Preset Mapping                           ✅ DONE  (~350 lines)
Phase 18.9   SizeChangeTool Improvements                      ✅ DONE  (~120 lines)
Phase 18.10  DLS SoundFont Support                            ✅ DONE  (~5 lines)
Phase 18.11  Fixed Measure Number Position                    ✅ DONE  (~2 lines)
Phase 18.12  Separate MarkerArea + Pixel Hit-Testing          ✅ DONE  (~40 lines)
Phase 18.13  Full-Height Divider & Border Cleanup             ✅ DONE  (~6 lines)
Phase 18.14  TrackList/ChannelList refreshColors()            ✅ DONE  (~50 lines)
Phase 18.15  moveToChannel/removeEvent toProtocol Param       ✅ DONE  (~40 lines)
Phase 18.16  FFXIVChannelFixer Memory Leak Fix                ✅ DONE  (~4 lines)
```

### Estimated Complexity

| Sub-phase | New Code | Modified Code | Risk |
|-----------|----------|---------------|------|
| 18.1 Null-byte fix | ~0 | MidiEvent.cpp (~6 lines) | None |
| 18.2 Context menu | ~245 lines (in MatrixWidget) | MainWindow.h/cpp (~15 lines) | Low |
| 18.3 Note duration presets | ~140 lines (NewNoteTool) | MainWindow.cpp (~60 lines) | Low-Medium |
| 18.4 Smooth scrolling | ~40 lines (MatrixWidget) | MidiSettingsWidget (~20 lines) | Low |
| 18.5 Timeline markers | ~200 lines (MatrixWidget) | Appearance.h/cpp (~50 lines), AppearanceSettingsWidget (~50 lines) | Medium |
| 18.6 Status bar + chords | ~100 lines (ChordDetector) | MainWindow.cpp (~80 lines), StatusBarSettingsWidget (~70 lines) | Medium |
| 18.7 MML importer | ~600 lines (new dir) | MainWindow.cpp (~15 lines), CMakeLists.txt (~5 lines) | Medium |
| 18.8 DrumKit presets | ~350 lines (DrumKitPreset) | SplitChannelsDialog (~50 lines) | Low-Medium |
| 18.9 SizeChangeTool | ~0 | SizeChangeTool.cpp (~120 lines) | Low |
| 18.10 DLS support | ~0 | MidiSettingsWidget.cpp (~1 line), FluidSynthEngine.h (~1 line) | None |
| 18.11 Fixed measure numbers | ~0 | MatrixWidget.cpp (~2 lines) | None |
| 18.12 MarkerArea hit-testing | ~10 lines (MatrixWidget.h) | MatrixWidget.cpp (~30 lines) | Low |
| 18.13 Divider & border cleanup | ~0 | MatrixWidget.cpp (~6 lines) | None |
| 18.14 List refreshColors | ~40 lines (Track/ChannelListWidget) | Appearance.cpp (~4 lines) | Low |
| 18.15 toProtocol param | ~0 | MidiEvent.h/cpp, OnEvent.h/cpp, MidiChannel.h/cpp (~40 lines) | Low |
| 18.16 ChannelFixer leak fix | ~0 | FFXIVChannelFixer.cpp (~4 lines) | None |
| **Total** | **~1635 lines new** | **~560 lines modified** | **Low-Medium** |

---

### 18.1 — Null-Byte Terminator Fix (TextEvents) ✅

> **Mewo commit:** `01e7455` — "Remove terminator null bytes & SizeChangeTool Cleanup"

**Problem:** Some MIDI files contain `\0` null bytes in text event data (track names,
lyrics, markers). These null bytes cause text truncation and render as `[]` boxes in
Windows UI. Qt's `QString::fromUtf8()` does not strip them automatically.

**Fix (6 lines in MidiEvent.cpp):**

**File:** `src/MidiEvent/MidiEvent.cpp` — Lines 327-331

**Current code:**
```cpp
textData.append((char) tempByte);
}

// QString::fromUtf8() safely handles malformed UTF-8 by replacing
// invalid sequences with Unicode replacement characters (U+FFFD)
textEvent->setText(QString::fromUtf8(textData));
```

**Change to:**
```cpp
textData.append((char) tempByte);
}

// Remove any terminator null bytes which cause truncation
// and "[]" characters in Windows UI
int nullIdx = textData.indexOf('\0');
if (nullIdx != -1) {
    textData.truncate(nullIdx);
}

// QString::fromUtf8() safely handles malformed UTF-8 by replacing
// invalid sequences with Unicode replacement characters (U+FFFD)
textEvent->setText(QString::fromUtf8(textData).remove(QChar(0)).trimmed());
```

**Dependencies:** None
**Risk:** None — only removes garbage bytes from text events

---

### 18.2 — Context Menu on MatrixWidget ✅

> **Mewo commit:** `e98935f` — "Context Menu"

**Goal:** Right-click on selected events in the piano roll opens a context menu with
common operations: quantize, copy, delete, transpose, move to track/channel, scale, legato.

**Files to modify:**
| File | Change |
|------|--------|
| `src/gui/MatrixWidget.h` | Add `contextMenuEvent()` override declaration (after `wheelEvent`, ~line 490) |
| `src/gui/MatrixWidget.cpp` | Add `contextMenuEvent()` implementation (~245 lines, after `keyReleaseEvent`) |
| `src/gui/MainWindow.h` | Add `moveSelectedEventsToChannel(int channel)` and `moveSelectedEventsToTrack(int trackIdx)` int-based overloads (~2 lines, near existing QAction versions at line 444) |
| `src/gui/MainWindow.cpp` | Add int-based overloads (~20 lines each) delegating to existing QAction-based methods |

**Implementation details:**

1. **MatrixWidget.h** — Add in protected section (after `wheelEvent` at ~line 490):
   ```cpp
   void contextMenuEvent(QContextMenuEvent *event) override;
   ```

2. **MatrixWidget.cpp includes** — Add at top:
   ```cpp
   #include <QContextMenuEvent>
   #include <QMenu>
   #include <QInputDialog>
   #include "TransposeDialog.h"
   ```

3. **contextMenuEvent() implementation** — Guard conditions:
   - `Selection::instance()->selectedEvents().isEmpty()` → return (no menu on empty selection)
   - `!file` → return
   - `inDrag` → return (don't show menu while drawing/dragging)

4. **Menu structure:**
   ```
   ┌──────────────────────────────────┐
   │ Quantize Selection               │
   │ ──────────────────────────────── │
   │ Copy                             │
   │ Delete                           │
   │ ──────────────────────────────── │
   │ Transpose Selection...           │
   │ Transpose Octave Up              │
   │ Transpose Octave Down            │
   │ ──────────────────────────────── │
   │ Move to Track       ▸  Track 0   │
   │                        Track 1   │
   │                        Track 2   │
   │ Move to Channel     ▸  Ch 1      │
   │                        Ch 2      │
   │                        ...       │
   │ ──────────────────────────────── │
   │ Scale Events...                  │
   │ ──────────────────────────────── │
   │ Stretch to Fill Neighbors        │
   │ Snap Start to Previous End       │
   │ Extend End to Next (Legato)      │
   └──────────────────────────────────┘
   ```

5. **Action routing** — All actions delegate to existing MainWindow slots:
   - `quantizeSelection()` — already exists (MainWindow.h line 678)
   - `copy()` — already exists (MainWindow.h line 515)
   - `deleteSelectedEvents()` — already exists (MainWindow.h line 432)
   - `transposeNSemitones()` — already exists (MainWindow.h line 523)
   - `transposeSelectedNotesOctaveUp()` / `...Down()` — already exist (line 6123/6149)
   - `moveSelectedEventsToTrack(QAction*)` — already exists (line 450)
   - `moveSelectedEventsToChannel(QAction*)` — already exists (line 444)
   - `scaleSelection()` — already exists (line 1639)

6. **Move to Track/Channel submenus** — Dynamically generate at menu-show time:
   ```cpp
   QMenu* moveToTrackMenu = contextMenu.addMenu(tr("Move to Track"));
   for (int i = 0; i < file->tracks()->size(); i++) {
       QAction* action = moveToTrackMenu->addAction(
           tr("Track %1").arg(i));
       action->setData(i);
   }
   ```
   Then connect selected action to MainWindow via `EditorTool::mainWindow()`.

7. **Legato actions** (Stretch to Fill, Snap Start, Extend End):
   These may need new MainWindow slots if not already present. Check for:
   - `fitBetweenNeighbors()` / `snapStartToPreviousEnd()` / `extendEndToNext()`
   - If missing: implement as Protocol-wrapped note duration adjustments.
   - **Alternative:** Skip legato actions initially, add in future iteration.

**Existing infrastructure verified:**
- `EditorTool::mainWindow()` returns the `MainWindow*` — accessible from MatrixWidget
- `TransposeDialog.h` exists at `src/gui/TransposeDialog.h`
- All 6 core actions (quantize, copy, delete, transpose, move-track, move-channel) have working slots

**Dependencies:** None — all required slots exist
**Risk:** Low — purely additive, doesn't modify existing mouse handling

---

### 18.3 — Note Duration Presets ✅

> **Mewo commits:** `e5ba56a` — "Note Durations", `ca8d70e` — "Note Duration Improvements"

**Goal:** When the pencil (NewNoteTool) is active, allow selecting a fixed note duration
(whole, half, quarter, 8th, 16th, 32nd) instead of always drag-to-size. Shows a
semi-transparent ghost preview on hover before clicking.

**Files to modify:**
| File | Change |
|------|--------|
| `src/tool/NewNoteTool.h` | Add `static int _durationDivisor`, getter/setter, duration methods |
| `src/tool/NewNoteTool.cpp` | Rewrite `draw()`, modify `press()` and `release()` for preset durations |
| `src/gui/MainWindow.h` | Add duration action slots (6 presets + "Free draw") |
| `src/gui/MainWindow.cpp` | Add duration actions in `setupActions()`, connect to NewNoteTool |

**Implementation details:**

1. **NewNoteTool.h** — Add static member + methods:
   ```cpp
   private:
       static int _durationDivisor;  // 0=free draw, 1=whole, 2=half, 4=quarter, 8=eighth, 16=16th, 32=32nd

   public:
       static int durationDivisor();
       static void setDurationDivisor(int divisor);
   ```

2. **NewNoteTool.cpp** — Initialize:
   ```cpp
   int NewNoteTool::_durationDivisor = 0;  // default: free draw (current behavior)
   ```

3. **draw() rewrite** — Two modes:
   - `_durationDivisor == 0` (free draw): Existing behavior (drag rectangle)
   - `_durationDivisor > 0` (preset): Calculate `durationTicks = (file()->ticksPerQuarter() * 4) / _durationDivisor`
     - **On hover (not inDrag):** Draw semi-transparent ghost note at snap position
       ```cpp
       painter->setOpacity(0.3);
       int startTick; rasteredX(mouseX, &startTick);
       int endMs = file()->msOfTick(startTick + durationTicks);
       int endX = matrixWidget->xPosOfMs(endMs);
       painter->fillRect(snapX, y, endX - snapX, lineHeight, Qt::black);
       painter->setOpacity(1.0);
       ```
     - **On drag:** Draw solid note (same duration calculation)

4. **release() modification** — Preset duration overrides drag end:
   ```cpp
   if (line <= 127 && _durationDivisor > 0) {
       int durationTicks = (file()->ticksPerQuarter() * 4) / _durationDivisor;
       endTick = startTick + durationTicks;
   }
   ```

5. **MainWindow.cpp setupActions()** — Add duration actions after pencil tool:
   ```cpp
   // Note Duration Presets (under Tools menu, after pencil)
   QMenu* durationMenu = toolsMB->addMenu(tr("Note Duration"));
   QActionGroup* durationGroup = new QActionGroup(this);
   durationGroup->setExclusive(true);

   struct { QString name; int divisor; QString shortcut; } durations[] = {
       {"Free Draw",  0,  ""},
       {"Whole Note",    1,  "Ctrl+1"},
       {"Half Note",     2,  "Ctrl+2"},
       {"Quarter Note",  4,  "Ctrl+3"},
       {"8th Note",      8,  "Ctrl+4"},
       {"16th Note",    16,  "Ctrl+5"},
       {"32nd Note",    32,  "Ctrl+6"},
   };
   ```
   Each action calls `NewNoteTool::setDurationDivisor(divisor)` and is checkable
   (QActionGroup ensures only one is active).

6. **Keyboard shortcut consideration:** Verify no conflict with existing Ctrl+1..6 binds.
   Check `_defaultShortcuts` map. If conflicts exist, use Alt+1..6 or numpad.

**Key tick-to-pixel conversion:**
```cpp
int durationTicks = (file()->ticksPerQuarter() * 4) / _durationDivisor;
// file()->ticksPerQuarter() is typically 480 → quarter = 480 ticks, whole = 1920 ticks
int endMs = file()->msOfTick(startTick + durationTicks);
int endX = matrixWidget->xPosOfMs(endMs);
```

**Dependencies:** None — NewNoteTool and MainWindow are self-contained
**Risk:** Low-Medium — modifies draw() and release() paths, but free-draw (divisor=0) preserves old behavior

---

### 18.4 — Smooth Playback Scrolling ✅

> **Mewo commit:** `4baf047` — "Smooth Playback Scrolling"

**Goal:** Optional smooth scrolling during playback — the cursor stays anchored at its
click-start position instead of the view "page-turning" when the cursor hits the edge.
Toggled via Settings → MIDI → "Smooth Playback Scroll".

**Files to modify:**
| File | Change |
|------|--------|
| `src/gui/MatrixWidget.h` | Add `bool _wasPlaying`, `int _dynamicOffsetMs` private members |
| `src/gui/MatrixWidget.cpp` | Modify `timeMsChanged()` to add smooth scroll branch |
| `src/gui/MidiSettingsWidget.h` | Add checkbox member |
| `src/gui/MidiSettingsWidget.cpp` | Add "Smooth Playback Scroll" checkbox, save to QSettings |

**Implementation details:**

1. **MatrixWidget.h** — Add private members (near `screen_locked`, ~line 595):
   ```cpp
   bool _wasPlaying = false;
   int _dynamicOffsetMs = 0;
   ```

2. **MatrixWidget.cpp** — Modify `timeMsChanged()` (currently at line 100):

   **Current code (page-turn mode):**
   ```cpp
   void MatrixWidget::timeMsChanged(int ms, bool ignoreLocked) {
       if (!file) return;
       int x = xPosOfMs(ms);
       if ((!screen_locked || ignoreLocked) && (x < lineNameWidth || ms < startTimeX || ms > endTimeX || x > width() - 100)) {
           if (file->maxTime() <= endTimeX && ms >= startTimeX) { update(); return; }
           emit scrollChanged(ms, ...);
       } else {
           update();
       }
   }
   ```

   **New code (adds smooth scroll branch):**
   ```cpp
   void MatrixWidget::timeMsChanged(int ms, bool ignoreLocked) {
       if (!file) return;
       int x = xPosOfMs(ms);
       bool smoothScroll = _settings->value("rendering/smooth_playback_scroll", false).toBool();
       bool isPlaying = MidiPlayer::isPlaying();

       // Capture dynamic offset when playback starts
       if (isPlaying && !_wasPlaying) {
           _dynamicOffsetMs = ms - startTimeX;
           // Clamp to 25%-75% of visible width
           int visibleMs = (width() - lineNameWidth) * 1000 / (PIXEL_PER_S * scaleX);
           _dynamicOffsetMs = qBound(visibleMs / 4, _dynamicOffsetMs, visibleMs * 3 / 4);
       }
       _wasPlaying = isPlaying;

       if (!screen_locked || ignoreLocked) {
           if (smoothScroll && isPlaying) {
               // Smooth: keep cursor anchored at its start position
               int desiredStartTime = qMax(0, ms - _dynamicOffsetMs);
               if (file->maxTime() <= endTimeX && desiredStartTime >= startTimeX) {
                   update(); return;
               }
               emit scrollChanged(desiredStartTime, ...);
               return;
           } else if (x < lineNameWidth || ms < startTimeX || ms > endTimeX || x > width() - 100) {
               // Standard page-turn mode (original behavior)
               if (file->maxTime() <= endTimeX && ms >= startTimeX) { update(); return; }
               emit scrollChanged(ms, ...);
               return;
           }
       }
       update();
   }
   ```

3. **MidiSettingsWidget.cpp** — Add checkbox in the rendering/playback section:
   ```cpp
   QCheckBox *smoothScrollBox = new QCheckBox(tr("Smooth Playback Scrolling"));
   smoothScrollBox->setChecked(_settings->value("rendering/smooth_playback_scroll", false).toBool());
   connect(smoothScrollBox, &QCheckBox::toggled, [this](bool checked) {
       _settings->setValue("rendering/smooth_playback_scroll", checked);
   });
   ```

**Key formula:**
- `_dynamicOffsetMs` = cursor offset from viewport left edge at playback start
- During playback: `viewportStart = cursorMs - _dynamicOffsetMs`
- Clamped to 25%-75% of viewport width to prevent edge-of-screen anchoring

**Dependencies:** `MidiPlayer::isPlaying()` — already available via `#include "../midi/MidiPlayer.h"`
**Risk:** Low — gracefully falls back to page-turn mode when smooth scroll disabled

---

### 18.5 — Timeline Markers (Visual CC/PC/Text markers) ✅

> **Mewo commit:** `8ec534f` — "Timeline Markers, Settings Redesign, More UI Improvements"

**Goal:** Display dashed vertical lines through the piano roll at positions where CC events,
Program Change events, or Text (Marker) events occur. Labels at the top show event type
(CC, PC, M). Markers are draggable to reposition. Colored by track or channel.

**Files to modify/create:**
| File | Change |
|------|--------|
| `src/gui/MatrixWidget.h` | Add `paintTimelineMarkers()`, `findTimelineMarkerNear()`, `_draggedMarker` member |
| `src/gui/MatrixWidget.cpp` | Add `paintTimelineMarkers()` (~120 lines), `findTimelineMarkerNear()` (~40 lines), modify `mousePressEvent/mouseMoveEvent/mouseReleaseEvent` for drag, modify `paintEvent` to call markers |
| `src/gui/Appearance.h` | Add `MarkerColorMode` enum, `showProgramChangeMarkers()`, `showControlChangeMarkers()`, `showTextEventMarkers()`, `markerColorMode()` static methods + members |
| `src/gui/Appearance.cpp` | Implement marker settings (load/save from QSettings), ~50 lines |
| `src/gui/AppearanceSettingsWidget.h` | Add marker setting widgets |
| `src/gui/AppearanceSettingsWidget.cpp` | Add marker toggle checkboxes + color mode combo, ~50 lines |

**Implementation details:**

1. **Appearance.h** — Add marker configuration:
   ```cpp
   enum MarkerColorMode { ColorByTrack, ColorByChannel };

   static bool showProgramChangeMarkers();
   static void setShowProgramChangeMarkers(bool enabled);
   static bool showControlChangeMarkers();
   static void setShowControlChangeMarkers(bool enabled);
   static bool showTextEventMarkers();
   static void setShowTextEventMarkers(bool enabled);
   static MarkerColorMode markerColorMode();
   static void setMarkerColorMode(MarkerColorMode mode);

   // Private
   static bool _showProgramChangeMarkers;
   static bool _showControlChangeMarkers;
   static bool _showTextEventMarkers;
   static MarkerColorMode _markerColorMode;
   ```

2. **Appearance.cpp** — Load/save:
   ```cpp
   _showProgramChangeMarkers = settings.value("appearance/show_pc_markers", false).toBool();
   _showControlChangeMarkers = settings.value("appearance/show_cc_markers", false).toBool();
   _showTextEventMarkers = settings.value("appearance/show_text_markers", true).toBool();
   _markerColorMode = (MarkerColorMode)settings.value("appearance/marker_color_mode", 0).toInt();
   ```

3. **MatrixWidget.h** — Add members and methods:
   ```cpp
   private:
       MidiEvent *_draggedMarker = nullptr;
       void paintTimelineMarkers(QPainter *painter);
       MidiEvent *findTimelineMarkerNear(int ms);
   ```

4. **paintTimelineMarkers()** algorithm:
   ```
   for each channel (0-16):
       for each event in visible tick range:
           if event matches enabled type (CC/PC/Text) AND track not hidden:
               group by tick → QMap<int, QList<MidiEvent*>>

   for each tick group:
       draw dashed vertical line from timeHeight to widget bottom
       stack labels (CC, PC, M) at timeline bottom, offset upwards
       color by track or channel per markerColorMode()
   ```

5. **paintEvent integration** — After existing event painting, before cursor:
   ```cpp
   // In paintEvent(), inside the pixmap painting block:
   pixpainter->setClipping(true);
   pixpainter->setClipRect(lineNameWidth, 0, width() - lineNameWidth, height());
   paintTimelineMarkers(pixpainter);
   pixpainter->setClipping(false);
   ```

6. **Marker dragging** — In `mousePressEvent`:
   ```cpp
   if (mouseInRect(TimeLineArea) && event->button() == Qt::LeftButton) {
       _draggedMarker = findTimelineMarkerNear(msOfXPos(event->pos().x()));
       if (_draggedMarker) {
           file->protocol()->startNewAction(tr("Move Marker"));
           return; // consume event
       }
   }
   ```
   In `mouseMoveEvent`: update `_draggedMarker->setMidiTime(file->tick(newMs))`
   In `mouseReleaseEvent`: `file->protocol()->endAction(); _draggedMarker = nullptr;`

7. **findTimelineMarkerNear()** — Tolerance-based search:
   ```cpp
   int thresholdMs = timeMsOfWidth(7); // 7 pixels
   // Search channels 0-16 for matching events near the click point
   // Return the closest match within threshold, or nullptr
   ```

8. **AppearanceSettingsWidget** — Add in the Appearance section:
   ```
   ☐ Show Program Change Markers
   ☐ Show Control Change Markers
   ☑ Show Text Event (Marker) Markers
   Color Mode: [Track ▾] / [Channel ▾]
   ```

**Dependencies:** `ControlChangeEvent`, `ProgChangeEvent`, `TextEvent` — all exist in `src/MidiEvent/`
**Risk:** Medium — modifies paintEvent pipeline and mouse handlers. Keep marker rendering behind feature flags (all off by default) so it's safe to ship incrementally.

---

### 18.6 — Status Bar with Chord Detection ✅

> **Mewo commit:** `4716fc5` — "Status Bar"

**Goal:** Persistent status bar at the bottom of the main window showing:
- Cursor position (measure:beat, tick, ms)
- Selected note names (e.g., "C4, E4, G4")
- Detected chord name (e.g., "Cmaj", "Am7", "Fdim")
- Number of selected events

**Files to create:**
| File | Description |
|------|-------------|
| `src/midi/ChordDetector.h` | Static class with `detectChord(QList<int> notes)` → `QString` |
| `src/midi/ChordDetector.cpp` | Chord detection algorithm (~95 lines) |
| `src/gui/StatusBarSettingsWidget.h` | Settings page for status bar options |
| `src/gui/StatusBarSettingsWidget.cpp` | Checkboxes for which sections to show |

**Files to modify:**
| File | Change |
|------|--------|
| `src/gui/MainWindow.h` | Add `QLabel *_statusCursorLabel, *_statusSelectionLabel, *_statusChordLabel` members |
| `src/gui/MainWindow.cpp` | Create labels in constructor, add `updateStatusBar()` slot, connect to selection/cursor signals |
| `src/gui/SettingsDialog.cpp` | Register StatusBarSettingsWidget tab |
| `CMakeLists.txt` | Add new source files |

**Implementation details:**

1. **ChordDetector** — interval-based chord recognition:
   ```cpp
   class ChordDetector {
   public:
       static QString detectChord(QList<int> midiNotes);
       static QString getNoteName(int note, bool includeOctave = false);
   private:
       static QString identifyChordType(int root, const QList<int>& intervals);
   };
   ```

   **Algorithm:**
   - Extract pitch classes: `note % 12` for each note, remove duplicates
   - If 1 unique pitch class → return single note name
   - For each possible root (try each pitch class as root):
     - Compute intervals relative to root
     - Match against known chord patterns:
       - **Triads (2 intervals):** major (4,7), minor (3,7), dim (3,6), aug (4,8), sus2 (2,7), sus4 (5,7)
       - **Sevenths (3 intervals):** maj7 (4,7,11), m7 (3,7,10), dom7 (4,7,10), m7b5 (3,6,10), dim7 (3,6,9)
       - **Extended (4 intervals):** 9th (2,4,7,10), maj9 (2,4,7,11)
     - If match found → return `rootName + chordType`
   - If no match → return empty string

2. **MainWindow constructor** — Create status bar labels:
   ```cpp
   _statusCursorLabel = new QLabel(this);
   _statusSelectionLabel = new QLabel(this);
   _statusChordLabel = new QLabel(this);
   statusBar()->addPermanentWidget(_statusCursorLabel);
   statusBar()->addPermanentWidget(_statusSelectionLabel);
   statusBar()->addPermanentWidget(_statusChordLabel);
   ```

3. **updateStatusBar() slot** — Connected to:
   - `Selection::instance()` selection changed signal (if available) or called from `selectionChangedFromOutside()`
   - `matrixWidget->objectListChanged()` signal
   - Cursor tick change

   ```cpp
   void MainWindow::updateStatusBar() {
       if (!file) return;

       // Cursor info
       int tick = file->cursorTick();
       int measure, beat;
       file->measure(tick, &measure, &beat); // get measure/beat
       _statusCursorLabel->setText(QString("M:%1 B:%2 | T:%3").arg(measure).arg(beat).arg(tick));

       // Selection info + chord detection
       QList<MidiEvent*> sel = Selection::instance()->selectedEvents();
       QList<int> notes;
       foreach (MidiEvent *ev, sel) {
           OnEvent *on = dynamic_cast<OnEvent*>(ev);
           if (on) notes.append(on->line());
       }
       _statusSelectionLabel->setText(QString("%1 events").arg(sel.size()));

       if (!notes.isEmpty()) {
           QString chord = ChordDetector::detectChord(notes);
           _statusChordLabel->setText(chord.isEmpty() ? "" : QString("Chord: %1").arg(chord));
       } else {
           _statusChordLabel->setText("");
       }
   }
   ```

**Note:** `statusBar()` is already used in MainWindow (line 1379, 3012, 3085) for
temporary messages. The `addPermanentWidget()` labels will coexist with `showMessage()`. 

**Dependencies:** Selection change notification — verify whether a signal exists or if polling is needed
**Risk:** Medium — need to find the right signal for selection updates

---

### 18.7 — MML Importer (Music Macro Language) ✅

> **Mewo commits:** `5bdd694` — "GuitarPro / MML -> MIDI Converters", `d34718c` — "Refactor Converters"

**Goal:** Import MML (Music Macro Language) text files as MIDI. MML is used by FFXIV
bard performers (via 3MLE tool) and retro game music communities.

**MML syntax basics:**
- `cdefgab` — notes (C through B)
- `+` / `-` — sharp / flat
- Number after note — duration (4=quarter, 8=eighth, etc.)
- `o4` — set octave, `>` / `<` — octave up/down
- `t120` — tempo 120 BPM
- `r8` — rest (8th note)
- `l4` — default length (quarter)
- `v15` — volume (0-15)
- `&` — tie notes
- Tracks separated by `;` or `,` (format-dependent)

**Files to create:**
| File | Description |
|------|-------------|
| `src/converter/MML/MmlImporter.h` | Entry point: `static MidiFile* import(QString path)` |
| `src/converter/MML/MmlImporter.cpp` | File reading, encoding detection, orchestration |
| `src/converter/MML/MmlLexer.h` | Tokenizer: MML text → token list |
| `src/converter/MML/MmlLexer.cpp` | Lexer implementation (~170 lines) |
| `src/converter/MML/MmlParser.h` | Parser: tokens → note/event commands |
| `src/converter/MML/MmlParser.cpp` | Parser implementation (~215 lines) |
| `src/converter/MML/MmlModels.h` | Data structures: MmlNote, MmlTrack, MmlSong |
| `src/converter/MML/MmlConverter.h` | MML song → MIDI file conversion |
| `src/converter/MML/MmlConverter.cpp` | Conversion (~55 lines) |
| `src/converter/MML/MmlMidiWriter.h` | Write MIDI events from MML data |
| `src/converter/MML/MmlMidiWriter.cpp` | MIDI writing (~90 lines) |
| `src/converter/MML/ThreeMleParser.h` | 3MLE format-specific parser |
| `src/converter/MML/ThreeMleParser.cpp` | 3MLE parsing (~335 lines) |

**Files to modify:**
| File | Change |
|------|--------|
| `src/gui/MainWindow.cpp` | Add `*.mml *.3mle` to file open filter (~line 1287), add import call in load() |
| `CMakeLists.txt` | Add `src/converter/MML/*.cpp` to sources |

**Implementation approach:**
1. Follow the existing `GpImporter` pattern: static method returns temporary MIDI file
2. Detect format from content/extension: `.mml` → standard MML, `.3mle` → 3MLE format
3. **3MLE format:** Has `[Settings]` and `[Channel*]` INI-style sections
4. **Standard MML:** Track data separated by `;`
5. Pipeline: `file → text → lexer → tokens → parser → commands → converter → MidiFile`

**File dialog filter update** (MainWindow.cpp ~line 1287):
```cpp
// Current:
"All Supported Files (*.mid *.midi *.gp *.gp3 *.gp4 *.gp5 *.gpx *.gtp)"
// Change to:
"All Supported Files (*.mid *.midi *.gp *.gp3 *.gp4 *.gp5 *.gpx *.gtp *.mml *.3mle)"
```
Add separate filter: `"MML Files (*.mml *.3mle)"`

**Dependencies:** None — new directory, new files
**Risk:** Medium — complex parser, but isolated from existing code. All new files.

---

### 18.8 — DrumKit Preset Mapping ✅

> **Mewo commit:** `c95a9ce` — "Split Channels Tool & DrumKit Preset Mapping"

**Goal:** When splitting Channel 9 (drums) into separate tracks, offer preset
mappings for common drum kits (GM, Rock, Jazz, Electronic) that pre-assign
note ranges to named tracks (e.g., "Kick", "Snare", "Hi-Hat", "Crash").

**Files to create:**
| File | Description |
|------|-------------|
| `src/gui/DrumKitPreset.h` | Preset data structures and static preset definitions |
| `src/gui/DrumKitPreset.cpp` | Preset database: GM drum map + custom kits (~325 lines) |

**Files to modify:**
| File | Change |
|------|--------|
| `src/gui/SplitChannelsDialog.h` | Add `DrumKitPreset *_selectedPreset`, preset combo |
| `src/gui/SplitChannelsDialog.cpp` | Integrate preset selection into dialog UI (~40 lines) |
| `CMakeLists.txt` | Add new source files |

**Implementation details:**

1. **DrumKitPreset.h** — Data structures:
   ```cpp
   struct DrumGroup {
       QString name;          // e.g., "Kick", "Snare"
       QList<int> noteNumbers; // e.g., {35, 36} for bass drums
   };

   class DrumKitPreset {
   public:
       QString name;                    // e.g., "General MIDI"
       QList<DrumGroup> groups;

       static QList<DrumKitPreset> presets();
       static DrumKitPreset gmPreset();
   };
   ```

2. **GM Drum Map** (the core database):
   ```
   35-36: Bass Drum
   37-40: Snare / Side Stick / Claps
   41-43: Low Tom
   44-46: Hi-Hat (closed, pedal, open)
   47-48: Mid Tom
   49, 57: Crash Cymbal
   50-53: High Tom
   51, 59: Ride Cymbal
   54-56: Tambourine, Splash, Cowbell
   ...
   ```

3. **SplitChannelsDialog integration:**
   - Add QComboBox at top: "Drum Kit Preset: [None] [General MIDI] [Rock] [Jazz]"
   - When "General MIDI" selected: auto-group channel 9 notes by preset
   - Each group becomes a separate track with the preset name
   - Pass `DrumKitPreset` to the split function that creates tracks

**Dependencies:** SplitChannelsDialog must exist (verified ✅)
**Risk:** Low-Medium — extends existing dialog

---

### 18.9 — SizeChangeTool Improvements ✅

> **Mewo commits:** `e38b178` — "Improve SizeChangeTool Behavior", `01e7455` — "SizeChangeTool Cleanup"

**Goal:** Improve the resize tool with better visual feedback (ghost notes during drag),
improved edge detection, proper cursor management, and grid-snapped resize.

**Files to modify:**
| File | Change |
|------|--------|
| `src/tool/SizeChangeTool.cpp` | Rewrite `draw()`, `press()`, `move()` methods (~120 lines changed) |

**Key improvements from Mewo:**

1. **Ghost note preview during drag** — Instead of plain black rectangle, draw a
   semi-transparent ghost with rounded corners:
   ```cpp
   // Ghost Note Colors (DAW Standards)
   bool darkMode = Appearance::shouldUseDarkMode();
   QColor ghostFill = darkMode ? QColor(255, 255, 255, 60) : QColor(0, 0, 0, 40);
   QColor ghostBorder = darkMode ? QColor(255, 255, 255, 120) : QColor(0, 0, 0, 80);

   painter->setBrush(ghostFill);
   painter->setPen(QPen(ghostBorder, 1, Qt::SolidLine));
   painter->drawRoundedRect(ghostRect, 1, 1);
   ```

2. **Pixel-accurate raw coordinate calculation** — Use `matrixWidget->xPosOfMs(msOfTick(tick))`
   instead of `event->x()` / `event->width()` for accurate ghost positioning:
   ```cpp
   OnEvent *onEvent = dynamic_cast<OnEvent *>(event);
   if (onEvent && onEvent->offEvent()) {
       int rawX = matrixWidget->xPosOfMs(matrixWidget->msOfTick(onEvent->midiTime()));
       int rawEndX = matrixWidget->xPosOfMs(matrixWidget->msOfTick(onEvent->offEvent()->midiTime()));
   }
   ```

3. **Edge cursor on hover (without drag)** — Show split cursor when hovering over
   note edges even before clicking:
   ```cpp
   // In draw(), when !inDrag:
   foreach (MidiEvent* event, Selection::instance()->selectedEvents()) {
       if (pointInRect(mouseX, mouseY, event->x() + event->width() - 2, ...))
           setCursor(Qt::SplitHCursor);  // right edge
       if (pointInRect(mouseX, mouseY, event->x() - 2, ...))
           setCursor(Qt::SplitHCursor);  // left edge
   }
   ```

4. **Improved press() logic:**
   - Check for left/right edge click first
   - If no edge found: left click = drag note start, right click = drag note end
   - Grid-snap via `rasteredX(mouseX)` from start
   - Return `false` for empty selection instead of entering drag state

5. **TitleCase protocol text:** "Change event duration" → "Change Event Duration"

**Dependencies:** `Appearance::shouldUseDarkMode()` — already exists
**Risk:** Low — improves existing tool, no new files

---

### 18.10 — DLS SoundFont Support ✅

> **Mewo commit:** `7a0945d` — "Support DLS SoundFonts"

**Goal:** Allow loading `.dls` (Downloadable Sound) files in addition to `.sf2`/`.sf3`.
FluidSynth has built-in DLS support — we just need to update the file dialog filter.

**Files to modify:**
| File | Change |
|------|--------|
| `src/gui/MidiSettingsWidget.cpp` | Line 428: Add `*.dls *.DLS` to file filter |
| `src/midi/FluidSynthEngine.h` | Line 84: Update doc comment to mention DLS |

**Current filter (line 428):**
```cpp
tr("SoundFont Files (*.sf2 *.sf3 *.SF2 *.SF3);;All Files (*)")
```

**New filter:**
```cpp
tr("SoundFont Files (*.sf2 *.sf3 *.dls *.SF2 *.SF3 *.DLS);;All Files (*)")
```

**Dependencies:** FluidSynth library must support DLS (it does since FluidSynth 2.x)
**Risk:** None — purely cosmetic file filter change

---

### 18.11 — Fixed Measure Number Position ✅

> **Mewo commit:** `5080ff3` — "More UI & Fixes"

**Problem:** When timeline markers are enabled, the `timeHeight` increases from 50 to
66+ pixels. The measure number text Y position was computed as `timeHeight - 9`, causing
numbers to shift downward when markers appear. This makes the timeline ruler look unstable.

**Fix:** Use a constant `textY = 41` instead of `timeHeight - 9` so measure numbers
stay anchored at the same vertical position regardless of marker bar visibility.

**File:** `src/gui/MatrixWidget.cpp` — `paintEvent()`, measure number drawing

**Change:**
```cpp
// Before:
int textY = timeHeight - 9;

// After:
int textY = 41;
```

**Dependencies:** None
**Risk:** None — purely visual positioning fix

---

### 18.12 — Separate MarkerArea + Pixel-Based Hit-Testing ✅

> **Mewo commit:** `5080ff3` — "More UI & Fixes"

**Problem:** Marker interaction (hover cursor, click-to-drag) uses `TimeLineArea` for
bounds checking and millisecond-based distance for hit detection. This means:
1. Hovering over the ruler area (where measure numbers are) triggers marker cursor
2. Clicking on the ruler can accidentally grab a nearby marker
3. Hit detection is imprecise — uses ms distance rather than pixel bounds of the 22px box

**Fix:**
- Add `QRectF MarkerArea` and `int _markerRowHeight` members to `MatrixWidget.h`
- In `calcSizes()`: compute `MarkerArea` as the 16px row below the 50px ruler
- `mouseMoveEvent()`: Show `SplitHCursor` only when hovering in `MarkerArea`, not `TimeLineArea`
- `mousePressEvent()`: Only start marker drag when clicking in `MarkerArea`
- `findTimelineMarkerNear(int x, int y)`: Change from ms-based to pixel-based hit testing —
  check `mouseInRect(MarkerArea)` first, then test if `x` falls within `evX..evX+24` range

**Files to modify:**
| File | Change |
|------|--------|
| `src/gui/MatrixWidget.h` | Add `QRectF MarkerArea`, `int _markerRowHeight`, `bool _hasVisibleMarkers`; change `findTimelineMarkerNear` signature to `(int x, int y)` |
| `src/gui/MatrixWidget.cpp` | Update `calcSizes()`, `mouseMoveEvent()`, `mousePressEvent()`, `findTimelineMarkerNear()`, `paintTimelineMarkers()` |

**Dependencies:** None
**Risk:** Low — improves existing marker interaction

---

### 18.13 — Full-Height Divider & Border Cleanup ✅

> **Mewo commit:** `5080ff3` — "More UI & Fixes"

**Problem:** 
1. The vertical divider between the piano/header area and the play area only starts at
   `timeHeight`, leaving the top header area without a divider
2. A bottom-right border (`drawLine` for bottom edge and right edge) adds unnecessary
   visual clutter

**Fix:**
- Change vertical divider line from `drawLine(lineNameWidth, timeHeight, ...)` to
  `drawLine(lineNameWidth, 0, lineNameWidth, height())` — full height
- Remove the two bottom/right border lines (replaced with comment)

**File:** `src/gui/MatrixWidget.cpp` — `paintEvent()`

**Dependencies:** None
**Risk:** None — purely visual cleanup

---

### 18.14 — TrackListWidget / ChannelListWidget refreshColors() ✅

> **Mewo commit:** `5080ff3` — "More UI & Fixes"

**Problem:** When themes change at runtime, `Appearance::refreshColors()` calls
`trackListWidget->update()` and `channelListWidget->update()`, but this only triggers
a repaint — it doesn't update the cached toolbar background palette. The mini-toolbars
inside each track/channel list item retain stale colors from the previous theme.

**Fix:** Add proper `refreshColors()` methods that update toolbar palettes:

**New methods:**
- `TrackListItem::refreshColors()` — updates `_toolBar` palette with `Appearance::toolbarBackgroundColor()`
- `TrackListWidget::refreshColors()` — iterates all items, calls `refreshColors()`, then `QListWidget::update()`
- `ChannelListItem::refreshColors()` — same pattern
- `ChannelListWidget::refreshColors()` — same pattern

Store `QToolBar *_toolBar` pointer in each list item for palette refresh.

Update `Appearance::refreshColors()` to call `refreshColors()` instead of `update()`.

**Files to modify:**
| File | Change |
|------|--------|
| `src/gui/TrackListWidget.h` | Add `_toolBar` member, `refreshColors()` methods |
| `src/gui/TrackListWidget.cpp` | Store toolbar pointer, implement `refreshColors()` |
| `src/gui/ChannelListWidget.h` | Add `_toolBar` member, `refreshColors()` methods |
| `src/gui/ChannelListWidget.cpp` | Store toolbar pointer, implement `refreshColors()` |
| `src/gui/Appearance.cpp` | Change `->update()` to `->refreshColors()` calls |

**Dependencies:** None
**Risk:** Low — additive change, no behavior change for existing code paths

---

### 18.15 — moveToChannel / removeEvent toProtocol Parameter ✅

> **Mewo commit:** `5080ff3` — "More UI & Fixes"

**Problem:** `MidiEvent::moveToChannel()` and `MidiChannel::removeEvent()` always
record undo/redo protocol entries. During bulk operations like the FFXIV Channel Fixer
(which moves hundreds of events), this creates massive undo history, is slow, and wastes
memory. Individual protocol entries for each event in a batch operation are pointless
since the operation should be atomic.

**Fix:** Add `bool toProtocol = true` parameter to both methods. Default preserves
existing behavior. Channel Fixer passes `false` to skip protocol recording.

**Files to modify:**
| File | Change |
|------|--------|
| `src/MidiEvent/MidiEvent.h` | `moveToChannel(int channel, bool toProtocol = true)` |
| `src/MidiEvent/MidiEvent.cpp` | Guard `copy()` + `protocol()` with `if (toProtocol)` |
| `src/MidiEvent/OnEvent.h` | Override signature update |
| `src/MidiEvent/OnEvent.cpp` | Pass `toProtocol` to base + off event |
| `src/midi/MidiChannel.h` | `removeEvent(MidiEvent*, bool toProtocol = true)` |
| `src/midi/MidiChannel.cpp` | Guard `copy()` + `protocol()` with `if (toProtocol)` |
| `src/support/FFXIVChannelFixer.cpp` | Pass `false` for `moveToChannel` and `removeEvent` calls |

**Dependencies:** None
**Risk:** Low — default parameter preserves all existing call sites unchanged

---

### 18.16 — FFXIVChannelFixer Memory Leak Fix ✅

> **Mewo commit:** `5080ff3` — "More UI & Fixes"

**Problem:** In `FFXIVChannelFixer::fixChannels()`, when removing Program Change events
from channels, the events are removed from the channel's event map but never `delete`d.
This leaks the `ProgChangeEvent` objects on the heap.

**Fix:** Add `delete ev;` after `channel->removeEvent(ev, false);` in the PC cleanup loop.

**File:** `src/support/FFXIVChannelFixer.cpp`

**Current code:**
```cpp
for (MidiEvent *ev : toRemove)
    channel->removeEvent(ev);
```

**New code:**
```cpp
for (MidiEvent *ev : toRemove) {
    channel->removeEvent(ev, false);
    delete ev;
}
```

**Dependencies:** 18.15 (toProtocol parameter)
**Risk:** None — events are already removed from all containers before deletion

---

## Phase 19: MidiPilot AI Improvements ✅

> **Goal:** Polish, fix, and extend the MidiPilot AI assistant — the core feature that
> makes MidiEditor AI unique. Focus on undo granularity, persistent history, token
> counting accuracy, and quality-of-life improvements for daily AI-assisted workflows.
>
> **Motivation:** After 18 phases of editor features, the AI assistant itself needs
> refinement. Users have reported that Agent mode lumps all changes into one undo step,
> that conversation history is lost on restart, and that token counters show 0 for some
> providers. This phase addresses all of those plus new ideas.
>
> **Status:** All 7 sub-phases implemented (v1.1.9, 2026-04-07). Tested and bugfixed.
>
> **Bugfixes during testing:**
> - Stop button crash: `cancel()` reordered to `cleanup()` before `cancelRequest()` to
>   prevent double-fire of `errorOccurred` from synchronous `abort()` → `onReplyFinished`
> - Simple mode JSON dump: streaming showed raw JSON in chat instead of dispatching actions.
>   Added `_streamIsJson` flag to suppress streaming bubble for JSON responses, and
>   `onStreamFinished` now delegates to `onResponseReceived` (handles `{"actions":[...]}`)
> - User prompt bubble deleted: `onResponseReceived` and `onErrorOccurred` blindly removed
>   the last chat widget (meant for "Thinking..." indicator). Now checks for "Thinking"
>   text via `qobject_cast<QLabel*>` before removing.
> - Preset save on unsaved file: replaced error dialog with `QFileDialog::getSaveFileName`
>   fallback so user can pick a MIDI file path.

### Implementation Order

```
Phase 19.1   Granular Agent Undo (per-tool protocol steps)    ✅  done 2026-04-06
Phase 19.2   Persistent Conversation History                  ✅  done 2026-04-06
Phase 19.3   Token Counting Fix + Estimation Fallback         ✅  done 2026-04-06
Phase 19.4   Agent Mode Progress & Action Log                 ✅  done 2026-04-07
Phase 19.5   Conversation Context Window Management           ✅  done 2026-04-06
Phase 19.6   Response Streaming (SSE)                         ✅  done 2026-04-07
Phase 19.7   Per-File AI Presets                              ✅  done 2026-04-07
```

### Estimated Complexity

| Sub-phase | New Code | Modified Code | Risk |
|-----------|----------|---------------|------|
| 19.1 Granular agent undo | ~60 lines | MidiPilotWidget.cpp (~40 lines), AgentRunner.cpp (~20 lines) | Medium |
| 19.2 Persistent history | ~300 lines (new class) | MidiPilotWidget.cpp (~100 lines) | Low-Medium |
| 19.3 Token counting fix | ~120 lines | AiClient.cpp (~40 lines), AgentRunner.cpp (~20 lines), MidiPilotWidget.cpp (~20 lines) | Low |
| 19.4 Agent progress log | ~100 lines | MidiPilotWidget.cpp (~50 lines) | Low |
| 19.5 Context window mgmt | ~80 lines | MidiPilotWidget.cpp (~20 lines) | Low |
| 19.6 Response streaming | ~200 lines | AiClient.cpp (~50 lines) | Medium |
| 19.7 Per-file AI presets | ~80 lines | MidiPilotWidget.cpp (~40 lines) | Low |
| **Total** | **~940 lines new** | **~380 lines modified** | **Low-Medium** |

---

### 19.1 — Granular Agent Undo (Per-Tool Protocol Steps) ✅

**Problem:** In Agent mode, all tool calls from one user request are wrapped in a single
`startNewAction("MidiPilot Agent: ...")` / `endAction()` pair. Ctrl+Z undoes EVERYTHING
the agent did — if it edited 5 tracks across 3 channels, you lose all of it or keep all
of it. No selective undo.

**Current behavior:**
```
User: "Transpose track 1 up an octave and set track 2 velocity to 100"
→ Agent calls: transpose_notes (track 1) + edit_notes (track 2)
→ Protocol: one undo step "MidiPilot Agent: Transpose track 1..."
→ Ctrl+Z: reverts BOTH changes
```

**Desired behavior:**
```
→ Protocol: step 1 "MidiPilot: Transpose notes on track 1"
→ Protocol: step 2 "MidiPilot: Set velocity on track 2"
→ Ctrl+Z: reverts step 2 only
→ Ctrl+Z again: reverts step 1
```

**Approach:**
- Remove the outer `startNewAction/endAction` wrapper in `onSendMessage()` for agent mode
- Each `dispatchAction(actionObj, false)` call should now wrap itself in its own protocol step
- Change the `showBubbles` parameter semantics: rename to a `ProtocolMode` enum
  (`PerAction`, `Compound`, `None`) for clarity
- Agent mode uses `PerAction` — each tool call gets its own named undo step
- The action name comes from the tool call's explanation/description field
- Add a "Revert last AI action" button in the chat that undoes just the most recent step

**Considerations:**
- If agent makes 10 tool calls, user gets 10 undo steps — this is intentional and desired
- The chat log should show which undo steps correspond to which actions
- If user undoes mid-agent-run (somehow), that's already impossible since UI is locked during agent execution

**Implementation Notes (from codebase analysis):**

```
FILES TO MODIFY:
  src/gui/MidiPilotWidget.h    — change showBubbles bool → ProtocolMode enum
  src/gui/MidiPilotWidget.cpp  — agent protocol wrapping + all action handlers
  src/ai/AgentRunner.cpp       — no changes needed (protocol is in widget, not runner)

CURRENT PROTOCOL FLOW (Agent mode):
  MidiPilotWidget.cpp:825  — _file->protocol()->startNewAction("MidiPilot Agent: " + text)
  MidiPilotWidget.cpp:1138 — _file->protocol()->endAction()  [onAgentFinished]
  MidiPilotWidget.cpp:1177 — _file->protocol()->endAction()  [onAgentError]

  Agent tool calls go: AgentRunner::processToolCalls() → ToolDefinitions::executeTool()
    → widget->executeAction(actionObj) → dispatchAction(actionObj, false)
    → e.g. applyAiEdits(response, false)  [showBubbles=false → skips protocol]

ACTION HANDLERS WITH PROTOCOL (all use "if (showBubbles)" guard):
  applyAiEdits()           — line 1394: startNewAction / line 1414,1426: endAction
  applyAiDeletes()         — line 1486: startNewAction / line 1504: endAction
  applyTrackAction()       — line 1530: startNewAction / line 1537: endAction
  applyTempoAction()       — line 1553: startNewAction / line ~1565: endAction
  applyTimeSignatureAction() — similar pattern
  applySelectAndEdit()     — similar pattern
  applySelectAndDelete()   — similar pattern
  applyMoveToTrack()       — similar pattern

STEP-BY-STEP IMPLEMENTATION:
  1. Define enum in MidiPilotWidget.h:
       enum class ProtocolMode { PerAction, Compound, None };

  2. Change all handler signatures from:
       QJsonObject applyAiEdits(const QJsonObject &, bool showBubbles = true);
     to:
       QJsonObject applyAiEdits(const QJsonObject &, ProtocolMode proto = ProtocolMode::PerAction);

  3. In each handler, replace:
       if (showBubbles) _file->protocol()->startNewAction(...)
     with:
       if (proto == ProtocolMode::PerAction) _file->protocol()->startNewAction(...)
     (PerAction = always wrap, Compound = skip individual wrapping, None = no protocol at all)

  4. In onSendMessage() agent branch (line ~825):
     REMOVE: _file->protocol()->startNewAction("MidiPilot Agent: " + text);
     KEEP:   the AgentRunner::run() call

  5. In onAgentFinished() (line ~1138):
     REMOVE: _file->protocol()->endAction();
     Same in onAgentError() (line ~1177)

  6. Change executeAction() (line ~1020):
     FROM: return dispatchAction(actionObj, false);
     TO:   return dispatchAction(actionObj, ProtocolMode::PerAction);
     Now each tool call wraps itself in its own protocol step

  7. Simple mode multi-action loop (line ~900):
     Already works — each dispatchAction(obj, true) creates its own step
     Just change true → ProtocolMode::PerAction

  8. The step label for each protocol action comes from the tool's "explanation" field
     (already present in action JSON from the AI), falling back to buildStepLabel()

EDGE CASES:
  - read-only tools (get_editor_state, query_events) don't call dispatchAction
    → no protocol step created → correct behavior
  - info/error actions in dispatchAction → no protocol step → correct
  - If agent errors mid-run, partial steps remain individually undoable → better than current
```

---

### 19.2 — Persistent Conversation History ✅

**Problem:** All conversation history is lost when the app closes, when the user starts
a new chat, or when switching modes. There's no way to review past conversations or
continue a previous session.

**Current state:** Two in-memory structures (`_conversationHistory` QJsonArray for API,
`_entries` QList for UI) — both cleared on new chat, mode change, or app exit.

**Approach — JSON file storage** (no new dependency):
- Storage format: one JSON file per conversation in a dedicated directory
- Location: `<app_data>/MidiPilotHistory/` (use `QStandardPaths::AppDataLocation`)
- File naming: `<timestamp>_<midi_filename_hash>.json`
- Each conversation file contains:
  ```json
  {
    "id": "uuid",
    "created": "2026-04-06T15:30:00Z",
    "updated": "2026-04-06T15:45:00Z",
    "midiFile": "path/to/Sweet Child O Mine.mid",
    "midiFileHash": "sha256_first8",
    "model": "gpt-4o-mini",
    "provider": "openai",
    "title": "Transpose and fix guitar channels",
    "tokenUsage": { "prompt": 12400, "completion": 3200 },
    "messages": [ ... ]
  }
  ```
- Auto-save: write after every assistant response (debounced 2s)
- No SQLite — plain JSON files via `QJsonDocument`. Fast enough for conversation-sized
  data (typically <100 messages), no new dependency, human-readable, easy to export
- Conversation title: auto-generated from first user message (first 60 chars), editable

**UI additions:**
- History button (📜) in MidiPilot toolbar → opens history panel/dropdown
- List shows: title, date, associated MIDI file, message count
- Filter by MIDI file ("show conversations for this file")
- Search across all conversations (simple substring match on messages)
- Click to load a past conversation (read-only or resumable)
- Export single conversation as `.json` or `.txt`
- "Clear all history" with confirmation dialog
- Auto-associate conversation with current MIDI file path + content hash

**Implementation Notes (from codebase analysis):**

```
FILES TO CREATE:
  src/ai/ConversationStore.h/.cpp  — new class for JSON file I/O

FILES TO MODIFY:
  src/gui/MidiPilotWidget.h   — add ConversationStore*, history UI members
  src/gui/MidiPilotWidget.cpp — save/load/list conversations
  CMakeLists.txt              — add ConversationStore.cpp to build

CURRENT IN-MEMORY STATE:
  MidiPilotWidget.h:
    QJsonArray _conversationHistory;     — API messages (role + content), sent to LLM
    QList<ConversationEntry> _entries;   — richer struct (timestamp, context, role, message)
    int _totalPromptTokens, _totalCompletionTokens; — session counters

  ConversationEntry struct (MidiPilotWidget.h:107):
    QString role, message;
    QJsonObject context;
    QDateTime timestamp;

SAVE/LOAD POINTS:
  - SAVE triggers:
    - After onResponseReceived() (Simple mode) — line ~870 area
    - After onAgentFinished() — line ~1138 area
    - After every addChatBubble() call (debounced via QTimer::singleShot)
  - LOAD triggers:
    - History panel click → repopulate _conversationHistory + _entries + chat bubbles
    - App startup → optionally auto-resume last conversation for current file
  - CLEAR triggers:
    - onNewChat() (line ~710) → mark old conversation as "ended", start fresh file
    - onModeChanged() → same as new chat

DIRECTORY STRUCTURE:
  QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
    → C:\Users\<user>\AppData\Local\MidiEditorAI\MidiPilotHistory\
  One .json file per conversation: "2026-04-06T1530_abcd1234.json"
  Index file NOT needed — just glob *.json and parse headers

ConversationStore CLASS DESIGN:
  class ConversationStore {
  public:
    struct ConversationMeta {
        QString id, title, midiFile, midiFileHash, model, provider;
        QDateTime created, updated;
        int messageCount, promptTokens, completionTokens;
    };

    static QString storageDir();  // creates dir if needed
    static QList<ConversationMeta> listConversations();  // scan dir, parse headers
    static QList<ConversationMeta> findByMidiFile(const QString &path);
    static QJsonObject loadConversation(const QString &id);
    static void saveConversation(const QString &id, const QJsonObject &data);
    static void deleteConversation(const QString &id);
    static void deleteAll();
  };

UI ADDITIONS:
  - MidiPilot toolbar already has: New Chat, Settings, Mode combo, Model combo
  - Add: History button (📜) between New Chat and Settings
  - History click → show dropdown/popup with QListWidget
  - Each row: [title] [date] [model] [msg count]
  - Double-click → load conversation, populating _conversationHistory and rebuilding chat bubbles
  - Right-click → Export as .txt / Delete

CONVERSATION RESUME:
  When loading a saved conversation:
  1. Clear current chat (same as onNewChat but no confirmation)
  2. Set _conversationHistory from saved messages
  3. Rebuild _entries from saved messages
  4. For each entry, call addChatBubble() to rebuild the visual chat
  5. Restore token counters from saved totals
  6. Resume is possible — user can type new messages, they append to same conversation file

MIDI FILE ASSOCIATION:
  - When saving, record:
    - _file->path() (full path)
    - First 8 chars of SHA-256 of file content (or just filename if path changes)
  - When opening a file, check if any conversations match → show "Resume?" prompt
  - Filter button in history: "Show only conversations for this file"
```

**Why JSON over SQLite:**
- Qt already has `QJsonDocument` — zero new dependencies
- Conversations are small — no need for indexed queries
- Files are human-readable and trivially portable
- Export/import is just file copy
- No schema migrations needed
- For the expected volume (<1000 conversations), file-based listing is fast enough

---

### 19.3 — Token Counting Fix + Estimation Fallback ✅

**Problem:** Token counters show 0 for some providers because the code only parses
the OpenAI Chat Completions `usage` format (`prompt_tokens`, `completion_tokens`,
`total_tokens`). Issues:

1. **OpenAI Responses API** (`/v1/responses`): `normalizeResponsesApiResponse()` doesn't
   normalize the `usage` field — it may use a different structure
2. **Anthropic** (via direct API): Returns `usage.input_tokens` / `usage.output_tokens`
   instead of `prompt_tokens` / `completion_tokens`
3. **Google Gemini**: Returns `usageMetadata.promptTokenCount` / `candidatesTokenCount`
4. **Some OpenRouter models**: May not return usage at all

**Approach — multi-layer token counting:**

1. **Normalize provider-specific usage fields** in `AiClient::onReplyFinished()`:
   - Detect response format and map to canonical `{prompt_tokens, completion_tokens}`
   - Handle: OpenAI Chat Completions, OpenAI Responses API, Anthropic native, Gemini native
   - All downstream code already uses the canonical format

2. **Client-side estimation fallback** when API returns no usage:
   - Use the tiktoken approximation: `~4 chars per token` for English/code
   - Count characters in the request messages and response, divide by 4
   - Display with a `~` prefix to indicate estimate: `~2.4k` vs `2.4k`
   - Configurable in settings: "Token counting: API | Estimate | Both"

3. **Per-model context window display:**
   - Store known context limits in a simple lookup table (gpt-4o: 128k, claude-3.5: 200k, etc.)
   - Show usage bar: `12.4k / 128k tokens` in the token label
   - Warn when approaching limit (>80%): yellow indicator

**Why not a real tokenizer:**
- tiktoken (Python) or equivalent C++ port would add a large dependency
- The `~4 chars/token` heuristic is within 20% for most content
- API-reported tokens are always preferred when available
- The estimation is mainly for providers that don't report usage

**Implementation Notes (from codebase analysis):**

```
FILES TO MODIFY:
  src/ai/AiClient.cpp         — normalize usage in onReplyFinished() + normalizeResponsesApiResponse()
  src/ai/AiClient.h           — add static normalizeUsage() helper, context window lookup
  src/ai/AgentRunner.cpp      — already reads usage at line 118, just needs normalization
  src/gui/MidiPilotWidget.cpp — estimation fallback, context bar display

CURRENT TOKEN FLOW:
  1. AiClient::onReplyFinished() → emits responseReceived(content, fullResponse)
     fullResponse includes raw "usage" object as-is from API
  2. MidiPilotWidget::onResponseReceived() (line ~852):
     usage = fullResponse["usage"].toObject()
     _lastPromptTokens = usage["prompt_tokens"].toInt()   ← OpenAI format only!
     _lastCompletionTokens = usage["completion_tokens"].toInt()
  3. AgentRunner::onApiResponse() (line ~118):
     Same pattern — reads usage["prompt_tokens"] etc.
     Emits tokenUsageUpdated(pt, ct, tt) → MidiPilotWidget accumulates

PROBLEM ANALYSIS:
  A) normalizeResponsesApiResponse() (AiClient.cpp:430) builds normalized["choices"]
     but does NOT copy/normalize the "usage" field from the Responses API response.
     OpenAI Responses API returns: { "usage": { "input_tokens": N, "output_tokens": N, "total_tokens": N } }
     Note: "input_tokens" not "prompt_tokens" — different field names!
     FIX: Add to normalizeResponsesApiResponse():
       if (respObj.contains("usage")) {
           QJsonObject rawUsage = respObj["usage"].toObject();
           QJsonObject usage;
           usage["prompt_tokens"] = rawUsage["input_tokens"];
           usage["completion_tokens"] = rawUsage["output_tokens"];
           usage["total_tokens"] = rawUsage["total_tokens"];
           normalized["usage"] = usage;
       }

  B) Anthropic native API returns:
     { "usage": { "input_tokens": N, "output_tokens": N } }
     When accessed via OpenRouter (OpenAI-compatible), usually remapped.
     When direct Anthropic API: need normalization.
     FIX: Add normalizeUsage() static helper in AiClient that detects format:
       - Has "prompt_tokens" → already normalized (OpenAI Chat Completions)
       - Has "input_tokens" → Anthropic/Responses API → remap
       - Has "promptTokenCount" → Gemini → remap from usageMetadata
       - None of the above → return empty (will trigger estimation)

  C) Gemini native API returns:
     { "usageMetadata": { "promptTokenCount": N, "candidatesTokenCount": N, "totalTokenCount": N } }
     FIX: Check for usageMetadata in addition to usage.

ESTIMATION FALLBACK:
  When normalizeUsage() returns empty (no API-reported tokens):
  - Count chars in the sent messages (system prompt + conversation + user msg)
  - Count chars in the response content
  - Divide by 4 (rough tiktoken approximation)
  - Set a flag _isEstimated = true
  - updateTokenLabel() shows "~2.4k" instead of "2.4k"

CONTEXT WINDOW LOOKUP TABLE (in AiClient.h or separate):
  static QHash<QString, int> contextWindows = {
      {"gpt-4o", 128000}, {"gpt-4o-mini", 128000},
      {"gpt-5", 1000000}, {"gpt-5.4", 1000000},
      {"o4-mini", 200000}, {"o3", 200000},
      {"claude-3.5-sonnet", 200000}, {"claude-3-haiku", 200000},
      {"claude-4-sonnet", 200000},
      {"gemini-2.5-pro", 1000000}, {"gemini-2.5-flash", 1000000},
  };
  Lookup by prefix match (model.startsWith) to handle version variants.
  Returns 0 for unknown models → context bar hidden.

DISPLAY UPDATE:
  updateTokenLabel() (MidiPilotWidget.cpp:996):
  Currently: "2450 | 12.3k🔥 [16.0k ✂]"
  New:       "2450 | 12.3k🔥 / 128k [16.0k ✂]"
  Or estimated: "~2450 | ~12.3k🔥 / 128k"
  Add yellow color when sessionTotal > 0.8 * contextWindow
```

---

### 19.4 — Agent Mode Progress & Action Log ✅

**Problem:** During Agent mode execution, the user sees "Thinking..." with a spinner
but has no visibility into what the agent is doing. After completion, they get one
chat bubble with the final response. The intermediate tool calls are invisible.

**Approach:**
- Show a collapsible "Agent Activity" panel during and after agent execution
- Each tool call gets a one-line entry: `🔧 transpose_notes → Track 1, +12 semitones`
- Entries appear in real-time as the agent works (signals from AgentRunner)
- After completion, the log persists as a collapsible section above the final response
- Each entry links to its undo step (from 19.1) — click to see what changed
- Show step count: `Step 3/50` with the configured limit
- Show per-step token usage if available

**Implementation:**
- New signal: `AgentRunner::toolCallExecuted(QString toolName, QJsonObject args, QJsonObject result)`
- `MidiPilotWidget` receives signal → appends to activity log widget
- Activity log is a `QListWidget` with custom styled items
- Collapsible via a toggle button ("▼ 5 actions" / "▶ 5 actions")

**Implementation Notes (from codebase analysis):**

```
KEY FINDING: AgentStepsWidget ALREADY EXISTS!
  MidiPilotWidget.cpp:101-220 — local class AgentStepsWidget inside the .cpp file
  Already has: planSteps(), addStep(), markActive(), completeStep(), setFinished()
  Already has: collapsible header, step counter, emoji status indicators
  Already receives signals: onAgentStepStarted, onAgentStepCompleted, onAgentStepsPlanned

WHAT'S ALREADY DONE (no work needed):
  ✅ Collapsible step display with header "▶/▼ Steps (3/5)"
  ✅ Real-time step updates (⏳ pending → 🔄 active → ✅ done / ❌ failed / ⚠ retrying)
  ✅ Descriptive step labels via buildStepLabel() (AgentRunner.cpp:222)
      e.g. "Insert events — Track 1 (12 events)", "Rename track — Track 0 → Piano"
  ✅ Steps planned upfront via stepsPlanned signal (batch display before execution)
  ✅ Widget inserted into chat flow, persists after completion

WHAT'S MISSING (actual work for 19.4):
  1. Per-step token usage display
     - AgentRunner already emits tokenUsageUpdated per API round-trip
     - But round-trips can contain MULTIPLE tool calls (batched)
     - Need to split token count across steps in a batch, or show per-round-trip
     - Simplest: show token count on the step label: "✅ Insert events (1.2k tokens)"

  2. Link steps to undo entries (depends on 19.1)
     - After 19.1, each tool call = one protocol step
     - Store the protocol step index alongside the step label
     - On click → highlight that protocol entry in the Protocol tab
     - Or: right-click step → "Undo this step" → protocol()->undo() N times

  3. Re-style AgentStepsWidget for dark themes
     - Current: hardcoded background "#F5F5F0", hardcoded colors
     - Should use Appearance::* helpers for theme-aware colors
     - Low effort, just change the setStyleSheet calls

  4. Show tool arguments summary in expandable detail
     - Currently only shows buildStepLabel() one-liner
     - Could add a tooltip or expandable detail with the full args JSON
     - Nice-to-have, not critical

FILES TO MODIFY:
  src/gui/MidiPilotWidget.cpp — AgentStepsWidget class (lines 101-220)
  src/ai/AgentRunner.cpp      — token usage per-step tracking
  src/gui/MidiPilotWidget.cpp — onAgentStepCompleted handler (line 1120)

ESTIMATED EFFORT: ~60 lines (much less than originally planned since widget exists)
```

---

### 19.5 — Conversation Context Window Management ✅

**Problem:** No truncation strategy when conversations get long. The full
`_conversationHistory` is sent with every request. If a conversation runs for 50+
messages, the context can exceed the model's limit, causing API errors or silently
dropped context.

**Approach:**
- Track cumulative token estimate for `_conversationHistory`
- When approaching the model's context limit (from 19.3 lookup table), apply a
  sliding window: keep system prompt + first 2 messages + last N messages
- Summarization option: before truncating, ask the model to summarize the dropped
  messages into a single "conversation summary" message
- Show a visual indicator when context is being truncated
- User can manually "compact" conversation via a button

**Implementation Notes (from codebase analysis):**

```
CURRENT CONVERSATION FLOW:
  Simple mode — AiClient::sendRequest() (line 196):
    messages = [system_prompt] + [_conversationHistory] + [user_message]
    Entire _conversationHistory sent every time — no size limit!

  Agent mode — AgentRunner::run() (line ~50):
    _messages = [system_prompt] + [conversationHistory + user_msg]
    Then during tool loop, _messages grows with assistant + tool messages
    By final round-trip, _messages can be HUGE (system + history + N tool calls)

RISK ANALYSIS:
  - System prompt (EditorContext::agentSystemPrompt): ~2-4k tokens
  - Each user message with context: ~1-3k tokens (includes editorState JSON)
  - Each assistant response: ~0.5-2k tokens
  - Agent tool loop: each round-trip adds ~2-5k tokens (assistant + tool results)
  - 10-step agent run ≈ 30-50k tokens in _messages
  - 20-message conversation ≈ 20-40k tokens in _conversationHistory
  - gpt-4o-mini has 128k context → safe for most sessions
  - But long conversations + agent mode can hit limits

IMPLEMENTATION APPROACH:
  1. Before each sendRequest/sendMessages, estimate total tokens:
     - Sum chars of all messages / 4 (reuse estimation from 19.3)
     - Compare to model's context window (from 19.3 lookup table)

  2. If estimated tokens > 80% of context window:
     - Sliding window: keep system prompt + first 2 messages (task context)
       + last N messages that fit in ~60% of context
     - Drop middle messages
     - Insert a "[Context truncated — older messages removed]" marker

  3. Summarization (optional, advanced):
     - Before truncating, send dropped messages to model with:
       "Summarize this conversation so far in 200 words"
     - Replace dropped messages with single summary message
     - Expensive (extra API call) → make it opt-in in settings

  4. Visual indicator in MidiPilot:
     - When truncation happens, show yellow bar: "⚠ Context truncated (15 of 42 messages)"
     - "Compact" button → manually trigger summarization

FILES TO MODIFY:
  src/gui/MidiPilotWidget.cpp — add truncation logic before sendRequest calls
    onSendMessage() line ~830 (agent) and ~840 (simple)
    New method: QJsonArray truncateHistory(const QJsonArray &history, int maxTokens)
  src/ai/AiClient.h — expose contextWindowForModel(model) from 19.3 lookup table

DEPENDS ON: 19.3 (context window lookup table + token estimation)
```

---

### 19.6 — Response Streaming (SSE) ✅

**Problem:** Currently waits for the complete API response before showing anything.
For long responses (especially in Agent mode analysis), the user stares at "Thinking..."
for 10-30 seconds with no feedback.

**Approach:**
- Use Server-Sent Events (SSE) streaming: `"stream": true` in API request
- `AiClient` reads chunks via `QNetworkReply::readyRead` signal
- Parse SSE `data:` lines, extract content deltas
- Emit `streamDelta(QString chunk)` signal for incremental display
- `MidiPilotWidget` builds up the response bubble character-by-character
- For JSON action responses: buffer until complete, then parse and execute
- Agent mode: stream the final text response only (tool calls remain non-streamed
  since they need complete JSON to execute)
- Graceful fallback: if streaming fails, fall back to non-streaming request

**Considerations:**
- Anthropic uses a different streaming format than OpenAI — need provider-specific parsing
- Token usage is reported in the final `[DONE]` chunk for OpenAI
- Reduces perceived latency significantly for conversational responses

**Implementation Notes (from codebase analysis):**

```
CURRENT REQUEST FLOW:
  AiClient::sendMessages() (line 227):
    - Builds QJsonObject body with model, messages, tools, etc.
    - Sends via _manager->post(request, QJsonDocument(body).toJson())
    - connect(_manager, &QNetworkAccessManager::finished, this, &AiClient::onReplyFinished)
    - onReplyFinished reads ALL data at once: reply->readAll()
    - Parses complete JSON, extracts choices[0].message.content

  For streaming, need to:
    1. Add "stream": true to the request body
    2. Instead of waiting for finished, connect to readyRead signal
    3. Parse SSE chunks incrementally

STREAMING FORMAT (OpenAI Chat Completions):
  Each chunk: "data: {"choices":[{"delta":{"content":"Hello"}}]}\n\n"
  Final:      "data: [DONE]\n\n"
  Token usage only in final chunk (stream_options: {"include_usage": true})

STREAMING FORMAT (Anthropic):
  event: content_block_delta
  data: {"type":"content_block_delta","delta":{"type":"text_delta","text":"Hello"}}
  Final: event: message_stop

IMPLEMENTATION PLAN:
  1. Add sendStreamingRequest() method to AiClient:
     - Same body as sendMessages() but with "stream": true
     - If OpenAI: add "stream_options": {"include_usage": true}
     - Connect to QNetworkReply::readyRead instead of finished
     - Parse SSE lines incrementally via QByteArray buffer

  2. New signals:
     - streamDelta(const QString &text) — partial content
     - streamFinished(const QString &fullContent, const QJsonObject &usage) — done
     - streamError(const QString &error) — error during streaming

  3. SSE parser helper (in AiClient):
     QByteArray _streamBuffer;
     void onStreamDataAvailable() {
         _streamBuffer += _currentReply->readAll();
         while (_streamBuffer.contains("\n\n")) {
             int idx = _streamBuffer.indexOf("\n\n");
             QByteArray chunk = _streamBuffer.left(idx);
             _streamBuffer.remove(0, idx + 2);
             if (chunk.startsWith("data: ")) {
                 QByteArray json = chunk.mid(6);
                 if (json == "[DONE]") { emit streamFinished(...); return; }
                 // parse delta, emit streamDelta(deltaContent);
             }
         }
     }

  4. MidiPilotWidget integration:
     - For Simple mode text responses: show streaming bubble that grows
     - For JSON actions: buffer until complete, then parse (no streaming display)
     - For Agent mode: only stream the FINAL text response
       AgentRunner tool calls stay non-streamed (need complete JSON to execute)

  5. Smart detection — when to stream:
     - Always stream in Simple mode for text responses
     - Don't stream when tools are provided (Agent mode API calls)
     - Stream the final agent response only (after all tool calls done)

FILES TO MODIFY:
  src/ai/AiClient.h           — add streaming methods + signals + _streamBuffer
  src/ai/AiClient.cpp         — new sendStreamingRequest(), onStreamDataAvailable(), SSE parser
  src/gui/MidiPilotWidget.cpp — connect to stream signals, build incremental bubble
  src/ai/AgentRunner.cpp      — optionally stream final response

RISK: Medium — SSE parsing needs robust buffering (chunks can split mid-JSON).
  Provider differences in streaming format add complexity.
  Fallback to non-streaming should be seamless.

DEPENDS ON: Nothing (independent feature)
```

---

### 19.7 — Per-File AI Presets ✅

**Problem:** Different MIDI files need different AI context. A 16-track orchestral
arrangement needs different system prompt guidance than a 3-track FFXIV bard song.
Currently the system prompt is global.

**Approach:**
- Allow saving a "preset" per MIDI file: custom system prompt additions, preferred model,
  preferred mode (Simple/Agent)
- Store as JSON sidecar: `<midi_filename>.midipilot.json` next to the MIDI file
- Auto-load when opening a MIDI file
- UI: "Save AI preset for this file" button in MidiPilot settings dropdown
- Presets are optional — global defaults still apply when no preset exists
- Include: custom instructions, temperature, max tokens, mode preference

**Implementation Notes (from codebase analysis):**

```
CURRENT SYSTEM PROMPT FLOW:
  MidiPilotWidget.cpp onSendMessage():
    Agent:  agentPrompt = EditorContext::agentSystemPrompt()
            if (ffxivMode()) agentPrompt += EditorContext::ffxivContext()
    Simple: simplePrompt = EditorContext::systemPrompt()
            if (ffxivMode()) simplePrompt += EditorContext::ffxivContext()

  EditorContext is fully static — no per-file state.

  Custom system prompt: SystemPromptDialog (src/gui/SystemPromptDialog.cpp)
    - Loads from QSettings("AI/custom_system_prompt")
    - Appended to the base system prompt
    - Global, not per-file

CURRENT MODEL/PROVIDER SETTINGS:
  All in QSettings:
    AI/provider, AI/api_base_url, AI/model, AI/reasoning_effort
    AI/api_key_<provider>
    AI/max_tokens_enabled, AI/max_tokens_limit
    AI/agent_max_steps

  MidiPilotWidget footer has: provider combo, model combo, effort combo, FFXIV checkbox
  These are global and change QSettings directly.

SIDECAR FILE DESIGN:
  File: <midi_filename>.midipilot.json (next to the .mid file)
  e.g.: "Sweet Child O Mine.mid" → "Sweet Child O Mine.mid.midipilot.json"

  Content:
  {
    "version": 1,
    "customInstructions": "This is an 8-track FFXIV bard arrangement...",
    "preferredModel": "gpt-4o",
    "preferredProvider": "openai",
    "preferredMode": "agent",
    "ffxivMode": true,
    "reasoningEffort": "medium",
    "maxTokensLimit": 8000
  }

  All fields optional — unset fields use global defaults.

IMPLEMENTATION:
  1. New method: MidiPilotWidget::loadPresetForFile(const QString &midiPath)
     - Check if <midiPath>.midipilot.json exists
     - Parse JSON, apply settings to UI combos (model, provider, effort, mode, ffxiv)
     - Append customInstructions to system prompt
     - Called from onFileChanged()

  2. New method: MidiPilotWidget::savePresetForFile()
     - Serialize current UI state to JSON
     - Write to <_file->path()>.midipilot.json
     - Prompt user for customInstructions text

  3. UI: "Save AI preset for this file" in the settings dropdown (⚙ button)
     - Opens a small dialog with:
       - Text area: "Custom instructions for this file"
       - Checkboxes: which settings to save (model, mode, ffxiv, etc.)
       - Save / Cancel

  4. Auto-load: in onFileChanged() (already called when file loads)
     - After setting _file, call loadPresetForFile(_file->path())
     - Show a subtle notification: "Loaded AI preset for this file"

FILES TO MODIFY:
  src/gui/MidiPilotWidget.h   — add loadPresetForFile(), savePresetForFile()
  src/gui/MidiPilotWidget.cpp — implement load/save, integrate into onFileChanged()
  src/gui/MidiPilotWidget.cpp — add menu item in settings dropdown

DEPENDS ON: Nothing (independent feature)
RISK: Low — simple JSON file I/O, no complex logic
```

---

## Phase 20: Audio Export (WAV / FLAC / OGG / MP3) ✅

> **Goal:** Let users render the current MIDI file (or a selected section) to standard
> audio formats using the loaded SoundFont(s). Supports full-file export and
> selection-range export with a unified Export Dialog. Accessible from File menu,
> toolbar, context menu, and keyboard shortcut.
>
> **Motivation:** MIDI files are great for editing but useless for sharing — you can't
> post a `.mid` to social media or send it to a non-musician. With FluidSynth already
> powering real-time playback, we have everything needed to render offline to WAV/FLAC/OGG
> natively. MP3 requires an external encoder (LAME) but is the #1 format people expect.
> Mewo started a basic WAV-only export in the upstream fork — we extend it into a proper
> export workflow with format selection, quality settings, range control, and progress UI.
>
> **Status:** All sub-phases complete (20.1–20.8, v1.2.0). Includes MP3/LAME (20.5),
> SoundFont management (20.7), and FluidSynth hardening (20.8).

### Analysis: What Exists Already

**Mewo's original MidiEditor (reference, DO NOT MODIFY):**
- `FluidSynthEngine::exportToWav(midiFilePath, wavFilePath)` — full implementation
  - Creates a separate FluidSynth instance with `audio.driver = "file"`
  - Loads all SoundFonts from current stack
  - Uses `fluid_player` + `fluid_file_renderer` to render block-by-block
  - Emits `exportProgress(int percent)` and `exportFinished(bool, QString)`
  - Thread-safe: runs in `QThreadPool::globalInstance()`
- `MidiSettingsWidget::onExportToWav()` — trigger from "Export MIDI" button
  - Saves workspace to temp `.mid` file → renders → saves WAV to Downloads
  - Progress shown as gradient fill on the disabled button (clever hack)
  - No format selection, no range support, WAV-only

**MidiEditor_AI (our codebase):**
- `FluidSynthEngine` has **NO export code** — no `exportToWav()`, no signals
- `MidiSettingsWidget` has **NO export button**
- `MatrixWidget` context menu exists (line ~1733) with editing actions — no export
- FluidSynth 2.5.2 pre-built binaries are available in `fluidsynth/`
- Real-time playback already works with SoundFont stack

**FluidSynth 2.5.2 native file format support** (via `audio.file.type` setting):
- **WAV** ✅ — always available (default)
- **FLAC** ✅ — if built with libsndfile (our build has it)
- **OGG/Vorbis** ✅ — via `oga` type, requires libsndfile + libvorbis
- **AIFF** ✅ — via libsndfile
- **RAW** ✅ — always available (headerless PCM)
- **MP3** ❌ — NOT supported by FluidSynth. Requires external LAME encoder.

**Sample format options** (`audio.file.format`):
- s16 (16-bit signed PCM — CD quality, default)
- s24 (24-bit signed PCM — studio quality)
- s32 (32-bit signed PCM)
- float (32-bit float)
- double (64-bit float)

### Implementation Order

```
Phase 20.1   FluidSynthEngine Export Core (render pipeline)         ✅
Phase 20.2   Export Dialog (format, quality, range selection)        ✅
Phase 20.3   Full-File Export (File menu + toolbar)                  ✅
Phase 20.4   Selection/Range Export (context menu + dialog)          ✅
Phase 20.5   MP3 Support via LAME (built-in static encoder)         ✅
Phase 20.6   Export Progress UI (progress bar + cancel)             ✅
Phase 20.7   SoundFont Management (enable/disable + FFXIV auto)     ✅
Phase 20.8   FluidSynth Hardening (driver fallback + error UX)      ✅
```

### Estimated Complexity

| Sub-phase | New Code | Modified Code | Risk |
|-----------|----------|---------------|------|
| 20.1 Export core | ~200 lines (FluidSynthEngine) | FluidSynthEngine.h (~30 lines) | Low |
| 20.2 Export dialog | ~400 lines (new ExportDialog class) | — | Low |
| 20.3 Full-file export | ~80 lines | MainWindow.cpp (~40 lines), MidiSettingsWidget.cpp (~20 lines) | Low |
| 20.4 Selection/range export | ~180 lines | MatrixWidget.cpp (~30 lines), MidiFile.cpp (~60 lines) | Medium |
| 20.5 MP3 via LAME | ~150 lines | ExportDialog.cpp (~30 lines), CMakeLists.txt (~20 lines) | Medium |
| 20.6 Progress UI | ~120 lines | ExportDialog.cpp (~40 lines) | Low |
| 20.7 SoundFont mgmt | ~80 lines | MidiSettingsWidget.cpp (~60), FluidSynthEngine.cpp (~40) | Low |
| 20.8 FluidSynth hardening | ~100 lines | FluidSynthEngine.cpp (~60), MidiSettingsWidget.cpp (~30), MidiOutput.cpp (~15) | Medium |
| **Total** | **~1130 lines new** | **~270 lines modified** | **Low-Medium** |

---

### 20.1 — FluidSynthEngine Export Core (Render Pipeline) ⬜

**Goal:** Add offline rendering to FluidSynthEngine that can export a MIDI file (or
section) to WAV/FLAC/OGG using the currently loaded SoundFont stack.

**Approach — port and extend Mewo's implementation:**

Mewo's `exportToWav()` is a solid foundation but limited to WAV. We extend it with:
1. Configurable output format (WAV, FLAC, OGG) via `audio.file.type`
2. Configurable sample format (s16, s24, float) via `audio.file.format`
3. Configurable sample rate (22050, 44100, 48000, 96000)
4. Optional tick range (startTick, endTick) for partial exports
5. Cancel support via atomic flag
6. Quality setting for lossy formats via `fluid_file_set_encoding_quality()`

**New method signature:**

```cpp
struct ExportOptions {
    QString midiFilePath;       // Source MIDI (temp file from workspace)
    QString outputFilePath;     // Destination audio file
    QString fileType;           // "wav", "flac", "oga", "aiff", "raw"  (FluidSynth audio.file.type)
    QString sampleFormat;       // "s16", "s24", "s32", "float", "double"
    double sampleRate;          // 44100.0, 48000.0, etc.
    double encodingQuality;     // 0.0–1.0 for lossy formats (OGG)
    int startTick;              // -1 = beginning (for range export)
    int endTick;                // -1 = end of file (for range export)
    bool includeReverbTail;     // Render extra 2s after last note for reverb decay
};

void exportAudio(const ExportOptions &options);
void cancelExport();
```

**New signals:**

```cpp
signals:
    void exportProgress(int percent);
    void exportFinished(bool success, const QString &outputPath);
    void exportCancelled();
```

**Implementation Notes:**

```
FILES TO MODIFY:
  src/midi/FluidSynthEngine.h   — add ExportOptions struct, exportAudio(), cancelExport(),
                                   signals, _cancelExport atomic flag
  src/midi/FluidSynthEngine.cpp — implement exportAudio() render loop

RENDER PIPELINE (based on Mewo's proven approach):
  1. Create SEPARATE FluidSynth settings/synth/player (don't touch live playback instance)
  2. Configure file output:
       fluid_settings_setstr(settings, "audio.driver", "file");
       fluid_settings_setstr(settings, "audio.file.name", outputPath);
       fluid_settings_setstr(settings, "audio.file.type", options.fileType);     // NEW
       fluid_settings_setstr(settings, "audio.file.format", options.sampleFormat); // NEW
       fluid_settings_setnum(settings, "synth.sample-rate", options.sampleRate);
  3. Load all SoundFonts from current _loadedFonts stack (copy paths under lock)
  4. Load MIDI file via fluid_player_add()
  5. If range export: use fluid_player_seek() to startTick (if supported),
     or let it play from beginning and skip writing until startTick
  6. Render loop:
       while (fluid_player_get_status(player) == FLUID_PLAYER_PLAYING) {
           if (_cancelExport.load()) { cleanup; emit exportCancelled(); return; }
           fluid_file_renderer_process_block(renderer);
           // Calculate progress
           int curTick = fluid_player_get_current_tick(player);
           int percent = calculateProgress(curTick, startTick, endTick, totalTicks);
           emit exportProgress(percent);
           // Stop at endTick if range export
           if (endTick > 0 && curTick >= endTick && !options.includeReverbTail) break;
           if (endTick > 0 && curTick >= endTick + reverbTailTicks) break;
       }
  7. If includeReverbTail: render ~2 extra seconds of silence (let reverb/release decay)
  8. Cleanup: delete renderer, player, synth, settings
  9. emit exportProgress(100); emit exportFinished(true, outputPath);

CANCEL SUPPORT:
  std::atomic<bool> _cancelExport{false};
  void cancelExport() { _cancelExport.store(true); }
  Checked every block in render loop — responsive cancel.

RANGE EXPORT (tick-based):
  FluidSynth's fluid_player doesn't support seek-to-tick natively in all versions.
  Two strategies:
  A) PREFERRED: Save a trimmed MIDI file from MidiFile (startTick → endTick) to temp,
     then render that temp file normally. This is simpler and guaranteed correct.
     MidiFile already has eventsBetween(start, end) — build a minimal MIDI from that.
  B) FALLBACK: Render full file but track tick position, only count progress within range.
     Wasteful for "export last 8 bars of a 200-bar piece" but always works.

  → Strategy A is better. New utility: MidiFile::saveRange(startTick, endTick, tempPath)

REVERB TAIL:
  After the last MIDI event, reverb/chorus effects may still be decaying.
  Render an extra ~2 seconds (sampleRate * 2 / period_size blocks) of silence
  so the audio doesn't cut off abruptly. Configurable via checkbox in dialog.

THREADING:
  Export runs on QThreadPool::globalInstance() (same as Mewo's approach).
  Signals cross thread boundary via Qt::QueuedConnection (automatic for QObject signals).
  The live playback synth is completely untouched — separate synth instance.

ENCODING QUALITY:
  fluid_file_set_encoding_quality(renderer, quality) — affects OGG/Vorbis compression.
  0.0 = lowest quality/smallest file, 1.0 = highest quality/largest file.
  Default 0.5 is good. Expose in dialog as "Quality" slider for OGG.

EDGE CASES:
  - No SoundFonts loaded → show error "No SoundFont loaded. Load a SoundFont in
    Settings before exporting."
  - Empty MIDI file → show error "Nothing to export."
  - Output path not writable → FluidSynth will fail to create renderer → handle gracefully
  - Very long files → progress bar essential, cancel button essential
  - Disk full during render → fluid_file_renderer_process_block returns error → handle

DEPENDS ON: Nothing (extends existing FluidSynthEngine)
RISK: Low — proven approach from Mewo's code, just extended
```

---

### 20.2 — Export Dialog (Format, Quality, Range Selection) ⬜

**Goal:** A user-friendly modal dialog for configuring audio export. Replaces Mewo's
button-only approach with a proper settings dialog.

**Dialog Layout:**

```
┌─────────────────── Export Audio ───────────────────┐
│                                                     │
│  Source: Sweet Child O Mine.mid                     │
│  Duration: 4:32 (272 seconds)                       │
│                                                     │
│  ┌─ Range ──────────────────────────────────────┐   │
│  │ ○ Full song                                   │   │
│  │ ○ Selection (Measure 5–12, 0:08–0:24)         │   │
│  │ ○ Custom range:  [From: ____] [To: ____]      │   │
│  └───────────────────────────────────────────────┘   │
│                                                     │
│  ┌─ Format ─────────────────────────────────────┐   │
│  │ Format:     [WAV ▼]                           │   │
│  │ Quality:    [CD Quality (16-bit, 44.1kHz) ▼]  │   │
│  │ Channels:   [Stereo ▼]                        │   │
│  └───────────────────────────────────────────────┘   │
│                                                     │
│  ┌─ Options ────────────────────────────────────┐   │
│  │ ☑ Include reverb tail (2s after last note)    │   │
│  │ ☐ Normalize volume                            │   │
│  │ SoundFont: GeneralUser GS v1.471.sf2          │   │
│  │            (Using current SoundFont stack)     │   │
│  └───────────────────────────────────────────────┘   │
│                                                     │
│  Estimated file size: ~28.4 MB                      │
│                                                     │
│            [Cancel]  [Export...]                     │
└─────────────────────────────────────────────────────┘
```

**UI Elements:**

| Widget | Type | Options |
|--------|------|---------|
| Range radio buttons | QRadioButton group | Full song / Selection / Custom |
| Custom range from/to | QSpinBox (measure) or QTimeEdit | Measure or mm:ss |
| Format combo | QComboBox | WAV, FLAC, OGG Vorbis, MP3* |
| Quality preset | QComboBox | Draft (s16/22050), CD (s16/44100), Studio (s24/48000), Hi-Res (s24/96000) |
| OGG quality slider | QSlider | 0.1–1.0 (shown only for OGG) |
| Reverb tail | QCheckBox | Default: checked |
| Estimated size | QLabel | Auto-calculated from duration × format × sample rate |
| SoundFont info | QLabel | Read-only, shows current SF stack |

*MP3 shown only if LAME encoder is available (Phase 20.5)

**Quality Presets:**

| Preset | Sample Format | Sample Rate | Bit Depth | Use Case |
|--------|--------------|-------------|-----------|----------|
| Draft | s16 | 22050 Hz | 16-bit | Quick preview, small file |
| CD Quality | s16 | 44100 Hz | 16-bit | Standard sharing (default) |
| Studio | s24 | 48000 Hz | 24-bit | Professional quality |
| Hi-Res | s24 | 96000 Hz | 24-bit | Archival / mastering |
| Custom | user-set | user-set | user-set | Advanced users |

**File Size Estimation Formula:**
```
WAV:   duration_sec × sample_rate × channels × (bit_depth / 8) bytes
FLAC:  WAV_size × ~0.55 (typical compression ratio)
OGG:   duration_sec × bitrate_estimate (based on quality slider)
MP3:   duration_sec × bitrate / 8
```

**Implementation Notes:**

```
FILES TO CREATE:
  src/gui/ExportDialog.h    — QDialog subclass
  src/gui/ExportDialog.cpp  — full dialog implementation (~400 lines)

FILES TO MODIFY:
  CMakeLists.txt — add ExportDialog.cpp to SOURCES

CLASS DESIGN:
  class ExportDialog : public QDialog {
      Q_OBJECT
  public:
      ExportDialog(MidiFile *file, QWidget *parent = nullptr);

      // Set up for selection-based export (from context menu)
      void setSelectionRange(int startTick, int endTick);

      // Returns configured ExportOptions (from 20.1)
      ExportOptions exportOptions() const;

      // Returns the chosen output file path (from Save dialog)
      QString outputFilePath() const;

  private slots:
      void onFormatChanged(int index);
      void onQualityPresetChanged(int index);
      void onRangeChanged();
      void updateEstimatedSize();
      void onExportClicked();

  private:
      void setupUi();
      void populateFormats();
      void populateQualityPresets();
      QString formatFilter() const;       // For QFileDialog filter
      qint64 estimateFileSize() const;    // Size estimation

      MidiFile *_file;
      int _selectionStartTick;   // -1 if no selection
      int _selectionEndTick;     // -1 if no selection

      // UI elements
      QRadioButton *_fullSongRadio;
      QRadioButton *_selectionRadio;
      QRadioButton *_customRangeRadio;
      QSpinBox *_fromMeasure, *_toMeasure;
      QComboBox *_formatCombo;
      QComboBox *_qualityPresetCombo;
      QSlider *_oggQualitySlider;
      QLabel *_oggQualityLabel;
      QCheckBox *_reverbTailCheck;
      QLabel *_estimatedSizeLabel;
      QLabel *_soundFontLabel;
      QPushButton *_exportBtn;
      QPushButton *_cancelBtn;
  };

RANGE HANDLING:
  - "Full song": startTick=-1, endTick=-1 (render everything)
  - "Selection": uses the startTick/endTick passed from context menu
    or computed from Selection::instance()->selectedEvents() min/max ticks
  - "Custom range": user picks measures via QSpinBox, converted to ticks via
    MidiFile::startTickOfMeasure(measure)

  Selection radio is DISABLED (grayed out) when no selection exists.
  When triggered from context menu, selection radio is pre-selected.

FORMAT COMBO ITEMS:
  - "WAV — Uncompressed PCM (.wav)" → fileType="wav"
  - "FLAC — Lossless Compressed (.flac)" → fileType="flac"
  - "OGG Vorbis — Lossy Compressed (.ogg)" → fileType="oga"
  - "MP3 — Lossy Compressed (.mp3)" → shown only if LAME available (20.5)

  When format changes:
  - OGG: show quality slider, hide bit-depth options (Vorbis handles internally)
  - MP3: show bitrate combo (128/192/256/320 kbps)
  - WAV/FLAC: show quality preset combo with bit-depth options

REMEMBER LAST SETTINGS:
  Save user's last export choices to QSettings("Export/..."):
  - Export/format, Export/quality_preset, Export/reverb_tail, Export/ogg_quality
  - Export/last_directory (for QFileDialog)
  Reload on next dialog open → user doesn't have to reconfigure every time.

DEPENDS ON: 20.1 (ExportOptions struct)
RISK: Low — standard QDialog construction
```

---

### 20.3 — Full-File Export (File Menu + Toolbar + Settings) ⬜

**Goal:** Wire up the Export Dialog to File menu, toolbar button, MidiSettingsWidget,
and keyboard shortcut for full-file export.

**Access Points:**

| Location | Trigger | Behavior |
|----------|---------|----------|
| File menu | `File → Export Audio...` (Ctrl+Shift+E) | Opens ExportDialog (full song mode) |
| Toolbar | 🔊 Export button (optional) | Same as File menu |
| MidiSettingsWidget | "Export Audio" button (replaces Mewo's "Export MIDI") | Same as File menu |
| Keyboard | Ctrl+Shift+E | Same as File menu |

**Workflow:**
1. User triggers export from any access point
2. ExportDialog opens in "Full song" mode
3. User configures format, quality, range (optional)
4. User clicks "Export..." → QFileDialog for output path
5. Dialog closes → progress UI appears (20.6)
6. Render runs in background thread → progress updates
7. Finished → notification "Export complete: filename.wav"

**Implementation Notes:**

```
FILES TO MODIFY:
  src/gui/MainWindow.h       — declare exportAudio() slot, _exportAudioAction
  src/gui/MainWindow.cpp     — add File menu item, toolbar button, shortcut, handler
  src/gui/MidiSettingsWidget.h   — declare _exportAudioBtn, onExportAudio()
  src/gui/MidiSettingsWidget.cpp — add export button in FluidSynth settings group

MAINWINDOW INTEGRATION:
  1. In setupActions() (after "Save As" action, ~line 3500):
     _exportAudioAction = new QAction(QIcon(":/run_environment/graphics/tool/export_audio.png"),
                                       tr("Export Audio..."), this);
     _exportAudioAction->setShortcut(QKeySequence(tr("Ctrl+Shift+E")));
     _exportAudioAction->setEnabled(false); // enabled when file loaded
     connect(_exportAudioAction, &QAction::triggered, this, &MainWindow::exportAudio);
     fileMB->addAction(_exportAudioAction);

  2. exportAudio() slot:
     void MainWindow::exportAudio() {
         if (!_file) return;
         ExportDialog dlg(_file, this);
         if (dlg.exec() == QDialog::Accepted) {
             startExport(dlg.exportOptions(), dlg.outputFilePath());
         }
     }

  3. startExport() helper:
     - Save workspace to temp MIDI (same as Mewo)
     - Set options.midiFilePath = tempMidi
     - Show progress UI (20.6)
     - QThreadPool::globalInstance()->start([options]() {
           FluidSynthEngine::instance()->exportAudio(options);
       });

  4. Enable/disable _exportAudioAction when file is loaded/closed
     (same pattern as existing Save/SaveAs actions)

  5. In onFileLoaded() / onFileClosed():
     _exportAudioAction->setEnabled(_file != nullptr);

MIDISETTINGSWIDGET "EXPORT AUDIO" BUTTON:
  Add button in FluidSynth settings group (where Mewo had "Export MIDI"):
    _exportAudioBtn = new QPushButton(tr("Export Audio..."), _fluidSynthSettingsGroup);
    _exportAudioBtn->setToolTip(tr("Export current file to WAV, FLAC, OGG, or MP3"));
    sfBtnCol->addWidget(_exportAudioBtn);
    connect(_exportAudioBtn, &QPushButton::clicked, this, &MidiSettingsWidget::onExportAudio);

  onExportAudio() just calls _mainWindow->exportAudio() (delegates to MainWindow).

ICON:
  Need a new icon: export_audio.png (speaker with arrow, or waveform with download arrow)
  Place at: run_environment/graphics/tool/export_audio.png
  For dark theme: run_environment/graphics/tool/dark/export_audio.png
  Can start with a placeholder from existing icons and refine later.

GUARD: If no SoundFonts are loaded:
  Show QMessageBox::warning("No SoundFont loaded",
    "Please load a SoundFont in Settings → FluidSynth before exporting audio.");

DEPENDS ON: 20.1, 20.2
RISK: Low — standard menu/toolbar wiring
```

---

### 20.4 — Selection/Range Export (Context Menu + Dialog) ⬜

**Goal:** Let users select notes/measures in the piano roll, right-click, and export
just that section as audio. This is the key user-requested feature.

**Workflow:**
1. User selects notes in piano roll (or sets loop markers for a region)
2. Right-click → context menu shows "Export Selection as Audio..."
3. ExportDialog opens with "Selection" radio pre-selected
4. Range shown: "Measure 5–12 (0:08 – 0:24)"
5. User can adjust range or switch to "Full song"
6. Export proceeds as normal

**Two selection modes:**

| Mode | Source | Tick Range |
|------|--------|------------|
| Note selection | `Selection::instance()->selectedEvents()` | Min tick → max tick+duration of selected events |
| Visible viewport | `MatrixWidget::minVisibleMidiTime()` / `maxVisibleMidiTime()` | Viewport bounds |

**Context menu integration:**

```cpp
// In MatrixWidget::contextMenuEvent() — after existing items, before menu.exec():
menu.addSeparator();
QAction *exportSelAct = menu.addAction(QIcon(":/run_environment/graphics/tool/export_audio.png"),
                                        tr("Export Selection as Audio..."));
exportSelAct->setEnabled(hasSelection);
connect(exportSelAct, &QAction::triggered, this, [this]() {
    MainWindow *mw = qobject_cast<MainWindow*>(window());
    if (mw) mw->exportAudioSelection();
});
```

**Implementation Notes:**

```
FILES TO MODIFY:
  src/gui/MatrixWidget.cpp   — add "Export Selection as Audio..." to contextMenuEvent()
  src/gui/MainWindow.h       — declare exportAudioSelection() slot
  src/gui/MainWindow.cpp     — implement exportAudioSelection()

RANGE CALCULATION:
  void MainWindow::exportAudioSelection() {
      if (!_file) return;
      QList<MidiEvent*> selected = Selection::instance()->selectedEvents();
      if (selected.isEmpty()) {
          QMessageBox::information(this, tr("No Selection"),
              tr("Select some notes first, then use Export Selection."));
          return;
      }

      // Find tick range from selection
      int minTick = INT_MAX, maxTick = 0;
      for (MidiEvent *e : selected) {
          minTick = qMin(minTick, e->midiTime());
          int endTick = e->midiTime();
          if (NoteOnEvent *note = dynamic_cast<NoteOnEvent*>(e)) {
              if (note->offEvent()) endTick = note->offEvent()->midiTime();
          }
          maxTick = qMax(maxTick, endTick);
      }

      // Expand to measure boundaries for cleaner export
      int measureStart, measureEnd;
      _file->measure(minTick, &measureStart, nullptr);
      _file->measure(maxTick, nullptr, &measureEnd);

      ExportDialog dlg(_file, this);
      dlg.setSelectionRange(measureStart, measureEnd);
      if (dlg.exec() == QDialog::Accepted) {
          startExport(dlg.exportOptions(), dlg.outputFilePath());
      }
  }

TEMP MIDI FOR RANGE EXPORT:
  The trickiest part: rendering only a portion of the song.

  Strategy: Create a temporary MIDI file containing ONLY events in the range,
  with correct tempo/time-signature setup events prepended.

  New utility method:
    bool MidiFile::saveRange(int startTick, int endTick, const QString &path);

  This method:
  1. Creates a new temporary MidiFile in memory
  2. Copies tempo events at or before startTick (so playback speed is correct)
  3. Copies time signature events at or before startTick
  4. Copies program change events at or before startTick for each channel
     (so instruments are correct)
  5. Copies all events between startTick and endTick, shifting ticks by -startTick
     so the exported file starts at tick 0
  6. Copies control change events (volume, pan, etc.) active at startTick
  7. Saves to the given path

  This ensures the range export sounds exactly like that section during playback,
  with correct instruments, tempo, and effects.

  Alternative (simpler but wasteful): render the FULL file and trim the WAV after.
  Downside: slow for "export last 8 bars of a 200-bar piece." The MIDI trim
  approach is much faster.

EDGE CASES:
  - Selection spans multiple tracks/channels → all included in range export
  - Notes start before range but end within → include them (partial note = audible)
  - No selection + context menu: "Export Selection" is grayed out
  - Very short selection (<1 measure): warn user but allow it
  - Selection crosses tempo change: include all tempo events → correct timing

MEASURE DISPLAY IN DIALOG:
  When setSelectionRange() is called, the dialog shows:
  "Selection: Measure 5–12 (0:08 – 0:24)" computed from ticks via
  MidiFile::measure() and MidiFile::msOfTick()

DEPENDS ON: 20.1, 20.2, 20.3
RISK: Medium — range/trim logic needs careful handling of setup events
```

---

### 20.5 — MP3 Support via LAME (Optional Encoder) ⬜

**Goal:** Add MP3 export support. Since FluidSynth doesn't support MP3 natively,
we use a two-step pipeline: render to WAV → encode to MP3 via LAME.

**Approach:**

Two options evaluated:

| Option | Approach | Pros | Cons |
|--------|----------|------|------|
| A: Ship LAME DLL | Bundle `libmp3lame.dll` + use C API | No user setup, seamless | License (LGPL), ~200KB extra |
| B: External `lame.exe` | Detect installed LAME, call via QProcess | No license concern, user's choice | User must install LAME separately |
| **C: Both** | Try DLL first, fall back to exe | Best UX | More code |

**Recommended: Option C (try DLL, fall back to exe)**

**Pipeline:**
```
MIDI → [FluidSynth renders WAV to temp] → [LAME encodes WAV→MP3] → [delete temp WAV]
```

**LAME detection:**
```cpp
// 1. Check for bundled libmp3lame.dll next to executable
// 2. Check for lame.exe in PATH
// 3. Check common install locations (Windows: C:\Program Files\LAME\)
// 4. Check QSettings("Export/lame_path") for user-configured path
```

**MP3 Quality Options:**

| Preset | Bitrate | Quality | Use Case |
|--------|---------|---------|----------|
| Low | 128 kbps CBR | Acceptable | Small file, previews |
| Medium | 192 kbps CBR | Good | General sharing (default) |
| High | 256 kbps CBR | Very good | Quality-conscious sharing |
| Maximum | 320 kbps CBR | Excellent | Near-lossless |
| VBR V2 | ~190 kbps VBR | Very good | Best quality/size ratio |

**Implementation Notes:**

```
FILES TO CREATE:
  src/audio/LameEncoder.h    — MP3 encoding wrapper
  src/audio/LameEncoder.cpp  — DLL loading + QProcess fallback (~150 lines)

FILES TO MODIFY:
  src/gui/ExportDialog.cpp — show MP3 option when LAME available, bitrate combo
  CMakeLists.txt           — optional LAME detection + linking

CLASS DESIGN:
  class LameEncoder {
  public:
      static bool isAvailable();           // Check if LAME is usable
      static QString lamePath();            // Path to lame.exe or DLL
      static void setLamePath(const QString &path);  // User override

      // Encode WAV to MP3. Blocking call (run in worker thread).
      // Emits progress via callback.
      static bool encode(const QString &wavPath, const QString &mp3Path,
                         int bitrate, bool vbr,
                         std::function<void(int percent)> progress = nullptr);
  private:
      static bool encodeDll(const QString &wavPath, const QString &mp3Path,
                            int bitrate, bool vbr,
                            std::function<void(int percent)> progress);
      static bool encodeExe(const QString &wavPath, const QString &mp3Path,
                            int bitrate, bool vbr,
                            std::function<void(int percent)> progress);
  };

EXPORT PIPELINE FOR MP3:
  In FluidSynthEngine::exportAudio() or MainWindow::startExport():
  if (format == "mp3") {
      // Step 1: render to temp WAV
      ExportOptions wavOpts = options;
      wavOpts.outputFilePath = QDir::tempPath() + "/midieditor_export_temp.wav";
      wavOpts.fileType = "wav";
      exportAudio(wavOpts);  // renders WAV

      // Step 2: encode WAV → MP3
      LameEncoder::encode(wavOpts.outputFilePath, options.outputFilePath,
                          options.mp3Bitrate, options.mp3Vbr,
                          [this](int p) { emit exportProgress(50 + p/2); });

      // Step 3: cleanup temp WAV
      QFile::remove(wavOpts.outputFilePath);
  }

  Progress is split: 0–50% for WAV render, 50–100% for MP3 encode.

DLL LOADING (Windows):
  Using QLibrary for runtime loading of libmp3lame.dll:
    QLibrary lame("libmp3lame");
    if (lame.load()) {
        auto lame_init = (lame_t(*)()) lame.resolve("lame_init");
        auto lame_encode_buffer = ... // resolve all needed functions
        // Use LAME C API directly
    }

  This avoids compile-time dependency — LAME is purely optional.
  If DLL not found, fall back to lame.exe via QProcess.

QProcess FALLBACK:
  QProcess lameProc;
  lameProc.start(lamePath(), {
      "-h",                           // High quality
      "--cbr", "-b", bitrate,         // Bitrate
      wavPath, mp3Path
  });
  lameProc.waitForFinished(-1);       // Block until done

  Progress: parse LAME's stderr output for progress percentage.

UI CHANGES:
  In ExportDialog:
  - MP3 format entry shown ONLY if LameEncoder::isAvailable()
  - If not available, show grayed-out "MP3 (LAME not found)" with tooltip:
    "Install LAME encoder or place libmp3lame.dll next to MidiEditor.exe"
  - When MP3 selected: show bitrate combo instead of quality preset combo
  - Link/button: "Download LAME..." → opens https://lame.sourceforge.io in browser

LICENSING NOTE:
  LAME is LGPL. If we ship the DLL:
  - LGPL requires: dynamic linking (✓ via QLibrary), provide LAME source/link, 
    allow user to replace the DLL
  - Our app is GPL3 ← compatible with LGPL
  - Add LICENSE.LAME to the distribution
  If we only support external lame.exe: no licensing concern at all.

DEPENDS ON: 20.1, 20.2, 20.6
RISK: Medium — external dependency, two encoding paths, DLL loading complexity
  Can be deferred if other formats are sufficient initially.
```

---

### 20.6 — Export Progress UI (Progress Bar + Cancel) ⬜

**Goal:** Show a clear, non-blocking progress indicator during export with cancel support.

**Two UI options evaluated:**

| Option | Description | Pros | Cons |
|--------|-------------|------|------|
| A: Modal progress dialog | QProgressDialog with cancel button | Simple, proven Qt pattern | Blocks interaction |
| B: Status bar + toast | Non-modal, export runs silently | User can keep editing | Easy to miss |
| **C: Floating progress bar** | Small overlay at bottom of MatrixWidget | Non-blocking, visible | Custom widget |

**Recommended: Option A (QProgressDialog) for simplicity, with enhancement**

QProgressDialog is the standard Qt approach and users expect it. Enhancement: add
file info below the progress bar (format, estimated time remaining).

**Progress Dialog Layout:**
```
┌──────────── Exporting Audio ─────────────┐
│                                           │
│  Rendering: Sweet Child O Mine.wav        │
│  Format: WAV (16-bit, 44100 Hz)           │
│                                           │
│  [████████████████████░░░░░░░░░] 62%      │
│                                           │
│  Elapsed: 0:12  |  Remaining: ~0:07       │
│                                           │
│              [Cancel]                      │
└───────────────────────────────────────────┘
```

**Implementation Notes:**

```
FILES TO MODIFY:
  src/gui/MainWindow.h    — add _exportProgressDialog member, progress/finished slots
  src/gui/MainWindow.cpp  — create QProgressDialog, connect signals, handle cancel

PROGRESS DIALOG:
  void MainWindow::startExport(const ExportOptions &opts, const QString &outputPath) {
      // Save to temp MIDI
      QString tempMidi = QDir::tempPath() + "/MidiEditor_export_" +
                         QString::number(QCoreApplication::applicationPid()) + ".mid";
      if (opts.startTick >= 0 && opts.endTick > 0) {
          _file->saveRange(opts.startTick, opts.endTick, tempMidi);
      } else {
          _file->save(tempMidi);
      }

      ExportOptions finalOpts = opts;
      finalOpts.midiFilePath = tempMidi;
      finalOpts.outputFilePath = outputPath;

      // Create progress dialog
      _exportProgressDialog = new QProgressDialog(
          tr("Exporting to %1...").arg(QFileInfo(outputPath).fileName()),
          tr("Cancel"), 0, 100, this);
      _exportProgressDialog->setWindowTitle(tr("Exporting Audio"));
      _exportProgressDialog->setWindowModality(Qt::WindowModal);
      _exportProgressDialog->setMinimumDuration(0);  // Show immediately
      _exportProgressDialog->setValue(0);

      // Connect signals
      connect(FluidSynthEngine::instance(), &FluidSynthEngine::exportProgress,
              _exportProgressDialog, &QProgressDialog::setValue);
      connect(FluidSynthEngine::instance(), &FluidSynthEngine::exportFinished,
              this, &MainWindow::onExportFinished);
      connect(FluidSynthEngine::instance(), &FluidSynthEngine::exportCancelled,
              this, &MainWindow::onExportCancelled);
      connect(_exportProgressDialog, &QProgressDialog::canceled,
              FluidSynthEngine::instance(), &FluidSynthEngine::cancelExport);

      // Track timing for "time remaining" estimate
      _exportStartTime = QElapsedTimer();
      _exportStartTime.start();
      connect(FluidSynthEngine::instance(), &FluidSynthEngine::exportProgress,
              this, [this](int percent) {
          if (percent > 0 && _exportProgressDialog) {
              qint64 elapsed = _exportStartTime.elapsed();
              qint64 remaining = (elapsed * (100 - percent)) / percent;
              _exportProgressDialog->setLabelText(
                  tr("Exporting... %1 elapsed, ~%2 remaining")
                  .arg(formatTime(elapsed))
                  .arg(formatTime(remaining)));
          }
      });

      // Run export in thread pool
      QThreadPool::globalInstance()->start([finalOpts]() {
          FluidSynthEngine::instance()->exportAudio(finalOpts);
      });
  }

FINISHED HANDLER:
  void MainWindow::onExportFinished(bool success, const QString &path) {
      // Cleanup
      _exportProgressDialog->close();
      _exportProgressDialog->deleteLater();
      _exportProgressDialog = nullptr;

      // Remove temp MIDI
      QFile::remove(QDir::tempPath() + "/MidiEditor_export_" +
                    QString::number(QCoreApplication::applicationPid()) + ".mid");

      if (success) {
          // Show success notification
          QMessageBox::information(this, tr("Export Complete"),
              tr("Audio exported successfully to:\n%1\n\nFile size: %2")
              .arg(path)
              .arg(formatFileSize(QFileInfo(path).size())));

          // Optional: offer to open file location
          // QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
      } else {
          QMessageBox::warning(this, tr("Export Failed"),
              tr("Failed to export audio. Check that:\n"
                 "• A SoundFont is loaded\n"
                 "• The output path is writable\n"
                 "• There is enough disk space"));
      }

      // Disconnect signals
      disconnect(FluidSynthEngine::instance(), &FluidSynthEngine::exportProgress, nullptr, nullptr);
      disconnect(FluidSynthEngine::instance(), &FluidSynthEngine::exportFinished, nullptr, nullptr);
      disconnect(FluidSynthEngine::instance(), &FluidSynthEngine::exportCancelled, nullptr, nullptr);
  }

CANCEL HANDLER:
  void MainWindow::onExportCancelled() {
      _exportProgressDialog->close();
      _exportProgressDialog->deleteLater();
      _exportProgressDialog = nullptr;

      // Cleanup temp files
      QFile::remove(QDir::tempPath() + "/MidiEditor_export_" + ...);
      // Cleanup partial output file
      // (FluidSynthEngine should delete partial output on cancel)
  }

TIME FORMATTING HELPERS:
  QString formatTime(qint64 ms) {
      int secs = ms / 1000;
      return QString("%1:%2").arg(secs / 60).arg(secs % 60, 2, 10, QChar('0'));
  }
  QString formatFileSize(qint64 bytes) {
      if (bytes < 1024) return QString("%1 B").arg(bytes);
      if (bytes < 1048576) return QString("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
      return QString("%1 MB").arg(bytes / 1048576.0, 0, 'f', 1);
  }

GUARD STATES:
  - Export in progress: disable File→Export, toolbar export, context menu export
  - No file loaded: disable all export triggers
  - No SoundFont: show warning before starting

DEPENDS ON: 20.1, 20.3
RISK: Low — QProgressDialog is battle-tested Qt component
```

---

### Phase 20 — Summary & Dependencies

```
20.1  Export Core          ─────────────────────────────────┐
                                                            ├──→ 20.3 Full-File Export
20.2  Export Dialog        ─────────────────────────────────┤
                                                            ├──→ 20.4 Selection Export
                                                            │
20.6  Progress UI          ─────────────────────────────────┤
                                                            │
20.5  MP3 (LAME)           ─── built-in static encoder ────┤
                                                            │
20.7  SoundFont Management ─── enable/disable + FFXIV auto ┤
                                                            │
20.8  FluidSynth Hardening ─── driver fallback + error UX ─┘
```

**Implementation order:** 20.1 → 20.2 → 20.6 → 20.3 → 20.4 → 20.5 → 20.7 → 20.8

**Total new files:** 4 (ExportDialog.h/.cpp, LameEncoder.h/.cpp)
**Total modified files:** 8 (FluidSynthEngine.h/.cpp, MainWindow.h/.cpp, MatrixWidget.cpp, MidiSettingsWidget.h/.cpp, MidiOutput.cpp, CMakeLists.txt)
**Estimated total:** ~1800 lines new code, ~450 lines modified

---

### 20.7 — SoundFont Management (Enable/Disable + FFXIV Auto-Toggle) ✅

> **Goal:** Let users temporarily disable individual SoundFonts in the stack without
> removing them. Automatically detect FFXIV SoundFonts by filename and toggle
> the FFXIV SoundFont Mode accordingly.

**Features implemented:**

- **Per-SoundFont checkboxes:** Each SoundFont in the list widget now has a checkbox.
  Unchecking a font removes it from the active FluidSynth stack but keeps it in the
  UI list. Re-checking reloads it. Disabled state persists across sessions via QSettings.
- **Dual-state system:** Runtime state (`_soundFontStack` + `_disabledSoundFontPaths`)
  and pending state (`_pendingSoundFontPaths` + `_pendingDisabledPaths`) for
  before/after FluidSynth initialization. `shutdown()` preserves both states.
  `allSoundFontPaths()` falls back to pending paths when stack is empty.
- **FFXIV SoundFont Mode auto-toggle:** `updateFfxivModeFromSoundFonts()` scans the
  UI list widget for checked SoundFonts with "ff14" or "ffxiv" in the filename
  (case-insensitive). If any match, FFXIV SoundFont Mode is auto-enabled; if none
  match, it's auto-disabled. Reads from UI state directly (not engine state) to
  ensure correctness before engine commits.
- **Mode updated before stack rebuild:** When a SoundFont checkbox changes, FFXIV mode
  is updated FIRST, then `setSoundFontEnabled()` rebuilds the FluidSynth stack.
  This ensures `applyChannelMode()` inside the stack rebuild uses the correct flag.
- **Proper GM drum restore:** When switching from FFXIV mode back to GM mode,
  `applyChannelMode()` now sends `bank_select(channel 9, bank 128)` +
  `program_change(channel 9, 0)` to properly restore the drum kit. Previously,
  channel 9 was left in a melodic state (drums playing as piano).

**Files modified:**
- `FluidSynthEngine.h` — added `_disabledSoundFontPaths`, `_pendingDisabledPaths`, `addPendingSoundFontPaths()`, `setSoundFontEnabled()`, `isSoundFontEnabled()`, `allSoundFontPaths()`
- `FluidSynthEngine.cpp` — updated `shutdown()`, `setSoundFontStack()`, `saveSettings()`, `removeSoundFontByPath()`, `applyChannelMode()`
- `MidiSettingsWidget.h` — added `updateFfxivModeFromSoundFonts()`, `onSoundFontItemChanged()` slots
- `MidiSettingsWidget.cpp` — checkbox rendering, state change handling, FFXIV auto-toggle

---

### 20.8 — FluidSynth Hardening (Driver Fallback + Error UX) ✅

> **Goal:** Make FluidSynth initialization more resilient and give users better
> feedback when things go wrong. FluidSynth settings should be accessible even
> when a different output device is selected.

**Features implemented:**

- **Audio driver fallback chain:** `FluidSynthEngine::initialize()` now tries multiple
  audio drivers in order: preferred driver (from settings) → wasapi → dsound → waveout →
  sdl3 → sdl2. If the preferred driver fails (common with SDL3 after a restart cycle),
  the next driver is tried automatically. The driver combo in settings reflects the
  actual driver used after initialization.
- **FluidSynth settings always accessible:** Removed the `setEnabled(false)` call on the
  FluidSynth settings group when a non-FluidSynth output is selected. Users can now
  browse, add, remove, and configure SoundFonts at any time, even while using Microsoft
  GS Wavetable Synth. Settings are applied when FluidSynth is next activated.
- **Pre-init SoundFont management:** `addPendingSoundFontPaths()` allows adding SoundFonts
  to the pending list when FluidSynth is not initialized. `setSoundFontStack()` and
  `removeSoundFontByPath()` also work before init by updating pending paths.
- **FluidSynth error dialog:** When switching to FluidSynth output fails (e.g., no audio
  driver available), a `QMessageBox::warning` is shown with the error details. The output
  reverts to the previous working port automatically.
- **Output port fallback:** `MidiOutput::setOutputPort()` saves the previous port before
  attempting to switch. If the new port's FluidSynth init fails, the previous port is
  restored automatically.

**Files modified:**
- `FluidSynthEngine.cpp` — `initialize()` driver fallback loop
- `MidiSettingsWidget.cpp` — settings always enabled, error dialog, driver combo update
- `MidiOutput.cpp` — fallback port restoration on failed FluidSynth init

**Export completion dialog:** After any audio export finishes, a dialog with three
buttons is shown: **Open File** (opens in default audio player), **Open Folder**
(opens containing directory in Explorer), **Close** (dismiss). Previously there was
no feedback after export completion.

**Guitar Pro export fix:** When exporting audio from a Guitar Pro file (.gp3–.gp8),
`file->path()` returned the GP file path which FluidSynth cannot parse. Now saves
the in-memory MidiFile to a temporary `.mid` file for the export process, then
cleans up via `_exportTempMidiPath`. This fixes the "silent/empty audio export from
Guitar Pro files" bug.

---

## Phase 21: Lyric Editor ✅

> **Goal:** A full-featured lyric editor that enables creating, importing, synchronizing,
> and exporting song lyrics directly in MidiEditor AI. Lyrics are stored as MIDI meta
> events (0x05 Lyric) embedded in the MIDI file or exported as SRT subtitle files.
> A new dedicated Lyric Timeline below the piano roll displays text blocks cleanly
> along the time axis — no more overlapping with other events.
>
> **Motivation:** Currently, lyrics are only shown as tiny, truncated marker labels in the
> MatrixWidget timeline — overlapping, unreadable, and not editable. Musicians and
> arrangers need a clean, synchronized text display that scrolls with the song, and a
> simple way to enter, synchronize, and export lyrics. SRT is the universal standard
> for subtitles and is understood by video editors and karaoke software alike.
>
> **Status:** In progress. Phase 21.1 complete, Phase 21.8 partial.

### Analysis: What Exists Already

**TextEvent Infrastructure (existing):**
- `TextEvent` class in `src/MidiEvent/TextEvent.{h,cpp}` — stores `QString text()`,
  `int type()` (0x01–0x07), inherits `midiTime()` from MidiEvent
- MIDI Meta-Event Types: 0x01=Text, 0x02=Copyright, 0x03=Track Name,
  0x04=Instrument Name, **0x05=Lyric**, 0x06=Marker, 0x07=Cue Point
- Parsing in `MidiEvent.cpp` (L300–346): All types 0x01–0x07 create `TextEvent` objects
- Storage in `QMultiMap<int, MidiEvent*>` per channel, accessed via `MidiFile::channelEvents(ch)`
- `TextEvent::save()` produces MIDI-compliant bytes (0xFF + type + varlen + text)

**Current Display (MatrixWidget, L840–970):**
- Dashed vertical lines through the timeline at TextEvent positions
- Small label badges at the top showing ~10–12 characters + ".." (truncated)
- Toggle: `Appearance::showTextEventMarkers()`
- **Problem:** Labels overlap, text is unreadable, not editable, not movable

**MainWindow Layout (leftSplitter, vertical):**
```
leftSplitter (Qt::Vertical)
├── matrixArea          (Piano Roll + vertical scrollbar)
├── velocityArea        (MiscWidget: Velocity/CC/Tempo Editor)
└── scrollBarArea       (Horizontal scrollbar, fixed height 20px)
```
→ The new LyricTimeline will be inserted as its own splitter entry between
  `velocityArea` and `scrollBarArea` — or between `matrixArea` and `velocityArea`,
  depending on UX testing.

**Playback Sync (PlayerThread):**
- Signal `timeMsChanged(int ms)` — main signal for playhead position
- `playerStarted()` / `playerStopped()` signals
- `MidiFile::msOfTick(int tick)` and `MidiFile::tick(int ms)` for conversion
- → Sync hook: `PlayerThread::timeMsChanged()` → Lyric widget highlights current block

**SRT Infrastructure:** Not present. Must be implemented from scratch.
SRT is a trivial text format — no parser library needed.

### SRT File Format

```
1
00:00:05,000 --> 00:00:10,500
First line of the verse

2
00:00:10,800 --> 00:00:15,200
Second line continues here

3
00:00:16,000 --> 00:00:21,000
Chorus begins now
```

Each block: sequence number, time range (HH:MM:SS,mmm --> HH:MM:SS,mmm), text (1+ lines),
blank line as separator. Parsing is ~50 lines of code.

### Implementation Order

```
Phase 21.1   LyricTimelineWidget (Display + Scroll Sync)           ✅ DONE
Phase 21.2   Lyric Blocks (Data Model + TextEvent Integration)     ✅ DONE
Phase 21.3   SRT Import/Export                                      ✅ DONE
Phase 21.4   Text Import Dialog (Paste + Manual Editor)             ✅ DONE
Phase 21.5   Lyric Sync Mode (Tap-to-Sync during Playback)         ✅ DONE
Phase 21.6   Interactive Editing (Drag, Resize, Edit in Place)      ✅ DONE
Phase 21.7   MIDI Lyric Embedding (Export as Meta Events)           ✅ DONE
Phase 21.8   UI Integration (Menu, Toolbar, Toggle, Settings)       ✅ DONE (partial — View toggle, auto-show, lyric color settings)
Phase 21.9   LRC Export (FFXIV MidiBard2 Lyric Format)              ✅ DONE
```

### Estimated Complexity

| Sub-phase | New Code | Modified Code | Risk |
|-----------|----------|---------------|------|
| 21.1 LyricTimelineWidget | ~400 lines (new widget) | MainWindow.cpp (~40 lines) | Medium |
| 21.2 Lyric Blocks (data model) | ~200 lines (LyricBlock class) | TextEvent.cpp (~20 lines) | Low |
| 21.3 SRT Import/Export | ~250 lines (SrtParser class) | MidiFile.cpp (~30 lines) | Low |
| 21.4 Text Import Dialog | ~300 lines (LyricImportDialog) | MainWindow.cpp (~20 lines) | Low |
| 21.5 Tap-to-Sync | ~350 lines (SyncDialog + logic) | PlayerThread.cpp (~15 lines) | Medium |
| 21.6 Interactive Editing | ~400 lines (drag/resize handlers) | LyricTimelineWidget (~80 lines) | Medium |
| 21.7 MIDI Lyric Embedding | ~150 lines | MidiFile.cpp (~40 lines), TextEvent.cpp (~20 lines) | Low |
| 21.8 UI Integration | ~100 lines | MainWindow.cpp (~60 lines), Appearance (~20 lines) | Low |
| 21.9 LRC Export (FFXIV) | ~200 lines (LrcExporter class) | MainWindow.cpp (~15 lines) | Low |
| **Total** | **~2350 lines new** | **~340 lines modified** | **Low–Medium** |

---

### 21.1 — LyricTimelineWidget (Display + Scroll Sync) ✅

> **Goal:** A new widget that displays lyric blocks as colored rectangles along the
> time axis. Horizontal scrolling is synchronized with MatrixWidget and MiscWidget.
> Togglable show/hide.

**Implementation Notes (completed):**
- ✅ `LyricTimelineWidget` inherits `PaintWidget`, synced with MatrixWidget via `xPosOfTick()`/`tickOfXPos()`
- ✅ Label panel: `_lyricArea` QWidget with QGridLayout ("Lyrics" label 110px | timeline | dummy scrollbar), matching velocityArea pattern
- ✅ Inserted into `leftSplitter` as 3rd child (index 2) between velocityArea and scrollBarArea
- ✅ `collectLyricEvents()` scans channels 0-16 for TextEvent types LYRIK (0x05) and TEXT (0x01)
- ✅ Blocks drawn as rounded rects with dynamic font size `qBound(9, blockHeight - 10, 18)`, centered text (AlignHCenter|AlignVCenter)
- ✅ Playback pop effect: playing block expands vertically (margin-3), lighter color (150%), alpha 240, drop shadow, glow border 2px, bold text
- ✅ Grid lines from `matrixWidget->divs()` aligned with piano roll
- ✅ View → "Lyric Timeline" toggle (Ctrl+L shortcut)
- ✅ Auto-show in `MainWindow::setFile()` when file has lyric events (configurable: `Appearance::autoShowLyricTimeline()`)
- ✅ Configurable lyric color: Fixed Color (default pinkish `QColor(230,140,180)`) or Track Color, selectable in Appearance settings
- ✅ Lyric color mode QComboBox + color picker QPushButton in AppearanceSettingsWidget
- ✅ Playback signals connected in `play()`/`record()`/`stop()` for cursor position tracking
- ✅ **Bonus:** MIDI text encoding fix — UTF-8 with Latin-1 fallback in MidiEvent.cpp (fixes German umlauts)

**Architecture:**

- New class `LyricTimelineWidget` inherits from `PaintWidget` (like MiscWidget)
- Inserted into `leftSplitter` between `velocityArea` and `scrollBarArea`
- Fixed minimum height ~60px, resizable via splitter handle up to ~200px
- Collapsible: can be collapsed to 0 height via toggle

**Display:**

```
┌─────────────────────────────────────────────────────────────────────────┐
│ Lyrics │ ┌──────────┐    ┌─────────────────┐  ┌──────┐    ┌──────────┐│
│        │ │ Verse 1  │    │ And the stars   │  │ fall │    │ Chorus   ││
│        │ │ begins   │    │ are shining     │  │ down │    │ here     ││
│        │ └──────────┘    └─────────────────┘  └──────┘    └──────────┘│
└─────────────────────────────────────────────────────────────────────────┘
```

- Each lyric block = colored rectangle with text, width proportional to duration
- Currently playing block is highlighted (e.g. brighter background, border)
- Vertical playhead line synchronized with MatrixWidget
- Measure/beat grid lines taken from MatrixWidget (same x-coordinates)

**Scroll Sync:**
- `MatrixWidget::xPosOfMs()` and `msOfXPos()` for coordinate-based synchronization
- Horizontal scrolling via the same `hori` QScrollBar as MatrixWidget
- `MatrixWidget::zoomChanged()` signal → LyricTimeline adjusts zoom accordingly

**MIDI Scroll Mode:** When playback is running and "Follow Playback" is active,
the Lyric Timeline auto-scrolls — same mechanism as MatrixWidget
(via `PlayerThread::timeMsChanged()` → auto-scroll).

**Implementation:**

```cpp
// src/gui/LyricTimelineWidget.h
class LyricTimelineWidget : public PaintWidget {
    Q_OBJECT
public:
    LyricTimelineWidget(MatrixWidget *matrixWidget, QWidget *parent = nullptr);

    void setFile(MidiFile *file);
    void setVisible(bool visible);

public slots:
    void onPlaybackPositionChanged(int ms);
    void onZoomChanged();
    void onScrollChanged(int value);

signals:
    void lyricBlockSelected(int tick);
    void lyricBlockMoved(int oldTick, int newTick);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    MatrixWidget *_matrixWidget;
    MidiFile *_file;
    int _currentPlaybackMs = 0;
    int _selectedBlockIndex = -1;
};
```

**Files:**
- **New:** `src/gui/LyricTimelineWidget.h`, `src/gui/LyricTimelineWidget.cpp`
- **Modified:** `MainWindow.cpp` (leftSplitter insertion), `CMakeLists.txt` (new files)

---

### 21.2 — Lyric Blocks (Data Model + TextEvent Integration) ✅

> **Goal:** A clean data model for lyric blocks that can exist independently (before
> embedding) and also interacts seamlessly with MIDI TextEvents (type 0x05).

**LyricBlock Data Structure:**

```cpp
// src/midi/LyricBlock.h
struct LyricBlock {
    int startTick;       // Start position in MIDI ticks
    int endTick;         // End position in MIDI ticks (= start of next syllable block)
    QString text;        // The lyric text (one line/syllable/phrase)
    int trackIndex;      // Assigned track (-1 = global)

    // Computed values
    int startMs() const; // Via MidiFile::msOfTick()
    int endMs() const;
    int durationTicks() const { return endTick - startTick; }
};
```

**LyricManager Class:**

```cpp
// src/midi/LyricManager.h
class LyricManager : public QObject {
    Q_OBJECT
public:
    LyricManager(MidiFile *file);

    // Access
    QList<LyricBlock> allBlocks() const;
    LyricBlock blockAtTick(int tick) const;
    LyricBlock blockAtMs(int ms) const;

    // Editing
    void addBlock(const LyricBlock &block);
    void removeBlock(int index);
    void moveBlock(int index, int newStartTick);
    void resizeBlock(int index, int newEndTick);
    void editBlockText(int index, const QString &newText);

    // Import
    void importFromTextEvents();   // Read existing 0x05 lyrics from MidiFile
    void importFromSrt(const QString &srtPath);
    void importFromPlainText(const QString &text);

    // Export
    void exportToTextEvents();     // Write blocks as 0x05 lyrics into MidiFile
    void exportToSrt(const QString &srtPath);

    // Sync
    void clearAllBlocks();
    bool hasLyrics() const;

signals:
    void lyricsChanged();
    void blockAdded(int index);
    void blockRemoved(int index);
    void blockModified(int index);

private:
    MidiFile *_file;
    QList<LyricBlock> _blocks;     // Sorted by startTick
};
```

**TextEvent Integration:**
- `importFromTextEvents()` scans all channels for `TextEvent` with `type() == 0x05`
- Converts to sorted `LyricBlock` list by `midiTime()`
- `endTick` of a block = `startTick` of the next block (or + 480 ticks default)
- `exportToTextEvents()` creates new `TextEvent(type=0x05)` for each block

**Undo Integration:**
- Every LyricManager action is wrapped in a Protocol action
- `Protocol::startNewAction("Edit Lyrics")` → changes → `Protocol::endAction()`
- Every lyric edit is undoable with Ctrl+Z

**Files:**
- **New:** `src/midi/LyricBlock.h`, `src/midi/LyricManager.{h,cpp}`
- **Modified:** `MidiFile.h` (new member `LyricManager *_lyricManager`)

---

### 21.3 — SRT Import/Export ✅

> **Goal:** Read and write SRT files. Import creates LyricBlocks,
> export writes LyricBlocks as SRT file.

**Implementation Notes (completed):**
- ✅ `SrtParser` class in `src/converter/SrtParser.{h,cpp}` — static import/export methods
- ✅ State machine parser (ExpectSeq → ExpectTiming → ExpectText) handles BOM, multi-line entries, comma/dot timestamp separators
- ✅ Regex: `(\d{2}):(\d{2}):(\d{2})[,.](\d{3})\s*-->\s*(\d{2}):(\d{2}):(\d{2})[,.](\d{3})`
- ✅ `LyricManager::importFromSrt()` and `exportToSrt()` with Protocol undo support
- ✅ Tools → Lyrics submenu: Import Lyrics (SRT)… [Ctrl+Shift+L], Export Lyrics (SRT)…, Embed Lyrics in MIDI, Clear All Lyrics
- ✅ All 4 MainWindow slots implemented with file dialogs, validation, and user feedback
- ✅ CMakeLists.txt updated: added `src/converter/*.cpp` GLOB for SrtParser and future converters

**SRT Parser:**

```cpp
// src/converter/SrtParser.h
class SrtParser {
public:
    static QList<LyricBlock> importSrt(const QString &filePath, MidiFile *file);
    static bool exportSrt(const QString &filePath, const QList<LyricBlock> &blocks, MidiFile *file);

private:
    static int timeStringToMs(const QString &timeStr);  // "00:01:23,456" → 83456
    static QString msToTimeString(int ms);               // 83456 → "00:01:23,456"
};
```

**Import Logic:**
1. Read file line by line
2. Regex pattern: `(\d{2}):(\d{2}):(\d{2}),(\d{3}) --> (\d{2}):(\d{2}):(\d{2}),(\d{3})`
3. Convert start/end times to milliseconds
4. `MidiFile::tick(ms)` → convert to ticks
5. Create `LyricBlock` and insert into `LyricManager`

**Export Logic:**
1. Iterate all blocks sorted by `startTick`
2. `MidiFile::msOfTick()` → milliseconds
3. Write SRT format with sequence number, time range, text

**Menu Integration:**
- **File → Import Lyrics (SRT)…** — opens file dialog (*.srt filter)
- **File → Export Lyrics (SRT)…** — saves as .srt

**Edge Cases:**
- Multi-line SRT entries → merged into one LyricBlock with `\n`
- Overlapping time ranges → warning, but import anyway
- Empty entries → skip
- BOM (UTF-8 Byte Order Mark) → detect and strip

**Files:**
- **New:** `src/converter/SrtParser.{h,cpp}`
- **Modified:** `MainWindow.cpp` (menu entries), `CMakeLists.txt`

---

### 21.4 — Text Import Dialog (Paste + Manual Editor) ✅

**Implementation Notes (completed):**
- ✅ `LyricImportDialog` in `src/gui/LyricImportDialog.{h,cpp}` — QDialog with QPlainTextEdit
- ✅ Options: skip empty lines, skip section headers `[...]`, default phrase duration, start offset
- ✅ Two import modes: "Import & Sync Later" (placeholder timings) and "Import with Even Spacing"
- ✅ Live preview shows detected phrase count
- ✅ Tools → Lyrics → "Import Lyrics (Text)..." menu entry
- ✅ Uses `LyricManager::importFromPlainText()` for block creation

> **Goal:** An editor window where users can paste plain text lyrics (copy/paste)
> or type them manually. Each line becomes one LyricBlock.

**LyricImportDialog (QDialog):**

```
┌─ Import Lyrics ─────────────────────────────────────────────┐
│                                                              │
│  Paste or type your lyrics below (one line = one phrase):    │
│  ┌────────────────────────────────────────────────────────┐  │
│  │ Verse 1:                                               │  │
│  │ I walk along the empty road                            │  │
│  │ The only one that I have ever known                    │  │
│  │                                                        │  │
│  │ Chorus:                                                │  │
│  │ And the stars are shining bright                       │  │
│  │ ...                                                    │  │
│  └────────────────────────────────────────────────────────┘  │
│                                                              │
│  Options:                                                    │
│  ☑ Skip empty lines (treat as phrase separators)            │
│  ☐ Skip lines starting with [ ] (section headers)           │
│  Default phrase duration: [2.0] seconds                      │
│  Start offset: [0.0] seconds                                 │
│                                                              │
│  Preview: 14 phrases detected                                │
│                                                              │
│  [Import & Sync Later]  [Import with Even Spacing]  [Cancel] │
└──────────────────────────────────────────────────────────────┘
```

**Two Import Modes:**
1. **Import & Sync Later** — Creates blocks with placeholder timings (evenly distributed
   across the song duration). User synchronizes later via Tap-to-Sync (Phase 21.5).
2. **Import with Even Spacing** — Distributes phrases evenly across the entire file duration
   with configurable default phrase duration.

**Features:**
- `QPlainTextEdit` as input field with line numbers
- Live preview: shows number of detected phrases
- Option: treat empty lines as separators (verse/chorus gaps)
- Option: skip lines starting with `[…]` as section headers
- Start offset: where in the song lyrics begin

**Files:**
- **New:** `src/gui/LyricImportDialog.{h,cpp}`
- **Modified:** `MainWindow.cpp` (menu/toolbar), `CMakeLists.txt`

---

### 21.5 — Lyric Sync Mode (Tap-to-Sync during Playback) ✅

**Implementation Notes (completed):**
- ✅ `LyricSyncDialog` in `src/gui/LyricSyncDialog.{h,cpp}` — modal dialog with playback integration
- ✅ Hold Space key: KeyPress = phrase starts, KeyRelease = phrase ends, advances automatically
- ✅ Real-time display: current phrase (large), next phrase (small), progress bar, time display
- ✅ Controls: Play/Pause, Rewind 5s, Undo Last, Done, Cancel
- ✅ Minimum phrase duration enforced (100ms)
- ✅ Entire sync operation wrapped in single Protocol action for undo
- ✅ Handles playback stop gracefully (auto-ends held phrase)
- ✅ Tools → Lyrics → "Sync Lyrics (Tap-to-Sync)..." menu entry

> **Goal:** The core of the lyric editor: the song plays back, and the user holds
> a key (e.g. Space) while a phrase is being sung — press = phrase starts,
> release = phrase ends. Timestamps are captured in real time and assigned to
> LyricBlocks.

**Sync Dialog (LyricSyncDialog):**

```
┌─ Sync Lyrics ────────────────────────────────────────────────┐
│                                                               │
│  ▶ Playing: 01:23 / 04:15                ♪ ████████░░░░ 33%  │
│                                                               │
│  Current phrase (3 / 14):                                     │
│  ┌──────────────────────────────────────────────────────────┐ │
│  │        "The only one that I have ever known"             │ │
│  └──────────────────────────────────────────────────────────┘ │
│                                                               │
│  Next: "Don't know where it goes"                             │
│                                                               │
│  ┌──────────────────────────────────────────────────────────┐ │
│  │   HOLD [Space] while the phrase is being sung            │ │
│  │   Press = phrase starts    Release = phrase ends          │ │
│  └──────────────────────────────────────────────────────────┘ │
│                                                               │
│  Synced: 2 / 14 phrases                                      │
│                                                               │
│  [◀ Rewind 5s]  [⏸ Pause]  [Undo Last]  [Done]  [Cancel]   │
└───────────────────────────────────────────────────────────────┘
```

**How It Works:**
1. Dialog starts playback from the beginning (or configurable offset)
2. Displays the current phrase prominently, the next phrase smaller below
3. User holds **Space** (or configurable key):
   - **KeyPress** → `startTick = currentPlaybackTick()`
   - **KeyRelease** → `endTick = currentPlaybackTick()`
4. Automatically advances to the next phrase
5. **Undo Last** — reset last phrase and replay from that point
6. **Rewind 5s** — rewind 5 seconds
7. Progress display: "Synced: X / Y phrases"

**Technical Implementation:**
- `QShortcut` or `keyPressEvent()`/`keyReleaseEvent()` override in dialog
- Playback via existing `PlayerThread` start/stop
- `PlayerThread::timeMsChanged(int ms)` → `MidiFile::tick(ms)` → determine tick
- Result written directly to `LyricManager::moveBlock()` / `resizeBlock()`
- Entire sync operation as one Protocol action (one Ctrl+Z undoes everything)

**Edge Cases:**
- User presses Space too late → Rewind + Undo Last
- User skips a phrase → Skip button or automatic counter advancement
- Playback ends before all phrases synced → warning, rest stays unsynced
- Very fast phrases → enforce minimum duration of 100ms

**Files:**
- **New:** `src/gui/LyricSyncDialog.{h,cpp}`
- **Modified:** `MainWindow.cpp`, `CMakeLists.txt`

---

### 21.6 — Interactive Editing (Drag, Resize, Edit in Place) ✅

**Implementation Notes (completed):**
- ✅ Mouse tracking with cursor changes: arrow (empty), open hand (over block), size-hor (at edges), closed hand (dragging)
- ✅ Click to select block (blue highlight border)
- ✅ Drag to move block horizontally (changes startTick, preserves duration)
- ✅ Drag left/right edges to resize (6px edge detection zone)
- ✅ Double-click on block: inline QLineEdit editor for text
- ✅ Double-click on empty area: insert new block (480 ticks default duration)
- ✅ Right-click context menu: Edit Text, Delete, Split at Cursor, Merge with Next, Insert Before/After
- ✅ All edit operations go through LyricManager (Protocol undo support)

> **Goal:** Move lyric blocks in the LyricTimelineWidget via mouse, resize them
> (adjust start/end), and double-click to edit text inline.

**Mouse Interactions:**

| Action | Behavior |
|--------|----------|
| **Click on block** | Select block, highlight |
| **Double-click on block** | Open inline text editor (QLineEdit overlay) |
| **Drag block horizontally** | Move block (change start + end tick) |
| **Drag left edge** | Change start tick (shorten/lengthen left side) |
| **Drag right edge** | Change end tick (shorten/lengthen right side) |
| **Right-click** | Context menu: Edit, Delete, Split, Merge, Insert Before/After |
| **Click in empty area** | Insert new block at this position |
| **Delete key** | Delete selected block |

**Context Menu:**
- **Edit Text…** — text dialog for the block
- **Delete Block** — remove block
- **Split at Cursor** — split block at mouse position (two blocks)
- **Merge with Next** — merge current block with the next one
- **Insert Block Before** — insert empty block before current
- **Insert Block After** — insert empty block after current

**Visual Aids:**
- Cursor changes: arrow (normal), hand (over block), double-arrow (at edges)
- Snap-to-grid: blocks snap to beat/measure boundaries (configurable)
- Drag preview: transparent block shows target position during drag

**Undo:** Every drag/resize/edit action = separate Protocol action → individually undoable.

**Files:**
- **Modified:** `LyricTimelineWidget.cpp` (~400 new lines of mouse handlers)

---

### 21.7 — MIDI Lyric Embedding (Export as Meta Events) ✅

**Implementation Notes (completed):**
- ✅ `LyricManager::exportToTextEvents()` — removes existing lyrics, creates new TextEvent(0x05) at each block's startTick (Phase 21.2)
- ✅ `LyricManager::importFromTextEvents()` — scans channels 0-16 for lyric/text events, builds sorted block list (Phase 21.2)
- ✅ Auto-import on file load in MidiFile constructor (Phase 21.2)
- ✅ "Embed Lyrics in MIDI" menu entry in Tools → Lyrics (Phase 21.3)
- ✅ `embedLyricsInMidi()` slot with user feedback (Phase 21.3)
- ✅ Standard MIDI Lyric Events (type 0x05) for universal compatibility

> **Goal:** Embed LyricBlocks as MIDI meta events (0x05 Lyric) into the MIDI file
> so they are displayed in any MIDI player with lyric support. And vice versa:
> import existing 0x05 events from a MIDI file into LyricBlocks.

**Export (LyricBlocks → MIDI):**
1. Remove existing 0x05 lyrics from the file (optional, with confirmation)
2. Create a `TextEvent(type=0x05)` at `startTick` for each `LyricBlock`
3. Insert into track 0 (or user-configurable track)
4. `MidiFile::save()` writes the events as standard MIDI meta events

**Import (MIDI → LyricBlocks):**
1. Collect all `TextEvent` with `type() == 0x05` from all tracks
2. Sort by `midiTime()`
3. Convert to `LyricBlock` list
4. `endTick` = `startTick` of next block (last block: + 960 ticks default)

**Automatic Import:** When loading a MIDI file, check if 0x05 events exist.
If so, they are automatically imported into the LyricManager and the
LyricTimeline displays them immediately.

**Compatibility:**
- Standard MIDI Lyric Events (type 0x05) — universally compatible
- Karaoke format: some MIDI karaoke files (.kar) use type 0x01 with
  slash/backslash prefixes for line breaks → optional parsing
- Guitar Pro: GP files have a dedicated lyrics field → could be converted
  to TextEvents during GpImporter import (bonus, not in v1)

**Files:**
- **Modified:** `LyricManager.cpp` (import/export logic), `MidiFile.cpp` (auto-import on load)

---

### 21.8 — UI Integration (Menu, Toolbar, Toggle, Settings) ✅ (partial)

> **Goal:** Clean integration of all lyric features into the MidiEditor AI
> user interface — menus, toolbar button, toggle, keyboard shortcuts, settings.

**Menu Entries (under "Tools" or new "Lyrics" menu):**

| Menu | Entry | Shortcut |
|------|-------|----------|
| Tools → Lyrics | Import Lyrics (SRT)… | Ctrl+Shift+L |
| Tools → Lyrics | Import Lyrics (Text)… | — |
| Tools → Lyrics | Export Lyrics (SRT)… | — |
| Tools → Lyrics | Embed Lyrics in MIDI | — |
| Tools → Lyrics | Sync Lyrics… | — |
| Tools → Lyrics | Clear All Lyrics | — |
| View | Show Lyric Timeline | Ctrl+L |

**Toolbar:**
- New toggle button with lyric icon (text/notes symbol) in the toolbar
- Click → show/hide LyricTimeline
- Visual state: active = highlighted, inactive = grayed out

**Settings (Appearance Tab):**
- Lyric block color (default: theme-dependent, e.g. accent color with transparency)
- Lyric text font size (default: 11pt)
- Active block highlight color
- Snap-to-grid for drag operations (on/off)

**Keyboard Shortcuts:**
- `Ctrl+L` — toggle Lyric Timeline visibility
- `Ctrl+Shift+L` — SRT Import Dialog
- `Space` (in Sync Dialog) — mark phrase (Press=Start, Release=End)

**Theme Support:**
- LyricTimelineWidget reads colors from `Appearance` class
- Block colors defined for all 7 themes
- Dark themes: light text on dark block background
- Light themes: dark text on light block background

**Files:**
- **Modified:** `MainWindow.cpp` (menus, toolbar, toggle), `Appearance.{h,cpp}` (colors/settings),
  `MidiSettingsWidget.cpp` (settings section for lyrics)

---

### 21.9 — LRC Export (FFXIV MidiBard2 Lyric Format) ✅ DONE

**Implemented:** LrcExporter.h/.cpp with exportLrc()/importLrc() supporting [MM:SS.cc] timestamp format,
LrcMetadata struct for header tags ([ar:], [ti:], [al:], [by:], [offset:]). Menu entries for
"Import Lyrics (LRC)..." and "Export Lyrics (LRC)..." added to Tools → Lyrics submenu.
Import creates LyricBlocks from LRC timestamps, export writes standard LRC format.

> **Goal:** Export lyrics in LRC format for use with MidiBard2's lyric display.
> LRC is a simple timestamp+text format used by karaoke software and FFXIV
> bard performance plugins. MidiBard2 reads `.lrc` files placed next to the
> MIDI file and displays the lyrics in-game during performance.

**LRC Format (from MidiBard2 template):**

```
[ar:Artist Name]
[ti:Song Title]
[al:Album]
[by:Lyrics by]
[offset:0]
[00:07.40]Bard Name:Lyric Line 1
[00:08.40]Another Bard Name:Lyric Line 2
[00:10.40]Bard Name:Lyric Line 3
[00:15.40]Lyric Line 4
```

**Format Details:**
- **Header tags:** `[ar:...]` (artist), `[ti:...]` (title), `[al:...]` (album),
  `[by:...]` (lyrics by), `[offset:N]` (global offset in ms)
- **Timestamp:** `[MM:SS.cc]` where cc = centiseconds (hundredths of a second)
- **Bard assignment (optional):** `BardName:Text` — assigns a lyric line to a
  specific bard in a MidiBard2 ensemble. If omitted, the line is global.
- **Multiple lines at same timestamp:** Allowed — MidiBard2 displays them simultaneously
- **Encoding:** UTF-8 (supports Umlauts, special characters)

**LrcExporter Class:**

```cpp
// src/converter/LrcExporter.h
class LrcExporter {
public:
    struct LrcMetadata {
        QString artist;
        QString title;
        QString album;
        QString lyricsBy;
        int offsetMs = 0;
    };

    static bool exportLrc(const QString &filePath,
                          const QList<LyricBlock> &blocks,
                          MidiFile *file,
                          const LrcMetadata &metadata = {});

    static QList<LyricBlock> importLrc(const QString &filePath, MidiFile *file);

private:
    static QString tickToLrcTimestamp(int tick, MidiFile *file);  // → "01:23.45"
    static int lrcTimestampToMs(const QString &ts);               // "01:23.45" → 83450
};
```

**Export Logic:**
1. Write header tags from metadata (or auto-fill from MIDI file: track name → title)
2. For each `LyricBlock` sorted by `startTick`:
   - Convert `startTick` → milliseconds via `MidiFile::msOfTick()`
   - Format as `[MM:SS.cc]` (centisecond precision)
   - If block has a bard/track assignment: `[MM:SS.cc]BardName:Text`
   - Otherwise: `[MM:SS.cc]Text`
3. Write to `.lrc` file (UTF-8, no BOM)

**Import Logic (bonus):**
1. Parse header tags → metadata
2. Regex: `\[(\d{2}):(\d{2})\.(\d{2})\](.+)`
3. Convert MM:SS.cc → milliseconds → `MidiFile::tick(ms)` → ticks
4. Optional bard prefix: split on first `:` if pattern matches `Name:Text`
5. Create `LyricBlock` for each entry

**FFXIV Integration:**
- When FFXIV mode is enabled and lyrics exist, offer "Export LRC for MidiBard2"
  in the export dialog
- Auto-suggest filename: `{midi_filename}.lrc` (same base name as MIDI file)
- If tracks have FFXIV instrument names, use them as bard names in the LRC
- MidiBard2 expects the `.lrc` file next to the `.mid` file with the same base name

**Menu Integration:**
- **Tools → Lyrics → Export Lyrics (LRC)…** — MidiBard2 format
- **Tools → Lyrics → Import Lyrics (LRC)…** — read existing LRC files

**Files:**
- **New:** `src/converter/LrcExporter.{h,cpp}`
- **Modified:** `MainWindow.cpp` (menu entries), `CMakeLists.txt`

---

### Feasibility Analysis

**Risk Assessment:**

| Aspect | Rating | Comment |
|--------|--------|---------|
| **Widget Integration** | ✅ Feasible | leftSplitter allows arbitrary new widgets, MiscWidget proves the pattern |
| **Scroll Sync** | ✅ Feasible | MiscWidget already synchronizes horizontally — same mechanism |
| **SRT Parsing** | ✅ Trivial | ~50 lines regex-based parser, no external library needed |
| **TextEvent MIDI I/O** | ✅ Existing | TextEvent class fully implemented, save/load works |
| **Playback Sync** | ✅ Feasible | `PlayerThread::timeMsChanged()` provides real-time position |
| **Tap-to-Sync** | ⚠️ Medium | keyPress/keyRelease timing must be robust enough (~50ms tolerance), Qt events may be delayed |
| **Inline Editing** | ⚠️ Medium | QLineEdit overlay on PaintWidget requires precise positioning |
| **Undo Integration** | ✅ Feasible | Protocol system supports arbitrary actions, proven pattern |
| **Performance** | ✅ No issue | Typical song has 30–100 lyric blocks — no performance risk |

**Dependencies:**
- No external libraries needed
- No new build system setup
- No API dependencies
- Purely Qt-based (QWidget, QPainter, QDialog)

**Main Risk:** The Tap-to-Sync feature requires precise timing with low latency
between the key event and the current playback position. Qt's event loop may
introduce a few milliseconds of delay. Solution: cache `PlayerThread::timeMsChanged()`
and use the last known value plus a small delta instead of querying the tick
synchronously in the event handler. In the music domain, ±50ms tolerance is
perfectly acceptable for lyric sync.

---

## Phase 22: Lyric Visualizer (Karaoke Display) ✅

> **Goal:** A live lyric visualizer ("Karaoke Mode") that can be docked in the toolbar
> area — similar to the existing MIDI Visualizer. During playback, it shows the current
> lyric phrase with smooth karaoke-style animations. When no lyrics are present in the
> file, the widget auto-hides; once lyrics are imported or available, it becomes visible.
>
> **Motivation:** Purely a "cool factor" feature. Musicians performing or previewing songs
> want to see lyrics scrolling in real-time. Combined with the Lyric Editor (Phase 21),
> this makes MidiEditor AI a complete karaoke authoring + preview tool.
>
> **Pattern:** Follows the same integration pattern as `MidiVisualizerWidget` — a compact
> `QWidget` that lives in the customizable toolbar, polls playback state via a timer,
> and can be toggled on/off. But unlike the 84×24px MIDI visualizer, this needs more
> horizontal space (~300-400px) to display text comfortably.

### Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│  Toolbar: [File] [Edit] ║ [🎵] [▶ ⏸ ⏹] ... [🎤 Lyric Display] │
│                          ║                                       │
│                          ║  ┌──────────────────────────────────┐ │
│                          ║  │   ♪ And the stars are shining ♪  │ │
│                          ║  │      ── falling down ──          │ │
│                          ║  └──────────────────────────────────┘ │
├──────────────────────────╨───────────────────────────────────────┤
│  Piano Roll ...                                                  │
```

**Two display lines:**
- **Line 1 (main):** Current phrase — large, prominent, animated highlight
- **Line 2 (preview):** Next phrase — smaller, dimmed, gives the reader a look-ahead

**When idle (no playback / between phrases):**
- Shows faded "♪ ♪ ♪" or the song title if metadata is set

### Widget: `LyricVisualizerWidget`

```cpp
// src/gui/LyricVisualizerWidget.h
class LyricVisualizerWidget : public QWidget {
    Q_OBJECT
public:
    explicit LyricVisualizerWidget(QWidget *parent = nullptr);

    void setFile(MidiFile *file);

public slots:
    void playbackStarted();
    void playbackStopped();
    void onPlaybackPositionChanged(int ms);

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

private slots:
    void refresh();

private:
    void updateCurrentLyric(int ms);

    MidiFile *_file = nullptr;
    QTimer _timer;                 // ~30fps animation timer
    bool _playing = false;

    // Current lyric state
    int _currentBlockIndex = -1;   // Index in LyricManager::allBlocks()
    QString _currentText;          // Current phrase text
    QString _nextText;             // Next phrase text (look-ahead)
    float _phraseProgress = 0.0f;  // 0.0 (start) → 1.0 (end) within current block
    int _lastMs = 0;               // Last known playback position

    // Animation
    float _fadeIn = 0.0f;          // 0→1 fade for new phrase
    float _glowPulse = 0.0f;      // Subtle glow oscillation
};
```

### Rendering (paintEvent)

**Visual Design (Dark Mode):**
```
┌─────────────────────────────────────────────────┐
│  ♪  And the stars are shi[ning]  ♪              │  ← Current: large, white
│         ── falling down ──                      │  ← Next: small, dim gray
└─────────────────────────────────────────────────┘
   ▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔
   ← progress bar (accent color, fills left→right)
```

**Karaoke highlight effect:**
- The `_phraseProgress` (0→1) determines how much of the text is "sung"
- Characters before the progress point: **bright accent color** (e.g. #58a6ff blue or lyric color)
- Characters after progress point: **white/light gray**
- Smooth transition — the highlight slides through the text like karaoke

**Animations:**
- **Fade-in:** When a new phrase starts, `_fadeIn` animates 0→1 over ~200ms (opacity transition)
- **Glow pulse:** Subtle sine-wave glow behind the current text (period ~2s, barely noticeable)
- **Progress bar:** Thin 2px accent-colored bar at the bottom, filling left→right with phrase progress

**Typography:**
- Current phrase: Bold, 14-16px, centered horizontally
- Next phrase: Regular, 10-12px, centered, 60% opacity
- Musical note decorations: "♪" at start/end of current phrase during playback

**Colors (themed):**
- Dark mode: Background `#0d1117`, current text white, highlight `#58a6ff`, next text `#8b949e`
- Light mode: Background `#f6f8fa`, current text `#24292f`, highlight `#0969da`, next text `#656d76`
- Or use `Appearance::lyricColor()` for the highlight, matching the Lyric Timeline setting

### Playback Integration

**Signal connections (same pattern as MIDI Visualizer):**
```cpp
// In MainWindow::createCustomToolbar() for "lyric_visualizer" action:
connect(MidiPlayer::playerThread(), SIGNAL(playerStarted()),
        _lyricVisualizer, SLOT(playbackStarted()));
connect(MidiPlayer::playerThread(), SIGNAL(playerStopped()),
        _lyricVisualizer, SLOT(playbackStopped()));
connect(MidiPlayer::playerThread(), SIGNAL(timeMsChanged(int)),
        _lyricVisualizer, SLOT(onPlaybackPositionChanged(int)));
```

**`onPlaybackPositionChanged(int ms)`:**
1. Store `_lastMs = ms`
2. Convert ms to tick via `_file->tick(ms)`
3. Find current block via `LyricManager::blockAtTick(tick)`
4. If block changed → update `_currentText`, `_nextText`, reset `_fadeIn = 0`
5. Calculate `_phraseProgress` = `(ms - blockStartMs) / (blockEndMs - blockStartMs)`

**Timer-based refresh (~30fps):**
- Advances `_fadeIn` and `_glowPulse` animation values
- Calls `update()` if playing or animating
- Same show/hide timer management as MidiVisualizerWidget

### Auto-Hide Logic

**The visualizer auto-hides when no lyrics are available:**

```cpp
void LyricVisualizerWidget::setFile(MidiFile *file) {
    _file = file;
    bool hasLyrics = file && file->lyricManager() && file->lyricManager()->hasLyrics();
    setVisible(hasLyrics);
}
```

**Visibility updates on:**
- File load (`MainWindow::setFile()` → `setFile(newFile)`)
- Lyric import (connect `LyricManager::lyricsChanged()` → re-check visibility)
- Lyric clear (same signal → hide if empty)

**When hidden:** Takes 0 space in toolbar (standard Qt behavior for hidden widgets).

**When lyrics become available:** The widget smoothly appears (or the toolbar updates to show it).

### Toolbar Integration

**Follows the MIDI Visualizer pattern exactly:**

```cpp
// In MainWindow constructor (action registration):
QAction *lyricVisAction = new QAction("Lyric Visualizer", this);
lyricVisAction->setToolTip("Live lyric display — shows current lyrics during playback (karaoke-style)");
_actionMap["lyric_visualizer"] = lyricVisAction;

// In createCustomToolbar():
if (actionId == "lyric_visualizer") {
    _lyricVisualizer = new LyricVisualizerWidget(currentToolBar);
    _lyricVisualizer->setFile(file);
    connect(MidiPlayer::playerThread(), SIGNAL(playerStarted()),
            _lyricVisualizer, SLOT(playbackStarted()));
    connect(MidiPlayer::playerThread(), SIGNAL(playerStopped()),
            _lyricVisualizer, SLOT(playbackStopped()));
    connect(MidiPlayer::playerThread(), SIGNAL(timeMsChanged(int)),
            _lyricVisualizer, SLOT(onPlaybackPositionChanged(int)));
    currentToolBar->addWidget(_lyricVisualizer);
    continue;
}
```

**Toolbar action order:** Inserted after `midi_visualizer` in default order.

### Implementation Order

```
Phase 22.1   LyricVisualizerWidget (core widget + rendering)    ✅ DONE
Phase 22.2   Playback integration (signal hookup + text lookup)  ✅ DONE
Phase 22.3   Karaoke animation (progress highlight + fade)       ✅ DONE
Phase 22.4   Toolbar integration (action + createCustomToolbar)  ✅ DONE
Phase 22.5   Auto-hide (visibility based on lyrics presence)     ✅ DONE
Phase 22.6   Toolbar integration bugfixes                        ✅ DONE
             — Added lyric_visualizer.png icon to resources.qrc
             — Added ToolbarActionInfo entry in getDefaultActions()
             — Added to getComprehensiveActionOrder(), getDefaultEnabledActions(),
               getDefaultRowDistribution() (was missing from all 3 "single source
               of truth" methods → invisible in Customize Toolbar dialog)
             — Removed setVisible(false) auto-hide: QToolBar doesn't reliably
               relayout hidden widgets. Now always visible (like MIDI Visualizer),
               paintEvent draws ♪♪♪ idle state when no lyrics.
             — Moved visualizer widget creation outside if(action) block in
               createCustomToolbar two-row mode (was inconsistent with other 3 sites)
Phase 22.7   Dynamic box sizing                                  ✅ DONE
             — Changed SizePolicy from Fixed to Preferred
             — sizeHint() now computes width from current phrase (200–600px)
             — updateGeometry() called on phrase change for toolbar relayout
             — Elides long text with "…" when toolbar constrains width
```

### Estimated Complexity

| Sub-phase | New Code | Modified Code | Risk |
|-----------|----------|---------------|------|
| 22.1 Core widget + rendering | ~200 lines (new class) | — | Low |
| 22.2 Playback integration | ~60 lines | — | Low |
| 22.3 Karaoke animation | ~80 lines | — | Low |
| 22.4 Toolbar integration | ~30 lines | MainWindow.cpp (~50 lines) | Low |
| 22.5 Auto-hide | ~30 lines | MainWindow.cpp (~10 lines) | Low |
| **Total** | **~400 lines new** | **~60 lines modified** | **Low** |

### Key Design Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Widget size | ~300×40px (flexible width) | Needs space for text, but shouldn't dominate the toolbar |
| Two-line display | Current + next phrase | Classic karaoke look-ahead keeps the reader prepared |
| Karaoke highlight | Left-to-right color sweep | Most recognized karaoke effect, smooth and readable |
| Timer-based vs signal-only | Hybrid (signals for data, timer for animation) | `timeMsChanged` provides position, timer smooths animation between signals |
| Auto-hide | Based on `LyricManager::hasLyrics()` | No point showing an empty box; appears when lyrics are imported |
| Toolbar placement | Custom widget in toolbar (like MIDI Visualizer) | User can position it anywhere via toolbar customization |

### Dependencies

- **Phase 21 (Lyric Editor):** ✅ Complete — provides `LyricManager`, `LyricBlock`, and all lyric data
- **MidiVisualizerWidget:** Reference implementation for toolbar widget pattern
- **No external libraries** — pure Qt painting
- **No new build system changes** — CMake GLOB picks up new files automatically

### Files

- **New:** `src/gui/LyricVisualizerWidget.h`, `src/gui/LyricVisualizerWidget.cpp`
- **Modified:** `MainWindow.h` (add `_lyricVisualizer` member), `MainWindow.cpp` (action registration + toolbar creation + file/playback connections)

---

## Phase 23: MCP Server, Documentation System & Prompt Architecture v3 ✅ DONE (v1.3.2)

> Goal: Optimize tool calls, model integration, and FFXIV prompt architecture
> for better results across all LLM providers. Leaner prompts, smarter tool
> results, retry logic, and provider robustness.

### Phase 23.1 — FFXIV Prompt Simplification

**Problem:** The FFXIV system prompt includes channel assignment mechanics, guitar
switch channel details, and program change insertion rules that the LLM doesn't
need to know — the `setup_channel_pattern` tool and the Channel Fixer handle all
of that automatically. This wastes ~600-800 tokens and confuses models.

**Keep in FFXIV prompt:**
- Max 8 tracks
- Monophonic rule (with polyphony exceptions: Lute/Harp 2-3, Piano 2)
- C3-C6 range (MIDI 48-84)
- Valid instrument names list
- Track naming = instrument selection
- No pitch_bend, no velocity editing
- Drum = separate tonal tracks (basic mapping: Bass Drum C4, Snare C5, Cymbal C5/C6)
- "Call `setup_channel_pattern` once after all tracks created/renamed" (one line)
- Guitar variants exist, switch by channel (one line, no mechanics)

**Remove from FFXIV prompt:**
- Channel assignment details (track N → channel N, percussion → CH9)
- Guitar switch channel mechanics (5 variants, per-note channel changes)
- Program change insertion rules
- Instrument native range/transpose table (player handles transposition)
- ElectricGuitarSpecial sound map (reduce to one-line: "pitch ranges = sound effects, use sparingly")

**Simplify `validate_ffxiv` tool:**
- Keep: track name check, track count, note range, polyphony
- The channel/program checks are the Channel Fixer's job

**Estimated impact:** ~150-200 lines removed from ffxivContext(), ~50 lines from ffxivContextCompact().
Saves ~800-1200 tokens per FFXIV request.

**Files:** `src/ai/EditorContext.cpp` (ffxivContext, ffxivContextCompact)

### Phase 23.2 — Tool Call Improvements

**23.2a — Enhanced tool result summaries**
After `insert_events`/`replace_events`, return richer feedback:
- Note count, pitch range (min-max as note names), tick range
- Pitch class distribution (for harmonic awareness across tracks)
- Duration stats (shortest/longest note)
- MidiEventSerializer is a pure serializer — summaries must be added in ToolDefinitions.cpp
  (in the tool execution return path after write operations)

**23.2b — Truncation auto-recovery**
When events array is empty (output truncation detected):
- Return actionable suggestion: "Split into smaller chunks (e.g., 4 measures at a time)"
- Include the tick range that needs filling so model can retry intelligently
- Consider auto-splitting large insert_events into multiple calls

**23.2c — Batch tool calls (optional)**
Allow `insert_events` to accept multiple tracks in one call:
- Reduces round-trips in Agent mode
- Lower latency for multi-track compositions
- E.g., `tracks: [{trackIndex: 0, events: [...]}, {trackIndex: 1, events: [...]}]`

**Files:** `src/ai/ToolDefinitions.cpp` (result summaries, truncation recovery, batch schema)

### Phase 23.3 — Model Integration Improvements

**23.3a — Retry logic**
Add single retry with backoff for transient failures:
- 429 (rate limit): wait 2s + retry once
- 5xx (server error): wait 1s + retry once
- Timeout (>45s no response): retry with reduced expectation (add "be concise" hint)
- No retry on 401 (auth), 400 (bad request), 403 (forbidden), or user cancel
- Show retry indicator in MidiPilot chat ("Retrying... (1/1)")

**23.3b — Dynamic model list**
Move context window map from hardcoded to configurable:
- Store in QSettings, allow user to add custom models + context sizes
- Ship sensible defaults for known models (GPT-5.x, Claude 4, Gemini 2.5)
- Update defaults on app update without losing user additions

**23.3c — Streaming for Agent mode (stretch)**
Parse `tool_calls` from streaming chunks:
- Shows progress earlier, better UX
- Most providers now support streaming + tool calls
- Falls back to non-streaming if provider doesn't support it

**23.3d — Provider abstraction (stretch)**
Create base class with per-provider subclasses:
- `AiProvider` base → `OpenAiProvider`, `AnthropicProvider`, `GeminiProvider`
- Each handles request format, response parsing, error mapping
- Cleaner code, easier to add new providers

**Files:** `src/ai/AiClient.h/.cpp`, `src/ai/AgentRunner.cpp`

### Phase 23.5 — MCP Server Support (Model Context Protocol)

**Problem:** MidiPilot currently bundles its own AI client (`AiClient.cpp`) that speaks
OpenAI-compatible HTTP to a fixed set of providers (OpenAI, Anthropic, Google, local).
Users are locked into the providers we explicitly support, and adding new ones requires
code changes (see 23.3d). Meanwhile, the AI ecosystem has converged on **MCP** (Model
Context Protocol) as the standard way for AI models to discover and use external tools.

**Solution:** Expose MidiEditor AI's existing tool set as an **MCP server** so that
any MCP-compatible client (Claude Desktop, VS Code Copilot, Cursor, Windsurf, Continue,
local LLM frontends, etc.) can connect and use MidiEditor's MIDI editing tools directly.

**MCP protocol version:** 2025-03-26 (or latest at implementation time)

**How MCP works:**
- MCP uses **JSON-RPC 2.0** over a transport (stdio or Streamable HTTP)
- The server advertises **tools** (with JSON Schema parameters) and **resources** (read-only context)
- The client (AI model/frontend) discovers tools via `tools/list`, calls them via `tools/call`
- The server executes the tool and returns results
- Server sends `notifications/tools/list_changed` when tool set changes (e.g., FFXIV mode toggle)

**Why Streamable HTTP transport (not stdio):**
- MidiEditor is a running GUI application - it can't be launched as a subprocess by a client
- HTTP allows multiple clients to connect simultaneously (multi-model workflows)
- We already link Qt6::Network (QTcpServer available)
- Configurable port (default 9420, user can change in Settings)
- The old SSE transport (separate /sse + /messages) was deprecated in MCP 2024-11-05;
  Streamable HTTP uses a **single endpoint** that accepts POST (messages) and GET (SSE stream)

**Architecture:**

```
+------------------------------------------------------------------+
|  MidiEditor AI (running)                                         |
|  +------------------------------------------------------------+  |
|  |  McpServer (QTcpServer on localhost:9420)                  |  |
|  |    POST /mcp     → JSON-RPC 2.0 messages (Streamable HTTP) |  |
|  |    GET  /mcp     → SSE stream (server-initiated messages)  |  |
|  |                                                            |  |
|  |  Advertised tools (auto-generated from ToolDefinitions):   |  |
|  |    get_editor_state, get_track_info, query_events,         |  |
|  |    create_track, rename_track, set_channel,                |  |
|  |    insert_events, replace_events, delete_events,           |  |
|  |    set_tempo, set_time_signature, move_events_to_track,    |  |
|  |    validate_ffxiv*, convert_drums_ffxiv*,                  |  |
|  |    setup_channel_pattern*     (*when FFXIV mode active)    |  |
|  |                                                            |  |
|  |  Advertised resources:                                     |  |
|  |    midi://state     → current file state (JSON)            |  |
|  |    midi://tracks    → track list with details              |  |
|  |    midi://config    → FFXIV mode, file path, tick info     |  |
|  +------------------------------------------------------------+  |
+------------------------------------------------------------------+
        ↕ JSON-RPC 2.0 / SSE              ↕ JSON-RPC 2.0 / SSE
+--------------------+            +--------------------+
| Claude Desktop     |            | VS Code Copilot    |
| (MCP client)       |            | (MCP client)       |
+--------------------+            +--------------------+
```

**Tool schema conversion:**
Our `ToolDefinitions::toolSchemas()` already generates JSON Schema for each tool.
The MCP server converts these from OpenAI format to MCP format at startup:
- OpenAI: `{"type": "function", "function": {"name": ..., "parameters": ...}}`
- MCP: `{"name": ..., "description": ..., "inputSchema": ...}`
This is a straightforward 1:1 mapping - no manual schema duplication needed.

**Tool execution:**
MCP `tools/call` requests are dispatched to the same `ToolDefinitions::executeTool()`
that the built-in AgentRunner uses. Zero code duplication for tool logic.

**Concurrency safety:**
Tool execution happens on the Qt main thread (via `QMetaObject::invokeMethod` with
`Qt::BlockingQueuedConnection` from the HTTP handler thread). This ensures all MIDI
file modifications go through the same thread as the GUI - same pattern as the existing
AgentRunner which processes events via `QCoreApplication::processEvents()`.

**Sub-items:**

- [ ] **23.5a** `McpServer` class (`src/ai/McpServer.h/.cpp`)
  - QTcpServer listening on configurable localhost port (default 9420)
  - Streamable HTTP transport: single `/mcp` endpoint (POST for messages, GET for SSE)
  - HTTP request parsing (minimal - POST /mcp and GET /mcp only)
  - SSE connection management (keep-alive, client tracking, session IDs via `Mcp-Session-Id`)
  - JSON-RPC 2.0 request/response handling
  - Methods: `initialize`, `tools/list`, `tools/call`, `resources/list`, `resources/read`
  - Send `notifications/tools/list_changed` when FFXIV mode toggled (adds/removes 3 tools)
  - Auto-convert ToolDefinitions schemas to MCP format
  - Thread-safe tool execution via Qt signal/slot
  - Backwards compat: also accept old-style GET /sse + POST /messages for older clients

- [ ] **23.5b** MCP Resources
  - `midi://state` - serializes `ToolDefinitions::execGetEditorState()` result
  - `midi://tracks` - track list with names, channels, event counts
  - `midi://config` - FFXIV mode status, file path, ticks per beat, tempo
  - Resources update when file changes (resource change notifications via SSE)

- [ ] **23.5c** Settings UI
  - New "MCP Server" section in AI Settings tab
  - Enable/disable toggle (default: disabled)
  - Port number (default: 9420)
  - Status indicator: "Running on localhost:9420" / "Stopped"
  - "Copy MCP Config" button - copies the JSON config snippet that users paste
    into their MCP client config (e.g., Claude Desktop's `claude_desktop_config.json`)
  - Connection log (shows connected clients)

- [ ] **23.5d** Security
  - Listen on localhost only (127.0.0.1) - no remote access
  - Validate `Origin` header on all requests (MCP spec requirement against DNS rebinding)
  - Optional auth token (generated on enable, shown in Settings, sent as `Authorization: Bearer` header)
  - Rate limiting: max 100 tool calls per minute per client
  - Read-only mode option (only get_editor_state, get_track_info, query_events)
  - Session management: assign `Mcp-Session-Id` on initialize, reject requests without valid session

- [ ] **23.5e** Documentation
  - Setup guide: how to connect Claude Desktop, VS Code Copilot, Cursor
  - Example MCP client config JSON for each popular client
  - Tool reference (auto-generated from schemas)
  - Website feature card

**Impact on Phase 23.3d (Provider abstraction):**
With MCP Server support, 23.3d becomes much less important. Instead of us abstracting
every provider, users connect their preferred MCP client which already speaks to any
model. The built-in MidiPilot (with AiClient) remains for users who want a simple
out-of-the-box experience. MCP is the power-user path.

**MCP client config example (Claude Desktop):**
```json
{
  "mcpServers": {
    "midieditor": {
      "url": "http://localhost:9420/mcp",
      "headers": {
        "Authorization": "Bearer <token_from_settings>"
      }
    }
  }
}
```

**Files:** `src/ai/McpServer.h/.cpp` (new), `src/gui/AiSettingsWidget.cpp` (settings UI),
`MainWindow.cpp` (server lifecycle), `CMakeLists.txt` (new source files)

### Phase 23.4 — Prompt Architecture v3

**23.4a — Token budgeting**
Before sending each request:
- Calculate: system prompt + FFXIV context + history + state = X tokens
- If X > 60% of context window, compress history (smarter truncation)
- Show warning in token label at 80% usage (already partially done)
- Smart truncation: keep first 2 messages + most recent, insert summary marker

**23.4b — Conditional prompt sections**
Only include what the current file needs:
- Drum mapping section: only if file has drum tracks or user mentions drums
- Guitar switch section: only if file has guitar tracks
- Surrounding events: configurable depth (±2 vs ±4 measures based on effort)

**23.4c — GM instrument program_change reminder**
Strengthen the "always insert program_change at tick 0" rule:
- This is the #1 user issue: tracks default to Piano sound when no program_change is set
- Add explicit reminder to tool description of `create_track` and `insert_events`
- Consider auto-inserting a program_change event when `create_track` is called with
  an instrument context (e.g., track named "Strings" → auto-insert program 48 at tick 0)
- Channel 9 (drums) excluded from auto-insert

**23.4d — Effort-based prompt selection**
- Low effort → compact prompt (ffxivContextCompact pattern)
- Medium effort → standard prompt
- High effort → detailed prompt with examples
- Extend compact variant to general (non-FFXIV) prompts too

**Files:** `src/ai/EditorContext.cpp`, `src/gui/MidiPilotWidget.cpp`

### Implementation Order

```
Phase 23.1   FFXIV prompt simplification                         ✅ DONE
Phase 23.2a  Enhanced tool result summaries                      ✅ DONE
Phase 23.4a  Token budgeting & smart truncation                  ✅ DONE
Phase 23.4b  Conditional prompt sections                         ✅ DONE
Phase 23.4c  GM program_change reminder                          ✅ DONE
Phase 23.3a  Retry logic (429/5xx)                               ✅ DONE
Phase 23.2b  Truncation auto-recovery                            ✅ DONE
Phase 23.4d  Effort-based prompt selection                       ✅ DONE
Phase 23.5a  MCP Server core (Streamable HTTP + JSON-RPC)         ✅ DONE
Phase 23.5b  MCP Resources (state/tracks/config)                 ✅ DONE
Phase 23.5c  MCP Settings UI                                     ✅ DONE
Phase 23.5d  MCP Security (localhost, Origin, auth token)        ✅ DONE
Phase 23.5e  MCP Documentation                                  ✅ DONE
Phase 23.5f  MCP Protocol prefix (client identification)         ✅ DONE
Phase 23.2c  Batch insert_events (optional)                      ⏭️ SKIPPED (low value, partial-failure risk)
Phase 23.3b  Dynamic model list                                  ⏭️ SKIPPED (low priority, hardcoded defaults sufficient)
Phase 23.3c  Streaming for Agent mode                            ⏭️ SKIPPED (high risk, provider-specific fragmentation)
Phase 23.3d  Provider abstraction                                ⏭️ SKIPPED (superseded by MCP Server)
```

### Estimated Complexity

| Sub-phase | New Code | Modified Code | Risk |
|-----------|----------|---------------|------|
| 23.1 FFXIV prompt simplification | — | EditorContext.cpp (~150-200 lines removed) | Low |
| 23.2a Tool result summaries | ~50 lines | ToolDefinitions.cpp (write-op return paths) | Low |
| 23.2b Truncation recovery | ~30 lines | ToolDefinitions.cpp | Low |
| 23.2c Batch insert (stretch) | ~80 lines | ToolDefinitions.cpp | Medium |
| 23.3a Retry logic | ~60 lines | AiClient.cpp | Medium |
| 23.3b Dynamic model list | ~80 lines | AiClient.cpp, AiSettingsWidget.cpp | Low |
| 23.3c Streaming Agent (stretch) | ~150 lines | AiClient.cpp, AgentRunner.cpp | High |
| 23.3d Provider abstraction (stretch) | ~400 lines (new classes) | AiClient refactor | High |
| 23.5a MCP Server core | ~500 lines (new class) | — | Medium |
| 23.5b MCP Resources | ~100 lines | McpServer.cpp | Low |
| 23.5c MCP Settings UI | ~120 lines | AiSettingsWidget.cpp | Low |
| 23.5d MCP Security | ~80 lines | McpServer.cpp | Low |
| 23.5e MCP Documentation | ~200 lines HTML | Website, manual | Low |
| 23.4a Token budgeting | ~40 lines | MidiPilotWidget.cpp | Low |
| 23.4b Conditional sections | ~30 lines | EditorContext.cpp | Low |
| 23.4c GM program_change | ~20 lines | ToolDefinitions.cpp, EditorContext.cpp | Low |
| 23.4d Effort prompts | ~60 lines | EditorContext.cpp | Low |
| **Total (core, no stretch)** | **~1230 lines** | **~420 lines modified** | **Low-Medium** |

### Key Design Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| FFXIV channel setup | Delegate entirely to `setup_channel_pattern` tool | Channel Fixer is deterministic and bug-tested; LLMs get confused by channel mechanics |
| Retry policy | Single retry only | Avoids infinite loops; user can always retry manually |
| Provider abstraction | Deprioritized by MCP | MCP Server lets external clients use any model; built-in MidiPilot covers basic needs |
| MCP transport | Streamable HTTP (MCP 2025-03-26) | MidiEditor is a GUI app, can't be a subprocess; single /mcp endpoint, multi-client |
| MCP port | localhost:9420 (configurable) | Localhost-only for security; port configurable in Settings |
| Batch insert | Optional stretch goal | Current per-track approach works; batch is optimization, not correctness |
| Token budgeting | Warn at 80%, compress at 60% | Matches existing yellow warning pattern in token label |

### Dependencies

- **Phase 12 (Prompt Architecture v2):** ✅ Complete — provides priority rules, validation block, timing reference
- **Phase 19 (MidiPilot AI Improvements):** ✅ Complete — provides streaming, presets, conversation history, context management
- **No external libraries** — pure Qt/C++ changes (MCP server uses Qt6::Network which is already linked)
- **No new build system changes** (McpServer.cpp added to existing CMake GLOB)

### Files

- **New:** `src/ai/McpServer.h/.cpp` (MCP server with JSON-RPC 2.0, SSE, tool/resource dispatching)
- **Modified:** `src/ai/EditorContext.cpp` (prompts), `src/ai/ToolDefinitions.cpp` (tool schemas + execution + result summaries + MCP schema conversion), `src/ai/AiClient.h/.cpp` (retry, model list), `src/gui/MidiPilotWidget.cpp` (token budgeting, effort prompts), `src/gui/AiSettingsWidget.cpp` (MCP settings section), `MainWindow.cpp` (MCP server lifecycle)

---

## Phase 24 - MusicXML & MuseScore (.mscz) Import

> **Goal:** Allow users to open MusicXML files (`.musicxml`, `.xml`) and MuseScore files
> (`.mscz`) directly in MidiEditor AI. MusicXML is the standard interchange format for
> sheet music. MuseScore uses .mscz (ZIP containing .mscx XML) as its native format.
> No external dependencies needed - Qt's XML and zlib (already available for GP6/7/8) handle everything.

> **OMR (Optical Music Recognition) was evaluated and dropped.** The native C++ OMR pipeline
> (ONNX Runtime + OpenCV + TrOMR models) added ~200 MB of dependencies, required model
> downloads, and produced unreliable results. MuseScore's free online service
> (https://musescore.com) handles PDF/image-to-MusicXML conversion reliably. Users can
> convert sheet music there and import the resulting .musicxml or .mscz file here.

### Problem Statement

Users frequently have sheet music in MusicXML format (exported from Finale, Sibelius,
MuseScore, Dorico, etc.) or as MuseScore .mscz files. Currently, they must convert to
MIDI externally before opening in MidiEditor. Direct import eliminates this friction.

### Architecture

```
User: File -> Open -> selects score.musicxml / score.mscz
                          |
           +--------------+--------------+
           |                             |
     .musicxml/.xml                    .mscz
           |                             |
  QXmlStreamReader                  zlib inflate
  parse MusicXML                   extract .mscx
           |                        from ZIP archive
           |                             |
           |                     QXmlStreamReader
           |                     parse .mscx XML
           |                             |
           +----------+------------------+
                      |
              Convert to MIDI events:
              - Notes (pitch, duration, voice)
              - Time signatures
              - Key signatures
              - Tempo markings
              - Program changes (instruments)
              - Track/part structure
                      |
              Write temp .mid file
              Load via MidiFile(tempPath)
              Open in editor
```

### Sub-Phases

#### Phase 24.1 - MusicXML to MIDI Converter (Foundation)

- [x] **24.1a** `MusicXmlImporter` class (`src/converter/MusicXml/MusicXmlImporter.h/.cpp`)
  - Parse MusicXML using Qt's `QXmlStreamReader` (no external XML lib needed)
  - Extract: parts, measures, notes (pitch, duration, voice), rests, time signatures,
    key signatures, tempo, dynamics
  - Convert to MIDI events and write to temp `.mid` file
  - Load via `MidiFile(tempPath)` - same pattern as GpImporter and MmlImporter
- [x] **24.1b** Register `.musicxml`, `.xml`, `.mxl` (compressed) in file dialog filter
- [x] **24.1c** Add format detection in `MainWindow::openFile()` alongside Guitar Pro and MML

#### Phase 24.2 - MuseScore .mscz Import

.mscz files are ZIP archives containing a `.mscx` file (MuseScore's internal XML format).
We already have zlib available for Guitar Pro 6/7/8 import (GP678_SUPPORT in CMakeLists.txt).

- [x] **24.2a** `MsczImporter` class (`src/converter/MusicXml/MsczImporter.h/.cpp`)
  - Extract .mscx from .mscz ZIP archive (reuse zlib, same as GpUnzip pattern)
  - Parse .mscx XML format (different schema than MusicXML but similar data)
  - Extract: parts/staves, measures, notes/chords, time/key signatures, tempo, instruments
  - Convert to MIDI events, write temp .mid, load via MidiFile
- [x] **24.2b** Register `.mscz` (and `.mscx`) in file dialog filter
- [x] **24.2c** Add .mscz / .mscx format detection in `MainWindow::openFile()`

#### Phase 24.3 - Polish

- [x] **24.3a** Error handling and user feedback for malformed files
  - Format-aware error messages in `MainWindow::openFile()` (MusicXML / MuseScore / Guitar Pro / MML)
- [x] **24.3b** Unit tests with sample MusicXML and .mscz files
  - Qt Test harness scaffolded under `tests/`, opt-in via `-DBUILD_TESTING=ON`
  - First suite: `test_xml_score_to_midi` (6 tests) covers the shared SMF writer
  - `build_tests.bat` builds + runs `ctest` end-to-end
  - Importer round-trip tests against sample files: deferred (needs `MidiFile` extracted into a static lib first)

### .mscz Format Details

```
score.mscz (ZIP archive)
  ├── META-INF/container.xml    (optional, points to .mscx)
  ├── score.mscx                (the actual score XML)
  ├── Thumbnails/thumbnail.png  (preview image, ignore)
  └── audiosettings.json        (playback config, ignore)
```

**.mscx XML structure (MuseScore 4.x):**
```xml
<museScore version="4.20">
  <Score>
    <Part>
      <Staff id="1"/>
      <Instrument id="piano">
        <Channel>
          <program value="0"/>  <!-- GM program number -->
        </Channel>
      </Instrument>
    </Part>
    <Staff id="1">
      <Measure>
        <voice>
          <TimeSig><sigN>4</sigN><sigD>4</sigD></TimeSig>
          <Chord>
            <durationType>quarter</durationType>
            <Note><pitch>60</pitch><tpc>14</tpc></Note>
          </Chord>
          <Rest><durationType>quarter</durationType></Rest>
        </voice>
      </Measure>
    </Staff>
  </Score>
</museScore>
```

**Key .mscx elements to parse:**
| Element | MIDI Mapping |
|---------|-------------|
| `<Part>/<Instrument>/<Channel>/<program>` | Program Change event |
| `<TimeSig>/<sigN>`, `<sigD>` | Time Signature event |
| `<KeySig>/<accidental>` | Key Signature event |
| `<Tempo>/<tempo>` | Tempo Change event (BPM = tempo * 60) |
| `<Chord>/<Note>/<pitch>` | NoteOn/NoteOff events |
| `<Chord>/<durationType>` | Note duration (whole/half/quarter/eighth/16th/32nd) |
| `<Rest>/<durationType>` | Advance tick position |
| `<voice>` index | MIDI voice/layer separation |
| `<Staff id>` | Track assignment |
| `<Tuplet>` | Duration scaling (e.g., triplets = 2/3) |
| `<dots>` | Dotted duration (1.5x) |

### Implementation Order

```
Phase 24.1a  MusicXmlImporter core parser                        ✅ DONE
Phase 24.1b  Register MusicXML in file dialog                    ✅ DONE
Phase 24.1c  Format detection in openFile()                      ✅ DONE
Phase 24.2a  MsczImporter core parser (.mscz + .mscx)            ✅ DONE
Phase 24.2b  Register .mscz / .mscx in file dialog               ✅ DONE
Phase 24.2c  Format detection for .mscz / .mscx                  ✅ DONE
Phase 24.3a  Error handling & feedback                           ✅ DONE
Phase 24.3b  Unit tests (XmlScoreToMidi)                         ✅ DONE
```

### Estimated Complexity

| Sub-phase | New Code | Modified Code | Risk | External Deps |
|-----------|----------|---------------|------|---------------|
| 24.1 MusicXmlImporter | ~500 lines | MainWindow.cpp (~20 lines), CMakeLists.txt | Medium | None (Qt XML) |
| 24.2 MsczImporter | ~400 lines | MainWindow.cpp (~10 lines), CMakeLists.txt | Medium | zlib (already available) |
| 24.3 Polish & tests | ~200 lines | — | Low | — |
| **Total** | **~1100 lines** | **~30 lines modified** | **Medium** | **None new** |

### Key Design Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| MusicXML parser | Qt QXmlStreamReader | Already available, no new dependency |
| .mscz ZIP extraction | zlib (inflate) | Already linked for GP6/7/8 support |
| .mscx XML parser | Qt QXmlStreamReader | Same as MusicXML, consistent approach |
| OMR (PDF/image import) | Dropped | Too complex, unreliable results, MuseScore's online service works better |
| Dependencies added | None | Both formats use only Qt XML + existing zlib |
- **Phase 24.4 depends on 24.3** - Documentation covers the complete feature
- **No dependency on Phase 23** - can be developed in parallel

### Files

- **New:** `src/converter/MusicXml/MusicXmlImporter.h/.cpp` (MusicXML parser),
  `src/converter/OMR/OmrEngine.h/.cpp` (pipeline orchestrator),
  `src/converter/OMR/OmrModelManager.h/.cpp` (model download/management),
  `src/converter/OMR/OmrPreprocessor.h/.cpp` (image preprocessing),
  `src/converter/OMR/OmrSegmentation.h/.cpp` (UNet segmentation),
  `src/converter/OMR/OmrStaffDetector.h/.cpp` (staff detection algorithms),
  `src/converter/OMR/OmrTransformer.h/.cpp` (TrOMR encoder/decoder),
  `src/converter/OMR/OmrMusicXmlWriter.h/.cpp` (MusicXML output),
  `manual/sheet-music-import.html`
- **Modified:** `MainWindow.cpp` (file dialog filters, openFile() routing),
  `CMakeLists.txt` (new source files, ONNX Runtime, OpenCV, Qt6::Pdf module),
  `SettingsDialog.cpp` (OMR settings section)
- **Shipped:** `onnxruntime.dll` (MIT), OpenCV statically linked
- **Downloaded at runtime:** `omr_models/segnet.onnx`, `omr_models/encoder.onnx`, `omr_models/decoder.onnx`

---

## Phase 25: Live Streaming Everywhere + Reasoning Summary (Planned)

> **Goal:** Make the Agent Mode feel like Copilot — text streams as it's generated,
> tool-call arguments stream as they're being constructed, and (where supported)
> a Reasoning Summary is displayed live. Behaviour is **opt-in/opt-out** via a
> toggle in the MidiPilot footer so users on slow links or with strict providers
> can still pick the legacy "wait for full response" path.

### Motivation

Today's state ([`AiClient::sendStreamingRequest`](../src/ai/AiClient.cpp), Agent path):

* ✅ Simple-mode plain text already streams (SSE → `streamDelta` → live render).
* ✅ Token usage is captured from the final SSE chunk via `stream_options.include_usage`.
* ✅ Agent loop emits `stepsPlanned` / `stepStarted` / `stepCompleted` so the UI shows
  per-tool status — but only **after** each round-trip completes.
* ❌ Agent Mode itself does **not** stream — `AgentRunner::sendNextRequest()` calls the
  non-streaming `_client->sendMessages(_messages, _tools)` path, so when the model is
  composing a long tool-call argument blob the user stares at "thinking..." for the
  whole round-trip.
* ❌ No Reasoning Summary surface anywhere. We send `reasoning_effort` but never
  request `reasoning.summary: "auto"` and have no UI to display it.
* ❌ The OpenAI Responses API is declared (`RESPONSES_API_URL`) but never used — all
  traffic goes through Chat Completions. That's fine for streaming text + tool-call
  deltas, but blocks first-class Reasoning Summaries on `gpt-5*` / `o*` models.

### Scope

**25.1 — Stream the Agent loop** — **DONE in 1.5.0** (incl. native Gemini path with live thought summaries via `streamGenerateContent?alt=sse` + `thinkingConfig.includeThoughts`)
* Add `AiClient::sendStreamingMessages(messages, tools)` mirroring the existing
  `sendMessages` API but with `stream: true` and SSE parsing for the
  `tool_calls[].function.arguments` deltas (Chat Completions returns them under
  `choices[0].delta.tool_calls[*].function.arguments` as incremental strings; assemble
  per-call by `id`).
* Emit new signals: `toolCallStarted(callId, toolName)`,
  `toolCallArgumentsDelta(callId, jsonFragment)`, `toolCallArgumentsDone(callId)`.
* `AgentRunner` consumes these and forwards a third per-step phase
  ("filling arguments") to `MidiPilotWidget` so the Steps panel shows
  `Insert events — Track 3 (composing…)` while the JSON is still arriving, and only
  flips to the existing labelled form when `arguments_done` fires.
* Existing non-streaming agent path stays as fallback.

**25.2 — Reasoning Summary surface**
* For OpenAI `gpt-5*` / `o*` and Gemini "thinking" models, send
  `reasoning: { summary: "auto", effort: <current>}` (Responses API) or the
  `reasoning_effort` field plus the appropriate `reasoning.summary` shim
  (Chat Completions where supported).
* Parse the `response.reasoning_summary.delta` event (Responses API) or the
  `choices[0].delta.reasoning_content` field (some Chat Completions providers)
  and emit `reasoningSummaryDelta(text)` / `reasoningSummaryDone()`.
* MidiPilotWidget renders a collapsed "💭 Thinking…" disclosure above the
  assistant bubble; expanding shows the live summary stream. Empty/null summary
  hides the disclosure entirely.

**25.3 — Opt-in/out toggle in the MidiPilot footer**
* Add a toolbar action next to the existing reasoning-effort combo:
  **`📡 Live Stream`** — three-state combo `Off` / `Text only` / `Text + tool args`.
* Default: `Text + tool args` for OpenAI / OpenRouter / Gemini (the providers we
  control), `Text only` for Custom (since exotic OpenAI-compatible servers may
  not implement tool-call delta streaming correctly).
* Persist under `AI/streaming_mode` in `QSettings`.
* When `Off`, agent path uses the existing non-streaming `sendMessages` and the
  simple chat path skips `sendStreamingRequest` for the legacy `sendRequest` flow.
* Hovering the combo shows a tooltip explaining the trade-off ("Live shows tokens
  as they arrive but uses an open HTTP connection; turn off if your network is
  flaky or your provider rejects SSE").

**25.4 — Settings opt-out for Reasoning Summary**
* Under **Settings → AI → Reasoning**, new checkbox **"Show reasoning summary
  when available"** (default on). Persisted under `AI/reasoning_summary_visible`.
* When unchecked, MidiPilotWidget never asks the API for a summary
  (`summary: null` instead of `"auto"`), saving a few hundred output tokens per
  reply on supported models.

### Provider Capability Matrix

> Researched 2026-04-22 against official docs. Where two row entries appear
> (e.g. "Chat Completions / Responses API"), MidiEditor will prefer the first
> for compatibility and only use the second when the user explicitly enables
> Reasoning Summary on a supported model.

| Provider | Stream text | Stream tool-call args | Reasoning surface | Request shape | Notes / gotchas |
|---|---|---|---|---|---|
| **OpenAI — Chat Completions** | ✅ `choices[0].delta.content` | ✅ `choices[0].delta.tool_calls[*].function.arguments` (delta) | ❌ Not exposed in CC | `stream: true`, `reasoning_effort: low/medium/high` | First chunk has `id` + `function.name`, subsequent chunks only `arguments`. Index identifies the call across chunks. |
| **OpenAI — Responses API** | ✅ `response.output_text.delta` | ✅ `response.function_call_arguments.delta/done` | ✅ `response.reasoning.delta/done` (summary, not raw thoughts) | `stream: true`, `reasoning: { effort, summary: "auto" }` | Required for first-class summaries on `gpt-5*`/`o*`. Different event-typed SSE schema than CC. |
| **OpenRouter — Chat Completions (default)** | ✅ same as OpenAI CC | ✅ same as OpenAI CC | ✅ `delta.reasoning` chunks when `reasoning: { enabled: true }` or `include_reasoning: true` is set | `stream: true`, `reasoning: { effort: "..." }` or legacy `include_reasoning: true` | Reasoning support depends on routed underlying model (DeepSeek R1, Gemini Thinking, GPT-5, Claude with interleaved thinking, etc.). For Anthropic models routed via OpenRouter the first tool-call chunk omits `function.name` — known parser pitfall. |
| **OpenRouter — Responses API Beta** | ✅ same event names as OpenAI Responses | ✅ same event names | ✅ `response.reasoning.delta` | mirrors OpenAI Responses | Beta — fall back to CC path if a 4xx comes back. |
| **Google Gemini — `:streamGenerateContent?alt=sse`** | ✅ each SSE chunk = `candidates[0].content.parts[*].text` fragment | ⚠️ **No arg-level deltas** — `parts[*].functionCall` arrives whole when the tool call is finalised | ✅ Thought summaries via `generationConfig.thinkingConfig.includeThoughts: true` → chunks carry `parts[*].thought == true` text parts | `POST .../models/<model>:streamGenerateContent?alt=sse&key=...` with `thinkingConfig.thinkingBudget` (–1 = auto, 0 = off, or fixed budget) | Native Gemini API — NOT OpenAI-shape. We need a second SSE parser path (or use the OpenRouter Gemini route to keep the OpenAI shape and lose thought-summary granularity). Tool calls are not streamed incrementally — UI shows "composing tool call…" then jumps to the full args at once. |
| **Custom — Ollama (`/api/chat`)** | ✅ NDJSON line-stream of `message.content` chunks | ✅ Streamed since the 2025 "streaming-tool" release — chunks carry `message.tool_calls[*]` (whole tool-call objects, not arg-deltas) | ❌ Only model-defined `<think>...</think>` text (model-specific, not standardised) | `stream: true`, `tools: [...]` | NDJSON, **not** SSE. Need an alternate framer (split on `\n`, parse each line). |
| **Custom — Ollama OpenAI-compat (`/v1/chat/completions`)** | ✅ same shape as OpenAI CC | ✅ same shape as OpenAI CC | ❌ | `stream: true` | Known bug ([ollama#15457](https://github.com/ollama/ollama/issues/15457)): with multiple tool calls, `tool_calls[].index` is always 0 — we must fall back to identifying calls by `id` instead. |
| **Custom — LM Studio / vLLM / LocalAI / Lemonade** | ✅ OpenAI-CC compatible | ✅ OpenAI-CC compatible (recent versions) | ❌ Varies | as OpenAI CC | LM Studio added proper tool-call delta streaming in 2025; older builds emit one chunk per tool call. vLLM is the most spec-conformant of the three. |

### Implementation Strategy Implied by the Matrix

1. **Two SSE/stream parsers, not three.** OpenAI-CC, OpenRouter-CC, OpenAI-Responses, OpenRouter-Responses, and all OpenAI-compatible custom servers (Ollama-compat, LM Studio, vLLM, LocalAI) share the OpenAI SSE shape. Gemini native + Ollama native each need their own framer (`alt=sse` for Gemini, NDJSON for Ollama). We can keep MidiEditor on the OpenAI-CC path universally by routing Gemini through OpenRouter (`google/gemini-2.5-pro`) — at the cost of losing per-thought-summary fidelity. Default policy: OpenAI-CC parser everywhere, optional Gemini-native parser only if the user explicitly picks the Gemini provider.
2. **Tool-call streaming is not universal.** Gemini and Ollama emit whole tool calls (no per-character arg deltas). The UI's "composing arguments…" phase therefore needs a graceful degradation: show the spinner, then jump straight to the labelled call when the whole `function_call`/`tool_calls` part arrives. The streaming-mode combo's "Text + tool args" setting is a hint, not a guarantee — providers that can't stream args still show plain step events.
3. **Reasoning surface is provider-specific:**
   * OpenAI `gpt-5*`/`o*` → Responses API + `reasoning.summary: "auto"` (only proper summary path).
   * OpenRouter → CC with `reasoning: { effort, enabled: true }` (or legacy `include_reasoning: true`); supported for DeepSeek R1, Gemini Thinking via OR, GPT-5 via OR, Claude with interleaved thinking. We render `delta.reasoning` chunks the same way as OpenAI's `response.reasoning.delta`.
   * Gemini native → `thinkingConfig.includeThoughts: true`; thought parts arrive interleaved with text parts inside the SSE stream (we filter on `parts[i].thought == true`).
   * Custom / Ollama → no standardised reasoning channel. Some models emit `<think>...</think>` inline in the text stream — we offer a per-provider regex extractor as a 25.5 follow-up only if users ask for it.
4. **Settings telemetry:** the streaming-mode combo's effective resolution is computed at request time as `min(user_setting, provider_capability)`. UI shows the resolved value as a tooltip ("Set to 'Text + tool args', current provider supports 'Text only' — falling back").

### Files

- **Modified:** `src/ai/AiClient.h/.cpp` (add `sendStreamingMessages`, parse
  `tool_calls[].function.arguments` deltas, emit new signals; opt-in
  Reasoning Summary parsing for Responses-API-capable models),
  `src/ai/AgentRunner.h/.cpp` (consume streaming signals, three-phase step
  display), `src/gui/MidiPilotWidget.h/.cpp` (footer streaming combo +
  reasoning summary disclosure widget), `src/gui/AiSettingsWidget.h/.cpp`
  ("Show reasoning summary" checkbox), `tests/test_ai_client.cpp` (new — SSE
  delta assembly + tool-call argument reassembly fixtures).

### Out of Scope

* Migrating the entire transport to the Responses API. Chat Completions SSE
  already covers 95 % of what we want; Responses API is only used as a fallback
  for first-class Reasoning Summaries on supported OpenAI models.
* Streaming for the Custom provider beyond `Text only` — too many implementation
  variants in the wild to claim "tool args delta works".

---

## Phase 26: Dynamic Provider Model List (DONE in 1.5.0)

> **Goal:** Drop the hardcoded `_modelCombo->addItem("gpt-5.4", …)` lists in
> [`AiSettingsWidget`](../src/gui/AiSettingsWidget.cpp) and
> [`MidiPilotWidget::populateFooterModels`](../src/gui/MidiPilotWidget.cpp). Fetch
> the current model list from the active provider's `/models` endpoint, cache
> to a JSON file under the user data dir, and refresh on user demand. Stops
> the situation where new models (gpt-5.5, claude-4.1, gemini-3.5, …) ship and
> the user has to type the name manually until we cut a release.

### Motivation

* Every provider hardcoded today already drifts: `gpt-4o`, `gpt-4.1*`, `gpt-5*`
  variants are pinned in source; new releases require a recompile.
* All four supported providers expose a model-list endpoint:

| Provider | Endpoint | Auth | Notes |
|---|---|---|---|
| OpenAI | `GET https://api.openai.com/v1/models` | `Authorization: Bearer <key>` | Returns `data[].id` |
| OpenRouter | `GET https://openrouter.ai/api/v1/models` | optional | Public; with key returns user-allowed models + per-call pricing |
| Google Gemini | `GET https://generativelanguage.googleapis.com/v1beta/models?key=<KEY>` | API key in query | Returns `models[].name` (`models/gemini-2.5-flash` → strip prefix) |
| Custom (OpenAI-compatible: Ollama, LM Studio, vLLM) | `GET <base>/v1/models` | per-server | Already partially scoped in Phase 8 for local providers — this phase generalises it |

### Scope

**26.1 — `AiClient::fetchModelList(provider, callback)`**
* New method per provider returning `QJsonArray` of `{ id, displayName,
  contextWindow, capabilities }` objects. Provider-specific shims normalise
  the four response shapes into one schema.
* Filters: drop embedding/audio/image-only/legacy models. For OpenAI we keep
  ids matching `gpt-*`, `o[1-9]*`, `chatgpt-*`. For OpenRouter we keep entries
  whose `architecture.modality` includes `text->text`. For Gemini we keep
  `supportedGenerationMethods` containing `generateContent`.
* On success, emit `modelListFetched(provider, jsonArray)`. On failure, emit
  `modelListFetchFailed(provider, error)` and fall back to whatever is on
  disk; if disk cache is also empty, fall back to the existing hardcoded
  list (kept as last-resort safety net so a network outage doesn't strand
  the user).

**26.2 — Disk cache: `<userdata>/midipilot_models.json`**
* Schema:
```json
{
  "version": 1,
  "providers": {
    "openai":     { "fetched_at": "2026-04-22T18:00:00Z", "models": [ { "id": "gpt-5.4", "context": 1000000, "supports_tools": true, "supports_reasoning": true } ] },
    "openrouter": { "fetched_at": "...", "models": [ ... ] },
    "gemini":     { "fetched_at": "...", "models": [ ... ] },
    "custom":     { "fetched_at": "...", "models": [ ... ], "base_url": "..." }
  }
}
```
* TTL: 7 days. On first load of the AI settings dialog, if the cached
  `fetched_at` is stale or missing, kick off a background refresh
  (non-blocking, falls through to whatever's already cached).
* Also drives `AiClient::contextWindowForModel()` — replaces the hardcoded
  prefix-match table; falls back to it only when the model id is not in
  the cache.

**26.3 — UI: refresh + manual entry kept**
* `AiSettingsWidget` and `MidiPilotWidget` footer combo:
  * Populate from the cache for the current provider.
  * Add a small **🔄** refresh icon-button next to the model combo that
    triggers `AiClient::fetchModelList(currentProvider)`. Tooltip:
    "Refresh model list from provider".
  * Combo stays editable (existing behaviour) so the user can still type
    a not-yet-published model id; if a typed id resolves successfully on
    first request, it's auto-added to the cache for that provider.
* Status indicator: greyed-out timestamp under the combo
  ("models updated 2 days ago").

**26.4 — Custom provider: discoverable via base URL**
* For the Custom provider, the refresh button calls `<base>/v1/models` so
  Ollama / LM Studio / LocalAI / vLLM users get their currently-loaded
  models without manual typing. This is the part already
  scoped in Phase 8 for "Local providers" — folded into 26 so all four
  providers share one code path.

### Files

- **New:** `src/ai/ModelListFetcher.h/.cpp` (per-provider fetch + normalise),
  `src/ai/ModelListCache.h/.cpp` (load/save/TTL of the JSON cache),
  `tests/test_model_list_fetcher.cpp` (response-shape normalisation fixtures
  using stubbed QNAM replies).
- **Modified:** `src/ai/AiClient.h/.cpp` (delegate `contextWindowForModel`
  to `ModelListCache`; new `fetchModelList` method + signals),
  `src/gui/AiSettingsWidget.h/.cpp` (drop hardcoded `populateModelsForProvider`
  body, replace with cache lookup; add refresh button),
  `src/gui/MidiPilotWidget.h/.cpp` (same in `populateFooterModels`),
  `manual/midipilot.html` (document model refresh + cache location).

### Out of Scope

* Pricing display in the model combo. OpenRouter's response does carry
  per-token cost, but surfacing it touches MidiPilot UX considerably and
  belongs in a follow-up.
* Auto-refresh on app launch. We refresh lazily (when the AI settings
  dialog opens or the user clicks 🔄) to stay out of the user's network
  on every startup.

---

## Phase 27: MidiPilot UX Polish — Universal Thoughts, Responses-API Streaming, History UX & Persistent Turns ✅ DONE (Unreleased)

> **Goal:** Round out Phase 25's streaming work so live thoughts work for
> *every* provider, OpenAI's Responses API streams as smoothly as Gemini,
> the conversation-history dropdown stays usable past 30 chats, and
> reopened conversations reproduce the live 💭/🔧 visual that the user
> just saw — not just the bare assistant text.

### Scope (all DONE)

**27.1 — Universal reasoning extractor**
* `AiClient::extractReasoningFromJson(QJsonObject)` walks every known
  thought/reasoning shape: OpenAI Responses (`output[].content[]` reasoning
  items), OpenAI Chat-Completions (`reasoning_content` / `reasoning`),
  Gemini (`parts[].thought == true`), Anthropic (`content[].type == "thinking"`),
  plus generic `reasoning` / `thoughts` fallbacks.
* Single `streamReasoningDelta(QString)` signal — the UI lambda is
  provider-agnostic.

**27.2 — OpenAI Responses-API live SSE**
* New `sendStreamingMessagesResponses` + `onResponsesStreamDataAvailable`
  in `AiClient`. Parses Responses-API typed events
  (`response.output_text.delta`, `response.reasoning_summary_text.delta`,
  `response.function_call_arguments.delta`, `response.completed`, …) and
  reassembles a synthetic Chat-Completions payload so `AgentRunner` is
  unchanged.
* Routing: `gpt-5.x` (or any model gated to the Responses API) + tools now
  streams instead of falling back to the non-streaming path.

**27.3 — Gemini thought-signature replay**
* `StreamToolCall::thoughtSignature` captures `part.thoughtSignature` from
  Gemini SSE and writes it back onto the `functionCall` part on the next
  request via `_gemini_thought_signature` on the synthetic tool_call.
* Fixes HTTP 400 *"missing thought_signature"* on Gemini 3.x multi-step
  tool loops.

**27.4 — Visual polish**
* Rotating Braille spinner (`⠋⠙⠹⠸⠼⠴⠦⠧⠇⠏`, ~8 fps) on the live thought
  label, replacing the underscore blink.
* `onAgentFinished` / `onAgentError` lift the Steps widget out of the dock
  so the order in chat becomes `[thoughts] → [final response] → [steps]`.

**27.5 — Scrollable date-grouped history popup**
* `showHistoryMenu` rewritten from `QMenu` to a frameless `QDialog` with
  `QLineEdit` search, `QScrollArea` body capped at ~500 px, conversations
  grouped by date bucket (Today / Yesterday / weekday / Month Year), and
  per-row dedicated **Load** + **Delete** buttons.
* Live filter hides non-matching rows and collapses now-empty section
  headers.

**27.6 — Persistent per-turn metadata in `MidiPilotHistory/<id>.json`**
* New `turns[]` array alongside `messages[]`. Each turn anchors to its
  assistant message via `assistantIndex` and stores `reasoning`, `steps[]`
  (`{step, tool, success, recoverable}`), `streamed`, `latencyMs`,
  `effort`, `provider`, `model`, `promptTokens`, `completionTokens`,
  `status`.
* `MidiPilotWidget`:
  * `resetTurnState()` snapshots provider/model/effort + start time at user-send.
  * `streamReasoningDelta` and `streamAssistantTextDelta` lambdas accumulate
    the reasoning text and flip `_turnStreamed`.
  * `onAgentStepCompleted` appends a compact step record.
  * `finalizeTurn()` is called from `onAgentFinished`, `onAgentError` and
    `onResponseReceived` (simple mode) and seals the record.
  * `loadConversation()` re-indexes `turns[]` by `assistantIndex` and renders
    the saved 💭 thought block above and the `🔧 Steps: ✓ tool1, ✗ tool2`
    summary below each assistant bubble — reopened conversations look like
    they did live.

### Files Modified
* `src/ai/AiClient.{h,cpp}`
* `src/gui/MidiPilotWidget.{h,cpp}`

### Out of Scope
* Persisting full tool argument/result payloads (kept compact: name +
  success only). A future "replay" feature could store them, but they
  bloat the history JSON quickly.

**27.7 — Streaming-fallback safety net for unknown providers** ✅ DONE
* Problem: third-party / OpenAI-compat endpoints often advertise SSE but
  either return HTTP 4xx for `stream:true` or stream a 200 OK with an
  event shape we don't parse, leaving the user staring at an empty bubble.
* `AiClient` now arms a per-request retry context
  (`armStreamingRetryAgent` / `armStreamingRetrySimple`) before each
  streaming POST. Every streaming finished-lambda inspects the outcome via
  `shouldFallbackToNonStreaming(httpStatus, netError, gotContent, gotToolCalls)`
  and, when true, calls `tryStreamingFallback(reason)` which dispatches to
  the regular `sendRequest` / `sendMessages` path and emits a new
  `retrying(QString)` signal so the UI can show the reason.
* The offending `<provider>:<model>` is persisted via
  `markStreamingUnsupportedForCurrentModel(reason)` into
  `QSettings("AI/streaming_blocklist/<provider>:<model>", true)` so the
  very next request skips the broken streaming code path immediately.
  `clearStreamingBlocklist(provider, model)` re-enables streaming when the
  user picks a different model in Settings.
* Wired into all four streaming entry points:
  * `sendStreamingMessages` (Chat-Completions agent loop)
  * `sendStreamingRequest` (Chat-Completions simple-mode)
  * `sendStreamingMessagesResponses` (OpenAI Responses-API agent)
  * `sendStreamingMessagesGemini` (Google native `streamGenerateContent`)
* Explicitly **not** retried:
  * `QNetworkReply::OperationCanceledError` (user pressed Cancel)
  * Gemini semantic finish reasons `SAFETY`, `RECITATION`, `MAX_TOKENS`,
    `MALFORMED_FUNCTION_CALL` — the regular endpoint would return the same
    error, retrying just wastes a request.

### Files Modified (27.7)
* `src/ai/AiClient.{h,cpp}` — public helpers + 6 private members + 6
  helper functions + 4 streaming finished-lambda fallback wires.

---

## Phase 28: OpenRouter Robustness & Capability-Aware Error Handling — 1.5.0

> **Goal:** Make the OpenRouter pathway as reliable as the OpenAI-native one.
> The OpenAI Chat-Completions schema is shared between providers, but OpenRouter
> proxies to ~50 upstream providers (Novita, Fireworks, Together, DeepInfra, …),
> each with different capabilities and reliability. Model-specific failures
> (no tool support, upstream timeouts, blocked providers) currently surface as
> opaque "HTTP 4xx" errors with no recovery and no actionable feedback.
>
> Fix this in three layers: **(a)** classify provider/upstream failures so we
> retry the recoverable ones; **(b)** detect non-recoverable model limitations
> up-front and surface them in the UI; **(c)** let users pin upstream providers
> when they want predictability.

### Background — Why OpenRouter is harder than OpenAI

* OpenRouter exposes one `/api/v1/chat/completions` endpoint that *fans out*
  to upstream providers. Every model card has a `top_provider`, but actual
  routing is decided per-request based on price/latency/availability.
* The request schema is OpenAI-compatible, but **capabilities are per-model**:
  * Tool calling — many models (e.g. `x-ai/grok-multi-agent`, most older
    Llama derivatives) do not support `tools[]` at all and the API returns
    `HTTP 404 "No endpoints found that support tool use"`.
  * Streaming — some upstream providers refuse `stream:true`. Already
    handled by 27.7's streaming-fallback blocklist.
  * `reasoning` / thinking summaries — provider-dependent.
  * `response_format: json_object` / `json_schema` — provider-dependent.
* Upstream providers periodically reject requests with `HTTP 400`
  `{"message":"Provider returned error","metadata":{"provider_name":"Novita"}}`.
  This is **transient**: a re-issue routes to a different upstream and
  usually succeeds. Currently captured (28.1 below already shipped in
  1.5.0-rc as a hot-fix).

### Observed real-world log (2026-04-24)

| Time | Model | Result | Root cause |
|------|-------|--------|------------|
| 19:32 | `anthropic/claude-opus-4.6-fast` | ✅ 5 toolCalls | OK |
| 19:33 | `x-ai/grok-4.20-multi-agent` | ❌ HTTP 404 *"No endpoints support tool use"* | **Model lacks tools** — bubble up |
| 19:33 | `x-ai/grok-4.20` | ⚠ second turn `chars=0 toolCalls=0` | Empty response — already retried by 27.7 + agent self-heal |
| 19:34 | `openai/gpt-5.4-pro` | ✅ (~8 min Reasoning) | OK |
| earlier | `moonshotai/kimi-k2` | ❌ HTTP 400 *"Provider returned error" / Novita* | **Upstream rejected** — retry routes elsewhere |

### Scope

**28.1 — Transient-upstream classifier (DONE in 1.5.0-rc)**
* `AgentRunner::classifyError` and `MidiPilotWidget::onErrorOccurred::isRetriable`
  now treat `"provider returned error"`, `"provider_name"` and `HTTP 400 + openrouter`
  as `RetryKind::Network` so the existing self-healing retry kicks in
  (3 attempts, exponential back-off). On retry OpenRouter selects a
  different upstream.
* No new settings; reuses `AI/agent_max_retries` / `AI/simple_max_retries`.

**28.2 — Capability-aware error surfacing (HTTP 404 — no tool support)** ✅ DONE
* New `AiClient::errorIndicatesNoToolSupport(error)` heuristic catches the
  OpenRouter HTTP 404 *"No endpoints found that support tool use"* family
  plus generic *"does not support tools / function calling is not supported"*
  variants from other gateways.
* `AgentRunner::onApiError` checks this **before** the retry classifier
  (so we don't waste 3 attempts on a permanent capability gap), calls
  `AiClient::markToolsIncapableForCurrentModel(reason)` and surfaces a
  clear, actionable message:
  > Model does not support tool calling — pick a different model in
  > Settings → AI, or switch to Simple mode for this request.
* `AiClient` persists the flag at `AI/incapable_tools/<provider>:<model>`
  via `toolsIncapableForCurrentModel()` /
  `markToolsIncapableForCurrentModel()` /
  `clearToolsIncapableFlag()` (mirrors the streaming-blocklist API
  introduced in 27.7).
* `MidiPilotWidget::onSendMessage` (agent branch) consults the flag
  **before** spinning up the agent loop. If set, it skips the API
  round-trip entirely and posts a friendly system bubble instead. The
  flag is per-(provider,model) so picking a different model in the
  Settings → AI dropdown re-enables agent mode automatically.

**28.3 — Per-model capability cache from `/models` list** — **TODO**
* `ModelListFetcher::normaliseOpenRouter` already parses the model list. Extend
  the parser to also capture:
  * `supported_parameters` array (`tools`, `tool_choice`, `response_format`,
    `reasoning`, `structured_outputs`, …).
  * `architecture.modality` (`text`, `text+image`, …).
  * `top_provider.context_length` and `pricing.{prompt,completion}`.
* Store as a sidecar JSON `~/.midieditor/openrouter_models.json` keyed by
  model id. `MidiPilotWidget` consults this cache before sending; if
  `tools` ∉ `supported_parameters`, fall back to Simple mode automatically
  (with a one-line info bubble) instead of provoking the HTTP 404.

**28.4 — Optional provider-pinning for OpenRouter** — **TODO**
* OpenRouter accepts an extra `provider` block in the request body:
  ```json
  "provider": {
    "order": ["Anthropic", "Together"],
    "allow_fallbacks": false,
    "data_collection": "deny",
    "require_parameters": true
  }
  ```
* Add a small "Provider routing" group inside Settings → AI (visible only
  when provider == openrouter):
  * Multi-select list of preferred upstreams (populated from the model
    list's `top_provider` info, falling back to a static curated list).
  * Toggle "Allow fallbacks" (default ON for resilience, OFF for
    reproducibility).
  * Toggle "Require parameters" (rejects providers that silently drop
    unsupported params — recommended ON when using `reasoning` /
    `tool_choice`).
  * Toggle "Deny data collection" (privacy preset).
* Settings keys:
  * `AI/openrouter/provider_order` (`QStringList`)
  * `AI/openrouter/allow_fallbacks` (`bool`, default true)
  * `AI/openrouter/require_parameters` (`bool`, default true)
  * `AI/openrouter/data_collection` (`"allow"`/`"deny"`, default `"allow"`)
* `AiClient::buildRequestBody()` (or wherever the chat-completions body is
  composed) injects the `provider` object only when `_provider == "openrouter"`
  AND any of the above settings is non-default.

**28.5 — Surfaced provider attribution in chat (lightweight)** — **TODO**
* When `usage` / response carries an upstream `provider_name` or
  `models_used` field, append it to the per-turn metadata stored by 27.6
  (`turns[].upstream`) and render it in the existing 🔧 Steps line:
  > 🔧 Steps: ✓ create_track, ✓ insert_events · via Together
* Helps the user understand *why* a request was slow / cheap / failed.

**28.6 — Long-timeout awareness for reasoning models** — **TODO**
* OpenAI Responses API + OpenRouter both expose long-running reasoning
  models (`o1`, `o3`, `gpt-5.x-pro`) that legitimately take 5–10 minutes.
  Confirm `QNetworkAccessManager` per-reply timeout is **disabled** (or
  ≥15 min) for these requests, and surface a *"Reasoning model — this can
  take several minutes…"* hint in the live status label whenever
  `reasoning` is requested.

### Out of Scope

* **Per-provider price awareness / budget caps** — interesting but a
  bigger feature; defer to Phase 29.
* **Replacing OpenAI client routing** — the OpenAI-native path stays the
  one-true-path for OpenAI models; OpenRouter remains opt-in.

### Files to Modify

| File | Section |
|------|---------|
| `src/ai/AgentRunner.{h,cpp}` | 28.2 — `RetryKind::ModelIncapable` + handler |
| `src/gui/MidiPilotWidget.{h,cpp}` | 28.2 — friendly bubble + incapable flag check before send; 28.5 — render upstream attribution |
| `src/ai/ModelListFetcher.{h,cpp}` | 28.3 — extended OpenRouter parser + capability sidecar JSON |
| `src/ai/AiClient.{h,cpp}` | 28.4 — inject `provider` block; 28.6 — confirm no per-reply timeout for reasoning models |
| `src/gui/AiSettingsWidget.{h,cpp}` | 28.4 — Provider routing group (OpenRouter only) |
| `src/ai/ConversationStore.{h,cpp}` | 28.5 — `turns[].upstream` field |

### Acceptance Criteria for 1.5.0

1. Picking `x-ai/grok-4.20-multi-agent` (or any tools-incapable model)
   produces a clear, actionable chat message instead of a raw HTTP 404, AND
   blocks subsequent tool requests until the user changes model.
2. A run that hits `Provider returned error` from one OpenRouter upstream
   completes successfully via the existing retry within ≤3 attempts.
3. Settings → AI shows a Provider Routing group when OpenRouter is the
   active provider; the chosen `provider.order` is round-tripped into
   the request body and visible in `midipilot_api.log`.
4. Reasoning models do not trigger a network timeout for runs ≤10 minutes.
5. No regression in OpenAI-native, Gemini-native, Anthropic, or Custom
   provider paths.

---

## Phase 29: Per-Model System Prompt Profiles — target 1.5.0

### Why

GPT-5.5 (`gpt-5.5*`, `gpt-5.5-pro-*`) gets stuck in 30+ step "I'll plan
the melody / actually let me reconsider the ticks / wait, did I just
insert pitch bends?" reasoning loops on prompts that all other tested
models (GPT-5.4, Gemini 2.x, Claude 3.x, Qwen 3.x, DeepSeek, OpenRouter
generics, ...) handle in 2–4 steps with the current default system
prompt. Reproduced 2026-04-25 with `midipilot_api.log` showing 38
consecutive `[STREAM-RESPONSES-DONE] chars=0 toolCalls=1` turns and zero
notes inserted. The reasoning text ("I mistakenly inserted pitch bends")
shows the model misreads pre-existing `editor_state` events as its own
prior tool output.

We do **not** want to bloat the default prompt with GPT-5.5-specific
caveats — every extra rule costs every other model tokens and may
regress them. Instead we let the user (and us, via shipped defaults)
attach a **prompt profile** to one or more models. Profile resolution
runs in `MidiPilotWidget::buildSystemPrompt()` before the default is
emitted.

This phase intentionally mirrors the 1.5.0 **Model Favorites** UX
(Phase 26) so it's discoverable: "the same provider/model picker, but
checkboxes attach a prompt profile instead of pinning to favorites".

### Goals

- Author a custom system prompt once; bind it to N models via checkbox.
- When a bound model is active, its profile prompt is used (replaces or
  appends to default — per-profile flag).
- Default prompt and user "personal" custom prompt both remain valid
  fallbacks; profiles are an optional layer on top.
- Ship one read-only built-in profile: **GPT-5.5 Decisive** bound to
  `gpt-5.5*` glob, appending an explicit "commit after one short
  analysis paragraph; treat editor_state events as pre-existing" rule.

### Non-Goals (this phase)

- Per-message dynamic prompt switching (chat-time A/B comparisons).
- Profile import/export between machines (later, sync feature).
- Per-tool prompt overrides (way out of scope).

### 29.1 — Data Model & Persistence

`QSettings("MidiEditor","NONE")` keyed under `AI/prompt_profiles/`:

```
AI/prompt_profiles/<id>/name              = "GPT-5.5 Decisive"
AI/prompt_profiles/<id>/system            = "<full prompt text>"
AI/prompt_profiles/<id>/append_to_default = true | false
AI/prompt_profiles/<id>/builtin           = true | false
AI/prompt_profiles/<id>/models            = JSON array of "<provider>:<modelId>"
                                            (supports "*" suffix glob)
AI/prompt_profiles/<id>/enabled           = true | false
AI/prompt_profiles/order                  = JSON array of <id> (sort)
```

**Mirrors Phase 26 favorites** — same `provider:model` key shape, same
glob pattern, same per-provider grouping. New class:

```cpp
src/ai/PromptProfileStore.{h,cpp}   // analogous to ModelFavorites
```

Public API:

```cpp
QList<PromptProfile> profiles() const;
PromptProfile resolveForModel(const QString &provider, const QString &model) const;
QString resolvePromptForModel(const QString &provider, const QString &model,
                              const QString &defaultPrompt,
                              const QString &userCustom) const;
void upsert(const PromptProfile &p);
void remove(const QString &id);
```

Resolution order in `resolvePromptForModel`:
1. Enabled profile whose `models[]` glob matches → if `append_to_default`,
   return `defaultPrompt + "\n\n" + profile.system`; else return
   `profile.system`.
2. No profile match → existing behaviour: user custom OR default.

### 29.2 — UI: Prompt Profiles Dialog

New dialog `src/gui/PromptProfilesDialog.{h,cpp}` modelled exactly on
`ModelFavoritesDialog` (1.5.0):

- **Left pane**: profile list with checkboxes (enabled), Add/Duplicate/
  Delete buttons. Built-ins appear with a lock icon and cannot be
  deleted/edited (only duplicated).
- **Right pane (top)**: name field, "append to default" checkbox,
  monospace prompt editor with token-count footer (reuses the
  Model Favorites token preview helpers).
- **Right pane (bottom)**: model picker — same `QTreeWidget`
  Provider → Model layout as Model Favorites, with a search box and
  per-row checkbox to bind to this profile. Models already shown in
  Model Favorites cache are reused (no extra fetch).

Reachable from:
- Settings → AI tab → button **"Prompt Profiles…"** below the existing
  "Custom system prompt" textarea.
- MidiPilot sidebar → ⚙ menu → **"Prompt Profiles…"** (same shortcut
  as Model Favorites).

### 29.3 — Integration in `MidiPilotWidget::buildSystemPrompt()`

Single line change at the existing custom-vs-default decision point:

```cpp
QString prompt = _profileStore->resolvePromptForModel(
    aiClient->provider(), aiClient->model(),
    /*default*/ DEFAULT_SYSTEM_PROMPT,
    /*userCustom*/ _customPromptEdit->toPlainText());
```

Sidebar status line shows: *"Prompt: GPT-5.5 Decisive (auto-bound)"*
when a profile resolved, else *"Prompt: Default"* / *"Prompt: Custom"*.

### 29.4 — Built-in Profile: GPT-5.5 Decisive

Shipped read-only with `builtin=true`. Bound to `openai:gpt-5.5*` and
`openrouter:openai/gpt-5.5*`. `append_to_default=true`. Body:

```
GPT-5.5 OPERATING MODE:
- After at most ONE short analysis paragraph (≤3 sentences), you MUST
  emit a tool call with concrete arguments. Do not re-analyze.
- Treat events already present in `editor_state` as pre-existing user
  data. Do NOT assume they are output of your previous tool calls.
- If you are unsure about ticks per measure, read it ONCE from
  `editor_state.ticksPerQuarter` and `timeSignature` and commit; do
  not re-derive it across turns.
- When a previous tool returned successfully, the change is applied —
  do not "redo" or "fix" it unless the user explicitly says so.
```

This is exactly the rule set the 38-step log proves the model needs,
and it does not affect any other model.

### 29.5 — Tests

`tests/test_prompt_profiles.cpp` (new, mirrors
`test_model_favorites.cpp`):

1. `resolve_returnsDefaultWhenNoProfileMatches`
2. `resolve_returnsProfileBodyWhenMatching` (replace mode)
3. `resolve_appendsToDefaultWhenAppendFlagSet`
4. `resolve_globMatch_gpt55StarMatchesGpt55ProDated`
5. `resolve_disabledProfileIsIgnored`
6. `persist_roundTripsAcrossStoreInstances`
7. `builtin_cannotBeDeleted` (delete returns false, store unchanged)
8. `userCustomTakesPrecedenceOverDefaultButNotOverProfile` ordering

Plus a smoke test that the **GPT-5.5 Decisive** built-in is registered
on first launch and resolves for `openai:gpt-5.5-pro-2026-04-23`.

### 29.6 — Files to Add / Modify

| File | Section |
|------|---------|
| `src/ai/PromptProfileStore.{h,cpp}` | NEW — 29.1 store + glob resolve |
| `src/ai/PromptProfile.h` | NEW — POD struct |
| `src/gui/PromptProfilesDialog.{h,cpp}` | NEW — 29.2 dialog (mirrors Model Favorites) |
| `src/gui/MidiPilotWidget.{h,cpp}` | 29.3 — resolve hook + sidebar status |
| `src/gui/AiSettingsWidget.{h,cpp}` | 29.2 — "Prompt Profiles…" button |
| `tests/test_prompt_profiles.cpp` | NEW — 29.5 test target |
| `tests/CMakeLists.txt` | NEW test target wiring |
| `Planning/02_ROADMAP.md` | this entry |
| `CHANGELOG.md` | Phase 29 line under 1.5.0 |
| `manual/midipilot.html` | short "Prompt Profiles" subsection |

### Acceptance Criteria for 1.5.0 (additive)

1. With `AI/prompt_profiles/` empty (fresh install except the
   built-in), GPT-5.5 produces ≤6 reasoning turns and at least one
   `insert_events` for the same prompt that hit 38 turns / 0 notes
   pre-feature. Reproduced from the 2026-04-25 log.
2. Disabling the built-in profile restores the old behaviour (proves
   it's the profile that fixes it, not unrelated changes).
3. Switching from a bound model (`gpt-5.5-pro-2026-04-23`) to an
   unbound one (`gpt-5.4`) on the same conversation reverts to the
   default prompt for the next turn.
4. Sidebar status updates live when the model is changed.
5. No regression in tests for `ModelFavorites`, `StreamingFallback`,
   `ProviderMatrix`.

---

## Phase 30: Lightweight Agent Conductor & Working State — DONE in 1.5.x

**Implemented:** `AgentRunner` now keeps a compact program-owned working state,
classifies each run as composition/edit/analysis/repair, injects a request-local
`Current Agent State` developer/system layer before every model round, converts
pitch-bend-only and duplicate-write rejections into next-turn steering, and logs
the state as `[AGENT-STATE]`. Added `tests/test_agent_runner_state.cpp` covering
classification, successful tool summaries, pitch-bend rejection steering,
duplicate-write failure tracking, and non-growing request-local injection.

### Why

Phase 29 proved that per-model prompt profiles help, but the GPT-5.5
Agent Mode failure is not only a prompt wording problem. Real logs from
2026-04-25 show a split behaviour:

- **Simple Mode succeeds:** GPT-5.5 can generate a dense, multi-track FFXIV
  lofi arrangement in one large response when it gets a stable one-shot JSON
  task.
- **Agent Mode fails:** the same model can enter multi-turn tool loops,
  forget freshly returned tool results, and emit placeholder-like
  `pitch_bend`-only write calls despite explicit instructions.

This means the model is strong as a composer/planner but weak as a repeated
tool-loop executor when the context is just an ever-growing chat transcript.
The next hardening step should therefore be a small runtime conductor around
the existing `AgentRunner`, not a larger model-specific tool API.

The design borrows the useful parts of a dynamic prompt-layer / dual-agent
architecture, but keeps MidiPilot maintainable:

- keep the existing tools and schemas;
- keep Pitch Bend supported;
- do not build a new special API for GPT-5.5;
- add a compact, program-owned working state that is injected before every
  agent round;
- make composition tasks use fewer, more coherent tool rounds.

### Weaknesses Found in the Current Agent Loop

`AgentRunner` currently stores only `_messages`, `_tools`, `_currentStep`,
retry counters, and a duplicate-write signature. That is enough to replay the
API protocol, but not enough to actively steer a brittle model.

Observed weak spots:

1. **No program-owned working memory.** Tool results are appended as raw
  `role:"tool"` messages, but there is no compact authoritative state like
  "tempo set", "tracks created", "track 1 write rejected", or "bars 1-16
  completed". GPT-5.5 can therefore re-derive or ignore recent facts.
2. **Tool feedback is too local.** Rejections contain guidance, but the next
  request has no separate high-priority layer that says "last action was
  rejected; choose a different valid action". The model may continue from its
  older internal plan.
3. **Loop history grows but does not summarize.** The agent transcript gains
  assistant/tool messages, including long reasoning and large event payloads,
  but does not distill progress into a small stable summary.
4. **Composition tasks are over-decomposed.** Simple Mode shows GPT-5.5 can
  create large coherent musical blocks. Agent Mode often asks it to perform
  many small execution rounds, which increases the chance of drift.
5. **Prompt profiles are static.** Phase 29 profiles are selected by model,
  but they do not adapt to current task type, last tool result, or failure
  history.
6. **Safety guards are reactive.** Duplicate-call and pitch-bend-only guards
  prevent damage, but they do not yet convert the failure into a stronger
  next-step frame.

### Design Goal

Add a lightweight conductor layer inside `AgentRunner`:

```cpp
struct AgentWorkingState {
   QString goal;
   QString taskType;          // composition / edit / analysis / repair
   QString confirmedState;    // program-owned facts from successful tools
   QString lastToolResult;    // compact result or rejection summary
   QString activeConstraints; // current hard rules for the next step
   QString nextStepHint;      // one short steering sentence
   int repeatedFailureCount = 0;
};
```

Before each model request, the conductor injects a short dynamic layer above
the rolling conversation:

```text
## Current Agent State
Goal: Create a 2-minute FFXIV lofi octet.
Task type: composition.
Confirmed state: tempo set to 82 BPM; 8 tracks created; bars 1-16 inserted.
Last tool result: replace_events on Track 3 succeeded, inserted 384 events.
Active constraints: do not repeat a rejected write call; Pitch Bend is allowed
only when musically requested and must not be used as a placeholder.
Next step: continue with bars 17-32 for strings and flute, or finish if complete.
```

This layer is not trusted user text; it is generated by the app from tool
results and editor state. It gives GPT-5.5 the same kind of stable execution
context that robust agent systems use, without requiring a second model by
default.

### Non-Goals

- No new special-purpose GPT-5.5 API.
- No removal of `pitch_bend` from event schemas.
- No mandatory dual-agent architecture in 1.5.x.
- No large prompt hierarchy UI in this phase.
- No full deterministic MIDI composer replacing the model.

Dual-agent routing remains a possible future Phase 31 if the lightweight
conductor is not enough: GPT-5.5 could become the composer/planner while a
more reliable model such as GPT-5.4 compiles tool calls. Phase 30 should first
test whether stronger orchestration fixes the issue with one model.

### 30.1 — Task Classification

Add a small heuristic classifier at agent-run start:

| Task Type | Examples | Steering Policy |
|-----------|----------|-----------------|
| `composition` | compose, create song, arrange, generate lofi, FFXIV octet | fewer rounds, substantial writes per section/track |
| `edit` | change notes, add bassline, transpose, humanize | inspect only if needed, one corrected write per target |
| `analysis` | what key, what chords, explain track | prefer query/info tools, finish with text |
| `repair` | fix channels, validate FFXIV, clean drums | deterministic tools first, validate after |

Implementation:

- Add a private `classifyTask(userMessage, systemPrompt)` helper in
  `AgentRunner`.
- Seed `AgentWorkingState.goal` from the current user message.
- Store the task type in logs as `[AGENT-STATE] task=composition ...`.

### 30.2 — Working-State Updates from Tool Results

After each `ToolDefinitions::executeTool(...)` result, update the working
state with compact program-owned facts:

- `create_track` success -> append `Created track <index> "<name>"`.
- `set_tempo` success -> append `Tempo set to <bpm> BPM`.
- `insert_events` / `replace_events` success -> append track index and inserted
  event count; for composition tasks also infer tick range when present.
- `query_events` / `get_editor_state` success -> summarize counts, not payload.
- rejected pitch-bend-only write -> set `lastToolResult` and
  `nextStepHint = "Replace the rejected write with program_change + note events; do not retry pitch_bend-only."`.
- duplicate write rejection -> increment `repeatedFailureCount` and force a
  different next action.

Keep this summary small: target <1200 characters total. Older facts can be
coalesced (e.g. `Tracks created: Piano, Bass, Drums, Lead`) instead of growing
forever.

### 30.3 — Dynamic State-Layer Injection

Do not permanently append state layers to `_messages`, or the conversation will
balloon. Instead, build a request-local message array:

```cpp
QJsonArray AgentRunner::messagesForNextRequest() const;
```

Flow:

1. `_messages` remains the canonical protocol transcript.
2. `sendNextRequest()` calls `messagesForNextRequest()`.
3. The helper copies `_messages` and inserts one synthetic high-priority state
  message immediately after the developer/system prompt.
4. The synthetic state layer is regenerated every turn from
  `AgentWorkingState`.

For reasoning models where the first message role is `developer`, the state
layer should also use `developer` (or be folded into the first developer
message) so it is not treated as user-provided content.

### 30.4 — Composition Cadence Policy

For `taskType == composition`, change the steering from "many small tool
turns" to "few coherent sections":

- Prefer one substantial `insert_events` or `replace_events` call per track or
  per musical section.
- Avoid inspect-after-every-phrase loops unless a previous tool failed.
- After all requested tracks/sections are confirmed, ask for a final summary
  instead of another write.
- If output size is at risk, write the smallest complete coherent version
  rather than an incomplete long one.

This preserves the successful Simple Mode behaviour while still using Agent
Mode tools for actual editor changes.

### 30.5 — Failure-to-Steering Conversion

Turn reactive guards into active next-turn steering:

- Pitch-bend-only rejection becomes a state-layer constraint for the next turn,
  not only a raw tool result.
- Duplicate write rejection states the exact blocked signature and requires a
  different tool call or final answer.
- Empty response / malformed tool retry hints should include the current
  working-state summary so the model resumes from confirmed facts.
- If `repeatedFailureCount >= 2`, stop with a useful user-facing explanation
  and preserve the partial successful changes under the existing protocol
  behaviour.

### 30.6 — Diagnostics & Tests

Add logs that make the conductor inspectable:

```text
[AGENT-STATE] step=4 task=composition confirmed="tempo 82; tracks 1-4" last="insert_events ok track=1 count=96" next="write bass track"
```

Tests:

1. Unit test `classifyTask` for composition/edit/analysis/repair phrases.
2. Unit test working-state update from representative tool result objects.
3. Unit test pitch-bend-only rejection updates `nextStepHint` without removing
  Pitch Bend support from schemas.
4. Unit test request-local state injection does not permanently grow
  `_messages`.
5. Regression scenario from the 2026-04-25 GPT-5.5 log: after a rejected
  pitch-bend-only write, the next request contains the state-layer rejection
  summary and corrected-action hint.

### Files to Modify

| File | Section |
|------|---------|
| `src/ai/AgentRunner.h` | `AgentWorkingState`, task type enum, helper declarations |
| `src/ai/AgentRunner.cpp` | classify task, update state from tool results, inject state layer |
| `src/ai/ToolDefinitions.cpp` | optional: expose compact result fields consistently for state summarization |
| `src/ai/PromptProfileStore.cpp` | optional: reduce GPT-5.5 profile once dynamic state layer carries runtime rules |
| `tests/test_agent_runner_state.cpp` | new pure unit tests for classifier/state/injection helpers |
| `Planning/02_ROADMAP.md` | this phase |

### Acceptance Criteria

1. GPT-5.5 Agent Mode no longer loops on the reproduced 2026-04-25
  composition prompt: it either creates real note events or stops after a
  bounded corrective attempt with an actionable explanation.
2. After any rejected write call, the next API request includes a concise
  `Current Agent State` layer with the rejection and a different next-step
  hint.
3. Successful tool calls are summarized into confirmed state; the model is not
  forced to infer progress only from raw tool-result JSON.
4. Composition tasks use fewer, larger coherent write rounds than before and
  do not inspect redundantly after every phrase.
5. Pitch Bend remains present and supported in event schemas; only
  pitch-bend-only placeholder writes are rejected.
6. No regression in `test_tool_definitions`, `test_prompt_profiles`,
  `test_streaming_fallback`, or provider streaming paths.

---

## Phase 31: GPT-5.5 Model-Isolation Policy — DONE in 1.5.0

> **Goal:** After Phase 30's lightweight conductor proved that GPT-5.5 still
> needs *additional* mitigations beyond a generic state layer, isolate **all**
> GPT-5.5-specific behaviour behind one central policy table so every other
> model is byte-identical to the previous behaviour. No more sprinkled
> `if (model.startsWith("gpt-5.5"))` branches across the agent code path.

### Background

Phase 29 (Prompt Profiles) and Phase 30 (Conductor + Working State) reduced
GPT-5.5's failure rate on long composition prompts but did not eliminate two
specific anti-patterns: it still occasionally emits `pitch_bend`-only writes
as placeholders, and on the OpenAI Responses API it still launches
parallel-tool-call fan-outs whose reasoning trees explode past the
context window. The root cause is model-specific (a known GPT-5.5 trait that
GPT-5.4 / Gemini / Claude / OpenRouter generics do not share), so the fix
should be model-specific too — and reversible the moment OpenAI ships a
fixed checkpoint.

Earlier prototypes had `gpt-5.5*` checks scattered across `ToolDefinitions`,
`AgentRunner` and `AiClient`. That is brittle (easy to add a new mitigation
that forgets one of the call sites) and risks regressing every other model
when a conditional is over-broad. Phase 31 collapses it into one table.

### 31.1 — `AgentToolPolicy` central table

`src/ai/AgentToolPolicy.{h,cpp}` is the single source of truth:

```cpp
struct AgentToolPolicy {
    bool schemaLightWriteEvents      = false; // drop pitch_bend branch in insert/replace_events
    bool sanitizeRejectionGuidance   = false; // never echo "pitch_bend" back into context
    bool forceSerialToolCalls        = false; // parallel_tool_calls:false on Responses API
    bool forceLowReasoningEffort     = false; // reasoning.effort:low on Responses API
    int  boundedFailureStop          = 2;     // halt after N consecutive incomplete writes
};

bool isGpt55Model(const QString &model, const QString &provider);
AgentToolPolicy policyForModel(const QString &model, const QString &provider);
```

Resolution rules:
* `provider == "openai"` AND `model.startsWith("gpt-5.5")` → all five mitigations on.
* `provider == "openrouter"` AND model is `openai/gpt-5.5*` → schema-light + sanitised
  guidance + bounded-failure stop on; the two API-body fields (`parallel_tool_calls`,
  `reasoning.effort`) stay off because they are Responses-API specific and OpenRouter
  proxies through Chat-Completions.
* All other models → default-constructed policy = no-op = byte-identical to the
  pre-Phase-31 behaviour.

### 31.2 — Wiring

Single consumer per concern:

| Mitigation | Consumer | Where |
|------------|----------|-------|
| `schemaLightWriteEvents` | `ToolDefinitions::buildAgentTools(policy)` | called once when agent loop starts; `insert_events` / `replace_events` schemas drop the `pitch_bend` `oneOf` branch |
| `sanitizeRejectionGuidance` | `AgentRunner::processToolCalls` | rewrites failure guidance to positive-only language; never includes the literal token `pitch_bend` |
| `forceSerialToolCalls` + `forceLowReasoningEffort` | `AiClient::sendStreamingMessagesResponses` / `sendMessages` (Responses path) | injected into the request body just before the POST |
| `boundedFailureStop` | `AgentRunner` (Phase 30 conductor uses the same threshold) | already in place; policy provides the value |

### 31.3 — Mode-scoped streaming-block UI (sub-phase 31.1 in CHANGELOG)

Independent fall-out from real-world testing of `gpt-5.5-pro-2026-04-23`:
the model streams cleanly in Agent Mode but Simple Mode falls through to
`/v1/chat/completions` and gets HTTP 404. The previous Phase 27.7
session-blocklist was global and therefore poisoned Agent Mode too.

`AiClient` now keys the streaming-block by `(provider, model, mode)` where
mode ∈ `{simple, agent}` (`tools=0` / `tools=1`). Public 2-arg helpers
keep their meaning by OR-ing both modes; new 3-arg overloads
(`streamingBlockedForSession(provider, model, withTools)`,
`markStreamingUnsupported(provider, model, withTools, reason)`) drive the
per-mode logic. Both Agent and Simple sender pre-checks query the
mode-aware variant; `tryStreamingFallback` records the block as
`withTools = !wasSimple`.

UI (Settings model dropdown, MidiPilot footer dropdown, Force Streaming
button) shows which mode is blocked: `⚠ <model> (Simple)` / `(Agent)` /
`(Simple+Agent)` with matching tooltips and button labels.

### 31.4 — Tests

`tests/test_agent_tool_policy.cpp` — 13 cases:
1. `isGpt55Model_openai_gpt55ProDated_returnsTrue`
2. `isGpt55Model_openai_gpt54_returnsFalse`
3. `isGpt55Model_openrouter_gpt55_returnsTrue`
4. `isGpt55Model_openrouter_gpt54_returnsFalse`
5. `isGpt55Model_anthropic_anyModel_returnsFalse`
6. `policyForModel_nonGpt55_isDefaultConstructed`
7. `policyForModel_openai_gpt55_allMitigationsOn`
8. `policyForModel_openrouter_gpt55_apiBodyFieldsOff`
9. `schemaLightWriteEvents_dropsPitchBendBranch`
10. `schemaLightWriteEvents_keepsNoteOnNoteOffBranches`
11. `sanitizedGuidance_neverContainsPitchBendToken`
12. `responsesApiBody_includesParallelToolCallsFalse_onPolicy`
13. `responsesApiBody_includesReasoningEffortLow_onPolicy`

Plus 4 schema-light cases in `tests/test_tool_definitions.cpp` and a
`QSKIP`'d historical entry in `tests/test_agent_runner_state.cpp` for the
removed pre-Phase-31 universal `pitch_bend`-only working-state branch.

### Acceptance Criteria (met)

1. Non-GPT-5.5 runs are byte-identical to v1.4.x behaviour — Pitch Bend
   stays in schemas, no rejection-guidance rewriting, no API-body overrides.
2. GPT-5.5 on OpenAI-native completes the reproduced 2026-04-25 composition
   prompt with bounded reasoning depth (≤6 turns) and inserts real notes
   instead of `pitch_bend`-only placeholders.
3. GPT-5.5 on OpenRouter benefits from the schema/prompt mitigations
   without the Responses-API-specific body fields being sent (which
   OpenRouter would reject).
4. Streaming failure on `gpt-5.5-pro-2026-04-23` Simple Mode no longer
   disables streaming for Agent Mode on the same model.

### Files

* `src/ai/AgentToolPolicy.{h,cpp}` — NEW
* `src/ai/ToolDefinitions.{h,cpp}` — `buildAgentTools(policy)` overload
* `src/ai/AgentRunner.{h,cpp}` — `_policy` member, `sanitizeRejectionGuidance` consumer
* `src/ai/AiClient.{h,cpp}` — Responses-API body injection; mode-aware streaming-block API
* `src/gui/AiSettingsWidget.cpp` / `src/gui/MidiPilotWidget.cpp` — per-mode warning markers
* `tests/test_agent_tool_policy.cpp` — NEW

---

## Phase 32: FFXIV Voice Limiter / Awareness — DONE in 1.6.0

**Implemented:** Headless `FfxivVoiceAnalyzer` (per-file cache, debounced
re-analysis on `Protocol::actionFinished`) backing two visualisers — the
toolbar `FfxivVoiceGaugeWidget` (24-segment stereo-style LED meter, fixed
left `N/16` readout, reserved `+N` overflow slot, dedicated tick mark at the
documented 16-voice ceiling) and the piano-roll `FfxivVoiceLaneWidget`
(auto-scaled Y-axis, soft / red threshold colouring, per-channel rate
hot-spot strip, dashed red ceiling line drawn on top with halo + `16` tag).
Sample-tail simulation in `FfxivVoiceLoadCore::sampleTailMs()` extends each
NoteOn..NoteOff window by an estimated audible release per GM family / drum
pitch so the voice count matches in-game / MogNotate observations instead of
under-reporting on plucked instruments. Visual thresholds (`green ≤18`,
`yellow 19–23`, `red ≥24`) are intentionally relaxed from the documented
hard ceiling per empirical in-game testing; the analyser still reports the
raw count and overflow against `16` for the AI tool. The Phase-32.2 matrix
tint overlay was removed in v1.6.0 because it was visually unreadable on
dense scores; the lane below the velocity lane is the primary visualiser.
Agent-side `analyze_voice_load` tool wired through `ToolDefinitions`
(test-stubbed via `TOOLDEFINITIONS_TEST_STUB_FFXIV` so
`test_tool_definitions` links without the full Qt analyser stack).

> *"Note: numbered as a new top-level Phase rather than `31.x` because Phase 31
> sub-sections (`31.1`–`31.4`) are already taken by the GPT-5.5 model-isolation
> work. Topically unrelated; deserves its own phase."*

### Why

In FFXIV's *Performance Mode* (Bard ensembles) the in-game audio engine has
hard, observable ceilings that silently truncate or drop notes from the player's
arrangement. The MIDI looks fine; what comes out of the game does not. Three
constraints matter for arranging:

| Constraint | Limit | Source | Symptom in-game |
|------------|-------|--------|-----------------|
| **Polyphony / voice ceiling** | **16 active voices total**, across the whole ensemble (not per player) | `https://ffxiv.consolegameswiki.com/wiki/Performance_Actions`; community confirmation (Reddit, BMP) | Older notes get cut off; wind/brass becomes inaudible during dense passages |
| **Note rate** | **≤ 14 notes / second per channel**, ~50 ms minimum interval (locked-60 fps assumption) | Same wiki page | Notes beyond the rate are dropped or merged |
| **Range** | **C3..C6** (MIDI 48..84) | Same wiki page | Out-of-range notes are auto-transposed by the client |
| **No native chords** | One key press = one note; arpeggiate or use Power Chord instruments | Same wiki page | Stacked notes on the same key collapse |

We already enforce **range** in the FFXIV Channel Fixer + bard accuracy path
(BARD-DRY-001 + Phase 3 percussion). What's missing is **predictive feedback**
on the polyphony ceiling and the note-rate ceiling *while the user is editing*,
so they can see where the arrangement would be silently degraded by the game
client before they ever upload it.

This phase introduces the **FFXIV Voice Limiter** (working name; alias
*FFXIV Voice Awareness*) — a passive analyser + two visualisers that surface
voice-count and note-rate hot-spots in real time, in the same idiom as the
existing **MIDI Visualizer** and **Lyric Visualizer** toolbar widgets and the
existing piano-roll *Show Program Markers* / *Show Control Markers* /
*Show Text Events* overlays.

### What it does — feature surface

#### 32.1 — Toolbar widget: live voice gauge

Companion to `MidiVisualizerWidget` and `LyricVisualizerWidget`. Lives in the
toolbar, registers as a customisable action (`ffxiv_voice_gauge`) in
`LayoutSettingsWidget`.

* Compact ~120×25 px widget with two areas:
  * **Left**: large coloured pill / LED — green when current playhead voice
    count ≤ 16, red when > 16.
  * **Right**: numeric overflow count, e.g. `+0` (green) or `+3` (red) meaning
    "3 notes over the 16-voice ceiling at the playhead".
* Tooltip: `Voices: 14 / 16 (max in piece: 22 at bar 17 beat 3.2)`.
* During playback: updates at the playback timer's repaint cadence (already
  ~30 Hz). When stopped: tracks `MatrixWidget`'s playhead / cursor position so
  hovering / clicking reveals voice load at any tick.
* Click toggles the piano-roll *voice-load lane* (32.3 below).
* Honours the existing FFXIV mode toggle: hidden / disabled when FFXIV
  SoundFont Mode is off (matches `XIV` toolbar widget pattern from 1.5.2).

#### 32.2 — Inline matrix overlay (per-bar tinting)

Same toggle pattern as `MatrixWidget::_div_program_visible` /
`_div_control_visible` / `_div_text_visible`:

* **View → Show Voice Load** (action id `view_voice_load`, persisted under
  `View/showVoiceLoad`).
* When enabled, `MatrixWidget::paintEvent()` tints the background of each bar
  (or each rendered tick column at high zoom) with a translucent green / red
  gradient based on peak voice count in that bar.
* Two tint thresholds:
  * **Green** (≤ 12 voices) — comfortable headroom.
  * **Yellow** (13..16 voices) — at the limit but legal.
  * **Red** (> 16 voices) — game will drop notes.
* Optional second pass for **note-rate hot-spots**: any 250 ms window with
  > 14 notes on the same channel gets a thin red vertical hatch overlay.

#### 32.3 — Voice-load lane (graph under the piano roll)

Companion lane to the velocity lane. Toggled from the toolbar widget click and
from the View menu (action `view_voice_lane`, persisted under
`View/showVoiceLane`).

* Renders a 16-row bar chart sampled per pixel column (or per beat, whichever
  is finer at the current zoom):
  * 16 vertical bars, one per simultaneous voice slot.
  * Bars filled green when total ≤ 16.
  * Bars 17+ stack above the 16-row ceiling marker, painted **red**.
  * Optional thin numeric label for any column with overflow ("18", "22", …).
* Click on a column scrolls / cursors the matrix to that tick — same idiom as
  the protocol panel's "Jump to event" entries.

#### 32.4 — Analyzer engine (`FfxivVoiceAnalyzer`)

The headless engine that backs all three visualisers. Lives in
`src/ai/` next to `FFXIVChannelFixer.{h,cpp}`.

* **Inputs**: `MidiFile *file`, optional `int previewWindowMs` (default 250 ms).
* **Outputs** (cached per file, invalidated on edit):
  * `QVector<int> voiceCountAtTick` — sparse, only at tick boundaries where
    NoteOn/NoteOff land. Built once via a single pass:
    1. Walk every NoteOn / OffEvent across all 16 standard channels (skip 16/17/18 meta channels).
    2. Increment / decrement a running counter at each event tick.
    3. Snapshot `(tick, count)` pairs.
  * `QVector<int> peakInBar(barIndex)` — convenience derived from the above.
  * `QVector<RateHotspot>` — `{channel, startTick, endTick, notesPerSecond}`
    entries for any 250 ms window exceeding 14 notes/s.
  * `int globalPeak` — overall maximum.
* **Update strategy**: connect to `Protocol::actionFinished` (existing signal).
  Re-analyse async on a `QTimer::singleShot(0, ...)` debounced 100 ms so a
  multi-event paste / agent tool call only triggers one rebuild. Keep results
  in a `QHash<MidiFile*, AnalysisResult>` cache cleared on file close.
* **Note-rate detection**: per-channel sliding window. Uses a `std::deque<int>`
  of NoteOn ticks per channel; on each new NoteOn, drop entries older than
  `(currentTick - 250 ms)` and check `size()`.
* **Drum-channel handling**: CH9 voice-counting follows the same rules — a kick
  + snare hit *is* two simultaneous voices in the in-game engine, not one.

#### 32.5 — Settings + thresholds

**Settings → MIDI → FFXIV → "Voice Limiter"** group:

* `[x]` Enable Voice Awareness widgets — default `true`. **Auto-bound to
  FFXIV SoundFont Mode**: enabling FFXIV Mode flips this on (and shows the
  toolbar gauge + matrix tint + voice lane); disabling FFXIV Mode flips it
  off (and hides the widgets and stops the analyser entirely so non-FFXIV
  users pay zero perf cost). Wired the same way as the bard-accurate-mode
  auto-bind from 1.5.3 (BARD-MODE-001): `FluidSynthEngine::ffxivSoundFontModeChanged(bool)`
  drives `setVoiceAwarenessEnabled(on)`. The user can still override:
  manually unchecking this box while FFXIV Mode is on persists the OFF
  choice (`FFXIV/voiceLimiter/userOverride = "off"`) and the auto-bind
  no longer flips it back; clearing the override (or a fresh install /
  reset-to-defaults) restores the auto-bound behaviour.
* Voice ceiling: `16` (read-only number; matches game)
* Yellow-warn threshold: `13` (spinner 1..15)
* Note-rate ceiling: `14 notes/s` (read-only)
* Note-rate window: `250 ms` (spinner 100..1000)
* Persisted under `FFXIV/voiceLimiter/*`.

#### 32.6 — Optional: agent-side awareness

Stretch goal — not required for shipping the visualiser:

* New read-only AI tool `analyze_voice_load(start_tick, end_tick)` that returns
  `{globalPeak, overflowRanges[], rateHotspots[]}`. Lets the agent self-check
  its compositions against the 16-voice ceiling and rewrite dense passages
  without the user having to ask.
* Wire into `AgentWorkingState.activeConstraints` when FFXIV mode is on:
  `"FFXIV: ≤16 voices, ≤14 notes/s/channel, C3..C6"` so the model is reminded
  every turn.

### Implementation plan — files

* `src/ai/FfxivVoiceAnalyzer.{h,cpp}` — NEW headless engine (32.4)
* `src/gui/FfxivVoiceGaugeWidget.{h,cpp}` — NEW toolbar widget (32.1)
* `src/gui/MatrixWidget.{h,cpp}` — `_voice_load_visible` flag + tint pass + new
  voice-load lane below the existing velocity lane (32.2 + 32.3)
* `src/gui/MainWindow.{h,cpp}` — register `view_voice_load` /
  `view_voice_lane` / `ffxiv_voice_gauge` actions, wire into View menu and
  toolbar; persist under `View/show*`
* `src/gui/LayoutSettingsWidget.cpp` — register `ffxiv_voice_gauge` in toolbar
  registry + default order
* `src/gui/MidiSettingsWidget.{h,cpp}` — new "Voice Limiter" group (32.5)
* `src/ai/ToolDefinitions.{h,cpp}` — optional `analyze_voice_load` tool (32.6)
* `src/ai/AgentRunner.{h,cpp}` — optional `activeConstraints` wiring (32.6)
* `tests/test_ffxiv_voice_analyzer.cpp` — NEW: deterministic fixtures
  * single-channel monophonic stays at 1 voice
  * 17-note simultaneous chord triggers `globalPeak == 17`, `overflow == 1`
  * fast 16-note arpeggio at 60 BPM 1/32 → 32 notes/s on one channel triggers
    a rate hotspot
  * NoteOn / NoteOff pairs straddling bar boundaries count correctly
  * empty file returns `globalPeak == 0` and no hotspots
* `manual/ffxiv-voice-limiter.html` — NEW manual page
* `manual/menu-view.html` — add `Show Voice Load` / `Show Voice Lane` rows
* `manual/menu-tools.html` — add the toolbar gauge row
* `manual/soundfont.html` — cross-link from the FFXIV Mode section

### Acceptance criteria

1. Loading a known-overflowing MIDI (e.g. dense Lute trio) shows the gauge
   widget pulsing red and the matrix tinted red in the dense bars without the
   user enabling anything beyond FFXIV Mode.
2. Toggling **View → Show Voice Load** off restores the matrix to the current
   1.5.3 appearance byte-identically (no perf or paint regression on files
   with no overflow).
3. Editing a single note (delete the top voice in the dense bar) re-analyses
   within ~100 ms and the bar flips from red to yellow / green live.
4. The voice-load lane stays in sync with horizontal scroll / zoom, same as
   the velocity lane.
5. With FFXIV Mode **off**, none of the new widgets are visible and the
   analyser does not run (zero perf impact for non-FFXIV users).
6. Headless analyser passes the test fixtures listed above.
7. Note-rate detection correctly flags a single channel hitting > 14 NoteOns
   in any 250 ms window; does **not** flag the cross-channel total
   (game limit is per-channel rate, ensemble-wide voice ceiling).

### Open questions / deferred

* **Power Chord instruments** (Bongo, Snare, Cymbal — the FFXIV percussion
  presets that play chords from a single key) — should those count as 1 voice
  or as the chord size in the analyser? Default: count as 1, matching what the
  game engine reports.
* **Multi-bard ensembles** — the 16-voice ceiling is *ensemble-wide* in the
  game, but in the editor we only see one player's MIDI. Document that the
  analyser shows the **per-file** ceiling and that real multi-bard sessions
  divide the budget across players.
* **Animation-mismatch** (newer client renders fluid animations not matching
  audio rate) is purely visual in-game and not addressable from the editor —
  out of scope.
* **Frame-rate dependency** of the 50 ms input interval (locked 60 fps
  assumption) — surface as a tooltip note rather than a hard analyser rule.

### Working names

* **FFXIV Voice Limiter** (preferred — matches FFXIV Channel Fixer naming).
* **FFXIV Voice Awareness** (alias — softer, emphasises the passive nature).

---

### 32.7 — Empirical voice-load model (v1.6.0 follow-up)

After the first build of 32.4 shipped a strict NoteOn..NoteOff voice
counter, in-game testing revealed two systematic problems:

1. **Counter under-reports vs. MogNotate.** The MIDI editor counted
   ~6–8 voices on dense Harp/Lute passages where MogNotate showed
   18–22 active voices.  Reason: a "voice" in the FFXIV mixer keeps
   ringing for the *full sample release tail* after NoteOff (plucked
   strings ~0.5 – 1 s), but our counter dropped voices the moment the
   user released the key.
2. **Visual thresholds were too strict.** Even after fixing (1), the
   gauge / lane painted entire arrangements solid red while those same
   arrangements played cleanly in-game with no audible cut-offs.  The
   documented "16 active voices" ceiling appears to be the *eviction
   trigger*, not the *audibility breakpoint*.

#### Sources consulted

| Source | Finding | Used for |
|--------|---------|----------|
| Square-Enix patch 4.15 notes | "Each action corresponds to one musical note." | Confirms no native chords |
| Square-Enix patch 5.1 notes | Ensemble Mode delays party member instruments | Confirms global delay, no ms figure |
| FFXIV ConsoleGames Wiki | Range C3–C6, ≤14 notes/s, 50 ms min interval @ 60 fps | Per-channel rate limit + range |
| Square-Enix forum bugreport | "Game allows only 16 active voices; sustained Flute/Brass cut off when Harp/Lute/Drum exceed it" | 16-voice eviction threshold |
| User-suggestion in same report | Forum users propose 32–64 as a "better" limit | Hint that ~32 is the practical breakage point |
| MidiBard2 Q&A (GitHub) | Excess notes are *queued and delayed*, not silently dropped | Eviction is graceful → headroom above 16 is acceptable |
| AllaganHarp (GitHub) | 35 ms aggressive / 50 ms safe arpeggio spacing; reverb tails are MIDI-length-independent | Audible tail ≠ sample length; arpeggio presets |
| LightAmp / BMP `MinimumLength()` table | Per-instrument *playback sample lengths* (1.1 – 1.7 s) | NOT directly usable as voice tails — these extend short MIDI notes so the sample plays out, they are not perceptual decay times |

#### Threshold model adopted in v1.6.0

The documented 16-voice ceiling is preserved as a reference (dashed
red line in the lane, `16` denominator in the gauge tooltip), but the
visual colour bands reflect *practical audibility* derived from the
sources above:

| Band | Voices | Meaning | Rationale |
|------|--------|---------|-----------|
| **Green** | ≤ 18 (gauge) / ≤ 18 (lane) | Safe in practice | ~10 % headroom over docs; MidiBard2 queue compensates |
| **Yellow** | 19–23 (gauge) / 19–23 (lane) | Over docs but typically clean | Bugreport: cut-off only when *sustained* notes coexist with continuous high load |
| **Red** | ≥ 24 (gauge & lane) | Likely audible voice drops | ~1.5× over docs; community consensus on practical breakage |

Constants live in:
* `FfxivVoiceGaugeWidget.cpp` → `kVisualGreen=18`, `kVisualYellow=23`
* `FfxivVoiceLaneWidget.cpp` → `kSoftWarn=19`, `kRedThreshold=24`

These can be tuned without touching the analyser or schemas.

#### Audible-tail model (`FfxivVoiceLoad::sampleTailMs`)

Each voice is counted as active from NoteOn until
`max(noteEnd, noteOn + sampleTailMs(program, pitch, isDrum, durationMs))`.
The tail values are **estimated audible release** (~time to drop below
−40 dB), roughly half of BMP's `MinimumLength` because BMP's table
extends sample *playback*, not perceptual decay.

| Family | GM Range | Audible tail (ms) |
|--------|----------|-------------------|
| Piano | 0–7 | 500 – 700 |
| Chromatic perc. | 8–15 | 500 |
| Organ | 16–23 | max(noteLen, 400) |
| Lute / Acoustic Guitar | 24–31 | 700 – 900 (pitch-dependent) |
| Bass | 32–39 | 700 |
| Bowed strings | 40–45 | max(noteLen, 300), capped 4500 |
| Orchestral Harp | 46 | 600 – 800 (pitch-dependent) |
| Timpani | 47 | 700 – 800 |
| Ensemble strings | 48–55 | max(noteLen, 300), capped 4500 |
| Brass | 56–63 | max(noteLen, 300), capped 4500 |
| Reed | 64–71 | max(noteLen, 500), capped 4500 |
| Pipe / Flute | 72–79 | max(noteLen, 500), capped 4500 |
| Synth lead/pad | 80–95 | max(noteLen, 400) |
| Percussive (96–119) | 112–119 | 500 |
| SFX | 120–127 | 350 |
| Drums (ch 9) | per-pitch | Kick 300, Snare 200, Tom 280, ClosedHat 250, OpenHat 600, Cymbal 1200, Bongo 350 |

These are heuristic starting values, **not measured**.  A future Phase
32.8 should record per-instrument samples in-game and re-derive them
empirically (RMS / peak vs. −40 dB threshold).

#### What 32.7 did not change

* The **range check** (C3..C6) and **per-channel rate detection**
  (≤14 notes/s) are unchanged from 32.4 — they are well-documented
  limits that match in-game behaviour.
* The **agent-side `analyze_voice_load` tool** (32.6) keeps reporting
  the raw voice count and `globalPeak` so AI-generated music is
  judged against the documented limit, not the relaxed visual band.
* The **matrix tint overlay** (originally 32.2) is removed in v1.6.0 —
  it was visually unreadable on dense scores; the lane below the
  velocity lane is the primary visualiser.

#### Future work (32.8 — measurement-based calibration)

1. Record one note per FFXIV instrument from the in-game performance
   mode at known volume.
2. Compute the time until each sample drops below −40 dB / −60 dB.
3. Build per-key-region tables (low/mid/high) and re-emit
   `sampleTailMs` from real data.
4. Add a "Calibration" panel in Settings → MIDI → FFXIV that lets
   advanced users override the heuristic table.

---

## Phase 33: Time-Preserving Tempo Conversion — DONE in 1.6.0

### Why

Today the user has two separate operations and no good way to combine them:

* **"Change BPM"** — flips the tempo meta but leaves note ticks alone, so the
  music plays back at a different speed and a different musical length.
* **"Scale notes"** — multiplies tick positions but leaves the tempo alone,
  so the music plays back at the same speed but takes longer / shorter in
  real time and re-aligns to different bars.

Neither does what the common user case needs: take a vocal MIDI recorded at
**90 BPM** and re-target it to a project that runs at **180 BPM** so the
vocal plays back **at the same real-time speed** as the original *and* lines
up with the 180 BPM grid (so quarter notes become half notes, the tempo meta
is 180 BPM, and the audible result is identical).

This is technically *event-tick scaling* + *tempo replacement* in one atomic
operation. We expose it as a single, clearly-named command so the user
doesn't have to combine two existing tools and get the maths wrong.

### Core formula

```
scale = target_bpm / source_bpm

new_tick     = round(old_tick     * scale)
new_duration = round(old_duration * scale)

tempo_meta := target_bpm
```

Worked example — 90 → 180 (`scale = 2.0`):

| Quantity              | Before        | After           |
|-----------------------|---------------|-----------------|
| `note.start`          | 480 ticks     | 960 ticks       |
| `note.length`         | 240 ticks     | 480 ticks       |
| `tempo`               | 90 BPM        | 180 BPM         |
| Real-time start (s)   | 0.333 s       | 0.333 s         |
| Real-time length (s)  | 0.167 s       | 0.167 s         |

Real playback duration is preserved; the file now lives in the host project's
180 BPM grid.

### Feature surface

#### 33.1 — Menu entry + dialog

**Tools → Tempo Tools → Convert Tempo, Preserve Duration…**

Action id `convert_tempo_preserve_duration`, registered in
`MainWindow::createMenus()` next to the existing tempo / quantization tools.

#### 33.1a — Right-click entry points

The same action is also exposed via context menus so the user can apply it
to exactly the track or channel they right-clicked, without having to first
open the Tools menu *and* re-pick the scope manually:

* **Tracks panel (`TrackListWidget`)** — right-click on a track row adds
  *Convert Tempo, Preserve Duration…* to the existing context menu
  (alongside Mute / Solo / Rename). When invoked this way:
  * Scope is pre-set to *Selected tracks*.
  * The chip strip is pre-filled with the right-clicked track plus any
    other tracks that were already multi-selected in the panel.
  * If multiple tracks are selected and the user right-clicks one of them,
    all of them are pre-filled (matches the existing Mute/Solo behaviour).
* **Channels panel (`ChannelListWidget`)** — same pattern. Right-click adds
  *Convert Tempo, Preserve Duration…*; scope pre-set to *Selected channels*
  with the right-clicked channel pre-filled.
* **Matrix / piano-roll (`MatrixWidget`)** — when at least one event is
  selected, the existing right-click menu adds *Convert Tempo, Preserve
  Duration…* with scope pre-set to *Selected events*. (Mirrors the
  current Quantize selection behaviour so the muscle memory transfers.)

All three entry points route to the same `MainWindow::onConvertTempoPreserveDuration(ScopeHint hint)`
slot; `ScopeHint` is a small POD `{ Scope scope; QSet<int> tracks; QSet<int> channels; }`
used only to seed the dialog's initial state. The dialog itself remains the
source of truth — the user can still change scope or chips before clicking
OK.

Dialog (`TempoConversionDialog`):

* **Source BPM** — `QDoubleSpinBox`, default = `currentTempoAtTick0()`
  (auto-detected; shows `(detected)` suffix in the label).
* **Target BPM** — `QDoubleSpinBox`, default = current project tempo (or the
  same value as Source if user hasn't changed the project tempo).
* **Scope** — `QComboBox`:
  * *Whole project* (default when nothing else applies)
  * *Selected tracks* (default + auto-prefilled when launched from the
    Tracks panel context menu — see 33.1a; the selected track ids are
    captured at launch time and shown as a chip strip below the combo, e.g.
    `[Track 3 "Lead Vocal"]  [Track 5 "Backing Vox"]  [×]`)
  * *Selected channels* (default + auto-prefilled when launched from the
    Channels panel context menu — see 33.1a; same chip strip,
    e.g. `[Ch 0]  [Ch 9]`)
  * *Selected events* (default when `Selection::instance()->selectedEvents()`
    is non-empty)

  The chip strip is editable: clicking the `×` on a chip removes that
  track / channel from the scope; an `+ Add…` chip opens a small picker
  populated from `file->tracks()` / `file->channels()`. This lets the user
  refine the scope without closing the dialog and re-launching from a
  different right-click.
* **Tempo handling** — `QComboBox`:
  * *Replace tempo map with target tempo* (Mode A — MVP default)
  * *Scale existing tempo map* (Mode B — v2, greyed out in MVP with a
    "Coming in v2" tooltip)
  * *Keep tempo map, scale events only* (Mode C)
* **Include** checkboxes (all default `true` for the MVP):
  * Notes (NoteOn / OffEvent)
  * Controllers (CC)
  * Pitch bend
  * Program changes
  * Channel pressure / poly aftertouch
  * Lyrics & text events (channel 16)
  * Markers (channel 16, `MARKER_TEXT` subtype)
  * Time signatures (channel 18) — *unchecked* by default; warning tooltip
    "Scaling time signatures is rarely what you want — leave unchecked
    unless you know why."
* **Quantize after conversion** checkbox — default off; when on, runs the
  existing quantize tool with the project's current grid.
* **Live preview pane** (read-only labels, recomputed on every field change):
  * `Original duration: 0:42.18`
  * `New duration: 0:42.18  (Δ +0.00 s ✓)` or `(Δ -2.13 s ⚠)` in red when
    Mode C scales but tempo isn't touched
  * `Scale factor: 2.000×`
  * `Affected events: 1247`
  * `Tempo events found in source: 1 (at tick 0)` — turns into a yellow
    warning row when > 1 (`"This file contains 4 tempo changes. Replace
    will collapse them to a single tempo at the target BPM."`)

#### 33.2 — Headless engine

`src/converter/TempoConversionService.{h,cpp}` (lives next to existing
converters like `MusicXMLImporter`):

```cpp
struct TempoConversionOptions {
    double sourceBpm = 0.0;            // 0 = auto-detect at tick 0
    double targetBpm = 0.0;            // required
    enum Scope { WholeProject, SelectedTracks, SelectedChannels, SelectedEvents };
    Scope scope = WholeProject;
    QSet<int> trackIds;                // used when scope == SelectedTracks
    QSet<int> channelIds;              // used when scope == SelectedChannels
    enum TempoMode { ReplaceFixed, ScaleTempoMap, EventsOnly };
    TempoMode tempoMode = ReplaceFixed;
    bool includeNotes        = true;
    bool includeControllers  = true;
    bool includePitchBend    = true;
    bool includeProgramChange = true;
    bool includeAftertouch   = true;
    bool includeLyricsText   = true;
    bool includeMarkers      = true;
    bool includeTimeSignature = false;  // off by default — see dialog
    bool quantizeAfter       = false;
};

struct TempoConversionResult {
    int      affectedEvents = 0;
    int      tempoEventsRemoved = 0;
    qint64   oldDurationMs = 0;
    qint64   newDurationMs = 0;
    double   scaleFactor = 1.0;
    QString  warning;       // empty on success
    bool     ok = false;
};

class TempoConversionService {
public:
    static TempoConversionResult preview(MidiFile *file,
                                         const TempoConversionOptions &opts);
    static TempoConversionResult convert(MidiFile *file,
                                         const TempoConversionOptions &opts);
};
```

#### 33.3 — Implementation rules

1. **Always scale at the absolute-tick layer.** MidiEditor already stores
   events with absolute ticks in `MidiChannel`'s `QMultiMap<tick, MidiEvent*>`
   so we never need to touch delta-time encoding — the existing
   `MidiFile::save()` writer already converts to deltas on export.
2. **Preserve NoteOn ↔ OffEvent pairing.** `OffEvent::onEvents` is a static
   `QMap` keyed by `(channel, key)` that links each `NoteOnEvent` to its
   matching `OffEvent`. The scaler must:
   * For each `NoteOnEvent`, retrieve its paired `OffEvent` via
     `noteOn->offEvent()`.
   * Scale `noteOn->midiTime()` and `offEvent->midiTime()` together so the
     pair stays consistent.
   * Never touch the `OffEvent` independently of its pair.
3. **Reuse the protocol stack** — wrap the entire operation in
   `Protocol::startNewAction("Convert tempo (preserve duration): X→Y BPM")`
   so a single Ctrl+Z reverts every event move + tempo-map rewrite.
4. **Tempo handling per mode**:
   * **ReplaceFixed (MVP default)** — collect all `TempoChangeEvent` on
     channel 17, remove them via `MidiChannel::removeEvent(ev, true)` inside
     the protocol action, then `MidiChannel::insertEvent(new TempoChangeEvent(...), 0)`
     with the target BPM at tick 0. Set `MidiFile::_initBpm = targetBpm`.
   * **ScaleTempoMap (v2)** — keep tempo events, but scale their tick
     positions by `scale` and *invert*-scale their BPM values
     (`new_bpm = old_bpm * scale`) so the real-time tempo curve is
     preserved; the resulting file plays back with the same time-stretched
     curve at the new musical-tick rate. Greyed out in MVP.
   * **EventsOnly** — do not touch tempo events; just scale event ticks.
     Useful for half-time / double-time corrections inside an existing
     project that already runs at the correct musical tempo.
5. **Rounding** — `qint64 newTick = qRound64(double(oldTick) * scale);`.
   Document that round-trip 90 → 180 → 90 may introduce ≤ 1-tick drift per
   event; provide an "After conversion: snap to grid" checkbox for users who
   want exact alignment.
6. **Negative ticks / pickup notes** — MidiEditor doesn't currently support
   negative ticks. After scaling, find `min(newTick)` across the affected
   set; if it's < 0 (only possible if the user manually edited the tempo
   table to non-zero start), shift everything by `-min` and warn.
7. **Scope filtering** — `collectEvents(opts.scope, opts.trackIds, opts.channelIds)`:
   * `WholeProject` → every event in every channel of `file->channels()`,
     respecting the `include*` flags.
   * `SelectedTracks` → events whose `MidiEvent::track()` is in the explicit
     `opts.trackIds` set seeded by the dialog (which in turn is seeded by
     either the Tracks panel selection or the right-click target).
   * `SelectedChannels` → events whose `MidiEvent::channel()` is in the
     explicit `opts.channelIds` set. **Important nuance**: meta channels
     (16 lyrics/text, 17 tempo, 18 time-sig) are *never* included implicitly
     — if the user picks Channel 0, only Channel 0's notes/CC/PB move; the
     project's tempo map and time signatures stay put. Mode A's
     `ReplaceFixed` is *disabled* in the dialog when scope is
     `SelectedChannels` because rewriting the project tempo from a single
     channel's local conversion would silently retime every other channel —
     the dialog footer explains this and forces the user into
     `EventsOnly` mode (or `ScaleTempoMap` once v2 ships).
   * `SelectedEvents` → exactly `Selection::instance()->selectedEvents()`,
     by-value semantics from v1.3.2.
8. **Drum / FFXIV percussion safety** — CH9 PC injection from the live
   playback path (BARD-DRUM-001) reads `noteOn->midiTime()` directly and
   doesn't cache; safe to scale.

#### 33.4 — UI live preview without committing

The dialog calls `TempoConversionService::preview(file, opts)` on every
field change with a 100 ms debounce. `preview` runs the full collection +
durationMs computation but **does not modify the file**; it returns the
counts and the projected duration delta. Cheap (one full-file walk on a
typical MIDI takes < 5 ms).

#### 33.5 — AI-tool integration (stretch, post-MVP)

New tool in `ToolDefinitions`:

```cpp
{
  "name": "convert_tempo_preserve_duration",
  "description": "Scale all event ticks and replace the tempo map "
                 "so the file plays back at the same real-time duration "
                 "but lives in a different musical tempo.",
  "parameters": {
    "source_bpm":   { "type": "number" },
    "target_bpm":   { "type": "number" },
    "scope":        { "type": "string", "enum": ["whole", "selected_tracks", "selected_channels", "selected_events"] },
    "track_ids":    { "type": "array", "items": { "type": "integer" }, "description": "Required when scope == selected_tracks." },
    "channel_ids":  { "type": "array", "items": { "type": "integer" }, "description": "Required when scope == selected_channels (0–15)." },
    "tempo_mode":   { "type": "string", "enum": ["replace", "scale_map", "events_only"] }
  }
}
```

Wired through `AgentRunner` so the user can say *"convert this 90 BPM vocal
to fit my 180 BPM project"* and the agent picks `target_bpm` from
`editor_state.tempo` automatically.

#### 33.6 — Edge cases & warnings

| Case | Behaviour |
|------|-----------|
| Scope = SelectedChannels + tempoMode = ReplaceFixed | Disabled in dialog. Footer: *"Replacing the project tempo from a single-channel scope would retime every other channel. Switch to Events-only or pick Whole project."* |
| Right-click on a track row in Tracks panel | Opens dialog with Scope = Selected tracks pre-filled with that track (+ any other tracks already multi-selected). |
| Right-click on a channel row in Channels panel | Opens dialog with Scope = Selected channels pre-filled with that channel. |
| > 1 tempo event in source, mode = ReplaceFixed | Yellow warning in dialog: *"Source contains N tempo changes — they will be collapsed to a single tempo at the target BPM."* |
| `sourceBpm == targetBpm` | Disable OK button, dialog footer: *"Source and target tempo are identical — nothing to do."* |
| Scope returns 0 events | Disable OK button, *"No events in scope."* |
| Round-trip drift | After-action toast: *"1247 events scaled. Round-trip back to source BPM may differ by ≤ 1 tick per event."* (only when `qFloor != qRound` for any event) |
| Time signatures unchecked but selection includes them | They stay at their original tick — visually the bar lines move relative to the notes. Warning shown. |
| `Selection::instance()->selectedEvents()` includes TimeSig / TempoChange | Honour the `includeTimeSignature` / `tempoMode` flags rather than blindly scaling — those are the user's explicit overrides. |

#### 33.7 — Tests (`tests/test_tempo_conversion_service.cpp`)

1. **Simple note** — 480/240 at 90 BPM → 960/480 at 180 BPM; real-time
   start/end identical to the second decimal.
2. **NoteOn / OffEvent pair integrity** — after conversion, every NoteOn's
   `offEvent()` still resolves to the correct OffEvent on the right tick.
3. **Multi-channel alignment** — three notes on three channels at the same
   tick stay on the same tick after conversion.
4. **CC automation** — pitch-bend and CC11 events between two notes keep
   their relative position to the surrounding notes (compute ratio
   `(cc.tick - note1.start) / (note2.start - note1.start)` before and after).
5. **Lyrics on channel 16** — lyric event at tick 480 with vocal note at
   480 stays paired after conversion.
6. **Round-trip 90 → 180 → 90** — every event tick within ±1 of the
   original.
7. **Mode A vs Mode C** — same input file, Mode A produces a file whose
   `durationInMs()` matches the original; Mode C produces a file whose
   musical bar count matches the original but `durationInMs()` differs by
   the inverse of `scale`.
8. **Empty selection** — `convert(file, {scope=SelectedEvents})` with no
   selection returns `ok=false, affectedEvents=0`.
9. **Protocol round-trip** — undo restores byte-identical `MidiFile::save()`
   output.
10. **Single-track scope** — vocal on track 3 at 90 BPM, drums on track 4
    at 180 BPM (untouched). Convert with `scope=SelectedTracks, trackIds={3}, tempoMode=EventsOnly`.
    Track 3 events scale by 2.0×; track 4 events untouched; tempo map
    untouched; project still plays back at 180 BPM with vocal now aligned.
11. **Single-channel scope** — same setup but the vocal is on channel 0 and
    drums on channel 9. `scope=SelectedChannels, channelIds={0}, tempoMode=EventsOnly`.
    Only channel 0 events move. Dialog must refuse `ReplaceFixed` for this
    scope.
12. **Right-click seeding** — simulate the Tracks-panel right-click slot
    with two tracks pre-selected; dialog opens with both chip-strip entries
    populated and Scope = Selected tracks.

#### 33.8 — File list

* `src/converter/TempoConversionService.{h,cpp}` — NEW headless engine
* `src/gui/TempoConversionDialog.{h,cpp}` / `.ui` — NEW dialog with live preview
* `src/gui/MainWindow.{h,cpp}` — register `convert_tempo_preserve_duration`
  action under **Tools → Tempo Tools**, add to action map for keyboard /
  toolbar customisation, plus the `onConvertTempoPreserveDuration(ScopeHint)`
  slot used by all three context-menu entry points
* `src/gui/TrackListWidget.{h,cpp}` — add the entry to the existing
  context menu; emit a signal carrying the right-clicked + multi-selected
  track ids
* `src/gui/ChannelListWidget.{h,cpp}` — same for channels
* `src/gui/MatrixWidget.{h,cpp}` — add the entry to the existing
  selection-aware right-click menu (Scope = Selected events)
* `src/MidiEvent/NoteOnEvent.h` / `OffEvent.h` — verify (no change expected)
  the existing `noteOn->offEvent()` accessor pattern works for our needs
* `src/ai/ToolDefinitions.{h,cpp}` — add the agent tool (33.5, post-MVP)
* `src/ai/AgentRunner.{h,cpp}` — route the new tool to the service (33.5)
* `tests/test_tempo_conversion_service.cpp` — NEW
* `manual/tempo-conversion.html` — NEW manual page (with the worked example)
* `manual/menu-tools.html` — add the Tempo Tools row
* `CHANGELOG.md` — feature entry under 1.6.0

### Acceptance criteria

1. Loading a 90 BPM vocal MIDI, opening the dialog with target = 180 and
   defaults, and clicking OK produces a file whose `durationInMs()` is
   within ±2 ms of the original *and* whose tempo meta is 180 BPM.
2. Pasting that converted file into a 180 BPM project lines up bar-for-bar
   with the project's grid.
3. Single Ctrl+Z reverts the entire operation — every event tick + the
   tempo map go back exactly.
4. Test 6 (round-trip drift) passes with ≤ 1-tick per event.
5. Tempo Tools menu entry shows the dialog; preview labels update without
   committing changes; Cancel discards everything.
6. With 0 events in scope, OK is disabled and the dialog footer explains
   why.
7. Right-clicking a track in the Tracks panel and choosing *Convert Tempo,
   Preserve Duration…* opens the dialog with Scope = Selected tracks and
   the chip strip pre-filled with that track. Same for Channels panel and
   Selected channels.
8. Converting a single track / channel with `tempoMode=EventsOnly` leaves
   every event outside that track / channel byte-identical (verified via
   `MidiFile::save()` diff).

### Working names

* **Convert Tempo, Preserve Duration** (preferred — verb-first, descriptive).
* **Time-Preserving Tempo Conversion** (alias — formal).
* **Re-anchor BPM** (rejected — too clever, hides what it actually does).

### Deferred to later phases

* **Mode B (Scale tempo map)** — keeps the tempo *curve* and just shifts the
  tick grid underneath it; mathematically straightforward but the dialog
  needs a mini visualiser of "before / after tempo curve" to be usable.
  v2.
* **Batch conversion** — *"Convert all loaded files to project BPM"* via
  the Recent Files panel. v3.
* **Auto-prompt on file open** — *"This MIDI is at 90 BPM, your project is
  at 180 BPM. Convert while preserving duration?"*. Safer once the MVP has
  shipped and we've had user feedback on edge cases. v3.
* **Negative-tick / pickup-note support** — depends on the broader
  pickup-bar work tracked in [Planning/03_bugs.md](03_bugs.md).
* **Reference projects** for the implementer:
  * [mido](https://github.com/mido/mido) — Python; clearest illustration of
    delta-time vs tempo meta-event semantics. Read `mido/midifiles/tracks.py`
    for the tick→seconds conversion the test plan relies on.
  * [miditoolkit](https://github.com/YatingMusic/miditoolkit) — tick-based
    rather than seconds-based. Closest to MidiEditor's internal model and
    the right reference for the scaling pass.
  * [pretty_midi](https://github.com/craffel/pretty_midi) — seconds-based;
    useful for the regression-test harness *only* (verify durations
    match), not for the implementation itself.

---

## Phase 34: Cross-Instance Paste — Track / Channel Assignment Dialog — DONE in 1.6.0

### Why

`SharedClipboard` (Phase 25, v1.4.x) lets the user copy events in one
MidiEditor instance and paste them into another, or between two MIDI files
opened in tabs. The serialization preserves the original track and channel
of every event — but at paste time
[EventTool::pasteFromSharedClipboard()](MidiEditor_AI/src/tool/EventTool.cpp)
**discards** that information and forces every regular event onto the
current `NewNoteTool::editChannel()` / `editTrack()` instead.

Result: a multi-track Lute-Bass-Drums copy from instance A pastes into
instance B as one homogeneous blob on the active edit track + channel. The
notes immediately become the current selection, but as soon as the user
clicks anywhere else the selection is lost — and now they can't easily tell
which pasted notes belonged to which source track. They have to guess by
pitch range, manually box-select, and shove them onto separate tracks one
at a time.

This is a long-standing footgun and becomes a *blocker* in combination with
Phase 33 ("Convert Tempo, Preserve Duration"): the typical Phase-33 workflow
is *"copy a vocal MIDI from another instance, paste it into this project,
re-tempo it, line it up"* — and Phase 33 itself is much less useful if the
paste step destroys the per-track structure first.

### Scope

* **Apply only** to the cross-instance / cross-file paste path
  (`EventTool::pasteFromSharedClipboard`) and the in-instance cross-file
  paste path when `_copiedTicksPerQuarter`-source != current file.
* **Do not** change the same-file paste path's default behaviour. Pasting
  inside the same file with the same edit context already does the right
  thing (collapse onto current edit track) and changing it would surprise
  every existing user. The dialog can still be reached via the Edit menu's
  *Paste Special…* entry.
* **Do not** change `SharedClipboard`'s wire format (`ClipboardHeader`
  already carries source channel + track-id metadata; we just stopped
  using it).

### 34.1 — Paste-Special dialog

`PasteSpecialDialog` (new), modal, opened automatically the first time
`pasteFromSharedClipboard()` runs in a session and on demand from
**Edit → Paste Special…** thereafter.

Three radio buttons:

1. **Create new tracks for pasted events** *(default — recommended for
   cross-instance paste)*
   * Creates one new `MidiTrack` per *distinct source track* in the
     clipboard.
   * Pasted track names use the source's `MidiTrack::name()` if present,
     prefixed with `"Pasted: "` (e.g. `Pasted: Lead Vocal`); fall back to
     `Pasted Track <N>` when the source had no name.
   * Channel is preserved from the source (so a source-CH9 drum stays on
     CH9).
   * Avoids merging into existing tracks → user can audition the pasted
     material in isolation, then drag-merge afterwards if desired.
2. **Preserve source track + channel mapping (1:1)**
   * Reuses the source's track index when an existing target track has a
     matching name; otherwise creates a new one with that name.
   * Source channels are preserved verbatim. Useful when copying between
     two arrangements of the same song.
   * If the source used CH9 drums and the target's CH9 already has notes,
     the dialog shows a yellow *"CH9 will be merged with existing drums"*
     warning row before commit.
3. **Paste to current edit track + channel (legacy)**
   * Today's behaviour: every regular event collapsed onto
     `NewNoteTool::editTrack()` / `editChannel()`. Kept for users who liked
     the old flow and for very-small same-pattern paste cases.
   * Meta events (tempo/time-sig/key-sig/lyrics) keep their dedicated
     channels (16/17/18) regardless of mode — same as today.

Below the radios:

* A small **scope summary** read from the clipboard header:
  *"Clipboard: 247 events, 3 source tracks (Lead, Bass, Drums), 3 channels
  (0, 1, 9), 4.2 s of music."*
* Checkbox **Don't ask again — use this for the rest of the session**
  *(default off)* — sets a `QSettings` value that survives the current
  session only (cleared on app exit so the next launch shows the dialog
  once again, since the safe behaviour for a brand-new session is "verify
  user intent").
* Checkbox **Make this the new default** — only enabled when "Don't ask
  again" is also checked — persists the choice under
  `Editing/pasteSpecialDefault` so the dialog stops appearing across
  launches. The dialog is still reachable via Edit → Paste Special… so
  the user is never locked into one mode.

### 34.2 — Engine

Extend `EventTool::pasteFromSharedClipboard()` (or factor into a new
`PasteSpecialService` if the function grows past ~250 lines):

```cpp
enum class PasteAssignment {
    NewTracksPerSource,    // 34.1 option 1
    PreserveSourceMapping, // 34.1 option 2
    CurrentEditTarget      // 34.1 option 3 (current behaviour)
};

struct PasteSpecialOptions {
    PasteAssignment assignment = PasteAssignment::NewTracksPerSource;
    bool applyTempoConversion  = true;        // existing flag
    int  targetCursorTick      = 0;
};
```

Implementation steps inside the existing `Protocol::startNewAction(...)`
block:

1. Read clipboard header → `QHash<int /*sourceTrackId*/, QString /*name*/>`
   and per-event `(sourceTrackId, sourceChannel)` from
   `SharedClipboard::getOriginalTiming()`-equivalent expanded accessor
   (extend that helper to also return track id + channel — already in the
   serialized payload, just not surfaced).
2. Build a target-track map according to `assignment`:
   * `NewTracksPerSource` → for each unique sourceTrackId create
     `currentFile()->addTrack()` with the prefixed name; map source→target.
   * `PreserveSourceMapping` → for each unique sourceTrackId, look up an
     existing track by name; create one if missing.
   * `CurrentEditTarget` → singleton map: every source track maps to
     `NewNoteTool::editTrack()`; every regular event's channel is
     overwritten with `NewNoteTool::editChannel()`.
3. For each pasted event, set `event->setTrack(map[sourceTrack], false)`
   and (in the first two modes) keep its source channel; insert into
   `currentFile()->channel(targetChannel)->insertEvent(...)` exactly as
   today.
4. The new tracks created in modes 1 / 2 are themselves part of the
   protocol action — so a single Ctrl+Z removes both the tracks and the
   pasted events.

### 34.3 — Edit-menu integration

* **Edit → Paste** (existing, Ctrl+V) — keeps current behaviour:
  * Same-file paste → unchanged.
  * Cross-instance paste → first time per session pops the dialog; after
    the user picks "Don't ask again", silently uses the chosen mode.
* **Edit → Paste Special…** *(new, no shortcut)* — always opens the dialog.
  Disabled when the shared clipboard is empty.
* `MainWindow::updatePasteActionState()` extended to also enable / disable
  the new Paste Special action.

### 34.4 — Open questions

* **Track ordering** — when option 1 creates 3 new tracks, should they
  appear at the end of the track list, or interleaved next to the active
  edit track? *Decision:* always append at the end so the existing
  protocol panel's "added track N" entry order is predictable; user can
  drag-reorder afterwards.
* **Empty source-track names** — fall back to `Pasted Track <N>` where N
  is a zero-based counter unique within this paste action.
* **Cross-format paste from a future MusicXML clipboard** — out of scope;
  Phase 34 only covers `SharedClipboard`.

### 34.5 — Tests (`tests/test_paste_special.cpp`)

1. **NewTracksPerSource** — clipboard with 3 source tracks → file gains
   exactly 3 new tracks with the expected `Pasted: <name>` names; events
   distributed accordingly; CH9 events stay on CH9.
2. **PreserveSourceMapping name match** — target file already has a track
   named "Bass"; clipboard contains a "Bass" source track → events land on
   the existing target Bass track, no new track created.
3. **PreserveSourceMapping no match** — new track is created with the
   source's name (no `Pasted:` prefix in this mode).
4. **CurrentEditTarget** — byte-identical to today's pre-Phase-34 paste
   output.
5. **Single Ctrl+Z** — undo restores both the new tracks and the pasted
   events; track list count returns to original.
6. **Don't-ask-again session-scoped** — set the flag, paste twice, verify
   the dialog only appears once; restart simulated by clearing the
   in-memory flag, dialog appears again.
7. **Make-this-the-default persistent** — verify
   `QSettings("Editing/pasteSpecialDefault")` round-trips and is
   honoured by `pasteFromSharedClipboard()` even on a fresh session.

### 34.6 — File list

* `src/gui/PasteSpecialDialog.{h,cpp}` / `.ui` — NEW
* `src/tool/EventTool.{h,cpp}` — extend `pasteFromSharedClipboard()` with
  `PasteSpecialOptions`; add `pasteWithOptions(opts)` overload
* `src/tool/SharedClipboard.{h,cpp}` — surface source track id + name +
  channel via an extended `getOriginalTiming(index)` returning a small
  POD struct (existing `QPair<int,int>` becomes a `PasteSourceInfo` or
  similar; old call sites updated)
* `src/gui/MainWindow.{h,cpp}` — register `paste_special` action under
  Edit menu; update `updatePasteActionState()`
* `src/tool/NewNoteTool.h` — no change (reads still return current edit
  context)
* `tests/test_paste_special.cpp` — NEW
* `manual/menu-edit.html` — add the Paste Special row
* `manual/clipboard.html` *(new, or extend an existing copy/paste page)*
  — explain the three modes and when to pick each
* `CHANGELOG.md` — feature entry under 1.6.0

### Acceptance criteria

1. Copying a 3-track arrangement from instance A and pasting into a fresh
   instance B opens the dialog; with the default option the target file
   gains 3 newly-named tracks containing the right notes on the right
   channels — no manual track-shoving needed.
2. The "current edit target" radio produces output byte-identical to the
   current 1.5.x behaviour (regression test).
3. Ctrl+Z removes every new track and every pasted event in a single step.
4. Subsequent pastes in the same session honour the "Don't ask again"
   choice silently.
5. Edit → Paste Special… always opens the dialog regardless of the
   session flag.
6. No regression in same-file paste (Ctrl+V inside one tab does not pop
   the dialog and behaves exactly like 1.5.x).

### Working names

* **Cross-Instance Paste Assignment** (formal).
* **Paste Special** (UX label — matches Office tradition; short, familiar).

---

## Phase 35: Auto-Fit Voice Load — PLANNED for 1.6.x

### Why

Phase 32 gave the user (and the agent) a read-only voice-load report:
`globalPeak`, `overflowRanges`, `rateHotspots`. In practice the next
question is always the same:

> *"OK, the file overflows in 7 places — now what?"*

Today the answer is "manually box-select the offending bars, decide which
chord notes to drop, repeat 7×, hope you didn't kill the lead line." That's
fine for a single song but it doesn't scale to imported scores or AI-generated
arrangements that routinely produce 20-voice peaks.

Phase 35 adds the *action* counterpart to Phase 32's *analysis* — a single
operation (and matching AI tool) that thins every overflow range down to the
16-voice ceiling using a deterministic, musically-defensible policy, inside
one undoable protocol step.

### Scope

* **Apply only** to FFXIV mode files (the 16-voice ceiling is FFXIV-specific).
  Non-FFXIV files keep the existing behaviour (no auto-fit; the analyzer
  still shows the lane in case the user enables FFXIV later).
* **Apply only** to NoteOn / NoteOff event pairs. Tempo / time signature /
  text / lyric events are never thinned.
* **Do not** change the ceiling itself — the value `16` lives in
  `FfxivVoiceAnalyzer` and stays the single source of truth.
* **Do not** auto-run during playback. The user (or the agent) explicitly
  invokes the action.

### 35.1 — Thinning policy

For every tick `t` where `voiceCount(t) > 16`, compute a removal set so
`voiceCount(t) <= 16` afterwards. Removal priority (highest = removed first):

1. **Duplicates** — same `(channel, key, octave-equivalent)` overlapping
   within ≤ 10 ticks. Pick the shorter / quieter copy.
2. **Inner chord voices** — voices that are not the lowest *or* highest
   pitch of the chord on their channel at this tick. Drum channels (CH9)
   are exempt — every drum hit is musically load-bearing.
3. **Quietest velocity** — among remaining candidates, drop the lowest
   `velocity()` first.
4. **Shortest duration** — tiebreak on `duration()` (shorter goes first;
   long sustains carry the harmonic skeleton).
5. **Lowest channel index** — final stable tiebreak so the result is
   deterministic across reruns.

Drum channels (CH9 in standard mode, the FFXIV percussion channels in
FFXIV mode) are **never** thinned — the rate ceiling there is 14 notes/sec
(rate-hotspot territory), not voice count.

### 35.2 — Tool surface

* **AI tool** `auto_fit_voice_load` (read-write, FFXIV-mode only):
  * Parameters: `startTick` *(int|null)*, `endTick` *(int|null)*,
    `dryRun` *(bool, default `false`)*, `targetCeiling` *(int|null,
    default 16, hard-clamped to `[2, 32]`)*.
  * Returns `{removedCount, remainingPeak, removedEvents:[{tick,
    channel, key, velocity, reason}]}`. With `dryRun=true` the file is
    not modified; the agent can preview the impact and ask the user
    before committing.
  * Wrapped in a single `Protocol::startNewAction("Auto-fit voice load")`
    so a single Ctrl+Z restores every removed note.

* **Menu** *MidiPilot → Tools → Auto-Fit Voice Load…* (next to the
  existing *Analyze Voice Load*). Opens a small confirmation dialog
  showing the dry-run summary (*"Will remove 47 notes across 7 ranges;
  peak 21 → 16"*) with **Apply** / **Cancel**, plus a *"Preview as
  selection"* button that selects the events that would be removed
  without committing.

* **Right-click on the voice-load lane** — the existing red overflow
  rectangles get a *"Auto-fit this range"* context-menu entry that
  pre-fills `startTick`/`endTick` from the rectangle's bounds.

### 35.3 — Engine

`src/converter/AutoFitVoiceLoadService.{h,cpp}` — pure, headless,
testable in isolation (mirrors the Phase 33 `TempoConversionService`
pattern):

```cpp
struct AutoFitOptions {
    int startTick = -1;     // -1 → file start
    int endTick   = -1;     // -1 → file end
    int targetCeiling = 16;
    bool dryRun = false;
};

struct AutoFitResult {
    int removedCount = 0;
    int remainingPeak = 0;
    QList<RemovedNote> removed;  // (tick, channel, key, velocity, reason)
};

AutoFitResult AutoFitVoiceLoadService::apply(MidiFile *file,
                                             const AutoFitOptions &opts);
```

Implementation steps:

1. Reuse `FfxivVoiceLoadCore::analyze()` to obtain `overflowRanges`.
2. For each range, walk a sweep-line over the per-tick voice set and
   apply the priority list from §35.1 until the count fits.
3. Mark each victim NoteOn (and its paired OffEvent) for deletion.
4. Outside `dryRun`: `currentFile()->protocol()->startNewAction(...)`,
   `removeEvent(victim, true)` per victim, `endAction()`. Inside
   `dryRun`: skip the protocol block; populate the result and return.

### 35.4 — Tests (`tests/test_auto_fit_voice_load.cpp`)

1. **No-overflow** — file with `globalPeak ≤ 16` returns
   `removedCount == 0`, no protocol entry created.
2. **Single 17-voice chord** — 17 notes at one tick, `targetCeiling=16`
   → removes exactly 1, the quietest inner voice; outer (lowest +
   highest) pitches survive.
3. **Drum exemption** — 17 simultaneous CH9 NoteOns are *not* thinned;
   `removedCount == 0`.
4. **Range scoping** — `startTick`/`endTick` respected; events outside
   are not touched even if they overflow.
5. **Dry-run** — `dryRun=true` returns the same result struct as the
   live run but `currentFile()->protocol()->stepCount()` is unchanged.
6. **Determinism** — running the same input twice produces byte-identical
   `removedEvents` lists.

### 35.5 — File list

* `src/converter/AutoFitVoiceLoadService.{h,cpp}` — NEW.
* `src/ai/ToolDefinitions.cpp` — register `auto_fit_voice_load` next to
  `analyze_voice_load`; gated on `AI/ffxiv_mode`.
* `src/gui/MainWindow.cpp` — register the new menu action, scoped
  context-menu entry on the voice-load lane.
* `src/gui/MatrixWidget.cpp` — context-menu hook for the red overflow
  rectangles in the voice-load lane.
* `tests/test_auto_fit_voice_load.cpp` — NEW (Qt Test, headless;
  follows the same ODR-shim layout as `test_tempo_conversion_service`
  to stay decoupled from the GUI).
* `manual/menu-tools.html` + `manual/voice-load.html` *(extend or new)*
  — document the priority list and the dry-run flow.
* `CHANGELOG.md` — feature entry under the next 1.6.x.

### Acceptance criteria

1. A 21-voice peak from a real Guitar Pro import is reduced to ≤ 16
   in one undoable step; outer voices and lead lines survive.
2. CH9 / FFXIV percussion is never auto-thinned.
3. `dryRun=true` returns an identical result struct without touching
   the file or the protocol stack.
4. The agent can call `auto_fit_voice_load` after `analyze_voice_load`
   without the user leaving the chat.
5. Same input → same removal set across runs (deterministic order).
6. No regression in non-FFXIV files (the menu entry is disabled and
   the agent tool is not registered).

### Working names

* **Auto-Fit Voice Load** (formal + UX).
* `auto_fit_voice_load` (AI tool id).


## Phase 36: Copy to Track / Copy to Channel -- DONE in 1.6.x

### 36.1 -- Goal

A pair of new editing actions, **Copy to Track...** and **Copy to
Channel...**, that mirror the existing *Move to Track* / *Move to Channel*
flow but **duplicate** the current selection in place onto a chosen
target track / channel instead of moving it.

The duplicated events keep:

* The same `midiTime()` and length (1:1 timing -- no shift, no quantise).
* The same pitch / velocity / controller value / pitch-bend amount.
* For NoteOn/Off pairs: their pairing (each duplicated NoteOn gets a
  duplicated paired OffEvent at the same off-tick).
* Their visibility -- the originals stay where they were and the
  **new copies become the selection** so the user can immediately
  transpose by an octave, change velocity, recolour, etc.

The action is independent of the playhead, the cursor tick, the
clipboard, and the active edit track/channel. It only depends on the
current `Selection::instance()->selectedEvents()` and the chosen
target.

### 36.2 -- Entry points

1. **Tools menu** -> `Tools -> Copy events to track...` and
   `Tools -> Copy events to channel...`. Disabled when the selection is
   empty (registered in `_activateWithSelections`).
2. **Matrix context menu** (right-click on a selected event) -> same two
   submenus appear next to the existing *Move to* entries. Cascading
   submenus listing every track / every channel (0-15), with the
   selection's current track / channel grayed out as a no-op (still
   selectable; falls through to a copy-onto-self which is allowed but
   warned-once via status bar).
3. **No new keyboard shortcut** in the first cut -- Ctrl+V semantics are
   already taken by Phase 34's Paste Special and we want to avoid
   surprising users who have muscle memory for *Move to*.

### 36.3 -- Engine spec

New static helpers on `EventTool` (consistent with `pasteAction()` /
`pasteFromSharedClipboard()`):

```cpp
static bool copySelectionToTrack(MidiTrack *target);
static bool copySelectionToChannel(int channel /*0..15*/);
```

Both wrap a single `Protocol::startNewAction("Copy to Track ...")`
block and:

1. Snapshot the current selection via
   `Selection::instance()->selectedEvents()` (returns by value since
   v1.3.2).
2. For each event, `MidiEvent *dup = ev->copy();` (deep clone via the
   existing virtual `copy()`).
3. Re-home the duplicate:
   * `dup->setTrack(target, /*toProtocol=*/false)` for *Copy to Track*.
   * For *Copy to Channel*, look up the matching channel on the same
     `MidiFile` and reattach via the channel's `insertEvent(...)`
     equivalent. Meta channels (16/17/18) refuse the operation -- the
     menu entry is hidden for any meta-channel selection because
     "channel" doesn't apply.
4. NoteOn handling: if `dup` is a `NoteOnEvent`, duplicate its paired
   `OffEvent` too (look up via `OffEvent::onEvents` static map for the
   original) and re-home the off-event identically. Both share the
   same new track/channel.
5. Insert each duplicate via the channel's
   `insertEvent(dup, dup->midiTime())` so the `QMultiMap<tick, ev*>`
   stays sorted and the event tree picks it up.
6. After all inserts: `Selection::instance()->setSelection(<dups>)` so
   the new copies become the live selection. Originals are deselected
   in the process -- that is the explicit UX choice ("the copy is what
   you just made, so you keep editing the copy").
7. `Protocol::endAction()`.

Edge cases:

* Empty selection -> no-op, no protocol entry.
* Target track is the same track for every selected event AND the
  user picked *Copy to Track* on the same track -> duplicates are
  inserted at the same tick on the same track; result is two
  overlapping notes (intentional; matches the LightAmp-style "double
  the line" workflow).
* Selection contains events from multiple source tracks -> all of them
  are copied to the single target track (same as *Move to Track*).
* `dup->copy()` returning `nullptr` for any event aborts the action
  cleanly via `Protocol::cancelAction()` and shows a status-bar
  warning; nothing in the file changes.

### 36.4 -- Files

* `src/tool/EventTool.{h,cpp}` -- add `copySelectionToTrack()` and
  `copySelectionToChannel()` plus a small private helper
  `cloneEventOnto(MidiEvent*, MidiTrack*, int targetChannel)` that
  centralises the NoteOn/Off pairing logic.
* `src/gui/MainWindow.{h,cpp}` -- two new submenus
  `_copyToTrackMenu` / `_copyToChannelMenu`, populated lazily in the
  same place that already populates `_pasteToTrackMenu` /
  `_pasteToChannelMenu`. Slots `copySelectedToTrack(QAction*)` and
  `copySelectedToChannel(QAction*)` parse the action's `setData(...)`
  payload and forward to `EventTool::copySelectionTo*()`.
* `src/gui/MatrixWidget.cpp` -- extend the right-click menu builder
  next to the existing *Move to* entries with the new *Copy to*
  cascades.
* `tests/test_copy_to_track_channel.cpp` -- NEW (Qt Test, headless,
  ODR-shim layout from `test_tempo_conversion_service.cpp`).

### 36.5 -- Tests (`tests/test_copy_to_track_channel.cpp`)

1. **Track copy preserves timing** -- two notes on track 1 at ticks
   480 / 960, copy to track 3 -> track 1 still owns the originals,
   track 3 owns two new notes at the same ticks/pitches, channel
   unchanged.
2. **Channel copy preserves timing** -- three CH0 notes copied to CH4
   -> originals stay on CH0, three new CH4 notes exist at the same
   ticks/pitches, track unchanged.
3. **NoteOn/Off pairing** -- copy a single NoteOn whose Off is at
   tick+240 -> exactly one new NoteOn AND one new Off appear on the
   target; `OffEvent::onEvents` map contains the new pair; the new
   pair sits on the target track/channel.
4. **Selection switches to copies** -- after the action,
   `Selection::instance()->selectedEvents()` contains exactly the
   newly-created events (same count as the source selection, including
   paired Off events if they were selected).
5. **Single undo** -- `Protocol::stepCount()` increases by exactly 1;
   one Ctrl+Z restores the file byte-identically (same event count,
   same selection as before the action -- verified via a serializer
   round-trip).
6. **Empty selection** -- no-op; `stepCount()` unchanged; no log spam.
7. **Meta-channel guard** -- calling `copySelectionToChannel(16)`
   (or 17 / 18) returns `false` and performs no edits; tested via
   the public helper directly.
8. **Cross-track selection -> single target track** -- selection spans
   tracks 1 + 2, *Copy to Track 3* -> all duplicates land on track 3
   on the same channels they had originally.

### Acceptance criteria

1. *Copy to Track / Copy to Channel* leaves the original notes in
   place and creates byte-identical duplicates on the target.
2. The duplicated events become the active selection; the user can
   immediately apply *Octave Up*, velocity edits, etc., and only the
   copies are affected.
3. The whole action is a single undoable step.
4. Drum / FFXIV CH9 selections are not specialcased -- they copy
   verbatim like every other channel; voice-load impact is the user's
   responsibility (Phase 35 helps here when paired).
5. Available from both the Tools menu and the matrix context menu;
   disabled when the selection is empty.
6. No regression in *Move to Track / Move to Channel* -- those keep
   their existing behaviour.

### Working names

* **Copy to Track...** / **Copy to Channel...** (formal + UX).
* `EventTool::copySelectionToTrack()` / `copySelectionToChannel()`
  (engine).


## Phase 37: Rebrand to "MidiEditor AI" -- Logo, Theme, Repo Rename

### 37.0 -- Goal & Strategy

Re-skin the project around the new **"MidiEditor AI"** brand identity
(see `Midieditor-ai_logo.png` / `Midieditor-ai_logo_blk.png` and the
brand sheet attached to the planning thread). The rebrand has four
*independent but ordered* tracks:

1. **37.1 -- Logo & icon swap** (lowest risk, cosmetic only).
2. **37.2 -- Brand theme `MidiEditor AI Brand`** (new QSS, palette
   below; existing themes stay shipped untouched).
3. **37.3 -- Manual & web doc rebrand** (banner, favicons, color
   accents in the generated HTML).
4. **37.4 -- GitHub repo rename `MidiEditor_AI` -> `MidiEditor-AI`
   + fallback / dummy-repo strategy** (executed LAST, only after the
   1.6.0 release that contains 37.1-37.3 ships and the auto-update
   ping is verified pointing at the new URL).

> Order rule: 37.1 -> 37.2 -> 37.3 -> 1.6.0 release with the rebrand
> baked in -> only THEN 37.4 (rename) so existing 1.5.x / pre-1.6.0
> clients still resolve the old repo URL until they self-update.
> See `Planning/08_REPO_RENAME.md` for the full URL inventory that
> 37.4 will consume.

The ASCII logo in `README.md` and on console banners stays as-is --
it is part of the project's terminal personality.

---

### 37.1 -- Logo & icon swap

**Source files (already in repo):**

```
Midieditor-ai_logo.ico                  <- Windows app icon (multi-res)
Midieditor-ai_logo_blk.ico              <- Black/dark variant
run_environment/graphics/Midieditor-ai_logo.png      <- 1024px primary
run_environment/graphics/Midieditor-ai_logo_blk.png  <- 1024px black/dark
manual/Midieditor-ai_banner.png         <- web/manual banner
manual/Midieditor-ai_banner_blk.png     <- web/manual banner (dark)
```

**Files that need updating:**

| File | Change |
|---|---|
| `midieditor.rc` | Swap `IDI_ICON1 ICON "..."` to point at `Midieditor-ai_logo.ico`. |
| `resources.qrc` | Add `<file>graphics/Midieditor-ai_logo.png</file>` and `<file>graphics/Midieditor-ai_logo_blk.png</file>`. Keep the old `midieditor.png` resource alias for backward compatibility (existing dialogs reference it). |
| `src/main.cpp` | `QApplication::setWindowIcon(QIcon(":/graphics/Midieditor-ai_logo.png"));` -- pick the dark variant when `palette().window().color().lightness() > 128` so it stays visible on light themes. |
| `src/gui/AboutDialog.cpp` | Replace the title-image with the new banner; theme-aware variant (use `_blk` on light backgrounds). |
| `src/gui/SplashScreen.*` *(if present)* | Same banner swap. |
| `packaging/org.midieditor.midieditor/meta/package.xml` | `<DisplayName>MidiEditor AI</DisplayName>` confirm; install icon path updated. |
| `scripts/packaging/windows/config.xml` | `<TargetDir>` / `<Icon>` references. |
| `scripts/packaging/debian/MidiEditor.desktop` | `Icon=midieditor-ai` + ship the PNG into `usr/share/pixmaps/midieditor-ai.png`. |
| `manual/index.html` JSON-LD | `"image"` field -> new banner URL. |

**Backward-compat shim:** keep `midieditor.png` and `icon.png` in
`run_environment/graphics/` for one release as alias resources (the
`.qrc` keeps emitting them). Existing third-party scripts / shortcuts
that hard-code those paths keep working until 1.7.x.

**Acceptance criteria 37.1:**

1. `MidiEditorAI.exe` shows the new icon in Explorer / taskbar / Alt+Tab.
2. About dialog shows the new logo + version under both `dark.qss`
   and `light.qss`.
3. Old themes still render without missing-resource warnings.
4. Built ZIP retains a working start-menu shortcut on Windows install.

---

### 37.2 -- Brand theme `MidiEditor AI Brand`

NEW QSS file: `src/gui/themes/midieditorai_brand.qss`. Loaded the
same way as the existing `dark.qss` / `materialdark.qss` -- via
`ThemeManager::applyTheme()` and registered in the *View -> Theme*
submenu (insert as the FIRST entry so it becomes the visible default
for new installs; existing users' theme choice in `QSettings` is
respected).

**Palette (single source of truth):**

| Token | Hex | Usage |
|---|---|---|
| `--bg`            | `#0B1020` | Window background, central widget |
| `--bg-secondary`  | `#10192A` | Toolbars, menubar, side panels |
| `--bg-card`       | `#162238` | Menus, popovers, tooltips, group boxes |
| `--bg-input`      | `#0E1626` | QLineEdit / QSpinBox / QComboBox surfaces |
| `--text`          | `#EAF3FF` | Primary text |
| `--text-muted`    | `#8FA3B8` | Secondary text, disabled mid-state |
| `--text-disabled` | `#51657A` | Disabled text |
| `--accent`        | `#00B8FF` | Brand cyan -- focus rings, primary buttons |
| `--accent-hover`  | `#25D6FF` | Hover/active accent |
| `--accent-violet` | `#7C5CFF` | AI-specific surfaces (MidiPilot header, agent badges) |
| `--border`        | `#293A55` | Separators, outlines, splitter handles |
| `--danger`        | `#FF4D6D` | Errors, destructive actions |
| `--success`       | `#2CEAA3` | Confirmations, "saved" indicators |

**Components covered (mirroring the existing `dark.qss` coverage so
nothing regresses):**

`QWidget`, `QMainWindow`, `QMenuBar`, `QMenu`, `QToolBar`,
`QToolButton`, `QTabWidget`, `QTabBar`, `QPushButton`, `QLineEdit`,
`QSpinBox`, `QDoubleSpinBox`, `QComboBox`, `QListView`, `QTreeView`,
`QTableView`, `QHeaderView`, `QGroupBox`, `QScrollBar`, `QSlider`,
`QProgressBar`, `QStatusBar`, `QToolTip`, `QSplitter`, `QDockWidget`,
`QCheckBox`, `QRadioButton`, `QDialog`, `QMessageBox`. Custom widgets:
`MatrixWidget` background, `MidiPilotWidget` header (uses `--accent-violet`
for the AI banner), `LyricTimelineWidget` track color, velocity lane
backdrop.

**Accessibility:**

* Contrast ratio for text-on-bg: `#EAF3FF` on `#0B1020` = 16.4:1 (AAA).
* Accent-on-bg: `#00B8FF` on `#0B1020` = 7.8:1 (AAA for normal text,
  AAA for UI components).
* Disabled text `#51657A` on `#0B1020` = 4.6:1 (AA, just above the
  WCAG floor for non-decorative text).

**Files:**

* `src/gui/themes/midieditorai_brand.qss` -- NEW.
* `resources.qrc` -- register the new QSS.
* `src/gui/ThemeManager.{h,cpp}` -- add the enum entry +
  `tr("MidiEditor AI Brand")` label; pin as "recommended default"
  *only* when no `Appearance/theme` key exists in QSettings (do not
  forcibly override existing user choice).
* `src/gui/SettingsDialog.cpp` -- the theme dropdown picks it up
  automatically once registered.

**Tests (`tests/test_theme_manager.cpp` -- extend existing):**

1. `applyTheme("MidiEditor AI Brand")` succeeds and the loaded QSS
   string contains all 11 palette hex codes.
2. Switching back to `dark.qss` clears all brand-specific styles
   (no leftover `:focus` accent rings).
3. New install (empty QSettings) returns `MidiEditor AI Brand` from
   `ThemeManager::defaultThemeName()`.

**Acceptance criteria 37.2:**

1. Every shipped widget renders without unstyled fallbacks.
2. Theme switch is instant (no relaunch needed) -- same as the other
   themes.
3. The MidiPilot header uses the violet accent so users can immediately
   tell it apart from the matrix background.
4. The AI agent badge in the chat tab gets the violet accent.

---

### 37.3 -- Manual & web doc rebrand

The HTML manual lives under `manual/` and is regenerated by
`scripts/build_changelog.py`. The brand swap consists of:

1. **Banner / hero image:**
   * `manual/index.html` -- replace `<img src="midieditor.png">` with
     `<picture>` containing both `Midieditor-ai_banner.png` (default)
     and `Midieditor-ai_banner_blk.png` (`prefers-color-scheme: light`).
2. **Favicon:**
   * Generate `manual/favicon-16.png`, `favicon-32.png`,
     `favicon-180.png` (apple-touch) from `Midieditor-ai_logo.png` --
     ship as static assets, link from every `<head>`.
3. **Color accents in CSS (`scripts/build_changelog.py`):**
   * Update the `HTML_HEAD` constant -- swap link / nav accent from
     the current orange-ish to `#00B8FF`, hover to `#25D6FF`.
   * Update the footer separator color to `#293A55`.
4. **JSON-LD metadata:**
   * `"name"`, `"image"`, `"url"`, `"sameAs"` blocks in
     `manual/index.html` updated to the new brand + URL set.
5. **Re-run `python scripts/build_changelog.py`** to regenerate all
   `manual/*.html` in one shot. Do not hand-edit individual HTML
   files -- the script overwrites them.

**Acceptance criteria 37.3:**

1. `https://midieditor-ai.de/` shows the new banner above the fold
   on both light and dark OS settings.
2. Browser tab shows the new favicon.
3. All 21 manual pages have consistent accent colors matching the
   in-app brand theme.
4. JSON-LD validates via Google Rich Results test.

---

### 37.4 -- GitHub repo rename + fallback strategy

**Trigger:** only after 1.6.0 ships with 37.1-37.3 baked in AND the
in-app *Help -> Check for updates* path has been smoke-tested against
the *new* repo URL (so existing 1.6.0 users can upgrade to 1.6.1+
after the rename).

**Execution:** follow `Planning/08_REPO_RENAME.md` step-by-step
(§5.1-§5.6 covers the full URL inventory that needs patching BEFORE
the GitHub-side rename happens).

**New: fallback dummy repository at the old name**

To protect the old URL even after GitHub auto-redirects expire (or if
someone re-creates `MidiEditor_AI` years later under a different
account), publish a **frozen archive repo**:

* **Name:** `happytunesai/MidiEditor_AI` (re-create the moment the
  rename completes, so nobody else can claim it).
* **Type:** GitHub *Archived* repository (read-only).
* **Contents:**
  1. A single `README.md` explaining the move:
     ```markdown
     # This repository has moved

     **MidiEditor AI** is now hosted at
     <https://github.com/happytunesai/MidiEditor-AI>.

     The canonical website is <https://midieditor-ai.de>.

     This repository is kept as an archive of the last 1.5.x source
     tree so older clients can still resolve their auto-update probe
     and so historical clones remain functional.

     For new development, please use the new repository.
     ```
  2. A frozen snapshot of the **last 1.5.x source tree** (so an
     older `MidiEditor.exe` that probes
     `api.github.com/repos/happytunesai/MidiEditor_AI/releases/latest`
     still gets a 200 with a sane "no newer version" payload, instead
     of a 404 that older clients might mis-render).
  3. **One** Release tagged `v1.5.x-archive` carrying that frozen
     source as a ZIP -- gives the GitHub Releases API something to
     return and points the user at the new URL via the release notes.
  4. Repository description: `Archived. New home:
     github.com/happytunesai/MidiEditor-AI`.
  5. Topic / tag: `archived`, `moved`.

* **Branch protections:** none -- it's archived, no writes possible.

* **GitHub Pages:** disabled on the dummy repo; the custom domain
  (`midieditor-ai.de`) stays bound to the new repo's Pages.

**Auto-update behavior matrix:**

| Client version | Probes URL | Behavior after 37.4 |
|---|---|---|
| <= 1.5.x | `.../MidiEditor_AI/releases/latest` | Hits dummy archive; sees `v1.5.x-archive`; release notes nudge user to manually download from new URL. |
| 1.6.0 with patched URL | `.../MidiEditor-AI/releases/latest` | Direct hit; auto-update works normally. |
| Future 1.6.x+ | `.../MidiEditor-AI/releases/latest` | Direct hit; chain continues. |

**Files modified by 37.4:**

* All files listed in `Planning/08_REPO_RENAME.md` §5.1-§5.5.
* `src/gui/UpdateChecker.cpp` -- rebase URL to
  `https://api.github.com/repos/happytunesai/MidiEditor-AI/releases/latest`.
* `src/gui/UpdateDialogs.cpp`, `src/gui/MainWindow.cpp` -- replace
  `happytunesai.github.io/MidiEditor_AI/...` paths with
  `https://midieditor-ai.de/...` (canonical, survives any future
  rename).
* `README.md` badges + clone URL.
* `CHANGELOG.md` top-of-file release URL.
* `scripts/build_changelog.py` constants -- then re-run to regenerate
  all manual HTML.

**Rollback:** documented in `Planning/08_REPO_RENAME.md` §8 -- the
GitHub rename can be reversed in seconds; the dummy archive repo can
be deleted (or kept as documentation history). The local working
folder is **not** renamed (out of scope per the workspace-editing
rule in `AGENTS.md`).

**Acceptance criteria 37.4:**

1. `git clone https://github.com/happytunesai/MidiEditor_AI.git`
   resolves (via either redirect or the archive repo) and shows the
   "moved" notice.
2. `git clone https://github.com/happytunesai/MidiEditor-AI.git`
   succeeds and contains the latest source.
3. CI / Actions on the renamed repo run green; badges on README
   render correctly.
4. In-app *Check for updates* resolves to the new repo and reports
   the current version.
5. `https://midieditor-ai.de/` continues to serve the manual; no
   404s in the manual nav.

---

### 37.5 -- Roll-out checklist

```
[x] 37.1  swap icons + logos + .rc + resources.qrc           -> commit
[ ] 37.1  smoke-test About / taskbar / installer            -> sign-off
[x] 37.2  midieditorai_brand.qss + ThemeManager registration -> commit
[ ] 37.2  default-theme test green                          -> sign-off
[x] 37.3  build_changelog.py palette + banner + favicon      -> commit
[x] 37.3  re-run build_changelog.py -> regenerate manual/*  -> commit
[ ] 37.3  deploy manual to midieditor-ai.de                 -> sign-off
[ ] ----  CUT 1.6.0 RELEASE  (rebrand baked in, repo NOT renamed yet)
[ ] ----  Verify 1.6.0 self-update probe still resolves
[ ] 37.4  patch URLs per Planning/08_REPO_RENAME.md          -> commit
[ ] 37.4  GitHub: rename MidiEditor_AI -> MidiEditor-AI
[ ] 37.4  Re-create happytunesai/MidiEditor_AI as archived dummy
[ ] 37.4  Push frozen 1.5.x snapshot + v1.5.x-archive release
[ ] 37.4  Update local git remote on every clone
[ ] 37.4  Smoke-test auto-update from a fresh 1.6.0 install
[ ] ----  CUT 1.6.1 RELEASE (URL changes verified end-to-end)
```

---

### 37.6 -- Open decisions

1. **Default theme on existing installs?** Recommendation: leave
   user's current theme untouched; only set `MidiEditor AI Brand` for
   *new* installs (no `Appearance/theme` QSettings key present).
2. **Splash screen?** Currently no splash. Optional 37.x-late: add
   a 2-second splash with the new banner during cold start. Not in
   the 1.6.0 cut.
3. **Migrate doc URLs to `midieditor-ai.de` or just bump path?**
   Recommendation: migrate to canonical custom domain everywhere --
   makes future renames painless.
4. **Park dummy repo as user-account or org?** Recommendation:
   user-account `happytunesai` (matches current ownership; no org
   permissions to wrangle).

---

37.1 New Midi Editor AI Theme

/*  MidiEditor AI — Brand Theme
 *  Logo-inspired dark theme for the Qt/QSS UI.
 *
 *  Palette:
 *  --bg:             #0B1020  midnight navy
 *  --bg-secondary:   #10192A  toolbars / menu bars / side panels
 *  --bg-card:        #162238  elevated widgets / menus / cards
 *  --bg-input:       #0E1626  inputs / editors
 *  --text:           #EAF3FF  primary text
 *  --text-muted:     #8FA3B8  secondary / disabled text
 *  --accent:         #00B8FF  brand cyan
 *  --accent-hover:   #25D6FF  bright cyan hover
 *  --accent-violet:  #7C5CFF  AI violet accent
 *  --border:         #293A55  separators / outlines
 *  --danger:         #FF4D6D
 *  --success:        #2CEAA3
 */


<file>src/gui/themes/midieditorai_brand.qss</file>

Palet: 
#0B1020  Midnight Navy
#10192A  Panel Navy
#162238  Card / Elevated
#0E1626  Input Background
#EAF3FF  Main Text
#8FA3B8  Muted Text
#00B8FF  Brand Cyan
#25D6FF  Cyan Hover
#7C5CFF  AI Violet
#293A55  Border
---

## Phase 38: Upstream Triage - Meowchestra Sync Window (post 1.6.0)

> Snapshot taken 2026-05-02. Upstream Meowchestra/MidiEditor pushed
> 32 commits in the 14 days leading up to our v1.6.0 cut. This phase
> tracks which of those commits we want to absorb into MidiEditor AI,
> what we deliberately skip, and why. Every Tier-1/2 item still needs
> a real code review before merging - the buckets below are triage
> only, not pre-approval.

### 38.1 - Tier 1: Low-conflict bug fixes (target 1.6.1)

| Upstream SHA | Title | Files | Why pull |
|---|---|---|---|
| 35f1ee | Preserve Unmatched Percussion on Channel 9 | src/support/FFXIVChannelFixer.cpp (+26/-1) | Direct bugfix in a code path we own. FFXIV-relevant, isolated, low risk. |
| e2d107f | SoundFont Update - SGM Pro 13 | src/gui/DownloadSoundFontDialog.cpp (3 lines) | Trivial URL bump, keeps the download dialog pointing at the current asset. |
| 8997ad7 | Improve pixmap subpixel antialiasing with fractional scaling | MatrixWidget.cpp, ProtocolWidget.cpp (+9/-3) | Closes upstream #53. Pure rendering quality fix at HiDPI / 125% / 150% scaling. |
| 366a92f | Option to disable startup update check | MainWindow.cpp, PerformanceSettingsWidget.{h,cpp} (+37/-3) | Closes upstream #52. Adds a setting; doesn't change defaults. Useful QoL. |
| Crash-fix slice from 21fe86b | Crash when deleting tempo+timesig event if it's the only event in the file | Subset of MainWindow.cpp / MidiTrack.cpp | Real crash. Pull just the guard, not the focus-tracking refactor (see Tier 3). |

**Process for 38.1:**
1. For each row, run git show <sha> against meow, then map to our tree.
2. Build + run the affected tests after each cherry-pick.
3. Write one commit per upstream SHA with Co-authored-by attribution.
4. Bundle into 1.6.1 release once all five are reviewed and green.

### 38.2 - Tier 2: Bigger features worth porting (target 1.7.0)

| Upstream SHA | Title | Files | Notes |
|---|---|---|---|
| 169605 | File Duration Improvements & Trim Start Tool | 9 files (+416/-19), brand-new TrimStartDialog.{h,cpp} | Self-contained new tool. Easy adoption; check our keybinds for collisions. |
| 80f3732 (memory-fix slice) | NoteOnEvent memory cleanup, SplitChannels Force Split option | NoteOnEvent.{h,cpp}, SplitChannelsDialog.{h,cpp} | Pull only the memory cleanup + Force Split; skip the Bard Metal Preset (we have our own FFXIV preset family). |
| 21fe86b (focus-tracking slice) | Channel/track focus follows cursor + program-change attribution | Subset of MainWindow.cpp, ChannelListWidget, MatrixWidget | UX upgrade. Conflicts likely with our context-menu / track-tab work; needs a careful manual port. |

### 38.3 - Tier 3: High-conflict refactors (skip unless we hit the underlying bug)

| Upstream SHA | Title | Why we hold off |
|---|---|---|
| dfa4593 | Improve Selection Performance & Delete Overlaps (+405/-189 across MatrixWidget, SelectTool, EventMoveTool, SizeChangeTool, Selection) | Touches the same code paths we re-architected for the v1.3.2 by-value Selection regression. Re-merging risks reintroducing that bug. Only revisit if we can prove a perf problem in our own code. |
| dde3fda | Improve Hit Detection & Piano Glissando (MatrixWidget.cpp +132) | Will conflict with our matrix-overlay extensions (chord highlighter, voice lane, lyric lane). Not worth a six-hour merge for a glissando feature we don't ship. |
| c288713 | Sequential (FIFO) Event Reading Order (MidiChannel, MidiFile) | Changes the multimap iteration semantics. Our AI tools, MCP server, and undo log all assume the current ordering; flipping it silently is a regression magnet. |
| 93ea466 | Change FluidSynth Defaults (WASAPI + 48 kHz) | Default changes break existing user setups. Our FFXIV SoundFont Mode already overrides defaults. No-pull. |

### 38.4 - Already in 1.6.x or out of scope (no-pull, recorded for completeness)

* 64cae2f Export Audio Formats - shipped in our 1.2.0.
* 849c18e MusicXML & MuseScore Converter - shipped in our 1.4.0.
* 81bc83f Text Encoding Fallback / uchardet / Remove QMake - we keep
  QMake as reference and don't bundle uchardet.
* 47390a Theme Presets,  f62353 Airy Theme - we run our own
  ThemeManager + brand theme (Phase 37.2).
* d116783 Velocity Tool, Track Context Menu - we have UX-CTX-001
  and our own Move-to / Octave context menu.
* d4e8cf FFXIV Track Rename Presets, 253b159 FFXIV Condense -
  shipped in our 1.2.x FFXIV Channel Fixer.
* c8d1e6c Toggleable Toolbar - we expose this through our own
  toolbar customisation already.
* 4adbfd4 / 1954e7b Marker / glissando UX - not part of our
  workflow; revisit only if user-requested.

### 38.5 - Open questions / decisions to make per item

1. **Cherry-pick vs manual port?** Whenever upstream touches a file
   we have already heavily diverged on (MainWindow.cpp,
   MatrixWidget.cpp), prefer manual port - the diff stays cleaner
   and intent is preserved.
2. **License + attribution?** Same GPL-2.0; cherry-picks must keep
   `--author` and we add `Co-authored-by: Meowchestra ...` in
   the commit trailer.
3. **Test coverage?** Tier-1 fixes should each get at least one
   regression test in our 	ests/ harness before landing.
4. **Release cadence:** Tier 1 = 1.6.1 hotfix. Tier 2 = 1.7.0
   feature release. Tier 3 = on demand only.


---

## Phase 39: FFXIV SoundFont Equalizer (Per-Instrument Volume Mixer)

> Status: Planned, no code yet. Target: 1.7.0 alongside Tier-2
> upstream pulls.
>
> Goal: replace the current "tweak FluidSynth gain in Settings"
> workflow with a dedicated, instrument-level mixer that ships with
> a curated default preset and lets every user save their own.

### 39.1 - User-facing entry point

* **Midi menu -> FFXIV Mode -> SoundFont Equalizer...**
  Visible only when FFXIV SoundFont Mode is active (mirrors the
  Voice Limiter and Channel Fixer entries).
* Optional toolbar button next to the existing FFXIV gauge for
  one-click reopen during a session.

### 39.2 - Equalizer dialog UX

* One row per instrument exposed by the loaded SoundFont (Lute,
  Harp, Piano, Flute, Oboe, Clarinet, Trumpet, Trombone, Horn,
  Saxophone, Violin, Viola, Cello, Bass, Timpani, Drum kits, ...).
* Each row:
  * Instrument name + GM family icon
  * Horizontal slider 0..200 percent (default 100 percent)
  * Numeric spinbox bound to the slider
  * Mute toggle (S/M style)
  * Reset-this-row button (to default-preset value)
* Header strip:
  * Preset selector (combo): "FFXIV Default", user presets, "+ New..."
  * Save / Save As / Delete preset buttons
  * Master gain slider 0..200 percent (multiplied on top of per-instr.)
  * Search box to filter the instrument list
* Bottom bar:
  * Live preview toggle (auditions changes against the playing file)
  * Apply / Cancel / Reset to default preset

### 39.3 - Storage model

* Presets persisted as JSON under
  `QSettings("MidiEditor", "NONE")/FFXIV/equalizerPresets/<name>`
  with payload:

```json
{
  "name": "FFXIV Default",
  "masterGain": 1.0,
  "tracks": {
    "Lute":   { "gain": 1.00, "muted": false },
    "Harp":   { "gain": 0.85, "muted": false },
    "Piano":  { "gain": 0.90, "muted": false }
  },
  "createdBy": "builtin",
  "version": 1
}
```

* Active preset key: `FFXIV/equalizerActivePreset` (string).
* Built-in **FFXIV Default** preset shipped read-only in the
  binary; user copies become editable.

### 39.4 - Audio path integration

* New service `FfxivEqualizerService` (`src/midi/FfxivEqualizerService.{h,cpp}`):
  * Owns the active preset.
  * Maps GM-program / drum-kit -> gain factor.
  * Exposes `gainFor(program, isDrum) -> float`.
* `FluidSynthEngine`:
  * On every NoteOn, multiply velocity by `service->gainFor(...)`
    *before* sending to FluidSynth.
  * Respect mute by skipping the NoteOn entirely.
* `AudioExporter` (WAV/FLAC/MP3) reads from the same
  `FfxivEqualizerService` so the rendered file matches what the user
  hears live - no surprises.
* All hooks gated by `FFXIV SoundFont Mode is on` so non-FFXIV users
  pay zero overhead.

### 39.5 - Default preset philosophy

* Curate the shipped FFXIV Default preset based on what
  sounds balanced in-game. The defaults should:
  * Tame the drum kit roughly -3 dB (it overwhelms melodic tracks).
  * Leave Lute/Harp at 100 percent (reference tracks).
  * Trim brass +/- a few percent for parity with strings.
* Document the curated values in `Planning/06_FFXIV_Equalizer.md`
  so future preset bumps are explainable.

### 39.6 - Files & touchpoints

* New:
  * `src/midi/FfxivEqualizerService.{h,cpp}`
  * `src/gui/FfxivEqualizerDialog.{h,cpp}`
  * `tests/test_ffxiv_equalizer_service.cpp`
  * `manual/ffxiv-equalizer.html`
  * `Planning/06_FFXIV_Equalizer.md` (default-preset rationale)
* Modified:
  * `src/midi/FluidSynthEngine.{h,cpp}` (NoteOn gain hook)
  * `src/midi/AudioExporter.cpp` (export uses same service)
  * `src/gui/MainWindow.cpp` (Midi menu entry, dialog launcher)
  * `src/ai/ToolDefinitions.cpp` (optional read-only AI tool: see 39.8)

### 39.7 - Safety / edge cases

* Velocity clamp: scaled velocity must stay within 1..127
  (zero becomes 1 to keep the NoteOn alive; mute uses the dedicated
  mute path).
* Live preview must debounce slider drags (~100 ms) so FluidSynth
  doesn't get spammed.
* Renaming or deleting the active preset gracefully falls back to
  "FFXIV Default".
* Backup-on-overwrite for user presets (one-level undo via QSettings).

### 39.8 - Optional AI tool (post-MVP)

* nalyze_mix_balance(start_tick, end_tick) - read-only report
  that compares average velocity per channel to the active
  Equalizer preset and flags channels likely to clip or get drowned
  out.
* Useful for AI-assisted mixing prompts: *"the lead lute is too
  quiet vs the drums - suggest preset adjustments."*

### 39.9 - Open decisions for review

1. **Per-channel vs per-instrument vs per-program?** Recommendation:
   per-program (so the same Lute on CH1 and CH5 shares a slider).
   Cleaner UX and matches how the SoundFont is actually structured.
2. **Should presets travel with the .mid file?** No - keep them
   per-install. A .mid file is shared between users; the mix
   preference is personal.
3. **Future: A/B comparison?** Keep two slots and let the user
   toggle between them. Defer to 1.7.x if MVP lands well.
4. **FFXIV Mode coupling?** Auto-disable the dialog when FFXIV Mode
   is off, mirroring the Voice Limiter auto-bind from 1.6.0.

