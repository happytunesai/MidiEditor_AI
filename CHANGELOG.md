# Changelog — MidiEditor AI

All notable changes to MidiEditor AI are documented here.
Releases: https://github.com/happytunesai/MidiEditor_AI/releases

---

## [1.1.7] - 2026-04-05 — The Totally Unnecessary Glow Up

> *Adding Dark/Light Mode and a totally useless but cool MIDI Visualizer.*

### Added
* **Dark & Light QSS Themes** — full application theming with seven modes:
  - **Dark** — deep blue-black palette (`#0d1117` bg, `#58a6ff` accent) for late-night editing sessions
  - **Light** — clean white palette (`#ffffff` bg, `#0969da` accent) for daytime use
  - **Sakura** — light cherry blossom theme (`#fff5f8` bg, `#db7093` accents) with slightly pink piano keys
  - **AMOLED** — pure black `#000000` backgrounds with orange `#e67e22` accents, optimized for OLED screens
  - **Material Dark** — charcoal `#1e1d23` backgrounds with teal `#04b97f` accents, Material Design aesthetic
  - **System** — auto-detects your OS dark/light preference
  - **Classic** — original system-native look, unchanged
  - Theme selector in Settings → Appearance
  - All standard Qt widgets styled: toolbars, lists, scrollbars, checkboxes, dialogs, menus
  - Custom-painted widgets (piano roll, velocity, misc) auto-adapt via `Appearance` color methods
* **Dark Title Bar (Windows)** — native Windows dark title bar using DWM API (`DWMWA_USE_IMMERSIVE_DARK_MODE`), applies to all windows and dialogs automatically
* **Theme Change Restart** — changing themes triggers app restart with confirmation dialog; reopens Settings → Appearance tab automatically via `--open-settings` CLI flag
* **MIDI Visualizer** — real-time 16-channel equalizer bars in the toolbar:
  - One bar per MIDI channel, green-to-blue color interpolation based on velocity
  - Smooth decay animation at ~30fps
  - Thread-safe: reads atomic `channelActivity` values written by the player thread
  - Polls `MidiPlayer::isPlaying()` directly — resilient to signal connection breaks
  - Togglable via Customize Toolbar settings
  - Custom `midi_visualizer.png` icon with dark mode auto-inversion
* **Note Bar Color Presets** — 10 one-click channel color schemes in Settings → Appearance:
  - Default, Rainbow, Neon, Fire, Ocean, Pastel, Sakura, AMOLED, Emerald, Punk
  - Preset selection persisted across sessions
* **Sakura piano keys** — white keys tinted lavender blush (`#FFF0F5`), black keys dark rose (`#502837`), with matching hover/selected/highlight states
* **Toolbar icon dark mode adjustment** — black toolbar icons automatically recolored to light gray in dark themes for visibility (colored icons like FFXIV Fix, Explode Chords, MidiPilot preserved as-is)
* **Checkbox visibility in dark mode** — brighter borders for checkboxes in dark theme

### Fixed
* **Color preset combo always showing "Default"** — added `_colorPreset` persistence to QSettings; combo now correctly shows the saved preset when re-entering Settings
* **Piano key note labels unreadable in dark mode** — C1/C2/C3 octave labels on the piano bar changed from hardcoded `Qt::gray` to light gray (`QColor(200,200,200)`) in dark themes for readability

### Changed
* Toolbar inline styles refactored to use centralized `Appearance` helper methods for theme consistency
* Version bump to 1.1.7

### Technical Notes
* **New files:** `src/gui/themes/dark.qss`, `src/gui/themes/light.qss`, `src/gui/themes/pink.qss`, `src/gui/themes/amoled.qss`, `src/gui/themes/materialdark.qss`, `src/gui/MidiVisualizerWidget.h/cpp`, `run_environment/graphics/tool/midi_visualizer.png`
* **Core modifications:** `Appearance.h/cpp` (theme management, 7 themes, 10 color presets, piano key overrides, DWM dark title bar, icon adjustment), `AppearanceSettingsWidget.h/cpp` (theme selector + preset combo UI), `MainWindow.h/cpp` (toolbar theming, visualizer lifecycle, restart mechanism), `SettingsDialog.h/cpp` (`setCurrentTab()`), `main.cpp` (`--open-settings` CLI arg), `LayoutSettingsWidget.cpp` (visualizer in customize toolbar)
* **Visualizer lifecycle:** Widget created fresh on each toolbar rebuild via `toolbar->addWidget()` — avoids Qt `QWidgetAction` ownership bugs where `setDefaultWidget()` transfers ownership to the toolbar which destroys the widget on rebuild
* **AMOLED/Material themes inspired by:** [GTRONICK/QSS](https://github.com/GTRONICK/QSS) (MIT License) — color palettes adapted into our QSS structure

---

## [1.1.6.1] - 2026-04-04 — Bugfix: Duplicate Guitar Track Channels

### Fixed
* **Fix X|V Channels: duplicate guitar variants mapped to wrong channel** — when two or more tracks shared the same guitar variant name (e.g. two "ElectricGuitarPowerChords" tracks), the second track was assigned its own channel instead of sharing the first occurrence's channel. This caused the duplicate track to receive a wrong program (e.g. Piano on CH3 instead of Distortion Guitar). Now all tracks with the same guitar variant name share a single channel with the correct program change, in both Rebuild (Tier 2) and Preserve (Tier 3) modes
* **Fix X|V Channels Tier 3: duplicate guitar notes not migrated** — in Preserve mode, duplicate guitar tracks had their notes stranded on the original channel. Added note migration for duplicate guitar tracks to move events to the shared target channel

### Changed
* Version bump to 1.1.6.1

---

## [1.1.6] - 2026-04-04 — Guitar Pro Import (GP1–GP8)

### Added
* **Native Guitar Pro import** — all Guitar Pro formats from 1990s DOS to 2024 are now supported:
  - **GP3 (.gp3)** — Guitar Pro 3 binary format (v3.00)
  - **GP4 (.gp4)** — Guitar Pro 4 binary format (v4.00–v4.06), adds lyrics, RSE, key signatures
  - **GP5 (.gp5)** — Guitar Pro 5 binary format (v5.00–v5.10), adds RSE2, extended note effects
  - **GP6/GPX (.gpx)** — Guitar Pro 6 BCFZ-compressed GPIF XML
  - **GP7/GP8 (.gp)** — Guitar Pro 7/8 ZIP-packaged GPIF XML
  - **GP1 (.gtp)** — Guitar Pro 1 legacy DOS format (v1.0–v1.04, French header "GUITARE")
  - **GP2 (.gtp)** — Guitar Pro 2 legacy DOS format (v2.20–v2.21), adds triplet feel, repeat markers, capo
  - Header-based format detection — works even with misnamed file extensions
* **GP3/GP4/GP5 binary parser fixes** (Phase 16.1) — the upstream Meowchestra fork contained an unfinished Guitar Pro parser skeleton that failed on every real-world file. Fixed:
  - Added missing `Gp5Parser::readNoteEffects()` override for GP5-specific effect flags
  - Fixed field ordering in `readNote()` (`accentuatedNote`/`ghostNote` flags)
  - Added EOF boundary checking to prevent crashes on truncated files
* **GP6 BCFZ decompression rewrite** (Phase 16.2) — upstream used zlib `inflate()` on BCFZ data which is completely wrong (BCFZ is a custom bit-level LZ77 algorithm, not zlib/DEFLATE). Rewrote `decompressGPX()` with the correct algorithm, ported from C# BardMusicPlayer/LightAmp source code
* **GP7/GP8 ZIP extraction rewrite** (Phase 16.3) — upstream parsed local file headers which have unreliable sizes when ZIP data descriptors are present. Rewrote to parse the **central directory** from the end of the file for correct entry sizes
* **GPIF XML node lookup fix** (Phase 16.4) — `getSubnodeByName()` could find nested elements (e.g. `<Bars>` inside `<MasterBar>`) before the top-level collection, causing silent data loss. All lookups now use `directOnly=true`
* **GP1/GP2 legacy parser** (Phase 16.6) — new `Gp12Parser` classes ported from TuxGuitar's `GP1InputStream.java` / `GP2InputStream.java` reference implementation
* **Explode Chords to Tracks icon** — toolbar icon added for the Explode Chords tool

### Changed
* Version bump to 1.1.6

### Technical Notes
* **Origin:** Meowchestra/MidiEditor upstream contained a non-functional Guitar Pro parser (`src/converter/GuitarPro/`) with basic structure for GP3–GP7. Binary parsers had field-ordering bugs, BCFZ used the wrong algorithm, ZIP extraction failed on data descriptors, XML lookups returned wrong elements. All bugs were fixed, GP1/GP2 support was added from scratch.
* **Architecture:** Three parser families — binary (`Gp345Parser` inheritance chain), XML (`Gp678Parser` with BCFZ/ZIP decompression), and legacy (`Gp12Parser` inheritance chain). All share `GpImporter` as entry point with header-based detection.
* **Tested formats:** GP1 (You've Got Something There.gtp, 8 tracks/12 measures), GP3 (U2 - Lemon.gp3, 9/199), GP4 (Sakuran.gp4, 5/137), GP5 (Flogging-Molly.gp5, 5/74), GP6 (Sweet Child O Mine.gpx, 6/180), GP7 (The Mirror.gp, 6/147)

---

## [1.1.5] - 2026-03-31 — Auto-Updater

### Added
* **Auto-Updater** — seamless in-app update directly from GitHub Releases:
  - **Update Now**: downloads ZIP, saves your work, replaces the EXE in-place, and restarts automatically — your open MIDI file is reopened after restart
  - **After Exit**: downloads ZIP in the background, applies the update when you close the application
  - **Download Manual**: opens the GitHub release page in your browser for manual download
  - **Skip**: dismisses the update notification
  - Progress dialog with download size and percentage
  - Self-update approach: renames running EXE to `.bak`, extracts new files, launches new EXE — no external batch file or installer needed
  - Old `.bak` files are automatically cleaned up on next startup
* **Manual: Auto-Update section** updated with screenshots (Update dialog, Progress bar, Scheduled confirmation)

### Fixed
* **MidiPilot chat: Ctrl+C copy** — selected text in chat bubbles can now be copied with Ctrl+C (previously only right-click → Copy worked)
* **MidiPilot chat: context menu** — replaced the unstyled default system context menu with a compact dark-themed menu (Copy / Select All) that matches the application style

### Changed
* Feature table: "Auto-Update Checker" renamed to "Auto-Updater" reflecting the new in-app update capability
* Version bump to 1.1.5

---

## [1.1.4.1] - 2026-03-30 — Chat UX Fix

### Fixed
* **MidiPilot chat: Ctrl+C copy** — selected text in chat bubbles can now be copied with Ctrl+C (previously only right-click → Copy worked)
* **MidiPilot chat: context menu** — replaced the unstyled default system context menu with a compact dark-themed menu (Copy / Select All) that matches the application style

### Changed
* Version bump to 1.1.4.1

---

## [1.1.4] - 2026-03-29 — Split Channels to Tracks

### Added
* **Split Channels to Tracks** — convert single-track multi-channel GM MIDI files into one track per instrument:
  - Toolbar button + menu entry under Tools → Split Channels to Tracks (Ctrl+Shift+E)
  - Preview dialog showing all active channels with GM instrument names and note counts
  - Auto-names tracks from GM Program Change events (e.g. "Synth Bass 1", "Vibraphone", "Drums")
  - Option to keep Channel 9 (Drums) on the original track
  - Option to remove empty source track after split (preserves meta events automatically)
  - Configurable track insertion position (after source or at end)
  - Entire operation wrapped in a single undo action
  - Toolbar migration code ensures button appears for existing users with saved toolbar settings
* **Manual: Split Channels to Tracks page** added with screenshots and animated GIF

### Changed
* Version bump to 1.1.4

---

## [1.1.3.1] - 2026-03-29 — Auto-Save

### Added
* **Auto-Save** — automatic backup saves to prevent data loss during editing:
  - Saves a sidecar backup (`.autosave`) alongside your file after a configurable idle period
  - Untitled (never-saved) documents are backed up to `AppData/MidiEditor AI/autosave/`
  - Original file is never overwritten — the backup is a separate `.autosave` file
  - On next open, if a newer autosave backup exists, offers to recover it
  - On startup, checks for orphaned untitled backups from crashes and offers recovery
  - Backup is automatically deleted on normal save or clean exit
  - Auto-save events appear in the Protocol (undo history) panel as "Auto-saved" markers
* **Auto-Save settings** in Settings → System & Performance:
  - Enable/disable auto-save (default: enabled)
  - Configurable idle interval from 30 to 600 seconds (default: 120 sec)
* **Manual: Auto-Save section** added to MidiPilot documentation page with screenshot

### Changed
* Version bump to 1.1.3.1

---

## [1.1.3] - 2026-03-28 — Prompt Architecture v2, Crash Fix & Provider Selector

### Added
* **Provider selector in MidiPilot footer** — switch between OpenAI, OpenRouter, Gemini, and Custom directly in the chat panel without opening Settings. Automatically swaps API key, base URL, and model list per provider
* **Prompt: Priority Rule (Phase 12.1)** — mode-specific prompts (e.g. FFXIV) now explicitly override general rules, eliminating the #1 source of FFXIV agent errors (inserting `program_change` when the mode says not to)
* **Prompt: Final Validation Block (Phase 12.2)** — compact checklist appended to system prompts that the LLM reviews before responding (field requirements, value ranges, FFXIV constraints)
* **Prompt: Timing Reference (Phase 12.3)** — explicit note duration formulas (quarter = ticksPerQuarter, eighth = ticksPerQuarter/2, etc.) so LLMs no longer miscalculate rhythms
* **Prompt: Truncation Fallback (Phase 12.4)** — instruction to produce the smallest complete musically coherent version instead of truncated output when a request is too large
* **Prompt: Schema unification (Phase 12.6)** — Simple mode prompt now always requires the `actions[]` array format, reducing ambiguity (parser still accepts single-action format for backward compat)
* **Agent mode: Invalid event feedback (Phase 12.5)** — `deserialize()` now collects validation errors and returns them in the tool result (`skippedErrors` array), allowing the LLM to self-correct in subsequent tool calls instead of silently losing events

### Fixed
* **Crash: New File during playback** — `newFile()` now stops playback before replacing the MIDI file, and `MidiPlayer::stop()` waits for the player thread to finish (`wait()`), preventing a use-after-free crash when the old file was deleted while `PlayerThread` still accessed it on a separate thread

### Changed
* Removed unused `tsEvents` variable in `EditorContext::captureKeySignature()` (Phase 12.7)
* Version bump to 1.1.3

---

## [1.1.2.2] - 2026-03-28 — Manual Update: Prompt Examples & CI

### Added
* **Prompt Examples: screenshots & MIDI downloads** — every prompt section (Composing, Editing, Harmony, Arrangement) now includes result screenshots and downloadable MIDI files
* **New manual assets** — `manual/midi/` and `manual/wav/` directories for hosting example files locally on GitHub Pages

### Fixed
* **Broken download links** — MIDI/WAV links in Prompt Examples pointed to non-existent `../examples/` path (404); now link to `midi/` and `wav/` within the manual
* **Stray `` `n `` artifact** in 15 HTML pages — a PowerShell newline literal was rendered as visible text in the navbar
* **Navbar overflow** — reduced font size, gap, and disabled wrapping so all 12 links fit in a single row

### Changed
* README: documentation link now points to the manual index instead of directly to the MidiPilot page
* CI: bumped GitHub Actions dependencies (checkout v6, upload-artifact v7, configure-pages v6, upload-pages-artifact v4, deploy-pages v5)

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
