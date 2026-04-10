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

## Phase 21: Lyric Editor ⬜

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
> **Status:** Planning. Not yet implemented.

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
Phase 21.1   LyricTimelineWidget (Display + Scroll Sync)           ⬜
Phase 21.2   Lyric Blocks (Data Model + TextEvent Integration)     ⬜
Phase 21.3   SRT Import/Export                                      ⬜
Phase 21.4   Text Import Dialog (Paste + Manual Editor)             ⬜
Phase 21.5   Lyric Sync Mode (Tap-to-Sync during Playback)         ⬜
Phase 21.6   Interactive Editing (Drag, Resize, Edit in Place)      ⬜
Phase 21.7   MIDI Lyric Embedding (Export as Meta Events)           ⬜
Phase 21.8   UI Integration (Menu, Toolbar, Toggle, Settings)       ⬜
Phase 21.9   LRC Export (FFXIV MidiBard2 Lyric Format)              ⬜
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

### 21.1 — LyricTimelineWidget (Display + Scroll Sync) ⬜

> **Goal:** A new widget that displays lyric blocks as colored rectangles along the
> time axis. Horizontal scrolling is synchronized with MatrixWidget and MiscWidget.
> Togglable show/hide.

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

### 21.2 — Lyric Blocks (Data Model + TextEvent Integration) ⬜

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

### 21.3 — SRT Import/Export ⬜

> **Goal:** Read and write SRT files. Import creates LyricBlocks,
> export writes LyricBlocks as SRT file.

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

### 21.4 — Text Import Dialog (Paste + Manual Editor) ⬜

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

### 21.5 — Lyric Sync Mode (Tap-to-Sync during Playback) ⬜

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

### 21.6 — Interactive Editing (Drag, Resize, Edit in Place) ⬜

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

### 21.7 — MIDI Lyric Embedding (Export as Meta Events) ⬜

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

### 21.8 — UI Integration (Menu, Toolbar, Toggle, Settings) ⬜

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

### 21.9 — LRC Export (FFXIV MidiBard2 Lyric Format) ⬜

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