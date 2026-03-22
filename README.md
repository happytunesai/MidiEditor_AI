<img align="left" width="70px" src="run_environment/midieditor.ico">

# MidiEditor

[![build windows](https://github.com/Meowchestra/MidiEditor/actions/workflows/xmake.yml/badge.svg)](https://github.com/Meowchestra/MidiEditor/actions/workflows/xmake.yml)

Downloads : [latest release](https://github.com/Meowchestra/MidiEditor/releases)

Project Page & Manual : [https://meowchestra.github.io/MidiEditor/](https://meowchestra.github.io/MidiEditor/)

Updated MidiEditor with additional improvements, custom changes, & merged pull-requests ahead of upstream. Based on [jingkaimori's fork](https://github.com/jingkaimori/midieditor/) of [ProMidEdit](https://github.com/PROPHESSOR/ProMidEdit), built on top of [MidiEditor](https://github.com/markusschwenk/midieditor).

### Introduction

MidiEditor is a free software providing an interface to edit, record, and play midi data.

The editor is able to open existing midi files and modify their content. New files can be created and the user can enter his own composition by either recording Midi data from a connected Midi device (e.g., a digital piano or a keyboard) or by manually creating new notes and other Midi events. The recorded data can be easily quantified and edited afterwards using MidiEditor.

![image](manual/screenshots/midieditor-full.png)

### Features

* Easily edits, records and plays Midi files
* Can be connected to any Midi port (e.g., a digital piano or a synthesizer)
* Tracks, channels and Midi events can be edited
* Event quantization
* Control changes can be visualized
* Free
* Available for Windows

---

## MidiPilot — AI-Powered MIDI Assistant

MidiPilot is an integrated AI copilot that can compose, edit, and transform MIDI data using natural language. It is embedded directly in the MidiEditor interface and communicates with OpenAI's API to understand musical intent and execute changes on the active MIDI file.

### Getting Started

1. Open **Settings** and enter your OpenAI API key.
2. Select a model (default: gpt-5.4) and configure reasoning effort.
3. Open the **MidiPilot** panel from the sidebar.
4. Type a natural language instruction (e.g. *"Create an 8-bar jazz waltz in Bb major"*) and press Enter.

### Modes

| Mode | Description |
|------|-------------|
| **Simple** | Single request/response. The model generates all tool calls in one shot. Best for small edits and quick tasks. |
| **Agent** | Multi-step agentic loop. The model calls tools iteratively, inspecting results between steps. The maximum number of steps is configurable in Settings (default: 50). |

### FFXIV Bard Performance Mode

When the **FFXIV** checkbox is enabled, MidiPilot enforces the constraints of Final Fantasy XIV's Bard Performance system:

- **8-track maximum** (octet ensemble)
- **Monophonic tracks** — one note at a time per instrument
- **Note range C3–C6** (MIDI 48–84) — MidiBard2 auto-transposes to each instrument's native range
- **No drum kit** — percussion is split into separate tonal tracks (Bass Drum, Snare Drum, Cymbal, Timpani, Bongo)
- **Guitar switches** — 5 electric guitar variants can share a track slot using channel-based switching
- **Automatic channel pattern setup** — assigns unique channels and program_change events for MidiBard2 compatibility
- **Velocity is ignored** — FFXIV plays all notes at the same loudness

### Supported Models

MidiPilot supports a range of OpenAI models with configurable reasoning effort:

- **gpt-5.4 / gpt-5.4-mini / gpt-5.4-nano** — Latest generation, reasoning-capable
- **gpt-5 / gpt-5-mini** — Previous generation reasoning models
- **gpt-4.1-nano / gpt-4.1-mini / gpt-4.1** — Efficient non-reasoning models
- **gpt-4o / gpt-4o-mini** — Fast general-purpose models
- **o4-mini** — Dedicated reasoning model

### Tools

MidiPilot has access to the following tools for inspecting and modifying MIDI files:

- `get_editor_state` — Read current file info, tracks, tempo, time signature, cursor position
- `create_track` / `rename_track` / `set_channel` — Manage tracks
- `insert_events` / `replace_events` / `delete_events` — Add, modify, or remove MIDI events
- `query_events` — Read events in a tick range on a track
- `move_events_to_track` — Move events between tracks
- `set_tempo` / `set_time_signature` — Change tempo and meter
- `setup_channel_pattern` *(FFXIV)* — Auto-configure MidiBard2 channel mapping
- `validate_ffxiv` *(FFXIV)* — Check FFXIV rule compliance
- `convert_drums_ffxiv` *(FFXIV)* — Convert GM drum tracks to FFXIV tonal percussion
