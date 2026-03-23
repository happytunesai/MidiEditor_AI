# MidiEditor AI

```
███╗   ███╗██╗██████╗ ██╗███████╗██████╗ ██╗████████╗ ██████╗ ██████╗      █████╗ ██╗
████╗ ████║██║██╔══██╗██║██╔════╝██╔══██╗██║╚══██╔══╝██╔═══██╗██╔══██╗    ██╔══██╗██║
██╔████╔██║██║██║  ██║██║█████╗  ██║  ██║██║   ██║   ██║   ██║██████╔╝    ███████║██║
██║╚██╔╝██║██║██║  ██║██║██╔══╝  ██║  ██║██║   ██║   ██║   ██║██╔══██╗    ██╔══██║██║
██║ ╚═╝ ██║██║██████╔╝██║███████╗██████╔╝██║   ██║   ╚██████╔╝██║  ██║    ██║  ██║██║
╚═╝     ╚═╝╚═╝╚═════╝ ╚═╝╚══════╝╚═════╝ ╚═╝   ╚═╝    ╚═════╝ ╚═╝  ╚═╝    ╚═╝  ╚═╝╚═╝
```

**An AI-powered MIDI editor with an integrated AI copilot.**

[![Build](https://github.com/happytunesai/MidiEditor_AI/actions/workflows/cmake-build.yml/badge.svg)](https://github.com/happytunesai/MidiEditor_AI/actions/workflows/cmake-build.yml)
[![Release](https://img.shields.io/github/v/release/happytunesai/MidiEditor_AI?include_prereleases&label=release)](https://github.com/happytunesai/MidiEditor_AI/releases/latest)
[![License: GPL-3.0](https://img.shields.io/badge/license-GPL--3.0-blue.svg)](LICENSE)
[![Platform: Windows](https://img.shields.io/badge/platform-Windows-0078D6?logo=windows)](https://github.com/happytunesai/MidiEditor_AI/releases)

**Version:** 1.0.0
**Status:** Release

📥 **[Download Latest Release](https://github.com/happytunesai/MidiEditor_AI/releases/latest)**

---

![MidiEditor AI](manual/screenshots/midieditor-full.png)

## ✨ Overview

MidiEditor AI is a free MIDI editor with **MidiPilot** — an integrated AI copilot that can compose, arrange, analyze, and edit MIDI data using natural language. Tell the AI what you want in plain English and watch it build your music.

Built on top of [Meowchestra/MidiEditor](https://github.com/Meowchestra/MidiEditor), which traces back through [jingkaimori](https://github.com/jingkaimori/midieditor/), [ProMidEdit](https://github.com/PROPHESSOR/ProMidEdit), and the original [MidiEditor](https://github.com/markusschwenk/midieditor) by Markus Schwenk.

## 🤖 MidiPilot — Your AI Copilot

MidiPilot is the AI brain embedded directly in MidiEditor AI. Open the sidebar, type what you want in plain English, and it builds your music — composing, editing, transforming, and analyzing MIDI events automatically.

<p align="center">
  <img src="docs/images/midipilot-overview.png" alt="MidiPilot integrated in MidiEditor AI" width="800"/>
  <br/>
  <i>MidiPilot panel docked alongside the piano roll editor</i>
</p>

### Chat Panel

The MidiPilot panel features a clean chat interface with context-aware track info, mode selection, and model switching — all accessible without leaving the editor.

<p align="center">
  <img src="docs/images/midipilot-panel.png" alt="MidiPilot chat panel" width="350"/>
  <img src="docs/images/midipilot-panel-alt.png" alt="MidiPilot with different provider" width="350"/>
  <br/>
  <i>Docked panel (Gemini 2.5 Flash) — Alt panel (Gemini 3.1 Pro)</i>
</p>

<p align="center">
  <img src="docs/images/midipilot-input-bar.png" alt="MidiPilot input bar close-up" width="600"/>
  <br/>
  <i>Input bar with Agent/Simple mode toggle, FFXIV checkbox, model selector & reasoning level</i>
</p>

### AI Settings

Configure your provider, API key, model, reasoning effort, context range, and more — all from a dedicated settings page.

<p align="center">
  <img src="docs/images/midipilot-settings.png" alt="MidiPilot AI settings" width="500"/>
  <img src="docs/images/midipilot-connection-test.png" alt="Connection test successful" width="500"/>
  <br/>
  <i>Provider configuration — Connection test: ✅ Model: gemini-2.5-flash</i>
</p>

### Custom System Prompts

Edit the AI's behavior per mode (Simple / Agent / FFXIV / FFXIV Compact) without recompiling. Export/import as JSON for sharing.

<p align="center">
  <img src="docs/images/midipilot-system-prompts.png" alt="System Prompt Editor" width="500"/>
  <br/>
  <i>Built-in System Prompt Editor with tab-based mode selection</i>
</p>

---

## ✨ Features

| Feature | Description |
|---------|-------------|
| 🤖 **MidiPilot AI Copilot** | Compose, edit, and transform MIDI via natural language chat |
| 🎹 **Full MIDI Editor** | Edit, record, and play MIDI files with track/channel/event editing |
| 🎯 **Agent Mode** | Multi-step agentic loop — AI calls tools iteratively, inspecting results between steps |
| 💬 **Simple Mode** | Single request/response for quick edits and small tasks |
| 🎮 **FFXIV Bard Mode** | Enforces Final Fantasy XIV Performance constraints (8 tracks, monophonic, C3–C6) |
| 🔌 **Multi-Provider** | OpenAI, OpenRouter, Google Gemini, or any OpenAI-compatible endpoint |
| 🧠 **Reasoning Support** | Configurable thinking/reasoning effort (None → Extra High) |
| 📊 **Token Tracking** | Real-time token usage display per request and session |
| ✏️ **Custom System Prompts** | Edit AI behavior via JSON — no recompiling needed |
| 🔄 **Auto-Update Checker** | Built-in update notifications from GitHub Releases |
| 🎵 **Quantization** | Event quantization and control change visualization |
| 🎤 **MIDI Recording** | Record from connected MIDI devices (keyboards, digital pianos) |

## 🏗️ Architecture

```
MidiEditor AI
├── Core Editor          → MIDI file I/O, tracks, channels, events
├── MatrixWidget         → Piano roll note editor with OpenGL rendering
├── MidiPilot            → AI copilot sidebar (chat + tools)
│   ├── AiClient         → OpenAI-compatible API client (streaming)
│   ├── EditorContext     → Musical context extraction for AI
│   ├── ToolDefinitions   → 13 MIDI manipulation tools for AI
│   └── SystemPrompts     → Customizable per-mode AI instructions
├── FFXIV Module         → Bard Performance validation & drum conversion
├── Multi-Provider       → OpenAI / OpenRouter / Gemini / Custom / Local
├── MIDI I/O             → RtMidi for real-time MIDI device communication
└── Settings             → Provider, model, appearance, keybinds, layout
```

## 🚀 Quick Start

### 1. Download & Run

1. Download the latest release from the [**Releases page**](https://github.com/happytunesai/MidiEditor_AI/releases/latest)
2. Extract the zip file
3. Run **MidiEditorAI.exe**

### 2. Configure AI

1. Open **Settings** (gear icon) and go to the **AI** tab
2. Select your provider (OpenAI, OpenRouter, Google Gemini, or Custom)
3. Enter your API key
4. Choose a model

### 3. Start Creating

1. Open the **MidiPilot** panel from the sidebar
2. Type a natural language instruction:
   > *"Create an 8-bar jazz waltz in Bb major with piano, bass, and drums"*
3. Press Enter and watch the AI build your music

## 🎮 FFXIV Bard Performance Mode

When the **FFXIV** checkbox is enabled, MidiPilot enforces Final Fantasy XIV constraints:

| Constraint | Rule |
|------------|------|
| 🎵 **Tracks** | Maximum 8 tracks (octet ensemble) |
| 🔊 **Polyphony** | Monophonic only — one note at a time per instrument |
| 🎹 **Range** | C3–C6 (MIDI 48–84) — MidiBard2 auto-transposes |
| 🥁 **Drums** | No drum kit — separate tonal tracks (Bass Drum, Snare, Cymbal, etc.) |
| 🎸 **Guitars** | 5 electric guitar variants via channel-based switching |
| ⚡ **Auto-Setup** | Channel pattern tool configures MidiBard2 mapping automatically |

## 🔌 Supported Providers

| Provider | Base URL | API Key | Free Tier |
|----------|----------|---------|-----------|
| **OpenAI** | `api.openai.com/v1` | Required | Limited |
| **OpenRouter** | `openrouter.ai/api/v1` | Required | Free models available |
| **Google Gemini** | `generativelanguage.googleapis.com` | Required | 15 RPM, 1M TPM |
| **Ollama** (local) | `localhost:11434/v1` | No | Unlimited |
| **LM Studio** (local) | `localhost:1234/v1` | No | Unlimited |
| **Custom** | User-specified | User-specified | Varies |

## 🛠️ MidiPilot Tools

The AI has access to 13 tools for inspecting and modifying MIDI files:

| Tool | Description |
|------|-------------|
| `get_editor_state` | Read file info, tracks, tempo, time signature, cursor |
| `create_track` / `rename_track` / `set_channel` | Manage tracks |
| `insert_events` / `replace_events` / `delete_events` | Add, modify, remove MIDI events |
| `query_events` | Read events in a tick range on a track |
| `move_events_to_track` | Move events between tracks |
| `set_tempo` / `set_time_signature` | Change tempo and meter |
| `setup_channel_pattern` | Auto-configure MidiBard2 channel mapping *(FFXIV)* |
| `validate_ffxiv` | Check FFXIV rule compliance *(FFXIV)* |
| `convert_drums_ffxiv` | Convert GM drums to FFXIV tonal percussion *(FFXIV)* |

## ⚙️ Settings

| Setting | Description |
|---------|-------------|
| **Provider & Model** | Select AI provider and model. Custom models can be typed manually. |
| **Thinking / Reasoning** | Toggle reasoning and set effort level (None → Extra High) |
| **Context Range** | Measures before/after cursor included as musical context (0–50) |
| **Agent Max Steps** | Maximum tool calls per Agent request (5–100, default 50) |
| **Output Token Limit** | Optional cap on output tokens to control costs |
| **System Prompts** | Customize AI behavior for each mode via built-in editor |

## 🎬 Examples

Compositions created entirely by AI in **Agent mode** — from a single text prompt to a finished MIDI file.

---
![Loading](examples/loading_animation.gif)
---

### Metal Mozart — Agent Mode (Gemini 3.1 Pro)

> *"Create a metal version of Mozart's Eine kleine Nachtmusik with shredding guitars, bass, strings, and a drum kit. Make it 20 measures long, with a guitar solo in the middle."*

![Metal Mozart by Gemini Agent](examples/Mozart_by_gemini_agent.png)

https://github.com/user-attachments/assets/f0113a74-14d4-47c1-945e-6fc61fa2fbc6

📥 [Download MIDI](examples/Mozart_by_gemini_agent.mid)

---

### Metal Mozart — FFXIV Octett Mode (Gemini 3.1 Pro)

> *"Create a metal version of Mozart's Eine kleine Nachtmusik with shredding guitars, bass, strings, and a drum kit. Make it 20 measures long, with a guitar solo in the middle. 8 Tracks ready for a FFXIV Octett"*

![Metal Mozart FFXIV by Gemini Agent](examples/Mozart_by_gemini_agent_FFXIV.png)

https://github.com/user-attachments/assets/789ad60b-69d1-46aa-acce-4970f6e2acf2

📥 [Download MIDI](examples/Mozart_by_gemini_agent_FFXIV.mid)

---

## 🔨 Building from Source

### Requirements

- **Windows 10/11**
- **Visual Studio 2019/2022** (MSVC Build Tools)
- **Qt 6.5.3** (with Multimedia module)
- **CMake 3.20+**

### Build

```bash
# Clone with submodules (RtMidi)
git clone --recursive https://github.com/happytunesai/MidiEditor_AI.git
cd MidiEditor_AI

# Configure and build
cmake -S . -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

The executable will be at `build/bin/MidiEditorAI.exe` with Qt DLLs auto-deployed.

## 📂 Project Structure

```
MidiEditor_AI/
├── src/
│   ├── main.cpp              # Application entry point
│   ├── gui/                   # UI components
│   │   ├── MainWindow.*       # Main application window
│   │   ├── MidiPilotWidget.*  # AI copilot sidebar
│   │   ├── AiSettingsWidget.* # AI provider/model settings
│   │   ├── MatrixWidget.*     # Piano roll editor
│   │   ├── AboutDialog.*      # Credits & version info
│   │   └── ...                # 40+ GUI components
│   ├── midi/                  # MIDI file I/O & devices
│   │   ├── MidiFile.*         # MIDI file read/write
│   │   ├── MidiInput.*        # Real-time MIDI input (RtMidi)
│   │   └── ...
│   ├── MidiEvent/             # MIDI event types
│   ├── protocol/              # Undo/redo protocol
│   └── tool/                  # Editor tools (select, draw, etc.)
├── run_environment/           # Runtime assets (graphics, metronome, icons)
├── manual/                    # HTML documentation & screenshots
├── examples/                  # AI-generated MIDI examples
├── .github/workflows/         # CI/CD (build + release)
├── CMakeLists.txt             # CMake build configuration
└── build.bat                  # Local build script
```

## 🤝 Credits & Acknowledgments

MidiEditor AI is built on the shoulders of these projects:

| Project | Author | Link |
|---------|--------|------|
| **MidiEditor** (upstream) | Meowchestra | [github.com/Meowchestra/MidiEditor](https://github.com/Meowchestra/MidiEditor) |
| **MidiEditor** (original) | Markus Schwenk | [github.com/markusschwenk/midieditor](https://github.com/markusschwenk/midieditor) |
| **ProMidEdit** | PROPHESSOR | [github.com/PROPHESSOR/ProMidEdit](https://github.com/PROPHESSOR/ProMidEdit) |
| **jingkaimori fork** | jingkaimori | [github.com/jingkaimori/midieditor](https://github.com/jingkaimori/midieditor) |

**Third-party resources:**
- 3D icons by Double-J Design
- Flat icons by Freepik
- Metronome sound by Mike Koenig
- RtMidi library by Gary P. Scavone

See the full list of contributors in the [CONTRIBUTORS](CONTRIBUTORS) file.

## 📄 License

This project is licensed under the [GNU General Public License v3.0](LICENSE).

## 👀 Contact

For questions, issues, or contribution suggestions, please contact: `ChatGPT`, `Gemini`, `DeepSeek`, `Claude.ai` 🤖
or try to dump it [here](https://github.com/happytunesai/MidiEditor_AI/issues)! ✅

**GitHub:** [github.com/happytunesai/MidiEditor_AI](https://github.com/happytunesai/MidiEditor_AI)

---

*Created with ❤️ + AI*


