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
Phase 4.6  Persistent history (SQLite)                  ⬜ TODO  (low priority)
Phase 8    Multi-provider & free API access              ✅ DONE (8.1, 8.2, 8.5 — providers + tokens + model lists)
Phase 9    Editable system prompts (JSON + dialog)       ✅ DONE (9.1-9.4)
Phase 10   Independent repo & rebranding                 ✅ DONE (10.1-10.7)
Phase 11   FFXIV Channel Fix (deterministic fixer + UI)   ✅ DONE (11.1-11.4)
Phase 11.5 FFXIV Channel Fix v2 — 3-Tier Detection       ✅ DONE (v1.1.0 + v1.1.2)
Phase 12   Prompt Architecture v2                         ⬜ TODO (12.1-12.7)
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
