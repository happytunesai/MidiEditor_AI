# Changelog — MidiEditor AI

All notable changes to MidiEditor AI are documented here.
Releases: https://github.com/happytunesai/MidiEditor_AI/releases

---

## [1.1.2.1] - 2026-03-26 — Hotfix: SoundFont Persistence & CI Fix

### Fixed
* **SoundFont stack lost when switching MIDI output** — switching from FluidSynth to another output (e.g. Microsoft GS Wavetable) and back no longer loses loaded SoundFonts; the engine now preserves font paths across shutdown/reinitialize cycles
* **CI/Release workflow: FluidSynth download 404** — updated FluidSynth v2.5.2 asset URL to match upstream's renamed zip (`fluidsynth-v2.5.2-…` instead of `fluidsynth-2.5.2-…`)

---

## [1.1.2] - 2026-03-26 — FluidSynth & FFXIV SoundFont Mode

### Added
* **Built-in FluidSynth synthesizer** (upstream merge from [Meowchestra/MidiEditor](https://github.com/Meowchestra/MidiEditor)) — no external softsynth needed. Select *FluidSynth (Built-in Synthesizer)* as MIDI output and load any SF2/SF3 SoundFont directly in Settings
* **SoundFont stack management** (upstream) — load multiple SoundFonts with drag-and-drop priority ordering; highest-priority font is checked first for presets
* **SoundFont download dialog** (upstream) — one-click download of recommended SoundFonts (General MIDI, FFXIV) from within the application
* **FFXIV SoundFont Mode** — single toggle in FluidSynth settings that:
  - Sets all 16 MIDI channels to melodic (bank 0) for FFXIV SoundFonts where percussion uses melodic presets
  - Injects per-note program changes on CH9 based on track name, so each drum instrument (Snare Drum, Bass Drum, Cymbal, Bongo, Timpani) plays with the correct SoundFont preset — no MIDI file modification needed
* **Velocity normalization** in Fix X|V Channels — Tier 2 (Rebuild) and Tier 3 (Preserve) now set all NoteOn velocities to 127 (max), since FFXIV performance has no dynamics
* **Manual: SoundFont & FluidSynth page** — new documentation section covering built-in synth setup, SoundFont management, FFXIV SoundFont Mode, and audio settings
* **Version in title bar** — application window title now shows `MidiEditor AI v1.1.2` (with version number) on startup, file open, save, and new document

### Fixed
* **Fix X|V Channels Rebuild mode: guitar notes stuck on wrong channel** — removed an incorrect guitar-channel exemption that prevented notes from being moved to their target channel when the source channel belonged to another guitar variant (e.g., Track 5 PowerChords notes stayed on CH1 instead of moving to CH5)
* **Transpose dialog button order** — swapped Cancel/Accept buttons back to original MidiEditor layout (Cancel left, Accept right) to match muscle memory from the original editor

### Changed
* **Fix X|V Channels** — "All Channels Melodic (FFXIV)" checkbox renamed to **"FFXIV SoundFont Mode"** with updated tooltip explaining both melodic channels and drum program injection
* **Fix X|V Channels result dialog** — now shows velocity normalization count (🔊 Normalized X note velocity(ies) to 127)
* **Fix X|V Channels tier descriptions** — both Rebuild and Preserve modes now list velocity normalization as a bullet point
* Manual: updated Fix X|V Channels page with new screenshots and velocity normalization info
* README: added FluidSynth / SoundFont section and updated Features table
* Version bump to 1.1.2

---

## [1.1.1] - 2026-03-25 — Upstream merge

### Changed
* **Metronome rewrite** — replaced `QSoundEffect` audio playback with General MIDI drum notes on Channel 10 (High/Low Wood Block). No more WAV file dependency, works through the connected MIDI output device. Downbeats and regular beats now use distinct drum sounds
* **Metronome timing** — all PlayerThread→Metronome signal connections now use `Qt::DirectConnection` for tighter timing in the audio thread
* **Removed Qt6::Multimedia and Qt6::Xml** dependencies — fewer Qt modules required, smaller deployment footprint
* **Plugin path fix** — `QCoreApplication::addLibraryPath(appDir + "/plugins")` added before QApplication construction; windeployqt now deploys to `plugins/` subdirectory for reliable Qt plugin discovery
* **rtmidi updated** — submodule bumped to latest upstream (`a3233c2`)
* Version bump to 1.1.1

### Removed
* Metronome WAV/MP3 audio files (no longer needed — metronome clicks via MIDI)
* Qt6::Xml and Qt6::Multimedia from build dependencies

---

## [1.1.0] - 2026-03-25

### Added
* **Fix X|V Channels** — one-click deterministic FFXIV channel fixer (no AI call needed):
  - Toolbar button + menu entry under Tools → Fix X|V Channels
  - 5-step algorithm: Analyze → Clean all program_change → Migrate channels → Program mapping → Report
  - Guitar tracks automatically get 5 channels for variant switching (Clean, Muted, Overdriven, PowerChords, Special)
  - **Confirmation dialog** with two modes: Rebuild (Full Reassignment) or Preserve (Minimal Changes), auto-detected based on file analysis
  - **Rich result summary** — HTML-formatted info panel showing channel mapping table, removed/inserted program changes, track renames, and undo hint
  - **Progress dialog** — shows percentage and phase description during channel fix to prevent apparent UI freeze on large files
  - Entire operation wrapped in a single undo action
* `FFXIVChannelFixer` class — static deterministic fixer delegated from `setup_channel_pattern` tool
* Toolbar migration code ensures Fix X|V button appears for existing users

### Fixed
* **QSettings constructor mismatch** — FFXIV tools (`setup_channel_pattern`, `validate_ffxiv`, `convert_drums_ffxiv`) were never sent to the LLM because `ToolDefinitions` read from a different registry path than `MidiPilotWidget` wrote to. Both now use `QSettings("MidiEditor", "NONE")`
* **Stale channel menus** — "Move events to channel" and similar channel context menus now refresh after Fix X|V Channels (added `updateChannelMenu()` to `updateAll()`)

### Changed
* **Fix X|V Channels dialog** — removed redundant Tier 1 (Abort) option; Abort button already cancels. Tier labels simplified to "Rebuild (Full Reassignment)" and "Preserve (Minimal Changes)" without tier numbers
* **Fix X|V Channels result** — replaced debug text popup with structured HTML info log showing success status, channel mapping table, program change stats, and track renames
* FFXIV system prompts simplified — removed verbose channel/program tables, now instructs the LLM to call `setup_channel_pattern` once
* UI label renamed from "Fix FFXIV Channels" to "Fix X|V Channels" across all dialogs and menus
* README: added Fix X|V Channels to Features table, FFXIV constraint table, and Tools reference
* Manual: new Fix X|V Channels section in MidiPilot page with before/after screenshots and animated GIF
* Manual: new Fix X|V Channels entry in Tools Menu page
* Manual: separate Rebuild and Preserve GIF animations replacing single animation
* **Version single source of truth** — `setApplicationVersion()` now reads from CMake define `MIDIEDITOR_RELEASE_VERSION_STRING_DEF` instead of a hardcoded string. Only update version in `CMakeLists.txt` line 3
* Version bump to 1.1.0

---

## [1.0.2] - 2026-03-24

### Changed
* FFXIV Bard Mode system prompts rewritten for better LLM compliance:
  - Strict 8-track maximum enforced in both full and compact prompts
  - Clear Track→Channel mapping (T0→CH0, T1→CH1, etc.; drums share CH9)
  - Concrete 8-track Octett example with guitar switch channels
  - All tracks must include program_change events for ALL used channels at tick 0
  - Guitar switches clarified: 5 variants share channels, no extra tracks needed
  - Track name determines instrument — channels are cosmetic except for guitar switches
  - Drum tracks ordered at end (highest track indices)
* Version bump to 1.0.2

---

## [1.0.1] - 2026-03-24

### Changed
* New Chat button: replaced ambiguous ➕ icon with a dedicated "message-plus" icon for clarity
* New Chat now shows a confirmation dialog when conversation history exists
* Manual: added Prompt Examples page with real-world prompts, screenshots, and demo videos
* Manual: added API Log documentation to MidiPilot page
* Manual: added navigation links on all pages for Prompt Examples
* MidiPilot page hero image updated to animated Agent Run GIF
* Manual: added lo-fi hip hop example files (MIDI + WAV) as downloadable examples
* FFXIV example prompts improved: removed redundant MidiBard2 references and over-specified constraints
* YouTube demo videos embedded on Prompt Examples page (4 videos: full agent run, audio preview, guitar solo, harmony)
* Version bump to 1.0.1

---

## [1.0.0] - 2026-03-20

### MidiPilot AI Assistant (New)
* **MidiPilot** — integrated AI chat panel for composing, editing, and transforming MIDI
* **Agent Mode** — multi-step autonomous AI with 13 tool functions (create tracks, insert/replace/delete events, set tempo, transpose, etc.)
* **Simple Mode** — single-step AI for quick edits with lower token usage
* **FFXIV Bard Mode** — toggle for Final Fantasy XIV performance constraints (C3-C6 range, monophonic, 8-track limit, instrument validation)
* **FFXIV Validation tool** — checks and auto-fixes MIDI files for FFXIV compliance
* **GM Drum Conversion tool** — splits GM drum tracks into separate FFXIV tonal drum tracks
* **Multi-Provider Support** — OpenAI, Google Gemini, OpenRouter, Groq, Ollama, LM Studio, or any custom OpenAI-compatible endpoint
* **Token Usage Tracking** — displays per-request and session token counts in the status bar
* **Editable System Prompts** — customize AI behavior via JSON file or built-in editor dialog
* **API Logging** — all API requests/responses logged to `midipilot_api.log` for debugging
* **Reasoning Display** — shows AI thinking/reasoning tokens in a collapsible section

### App & Infrastructure
* Rebranded from MeowMidiEditor to **MidiEditor AI**
* About dialog credits full upstream chain (Markus Schwenk → ProMidEdit → Meowchestra → MidiEditor AI)
* Update checker redirected to `happytunesai/MidiEditor_AI` releases
* GitHub Actions CI/CD: automated build on push + release workflow on tag
* GitHub Pages manual/wiki deployed automatically
* Dark-themed manual with MidiPilot documentation, screenshots, and getting started guide

### Based On
* [Meowchestra/MidiEditor](https://github.com/Meowchestra/MidiEditor) v4.3.1 — all upstream features included:
  - Strummer tool, Glue tool, Scissors tool, Delete Overlaps tool
  - Explode Chords to Tracks, Convert Pitch Bends
  - Shared Clipboard (cross-instance copy/paste)
  - Custom keybinds, drag & drop track reordering
  - Windows dark mode, customizable toolbar
  - Hardware-accelerated rendering, Qt 6.5.3
  - Vertical out/in updated to ``shift`` + ``-/=`` to match Qt vertical mouse scrolling on shift.
  - Scroll multiplier with ``ctrl`` + ``shift`` + ``mousewheel``. Default zoom now only scrolls 1 line at a time.
  - Full reset view with ``ctrl`` + ``shift`` + ``backspace`` and updated zoom reset to ``ctrl`` + ``backspace``.
* Numerous fixes such as sustain notes disappearing if the start & end times were outside the view, sustains partially outside the view visually clipping the note length when dragged back into view, dead scrolls when zoomed in too far, cancelling color selection setting color black instead of properly cancelling, opacity not applying to custom colors set after opacity adjustment, and more.

</details>

## [4.0.0] - 2025-07-22
<details>
<summary>Summary</summary>

Substantially updated MidiEditor that doesn't have as many memory leaks. Numerous changes and improvements made prior to v4.0, pre-changelog.

Some notable features:

* Support for all 3 main strip styling people may want. By keys, alternating, and by octave (c).
* Added an option to highlight c3/c6 range lines so you can more easily tell if notes are outside the range.
* You can quickly transpose notes up/down octaves by selecting them and then ``shift + up/down``.
* You can quickly update note on/off tick duration times by selecting notes and then holding ``ctrl + drag left/right edge``
* You can quickly move events to a different track by ``shift + #``
* You can lock position movement when dragging notes around while holding ``shift`` to go up/down or ``alt`` to go left/right.
* Support for different application styles if you have a specific preference.

Most noticeably should no longer see the app go to 1gb+ when making major edits such as bulk deleting and undoing over and over. Should be a bit more lightweight with less spikes overall from optimizations / qt6. Also less crashing when loading rare malformed midis / long track names.

Plus other improvements from various forks and imported changes to be discovered.

</details>
