# Changelog — MidiEditor AI

All notable changes to MidiEditor AI are documented here.
Releases: https://github.com/happytunesai/MidiEditor_AI/releases

---

## [1.3.2.1] - 2026-04-16 — Hotfix: Ctrl+Drag Deselection & CI Workflow

* **Fixed Ctrl+drag box deselection not working** - The v1.3.2 selection system rewrite (`SelectTool::release()`) built a local event list but only had code to *add* events - there was no code path to *remove* already-selected events when Ctrl was held. Ctrl+drag box selection over selected notes did nothing instead of deselecting them. Added `ctrlHeld` check: when Ctrl is held, events inside the selection box are removed from the current selection instead of added. Applied to box selection, single selection, and left/right selection tools. **Regression since v1.3.1.**
* **Added GitHub Actions CI/Release workflow** - New `.github/workflows/ci.yml` automates the full build-and-release pipeline. Triggers on push to `main`, `v*` tag push, or manual dispatch with optional release tag. Build job: checks out repo, installs Qt 6.5 (cached), sets up MSVC x64, downloads FluidSynth 2.5.2, configures and builds via CMake/NMake, deploys Qt with `windeployqt`, uploads build artifact (14-day retention for CI, 90-day for releases). Release job: downloads artifact, creates versioned ZIP, extracts release notes from CHANGELOG.md (falls back to git log), publishes GitHub Release with the ZIP attached. Includes build summary with duration, commit, and toolchain info.
* **Files modified:** `SelectTool.cpp`, `.github/workflows/ci.yml`

---

## [1.3.2] - 2026-04-15 — MCP Server, Documentation System & Prompt Architecture v3

### Summary
* **MCP Server** - Built-in Model Context Protocol server exposing all MidiEditor AI tools (15 tools) with security, rate limiting, and client identification
* **MCP toolbar toggle** - Clickable MCP Server button in the toolbar (green when running, gray when stopped); toggleable in Customize Toolbar; synced with the Settings checkbox
* **Centralized Navigation System** - Single-source-of-truth for all doc pages (navigation.js); automatic active page highlighting and easy maintenance
* **Global Audio Player System** - Reusable audio player component (audio.css) with browser controls and download links
* **Enhanced Manual** - Lightbox on all images/videos, audio demos with players, improved MCP documentation, showcase integration
* **Prompt Architecture v3** - FFXIV simplification, token budgeting, conditional sections, retry logic, truncation recovery
* **Selection system overhaul** - Fixed empty Event tab on note click, broken box selection, and broken deselect; reverted flawed SEL-001 early-return and O(n) comparison from v1.3.1/v1.3.1.2; `selectedEvents()` now returns by value to prevent internal list mutation
* **Fixed Lyric Visualizer not displaying during playback** - v1.3.1 P3-008 timer idle stop exposed a latent bug: on Windows, `PlayerThread` is destroyed and recreated on each `play()`, breaking the `playerStarted`/`playerStopped` signal connections established at toolbar creation time; now reconnects all three signals in `play()` and `record()`
* **Fixed MCP toolbar button showing as text placeholder** - `_mcpServer` was created after `setupActions()`, so the toolbar widget check (`if _mcpServer`) always failed during construction; moved server creation before toolbar build
* **Fixed Lyric Timeline not showing after LRC import** - `importLyricsLrc()` was missing `_lyricArea->setVisible(true)` that both SRT and plain text import had; the timeline stayed hidden while the Visualizer displayed lyrics correctly

<details>
<summary>Full Changelog - MCP Server + Documentation System + Prompt v3</summary>

### MCP Server (Phase 23.5)
* **Built-in MCP server** - Exposes all MidiEditor AI tools via the Model Context Protocol (2025-03-26), allowing Claude Desktop, VS Code Copilot, Cursor, and other MCP clients to edit MIDI files directly
* **Streamable HTTP transport** - Single `/mcp` endpoint on localhost (default port 9420) with JSON-RPC 2.0
* **MCP Resources** - Three read-only resources: `midi://state`, `midi://tracks`, `midi://config`
* **Security** - Localhost-only binding, Origin header validation, optional Bearer token auth, rate limiting (100 calls/min), session management with 1-hour expiry
* **Settings UI** - Enable/disable, port, auth token generation, and "Copy MCP Config" button for easy client setup
* **MCP toolbar toggle** - New `McpToggleWidget` adds a clickable MCP Server button to the toolbar; shows `mcp_on.png` (green) when the server is running, `mcp_off.png` when stopped; click to start/stop without opening Settings; selectable and repositionable in Customize Toolbar dialog; checkbox in Settings stays synced bidirectionally
* **Client identification in Protocol panel** - MCP actions show the client name and model (e.g. "MidiPilotMCP (VS Code Copilot Claude Opus 4.6): Agent insert events") - parsed from `clientInfo` sent during MCP `initialize`
* **MCP Documentation** - Setup guide for Claude Desktop, VS Code, Cursor, and Windsurf in the built-in manual; removed unnecessary restart requirement (server starts immediately when toggled)
* **MCP Demo** - Metal remix of Mozart's Eine kleine Nachtmusik (20 measures with guitar solo) composed via MCP protocol; includes MIDI file, WAV audio with embedded player, and screenshots in both manual and showcase

### Documentation System
* **Centralized Navigation (navigation.js)** - Single file controls all 17 doc pages; auto-injects doc-subnav on every page with automatic active link highlighting; new pages inherit full nav by including 2 scripts
* **Global Audio Player System (audio.css)** - Reusable `.audio-box` component with native browser player, download links, and dark theme; added to all 20 pages
* **Lightbox on All Pages** - Images and videos click-to-enlarge; automatically excludes tiny icons and video fallbacks; no special markup required
* **Showcase Enhancement** - Added MCP demo (webp) as slide 11 in the carousel; now 11 slides showing all major features

### Prompt Architecture v3 (Phase 23.x)
* **FFXIV prompt simplification** - Streamlined FFXIV context for lower token usage
* **Enhanced tool result summaries** - Improved tool output formatting for AI readability
* **Token budgeting** - Smart history truncation when approaching context window limits
* **Conditional prompt sections** - Only include drum/guitar sections when relevant to the file
* **GM program_change reminder** - Strengthened create_track and insert_events tool descriptions
* **Retry logic** - Automatic retry on 429/5xx errors with exponential backoff
* **Truncation auto-recovery** - Detects and retries when API response is truncated
* **Effort-based prompt selection** - Low/medium/high effort selects compact/standard/detailed prompts

### Selection System Overhaul (3 compounding bugs)
The v1.3.1 SEL-001 fix (early-return when selection unchanged) and v1.3.1.2 hotfix (O(n) comparison threshold) were both reverted because they masked a deeper architectural issue: `selectedEvents()` returned a reference to the internal `_selectedEvents` list, allowing callers to mutate it directly. This caused the early-return comparison to always see identical lists (the parameter was already the same object), so `setSelection()` never reached `_eventWidget->setEvents()`.

* **Fixed empty Event tab when clicking a note** - `selectedEvents()` now returns by value instead of by reference. Code that previously accumulated events through the reference (EventTool, SelectTool) now builds a local list and calls `setSelection()` once. The SEL-001 early-return optimization has been removed entirely.
* **Fixed box selection not selecting any events** - `SelectTool::release()` called `selectEvent(event, false, false, false)` with `setSelection=false`, relying on reference mutation to accumulate events. With return-by-value, each call modified a throwaway copy. Rewritten to build a local `QList<MidiEvent*>` and call `setSelection()` once with the complete list.
* **Fixed deselect not updating Event tab** - `EventTool::deselectEvent()` removed an event from the local copy but never called `setSelection()` to propagate the change. Added `setSelection(selected)` after removal.
* **Fixed Protocol::endAction() not emitting signals** - `endAction()` only emitted `actionFinished()` and `protocolChanged()` when protocol items were recorded. Since selection changes don't record protocol items, the signals never fired and `EventWidget::reload()` was never triggered. Now always emits signals and marks file unsaved.
* **Reverted v1.3.1.2 O(n) comparison** - The <200 event threshold comparison in `setSelection()` is no longer needed since the early-return was removed entirely.
* **Files modified:** `Selection.h`, `Selection.cpp`, `EventTool.cpp`, `SelectTool.cpp`, `EditorContext.cpp`, `MainWindow.cpp`, `Protocol.cpp`

### Bug Fixes
* **Fixed Lyric Visualizer not displaying during playback** - The v1.3.1 P3-008 fix (timer stops when idle) exposed a latent bug: on Windows, `MidiPlayer::play()` deletes and recreates the `PlayerThread` object, breaking the `playerStarted`/`playerStopped` signal connections established at toolbar creation time. The timer only restarts via `playbackStarted()`, which never fired because it was connected to the destroyed object. Before P3-008 the timer ran at 30fps forever, masking the broken connection. Fixed by reconnecting all three visualizer signals (`playerStarted`, `playerStopped`, `timeMsChanged`) in both `play()` and `record()`
* **Fixed MCP toolbar button showing as text placeholder** - `_mcpServer` was created after `setupActions()` built the toolbar, so the check `if (actionId == "mcp_toggle" && _mcpServer)` always failed during construction, falling through to the placeholder text path. Moved `_mcpServer` creation to before `setupActions()` and removed the duplicate creation at the later location
* **Fixed Lyric Timeline not showing after LRC import** - `importLyricsLrc()` did not call `_lyricArea->setVisible(true)` or update `_toggleLyricTimeline`, so the timeline lane stayed hidden after importing an LRC file. Both `importLyricsSrt()` and `importLyricsText()` already had this. The Lyric Visualizer in the toolbar displayed lyrics correctly because it doesn't depend on `_lyricArea` visibility

### Files Added/Modified
* **New:** `src/gui/McpToggleWidget.h/.cpp` - MCP Server toolbar toggle button widget
* **New:** `navigation.js` (2.6 KB) - Centralized navigation controller
* **New:** `audio.css` (620 bytes) - Global audio player styles
* **Modified:** All 17 doc pages - Added centralized navigation and audio support
* **Modified:** mcp-server.html - Added MCP demo with audio player, updated Quick Start
* **Modified:** index.html - Added MCP demo slide to showcase
* **Added:** midieditor_ai_MCP.mid, midieditor_ai_MCP.wav (manual demos)
* **Added:** Screenshots and webp in manual/screenshots/

</details>

---

## [1.3.1.2] - 2026-04-12 — Hotfix: Selection Regression (superseded by v1.3.2)

> **Note:** Both fixes below were reverted in v1.3.2 and replaced by a proper architectural fix (`selectedEvents()` returns by value, SEL-001 early-return removed entirely). See v1.3.2 "Selection System Overhaul" for details.

* **Fixed Ctrl+A / large selection freezing the UI** - `batchSelectEvents()` obtained a reference to the internal `_selectedEvents` list and modified it in-place (clear + append), so `setSelection()`'s equality check (SEL-001) always saw them as identical and returned early - no protocol entry, no EventWidget update, no Ctrl+T. Fixed by building a new local list instead of mutating through the reference.
* **Fixed O(n) selection comparison blocking large files** - `setSelection()` did an element-by-element `QList::operator==` on every call, taking seconds for 4000+ events. Now only performs the full comparison for selections under 200 events; larger selections skip the check entirely.

---

## [1.3.1.1] - 2026-04-12 — Hotfix: Post-Update Changelog

* **Fixed "Could not load patch notes" in post-update and update-available dialogs** - `fetchChangelog()` shared the same `QNetworkAccessManager` whose `finished` signal was already connected to `onResult()`, consuming the response before the changelog lambda could read it. Additionally, the GitHub Pages URL redirected to the custom domain, but Qt6 does not follow redirects by default. Fixed by using a separate `QNetworkAccessManager`, correcting the URL to `midieditor-ai.de`, and adding `NoLessSafeRedirectPolicy` as a safety net.

---

## [1.3.1] - 2026-04-12 — Bugfix Release

* **16 bug fixes** across AI subsystem, lyric editor, clipboard, selection, FluidSynth export, and FFXIV Channel Fixer - memory leaks, use-after-free, undo spam, overlap prevention, and performance improvements
* **Fix FFXIV Channel Fixer undo crash** - Reverting Fix XIV Channels (Tier 2 or Tier 3) caused a crash due to use-after-free; removed incorrect event deletion that conflicted with the Protocol undo system
* **Documentation fixes** - Fixed lyrics manual page layout, removed dead CLI argument documentation
* **Website UX tweaks** - Showcase carousel videos, 16:9 aspect ratio, darker caption bar, What's New section updates

<details>
<summary>Full Changelog - 15 Bug Fixes + Documentation + Website</summary>

### Bug Fixes (Pass 4 - AI Subsystem & Core)

* **AI cancel safety** — Disconnect network reply signals before `abort()` in streaming cancel, preventing potential use-after-free (AI-002)
* **Agent cancel after processEvents** — Added `_cancelled` check immediately after `QCoreApplication::processEvents()` in the tool-call loop (AI-001)
* **Conversation store O(1) lookup** — `loadConversation()` now uses direct path lookup instead of scanning all JSON files (AI-009)
* **FFXIV Channel Fixer memory leaks** — Removed events (ProgChange, CC, PitchBend) are now properly `delete`d in the CLEAN phase (AI-010, AI-011)
* **Selection undo spam** — `setSelection()` returns early when selection is unchanged, avoiding redundant Protocol entries (SEL-001) — **reverted in v1.3.2** (caused empty Event tab; see Selection System Overhaul)
* **SharedClipboard dead catch** — Removed `try/catch(...)` around `delete` (destructors are noexcept in C++11+) (CLIP-001)
* **Export temp file cleanup** — `ExportOptions::deleteMidiFileAfterExport` flag is now honored; temp MIDI files are deleted after export (FLUID-002)
* **Key signature scan** — `captureKeySignature()` now only scans channel 18 (meta) instead of all 19 channels (AI-008)
* **API log truncation** — Non-streaming request/test logs now truncate body to 4000 chars, matching the streaming pattern (AI-005)

### Bug Fixes (Remaining Lyric & AI Bugs)

* **Insert overlap prevention** — Double-click, Insert Before, and Insert After now clamp new blocks to avoid overlapping adjacent blocks (P2-007)
* **Split encapsulation** — Split operation now uses `addBlockDirect()` instead of calling `insertSorted()` + emitting `lyricsChanged()` from outside LyricManager (P3-007)
* **Visualizer timer idle stop** — `LyricVisualizerWidget` timer now stops when not playing and fully faded in, eliminating unnecessary 30fps CPU usage (P3-008)
* **Multi-timestamp LRC import** — LRC import now handles karaoke-style multi-timestamp lines like `[00:12.34][01:56.78]Text`, creating one block per timestamp (P3-011)
* **Truncated response logging** — Truncated API responses now log partial content (up to 500 chars) for debugging before emitting the error (AI-004)
* **FFXIV polyphony sort** — `execValidateFFXIV` now sorts notes by tick before the polyphony check, ensuring the first reported overlap is chronologically first; inner loop early-breaks for better performance (AI-006)

### Documentation Fixes

* **Fixed lyrics manual page** — "Lyric Metadata" section was a mismatch of Customize Toolbar and metadata content. Split into two proper sections: "Customize Toolbar" (toggle Lyric Visualizer on/off) and "Lyric Settings & Metadata" (LRC metadata fields with `lyric_settings.png` screenshot)
* **Removed dead documentation** — "Larger Playback Toolbar" section in setup.html referenced a `--large-playback-toolbar` CLI argument that doesn't exist in the codebase. Removed section and screenshot reference

### Website UX Tweaks

* **Showcase carousel videos** — Replaced static screenshots with webm videos for MidiPilot, Lyric Visualizer, and other features
* **Showcase aspect ratio** — All slides now use a fixed 16:9 aspect ratio with `object-fit: cover` cropping for a consistent, clean layout; click any slide for the full uncropped view in the lightbox
* **Showcase caption bar** — Darker, taller gradient overlay for better text readability
* **What's New section** — Fixed year (2025 to 2026), updated version to v1.3.1, added bug fix count

</details>

---

## [1.3.0] - 2026-04-11 — Lyric Editor & Visualizer

* **Lyric Timeline** — New dedicated lane below the piano roll for visual lyric editing (add, delete, move, resize, split, merge, inline text editing)
* **Sync Lyrics dialog** — Tap-to-sync: hold Space while each phrase is sung to capture precise timing in real time
* **Import/Export** — Import from plain text, SRT subtitles, or MIDI Text/Lyric events; export to SRT or LRC (karaoke) format
* **Lyric Visualizer** — Karaoke-style toolbar widget with left-to-right color sweep, two-line display (current + next phrase), dynamic sizing (200–600px)
* **Full undo/redo** — All lyric operations integrate with the Protocol system for complete undo/redo support
* **FluidSynth fixes** — `synth.reverb.engine` compatibility guard for FluidSynth 2.5.2; bundled SoundFonts survive clean builds
* **FFXIV Channel Fixer — 5 Tier 3 bugs fixed** — wrong programs, broken renames, unwanted event migration; `progAtTick(0)` now single source of truth for channel programs
* **Website & documentation** — New Lyrics manual page, version bump to 1.3.0 across all pages, GIF→webm migration for bandwidth savings, video lightbox support

<details>
<summary>Full Changelog — Lyric Editor, Visualizer & FluidSynth Fixes</summary>

### Lyric Timeline Widget

* **New Lyric Timeline lane** — A dedicated lane below the piano roll displays lyric blocks
  aligned to the MIDI timeline. Blocks show text, timing, and playback highlighting with a
  pop/glow effect on the currently playing phrase.
* **Full lyric editing** — Add, delete, move, resize, split, and merge lyric blocks directly
  in the timeline. Drag blocks to reposition, drag edges to resize, double-click to edit text
  inline, and use Delete/Backspace to remove. Shift+Click for multi-select with range support.
* **Sync Lyrics dialog** — Tap-to-sync mode: play the song and hold Space while each phrase
  is being sung. Press = phrase starts, Release = phrase ends. Captures precise timing in real
  time. Features a teleprompter view showing the next 8 upcoming phrases, separate Play/Stop
  buttons, and progress tracking.
* **Import lyrics** — Import from plain text (one phrase per line), SRT subtitle files
  (with timestamp conversion), or existing MIDI Text/Lyric events. Automatically creates
  properly timed lyric blocks from any source.
* **Export lyrics** — Export to SRT subtitle format or LRC (karaoke) format. LRC export
  supports both 2-digit centisecond and 3-digit millisecond timestamps on import.
* **Undo/Redo support** — All lyric operations (add, delete, move, resize, edit, split,
  merge, sync, import) integrate with the Protocol system for full undo/redo. A dedicated
  `undoRedoPerformed` signal ensures lyric blocks stay in sync after undo without disrupting
  forward editing operations.
* **Context menu** — Right-click any block for Edit Text, Delete, Split at Cursor, Merge
  with Next, Insert Before/After. Right-click empty space to insert a new block.

### Lyric Visualizer (Karaoke Display)

* **Karaoke toolbar widget** — A compact toolbar widget that shows the current lyric phrase
  during playback with a karaoke-style left-to-right color sweep. Two-line display: current
  phrase (large, bold, with highlight animation) and next phrase (small, dimmed preview).
  Includes a thin progress bar at the bottom of the widget.
* **Dynamic sizing** — The visualizer box dynamically expands (200–600px) based on the
  current phrase length. Long text is gracefully elided with "…" when the toolbar constrains
  the available width. `updateGeometry()` called on phrase change for smooth toolbar relayout.
* **Always visible** — Stays visible at all times (matching the MIDI Visualizer pattern).
  Shows "♪ ♪ ♪" idle state when no lyrics are loaded; shows song title from lyric metadata
  when stopped.
* **Full toolbar integration** — Appears in the Customize Toolbar dialog with its own icon,
  can be repositioned via drag-and-drop, and persists across layout changes (single-row /
  double-row). Registered in all 4 toolbar creation sites and both migration blocks for
  seamless upgrades from older settings.
* **Toolbar integration fixes** — Added `lyric_visualizer` to all three "single source of
  truth" methods (`getComprehensiveActionOrder`, `getDefaultEnabledActions`,
  `getDefaultRowDistribution`) so it appears in the Customize Toolbar dialog. Added icon to
  `resources.qrc` and `ToolbarActionInfo` entry in `getDefaultActions()`. Fixed inconsistent
  widget creation placement in `createCustomToolbar()` two-row mode.

### FluidSynth Fixes

* **`synth.reverb.engine` compatibility** — FluidSynth 2.5.2 doesn't support the
  `synth.reverb.engine` setting. Added `fluid_settings_get_type()` guard before calling
  `fluid_settings_setstr()` to prevent initialization errors.
* **Bundled SoundFonts survive clean builds** — Added CMake post-build rule to copy
  `soundfont/*.sf2` to `build/bin/soundfonts/`. Previously, a clean build (`Remove-Item build`)
  deleted the user's downloaded SoundFont files.

### Technical Notes
* **New files:** `src/gui/LyricVisualizerWidget.h/.cpp`, `run_environment/graphics/tool/lyric_visualizer.png`
* **Modified files:** `MainWindow.h/.cpp` (action registration, 4 toolbar creation sites, 2 migration blocks, signal connections), `LayoutSettingsWidget.cpp` (ToolbarActionInfo entry, 3 single-source-of-truth methods, default order/distribution), `resources.qrc` (icon), `FluidSynthEngine.cpp` (reverb engine guard), `CMakeLists.txt` (SoundFont copy rule)
* **Lyric Editor files (from Phase 21):** `src/midi/LyricManager.h/.cpp`, `src/midi/LyricBlock.h`, `src/gui/LyricTimelineWidget.h/.cpp`, `src/gui/LyricEditorDialog.h/.cpp`, `src/gui/SyncLyricsDialog.h/.cpp`

### Website & Documentation

* **New Lyrics manual page** — Full `manual/lyrics.html` with 10+ sections: Timeline overview,
  editing, context menu, Tap-to-Sync, import/export, Lyric Visualizer, appearance settings,
  keyboard shortcuts, tips, and menu reference. Includes screenshots and video demos.
* **Version bump to 1.3.0** — Updated `CMakeLists.txt`, `README.md`, `index.html` (4 locations),
  and `docs-index.html`.
* **README.md updated** — 3 new feature rows, 2 architecture entries, full Lyric Editor section,
  4 project structure entries.
* **Navigation updated on all pages** — Doc-subnav (🎤 Lyrics link) added to all 16 doc pages;
  nav dropdown (Lyric Editor) added to all 19 HTML pages; index.html updated with feature card,
  What's New section, and comparison table row.
* **GIF→webm migration** — All 11 standalone GIF `<img>` tags across 8 HTML pages replaced with
  `<video autoplay loop muted playsinline>` using webm as primary source and GIF as `<img>`
  fallback. Saves significant bandwidth (webm is ~10× smaller than GIF).
* **Video lightbox** — Extended `lightbox.js` to support click-to-enlarge on `<video>` elements.
  Clicking any inline video opens it full-screen in the lightbox overlay with autoplay+loop.
  Added matching CSS for `video.lightbox-thumb` hover states and overlay styling in both
  `style.css` and `site.css`.
* **Lightbox added to lyrics page** — Added missing `lightbox.js` script include; wrapped
  `lyric_context_menu.png` and `lyric_tool_menu.png` in `<a class="lightbox-link">` anchors.
* **Fixed mojibake in FFXIV page** — Corrected `â€"` (corrupted UTF-8 em dash) to proper `—`
  in `ffxiv-channel-fixer.html` aria-label attributes during webm migration.
* **Lightbox click-to-close on images** — Fixed enlarged images not closing when clicked (only
  overlay edge or × button worked). Root cause: the lightbox's own `<img>` element was picked up
  by the thumbnail handler, causing close+reopen on every click. Added skip for `lbImg` in the
  image loop and changed enlarged image/video cursor to `pointer`.

### FFXIV Channel Fixer — Tier 3 Overhaul (5 bugs)

* **Bug #1:** Reserved guitar channels (from Tier 2) were invisible to rename/switch logic — `allGuitarChs` now includes `guitarChToProgram` channels
* **Bug #2:** Track name was used instead of actual channel program for PC insertion and `chToVariant` — now uses `guitarChToProgram` as primary source
* **Bug #3:** Duplicate guitar track names caused event migration between channels — removed all duplicate logic from Tier 3 (every track keeps its own channel)
* **Bug #4:** Rename on switching tracks (notes on multiple channels) was unreliable — now only renames single-channel guitar tracks
* **Bug #5:** `guitarChToProgram` scan took the first PC at tick 0 instead of the effective one — now uses `channel->progAtTick(0)` (matches channel view display)
* **Files modified:** `src/ai/FFXIVChannelFixer.cpp`

### Lyric System Bug Fixes (Pass 3 Audit — 7 fixes)

* **P3-001 (Critical)** — `lyricsChanged` signal unconditionally cleared the timeline selection
  during an active drag operation. Added `_dragActive` guard in the lambda so drags are never
  interrupted by a background lyrics refresh.
* **P3-002 (High)** — `importLyricsLrc()` called `addBlock()` in a loop, each creating its own
  Protocol action — importing 50 lines produced 50 separate undo steps. Rewritten to use
  `insertSorted()` with direct event creation inside a single `startNewAction`/`endAction` pair.
* **P3-003 (Medium)** — Consecutive MIDI Text events at the same tick produced zero-duration
  lyric blocks (`endTick == startTick`), which were invisible and unmovable. Added 120-tick
  minimum duration enforcement in `importFromTextEvents()`.
* **P3-004 (Medium)** — `LyricTimelineWidget::paintEvent()` directly overwrote the `_file`
  member from `matrixWidget->midiFile()`, bypassing `setFile()` signal connections. Changed to
  a local variable with a conditional `setFile()` call when the file actually changes.
* **P3-005 (Medium)** — `LyricSyncDialog::onDone()` did not re-sort blocks after applying
  tap-to-sync timings. Added `sortBlocks()` method to `LyricManager`; called after all timings
  are applied.
* **P3-006 (Medium)** — `LyricTimelineWidget::setFile()` never disconnected the old file's
  `lyricsChanged` signal, causing accumulated connections and stale callbacks over multiple
  file loads. Added `disconnect()` before connecting the new file.
* **P3-009 (Low)** — `LyricVisualizerWidget::onLyricsChanged()` did not reset
  `_currentBlockIndex`, so the visualizer could display a stale phrase after lyrics were
  re-imported or edited. Now resets to `-1` before calling `update()`.

</details>

---

## [1.2.2] - 2026-04-11 — FFXIV Channel Fixer Bugfix & Website Fixes

* **Fix XIV Channels — Tier 3 (Preserve) now sees reserved guitar channels**
  Previously, Tier 3 only recognized guitar channels that had an active track assigned.
  Reserved channels from a prior Tier 2 run (e.g. ElectricGuitarSpecial on CH6) were
  invisible to the rename logic and switch-point detection. This caused:
  - Notes manually placed on a reserved guitar channel were ignored
  - Track was not renamed when the first note sat on a reserved channel
  - Guitar switch program changes were not inserted for reserved channels
  - Re-running Tier 3 after a manual track rename could reassign *all* notes to the wrong variant

  **Fix:** `allGuitarChs` now includes channels from `guitarChToProgram` (reserved channels
  from previous Tier 2 runs). A new `guitarChsFromTracks` set limits PC cleaning to channels
  with actual guitar tracks, keeping reserved channel PCs intact. The `chToVariant` reverse
  map used by rename and switch logic is now built from both `guitarChannelMap` and
  `guitarChToProgram`.

* **Fix lightbox on manual pages** — Images wrapped in `<a class="lightbox-link">` on
  split-channels, themes, editing-midi-files, playback, and editor-and-components pages
  did not open the lightbox overlay on click. Added `preventDefault` / `stopPropagation`
  to the image click handler so the enclosing anchor no longer intercepts the event.
  Also added the missing `lightbox.js` script include on the Themes page.

* **Updated FFXIV Channel Fixer manual page** — Clearer Tier 2 / Tier 3 descriptions,
  especially around reserved guitar channels and Preserve mode behavior.

---

## [1.2.1] - 2026-04-09 — Stability & Bugfix Release

* **72 bug fixes** across the entire codebase — memory leaks, crashes, undefined behavior, protocol corruption, concurrency issues, parser security hardening, and correctness fixes
* **Guitar Pro 5 parser fixes** — 4 byte-alignment bugs fixed; GP5 files that previously failed to open now load correctly
* **MidiPilot timestamp readability** — Chat timestamps moved from inside the bubble to a clean label above each message; uses light gray text for dark themes, muted gray for light themes — readable across all 5 themes

<details>
<summary>Full Changelog — 72 Bug Fixes + GP5 Parser</summary>

### Critical: Crash & Hang Prevention (9 fixes)

* **EVT-001** — `loadMidiEvent()` infinite recursion on malformed MIDI data. Added recursion guard: bail when running status byte is 0 instead of recursing forever
* **EVT-002** — SysEx loader infinite loop on truncated stream (missing 0xF7 terminator). Added `content->atEnd()` check in the read loop
* **EVT-003** — `TempoChangeEvent` constructor division by zero on zero tempo value. Guards `value <= 0` with 120 BPM default
* **EVT-004** — `TempoChangeEvent::save()` division by zero when `_beats` is 0. Guards `_beats > 0` before division
* **PROTO-005** — `Protocol::goTo()` use-after-free when navigating to a redo step. Replaced dangling-pointer `contains()` loop with index-based count
* **CONV-001** — `readIntByteSizeString()` negative size from file → SIZE_MAX string read. Returns empty string when `d < 0`
* **CONV-002** — `readIntSizeString()` negative length from file → SIZE_MAX string read. Returns empty string when `length < 0`
* **CONV-005** — GP6 BitStream `powers[]` array out-of-bounds read for wordSize > 10. Replaced lookup table with direct `(1 << i)` bit shift
* **CONV-010** — Stack buffer overflow: `lastNoteIdx[10]` OOB write from untrusted GP6/7 string number. Added `MAX_STRINGS` constant with `std::min` clamping

### Protocol System Memory Leaks (5 fixes)

* **PROTO-001** — `Protocol` had no destructor — both undo/redo stacks and all contained steps leaked on file close. Added `~Protocol()` with `qDeleteAll` cleanup
* **PROTO-002** — `startNewAction()` cleared redo stack without deleting `ProtocolStep` objects. Added `qDeleteAll` before `clear()`
* **PROTO-006** — `enterUndoStep()` leaked `ProtocolItem` when no action step was open. Added `delete item` fallback
* **PROTO-007** — `ProtocolStep` destructor didn't delete contained `ProtocolItem` objects. Added `qDeleteAll(*_itemStack)`
* **PROTO-008** — `releaseStep()` leaked old items after calling `release()`. Added `delete item` after extracting reverse action

### MIDI Engine (8 fixes)

* **MIDI-001** — `msOfTick()` leaked a heap-allocated QList on every call (the most-called timing function). Changed to stack allocation — eliminates the single largest memory leak in the codebase
* **MIDI-002** — `save()` leaked QFile and QDataStream on every save. Changed to stack allocation
* **MIDI-003** — `timeSignatureEvents()`/`tempoEvents()` used `reinterpret_cast` from `QMultiMap*` to `QMap*` — undefined behavior in Qt 6 where these are different types. Changed return types to `QMultiMap*`, updated 12 callers
* **MIDI-004** — `MidiChannel::number()` checked `this == nullptr` (UB in C++) and used useless try/catch. Removed dead guards
* **MIDI-005** — `PlayerThread::stopped` used `volatile bool` instead of `std::atomic<bool>` — data race between main and player threads
* **MIDI-006** — `playedNotes` QMap accessed from multiple threads without synchronization. Added `QMutex` protection around all accesses
* **MIDI-007** — `setMaxLengthMs()` leaked QList from `eventsBetween()`. Added `delete ev`
* **MIDI-008** — `reloadState()` leaked old `_tracks` list on every undo/redo. Added `delete _tracks` before reassignment

### MidiEvent Layer (5 fixes)

* **EVT-005** — `OnEvent::moveToChannel()` null dereference when OffEvent is missing. Added null check
* **EVT-006** — `TimeSignatureEvent::ticksPerMeasure()` used `powf` for integer power-of-2 (float precision loss) and could divide by zero. Replaced with `1 << denominator` and added null/zero guards
* **EVT-007** — `KeySignatureEvent::save()` OR'd meta type byte 0x59 with channel — latent corruption if channel != 16. Changed to hardcoded `char(0x59)`
* **EVT-008** — `ProtocolEntry::protocol()` leaked the `copy()` allocation when `file()` is null (during file loading). Added `delete oldObj` fallback — fixes memory leak of 2 objects per TextEvent loaded
* **EVT-009** — `TempoChangeEvent::msPerTick()` null dereference when `file()` is null. Added null/zero guards with 1.0ms fallback

### Tools & Selection (8 fixes)

* **TOOL-001** — `SelectTool::draw()` used the macro literal `SELECTION_TYPE_BOX` (always 2, always true) instead of `stool_type == SELECTION_TYPE_BOX`. All selection types incorrectly drew box rectangles
* **TOOL-005** — `EventTool::copiedEvents` leaked old clipboard events on each copy operation. Added `qDeleteAll` before `clear()`
* **CONV-003** — `GpBinaryReader::checkBounds()` integer overflow: `count * N` could wrap negative for large counts. Added `count < 0` / `pointer_ < 0` guards with `size_t` cast
* **CONV-006** — GP7/8 ZIP `extract()` didn't validate data offset+size against buffer. Added bounds check
* **CONV-007** — GP7/8 ZIP `inflateData()` trusted claimed uncompressed size for allocation (DoS). Added 256MB cap
* **CONV-008** — `readDuration()` left-shift overflow from out-of-range signed byte. Clamped to `std::clamp(byte + 2, 0, 7)`
* **CONV-011** — `SimileMark::simple` on first measure accessed `measures[-1]`. Added `measureIndex > 0` guard
* **CONV-013** — `GpMidiExport::createBytes()` accessed `raw[0]` on empty vector for unrecognized message types. Added empty check

### Medium-Severity Fixes (29 fixes)

#### Protocol & Undo

* **PROTO-003/004** — `endAction()` marked file unsaved and emitted signals even with no active action step. Now only triggers when items were actually recorded
* **PROTO-009** — `ProtocolItem::release()` deletion guard checked the wrong variable (`entry` instead of `_oldObject`)

#### MIDI Engine Correctness

* **MIDI-011** — `PlayerThread` had no destructor — QTimer and QElapsedTimer leaked on every play/stop cycle (Windows). Added `~PlayerThread()`
* **MIDI-012** — MIDI input `receiveMessage` callback wrote shared state from RtMidi thread without synchronization. Added `QMutex` protection
* **MIDI-013** — `endInput()` invalidated QMultiMap iterator during container modification. Changed to `erase()` which returns next valid iterator
* **MIDI-014** — `progAtTick()` off-by-one skipped the first element in backward search. Fixed loop to check `begin()` element
* **MIDI-015** — Metronome sent triple NoteOn with single NoteOff, creating stuck notes that accumulated. Reduced to single NoteOn
* **MIDI-016** — `MidiFile(int, Protocol*)` protocol-copy constructor left `channels[]`, `_tracks`, etc. uninitialized. Added safe defaults
* **MIDI-018** — `MidiInput::inputPorts()` crashed if `init()` failed (null `_midiIn`). Added null check

#### MidiEvent Correctness

* **EVT-010** — `PitchBendEvent::toMessage()` sent "cc" instead of "pitchbend" command — wrong MIDI messages during playback
* **EVT-011** — `_tempID` uninitialized in MidiEvent constructors. Initialized to -1
* **EVT-012** — Tempo loading used fragile magic-number subtraction (`value -= 50331648`). Replaced with `value &= 0x00FFFFFF` bitmask
* **EVT-014** — `NoteOnEvent::setNote()` had no range validation. Added `qBound(0, n, 127)`

#### Tool System

* **TOOL-002** — `NewNoteTool()` constructor reset static `_channel`/`_track` to 0, losing user's selection when StandardTool was created
* **TOOL-003** — `Tool::setImage()` leaked previous QImage on reassignment. Added `delete _image` before `new`
* **TOOL-004** — `Tool` copy constructor shallow-copied QImage pointer (shared ownership → double-free risk). Changed to deep copy
* **TOOL-006** — `EventTool` copy/paste null dereference if OnEvent has no OffEvent. Added null check
* **TOOL-007** — `EventMoveTool::computeRaster()` null dereference on missing OffEvent. Added null check
* **TOOL-012** — `TempoTool::release()` leaked TempoDialog after `exec()`. Stack-allocated instead
* **TOOL-013** — `TimeSignatureTool::release()` leaked TimeSignatureDialog after `exec()`. Stack-allocated instead

#### Converter & Parser

* **CONV-004** — `GpBinaryReader::skip()` didn't validate pointer stayed in bounds. Added negative count guard and `checkBounds()`
* **CONV-009** — Tuplet `enters=0` from GP6/7 XML caused float division by zero. Added `enters > 0` guard
* **CONV-012** — `SimileMark::secondOfDouble` with notes underflow accessed negative indices. Added `measureIndex >= 2` bounds check
* **CONV-014** — GP6/7 outer parser leaked when transferring to inner GP5 object. Fixed `self` pointer transfer to null before `reset()`
* **CONV-017** — GP1/2 `gpReadStringBSoB` with byte=0 → negative size → same crash as CONV-001. Added `size < 0` guard

#### Terminal & System

* **TERM-001** — `Terminal::execute()` leaked old QProcess on re-execute. Added `deleteLater()` before creating new process
* **TERM-002** — `Terminal::processStarted()` leaked QTimer on each retry (one per second). Connected `timeout` to `deleteLater()`
* **MAIN-001** — `wstrtostr()` buffer overflow with multi-byte characters (CJK usernames). Rewrote with two-pass `WideCharToMultiByte`

### Low-Severity Fixes (8 fixes)

* **MIDI-019** — `MidiChannel::visible()` was hardcoded to `return true` (dead code). Now delegates to `ChannelVisibilityManager`
* **MIDI-020** — `MidiChannel::setVisible()` used try/catch for control flow. Replaced with `_num` range guard
* **MIDI-021** — `MidiTrack::reloadState()` called `setNumber()` which re-entered the protocol system during undo. Changed to direct member assignment
* **EVT-017** — `OffEvent::onEvents` static map was heap-allocated and never freed. Changed to static local storage
* **TOOL-009** — `GlueTool::mergeNoteGroup()` null dereference on `lastNote->offEvent()`. Added null guard
* **TOOL-010** — `ToolButton::refreshIcon()` dereference of potentially null tool image. Added null guard
* **TOOL-011** — `StandardTool` copy constructor didn't copy `newNoteTool` (uninitialized pointer). Added missing copy
* **MAIN-002** — `MainWindow` heap-allocated in `main()` but never deleted. Added `delete w` before return

### Guitar Pro 5 Parser Fixes (4 fixes)

* **GP5 readGrace() override** — GP5 grace notes have 5 bytes (fret, dynamic, transition, duration, flags) but MidiEditor inherited GP3/4's 4-byte version. Added `Gp5Parser::readGrace()` override reading the missing dead/onBeat flags byte. This was the primary cause of cumulative byte misalignment
* **GP5 readMixTableChange() skip(1)** — Added missing `reader.skip(1)` after allTracksFlags byte, matching TuxGuitar's reference implementation
* **GP5 readMixTableChange() v5.1+ strings** — Changed conditional RSE string reads to always read 2 strings for GP5.1+ files, matching TuxGuitar
* **GP5 readMeasureHeader() field order** — Moved repeatAlternative (flag 0x10) from before marker/keySignature to after beams section, matching TuxGuitar's byte ordering

### Deferred Bugs (8 — low risk, high complexity)

* **EVT-013** — On/OffEvent copy constructor dangling pointers (deep protocol architecture issue)
* **EVT-015** — SysEx VLQ length prefix omission (load/save internally consistent; changing one breaks the other)
* **EVT-016** — QDataStream status checks throughout loadMidiEvent (too invasive for low crash risk)
* **EVT-018** — Dead `< 0` checks on `quint8`-sourced values (harmless defensive code)
* **CONV-015** — Unchecked `std::stoi()` on ~30+ GP6/7 XML sites (needs helper + mass update)
* **CONV-016** — Custom GP6 XML parser edge cases (design-level issue)
* **MIDI-017** — RtMidi static objects never deleted (OS reclaims at exit)
* **TOOL-008** — ScissorsTool protocol recording (already works — `setMidiTime()` defaults to `toProtocol=true`)

### Technical Notes

* **Bug audit**: 137 bugs reported by automated analysis, 130 confirmed (7 false positives), 50 already fixed in v1.1.9/v1.2.0, 80 remaining → 72 fixed + 8 deferred
* **Files modified (40+)**: MidiEvent.cpp, TempoChangeEvent.cpp, TimeSignatureEvent.cpp, KeySignatureEvent.cpp, OnEvent.cpp, NoteOnEvent.cpp, PitchBendEvent.cpp, OffEvent.cpp, ProtocolEntry.cpp, Protocol.h/.cpp, ProtocolStep.cpp, ProtocolItem.cpp, MidiFile.h/.cpp, MidiChannel.cpp, MidiTrack.cpp, MidiInput.h/.cpp, MidiOutput.h/.cpp, PlayerThread.h/.cpp, Metronome.cpp, MidiPlayer.cpp, GpBinaryReader.cpp, Gp345Parser.h/.cpp, Gp678Parser.cpp, GpToNative.cpp, GpMidiExport.cpp, GpUnzip.cpp, GpImporter.cpp, GpModels.h, Gp12Parser.cpp, SelectTool.cpp, EventTool.cpp, EventMoveTool.cpp, NewNoteTool.cpp, Tool.cpp, GlueTool.cpp, ToolButton.cpp, StandardTool.cpp, TempoTool.cpp, TimeSignatureTool.cpp, Terminal.cpp, main.cpp
* **GP5 parser reference**: TuxGuitar's `GP5InputStream.java` used to identify byte-alignment differences
* **Bug report**: `Planning/03_bugs.md` — full scan results with verification status

</details>

---

## [1.2.0] - 2026-04-08 — Audio Export, MP3, FluidSynth Hardening

* **Audio Export** — Export MIDI as WAV, FLAC, or OGG Vorbis using loaded SoundFonts (File → Export Audio, Ctrl+Shift+E)
* Export Dialog with format selection (WAV/FLAC/OGG/MP3), quality presets (Draft/CD/Studio/Hi-Res), range options (full song/selection/custom measures), reverb tail toggle, and estimated file size
* Selection export via right-click context menu — select notes, right-click → "Export Selection as Audio..."
* Export Audio button added to SoundFont settings panel
* Background export with progress dialog and cancel support
* **MP3 Export** — Built-in LAME 3.100 encoder compiled as static C library; export directly to MP3 without external tools
* **Export Completion Dialog** — After export finishes, shows dialog with Open File, Open Folder, and Close buttons
* **Guitar Pro Audio Export Fix** — Exporting audio from Guitar Pro files (.gp3–.gp8) no longer produces silent/empty output; saves in-memory MIDI to temp file for rendering
* **SoundFont Enable/Disable** — Per-SoundFont checkboxes in the settings list; uncheck to temporarily disable a font without removing it; state persists across sessions
* **FFXIV SoundFont Mode Auto-Toggle** — SoundFonts with "ff14" or "ffxiv" in the filename automatically enable/disable FFXIV SoundFont Mode when checked/unchecked
* **FluidSynth Audio Driver Fallback** — If the preferred audio driver fails (e.g., SDL3 after restart), automatically tries wasapi → dsound → waveout → sdl3 → sdl2
* **FluidSynth Settings Always Accessible** — SoundFont list and settings are now configurable even when Microsoft GS Wavetable Synth is the active output
* **FluidSynth Error Dialog** — Shows a clear error message when FluidSynth initialization fails, with automatic revert to previous output
* **Drum Channel Reset Fix** — Switching from FFXIV SoundFont back to GM mode now properly restores channel 9 drums (bank select 128 + program change); previously drums played as piano
* **Radio button styling fix** — checked radio buttons now render as proper circles in all 5 themes (dark, light, amoled, materialdark, pink)

<details>
<summary>Full Changelog</summary>

### Added
* **MP3 export via LAME** — LAME 3.100 compiled from source as a static C library (`libmp3lame.a`) and linked directly into the build. No DLL or external encoder needed. Export dialog shows MP3 as a format option alongside WAV/FLAC/OGG. Supports VBR quality presets matching the existing quality tiers.
* **Export completion dialog** — `QMessageBox` with three buttons after any audio export: **Open File** (launches default audio player), **Open Folder** (opens containing directory in Explorer), **Close** (dismiss). Previously there was no feedback after export completion.
* **SoundFont enable/disable checkboxes** — Each SoundFont in the FluidSynth settings list has a checkbox. Unchecking removes it from the active FluidSynth stack without deleting it from the list. Re-checking reloads it. Disabled state persisted in QSettings across sessions. Dual-state system: runtime (`_soundFontStack` + `_disabledSoundFontPaths`) and pending (`_pendingSoundFontPaths` + `_pendingDisabledPaths`) for before/after FluidSynth initialization.
* **FFXIV SoundFont Mode auto-toggle** — `updateFfxivModeFromSoundFonts()` scans checked SoundFonts for "ff14" or "ffxiv" in the filename (case-insensitive). Auto-enables FFXIV SoundFont Mode when a matching font is checked; auto-disables when all matching fonts are unchecked. Reads from UI list widget directly to ensure correctness before engine commits.
* **Audio driver fallback chain** — `FluidSynthEngine::initialize()` tries multiple audio drivers in sequence: user preference → wasapi → dsound → waveout → sdl3 → sdl2. Common with SDL3 failures after a shutdown/restart cycle. Driver combo in settings reflects the actual driver used.
* **FluidSynth settings always enabled** — Removed `setEnabled(false)` on the FluidSynth settings group when non-FluidSynth output is selected. Users can manage SoundFonts at any time; settings applied when FluidSynth is next activated.
* **Pre-init SoundFont management** — `addPendingSoundFontPaths()` method allows adding SoundFonts before FluidSynth is initialized. `setSoundFontStack()` and `removeSoundFontByPath()` also update pending paths when engine is not initialized.
* **FluidSynth error feedback** — `QMessageBox::warning` shown when switching to FluidSynth output fails, with error details. Output automatically reverts to previous working port.
* **Output port fallback** — `MidiOutput::setOutputPort()` saves previous port; if new port's FluidSynth init fails, previous port restored automatically.

### Fixed
* **Guitar Pro audio export producing silence** — `file->path()` returned the `.gp5`/`.gpx` path which FluidSynth cannot parse. Now saves the in-memory MidiFile to a temporary `.mid` file, exports from that, then cleans up via `_exportTempMidiPath`.
* **Drum channel playing piano after FFXIV→GM switch** — Two issues: (1) `updateFfxivModeFromSoundFonts()` was called AFTER `setSoundFontEnabled()`, so `applyChannelMode()` still used old FFXIV flag during stack rebuild. Reordered to update mode FIRST. (2) `applyChannelMode()` GM restore didn't reset channel 9 properly — now sends `bank_select(ch9, 128)` + `program_change(ch9, 0)` to restore drum kit, plus `program_change(0)` on all melodic channels.
* **SoundFont state lost on shutdown** — `shutdown()` only preserved `_loadedFonts` (enabled fonts), losing disabled font paths. Now preserves full stack via `allSoundFontPaths()` → `_pendingSoundFontPaths` + `_pendingDisabledPaths`.
* **SoundFont list empty before init** — `allSoundFontPaths()` returned empty when `_soundFontStack` was empty (before initialization). Now falls back to `_pendingSoundFontPaths`.
* **Disabled SoundFonts lost on settings change** (V12-002) — Changing audio driver, sample rate, or reverb engine silently discarded disabled SoundFonts from the stack. Removed redundant `setSoundFontStack()` calls; `shutdown()`+`initialize()` already preserves and restores the full stack.
* **MP3 export reported success after encoding error** (V12-003) — LAME encoder returned true even when `lame_encode_buffer_interleaved()` reported an error. Now tracks error state, skips flush, deletes corrupt output, and returns false.
* **Cancel button ineffective during MP3 encoding** (V12-004) — Cancel only worked during WAV rendering phase. Now passes the cancel flag to `LameEncoder::encode()`, which checks it each iteration and aborts cleanly.
* **Export failure showed file path instead of error** (V12-005) — Error dialog displayed the output file path rather than an actionable message. Now shows descriptive success/failure messages with guidance.
* **Misleading comment in driver fallback loop** (V12-007) — Removed incorrect comment about re-creating settings+synth per driver attempt.
* **Radio button styling** — checked radio buttons render as proper circles in all 5 themes.

### Changed
* Export dialog now shows MP3 as a fourth format option
* Phase 20 title updated to "Audio Export & FluidSynth Hardening"
* Version bump to 1.2.0

### Technical Notes
* **LAME integration:** `lame/` directory contains LAME 3.100 source; compiled as static C library via `add_library(mp3lame STATIC ...)` in CMakeLists.txt. `LameEncoder.h/.cpp` wraps the LAME API for Qt integration.
* **FluidSynth driver fallback:** Preferred driver stored in `QSettings("FluidSynth/audio_driver")`. Fallback order: wasapi (Windows native) → dsound (DirectSound) → waveout (legacy) → sdl3 → sdl2. Each attempt calls `fluid_settings_setstr` + `new_fluid_audio_driver`; on failure, deletes settings/synth and tries next.
* **SoundFont state management:** Four containers: `_soundFontStack` (active ordered list), `_disabledSoundFontPaths` (runtime disabled QSet), `_pendingSoundFontPaths` (pre-init ordered list), `_pendingDisabledPaths` (pre-init disabled QSet). `shutdown()` merges runtime → pending. `initialize()` consumes pending → runtime.
* **Files created:** `src/midi/LameEncoder.h/.cpp`
* **Files modified:** `FluidSynthEngine.h/.cpp`, `MidiSettingsWidget.h/.cpp`, `MainWindow.h/.cpp`, `MidiOutput.cpp`, `CMakeLists.txt`

</details>

---

## [1.1.9] - 2026-04-07 — MidiPilot AI Improvements + GUI Bug Sweep

* Granular agent undo — each tool call gets its own Ctrl+Z step instead of one compound action
* Token counting fix — OpenAI Responses API, Anthropic, and Gemini usage fields now correctly normalized
* Persistent conversation history — conversations auto-saved as JSON, loadable from history menu
* Context window management — sliding-window truncation prevents exceeding model context limits
* Token label now shows context window size and warns at 80% usage
* Agent progress steps now theme-aware (dark/light mode colors)
* Response streaming (SSE) for Simple mode — text appears incrementally
* Per-file AI presets — save/load model, provider, mode, FFXIV, effort, and custom instructions per MIDI file
* 4 runtime bugfixes from Phase 19 testing — stop button crash, streaming JSON dump, vanishing prompt bubble, preset save on unsaved files
* MidiPilot Send/Stop buttons now use proper themed icons instead of Unicode emoji (consistent with toolbar style)
* **45 bug fixes** across 18 GUI source files — memory leaks, crash-causing null derefs, undo/redo corruption, data loss, division-by-zero, deprecated Qt6 API usage, and more

<details>
<summary>Full Changelog</summary>

### Phase 19 — MidiPilot AI Improvements

#### Fixed
* **Granular agent undo (19.1)** — Previously, all tool calls from one Agent request were wrapped in a single `startNewAction/endAction` pair, so Ctrl+Z reverted everything at once. Now each tool call (e.g. transpose, edit, rename) creates its own protocol step. Undo reverts one action at a time.
* **Token counting for non-OpenAI providers (19.3)** — `normalizeResponsesApiResponse()` was not copying the `usage` field from OpenAI Responses API responses (`input_tokens`/`output_tokens`). Also added normalization for Anthropic (`input_tokens`/`output_tokens`) and Gemini (`usageMetadata.promptTokenCount`/`candidatesTokenCount`) formats. All downstream code uses canonical `prompt_tokens`/`completion_tokens` field names.
* **Stop button crash** — Clicking Stop during an API request could crash because `cancel()` called `cancelRequest()` first, which triggered a synchronous `abort()` → `onReplyFinished` → `errorOccurred` double-fire. Fixed by reordering: `cleanup()` runs before `cancelRequest()`.
* **Simple mode JSON dump** — Streaming in Simple mode displayed raw JSON `{"actions":[...]}` in chat instead of executing it. Added `_streamIsJson` flag to suppress the streaming bubble for JSON action responses; `onStreamFinished` now delegates to `onResponseReceived` for proper action dispatch.
* **User prompt bubble disappearing** — `onResponseReceived` and `onErrorOccurred` blindly removed the last chat widget (intended for the "Thinking..." indicator), which sometimes deleted the user's own prompt bubble. Now checks for "Thinking" text via `qobject_cast<QLabel*>` before removing.
* **Preset save on unsaved file** — Saving an AI preset when the MIDI file had never been saved showed an error. Replaced with a `QFileDialog::getSaveFileName` fallback so the user can pick a file path first.

#### Added
* **Persistent conversation history (19.2)** — New `ConversationStore` class saves conversations as JSON files in `AppData/MidiPilotHistory/`. Auto-saves after every assistant response (debounced 2s). History button in toolbar shows past conversations sorted by date. Click to load and resume any previous conversation.
* **Context window management (19.5)** — Before each API request, conversation history is estimated for token count (~4 chars/token). If exceeding 70% of the model's context window, a sliding window keeps the first 2 messages + most recent messages that fit, inserting a truncation marker. Token label now shows `session / contextWindow` and turns yellow at 80% usage.
* **Model context window lookup** — `AiClient::contextWindowForModel()` returns known context windows for GPT-5/4o/4.1, Claude 3/4, Gemini 1.5/2.0/2.5, o-series models.
* **Agent progress polish (19.4)** — `AgentStepsWidget` step labels now use theme-aware colors via `Appearance::isDarkModeEnabled()`. Dark and light mode each have distinct pending/active/success/retry/failed colors.
* **Response streaming (19.6)** — Simple mode now uses SSE streaming (`"stream": true`). Text appears incrementally in a streaming bubble. On completion, the bubble is replaced with a proper styled chat bubble. Handles `[DONE]` sentinel, captures usage from final chunk. Agent mode remains non-streaming.
* **Per-file AI presets (19.7)** — Gear button now opens a menu with "Open AI Settings" and "Save AI preset for this file". Presets are stored as `<filename>.midipilot.json` sidecar files. Saves provider, model, mode, FFXIV, effort, and custom instructions. Auto-loaded when a file is opened via `onFileChanged()`. Custom instructions are appended to both Agent and Simple mode system prompts.

### GUI Bug Sweep — 45 fixes across 18 files

Automated bug-hunter scan of all 98 GUI source files identified 47 potential issues; 45 confirmed, 2 false positives. All fixes applied and verified.

#### Data Loss Prevention (3 fixes)
* **CORE-006/007/014** — `newFile()`, `loadFile()`, and `load()` now check the return value of `saveBeforeClose()`. Previously, clicking Cancel in the save prompt was ignored and unsaved work was silently discarded.

#### Undo/Redo Protocol Corruption (3 fixes)
* **CORE-005** — `equalize()` called `endAction()` without a matching `startNewAction()` when ≤1 notes were selected, corrupting the undo stack. Moved `endAction()` inside the guard.
* **WIDGET-001** — `EventWidget::setModelData()` left the protocol in a dangling state on validation error returns. Added `endAction()` before early returns.
* **WIDGET-002** — `MiscWidget::mouseReleaseEvent()` left the protocol open when track was null. Added `endAction()` before early returns.

#### Crash Fixes — Null Derefs & Out-of-Bounds (5 fixes)
* **CORE-009** — `MatrixWidget::mouseDoubleClickEvent()` null dereference when no file loaded. Added null check.
* **CORE-013** — `MainWindow::editChannel()` null dereference when `file` is null. Wrapped in `if (file)`.
* **MISC-004** — `TweakTarget::getTimeOneDivEarlier/Later()` crashed on single-element divs list (out-of-bounds access). Added `if (divs.size() < 2) return time` guard.
* **MISC-005** — `NoteTweakTarget` and `ValueTweakTarget` missing upper-bound checks — arrow keys could push note/velocity/CC past 127, pitch bend past 16383. Added `qBound()` clamping.
* **WIDGET-005** — `MiscWidget` `trackIndex` could go out-of-bounds if undo changed data between mouse press and release. Added bounds check.

#### Memory Leaks (14 fixes)
* **CORE-001/002** — `forward()` and `back()` leaked heap-allocated `QList` on every call. Changed to stack allocation.
* **CORE-003/004** — `saveas()` and `load()` leaked heap `QFile` and discarded directory result. Changed to stack `QFile`, assigned `dir` properly.
* **CORE-008** — `MatrixWidget::paintEvent()` leaked `QPainter` on two early-return paths. Added `delete painter` before returns.
* **CORE-012** — Multiple dialog functions leaked heap-allocated dialogs. Added `WA_DeleteOnClose` for `show()` dialogs, `delete` after `exec()` dialogs.
* **DLG-001** — `AboutDialog::loadContributors()` returned heap `QList*` that was never freed. Changed to return by value.
* **DLG-002/003** — `RecordDialog::enter()` leaked filtered-out MidiEvent objects; no destructor to clean up `_data`. Added destructor and cleanup loop.
* **DLG-005** — `TransposeDialog` leaked parentless `QButtonGroup`. Added `this` as parent.
* **DLG-006** — `SettingsDialog` leaked heap-allocated `_settingsWidgets` QList. Changed to stack allocation.
* **MISC-001** — `Appearance::decode()` leaked `defaultColor` parameter on success path. Added `delete defaultColor` before returning new color.
* **MISC-002** — `ClickButton::setImageName()` leaked previous `QImage` on reassignment. Added `delete image` before `new`, initialized to `nullptr`.
* **MISC-010** — `AppearanceSettingsWidget` leaked heap-allocated `_channelItems`/`_trackItems` QLists. Changed to stack allocation.

#### Division by Zero (2 fixes)
* **CORE-010** — `MatrixWidget::xPosOfMs()`, `msOfXPos()`, `timeMsOfWidth()` could divide by zero with zero-length files or very narrow widget. Added zero-checks.
* **WIDGET-006** — `MiscWidget::value()` divided by `height()` without checking for zero. Added `if (h <= 0) return 0` guard.

#### Signal & Resource Management (2 fixes)
* **CORE-011** — `play()` and `record()` accumulated `timeMsChanged` signal connections on each play/stop cycle, causing N+1 repaints per tick. Added `disconnect()` before `connect()`.
* **MISC-008** — `Appearance::processNextQueuedIcon()` used try/catch on potentially dangling `QAction*` pointer (undefined behavior). Changed `iconUpdateQueue` to use `QPointer<QAction>` for safe null detection.

#### Logic & Correctness (4 fixes)
* **WIDGET-003** — `EventWidget::setEditorData()` called `setMaximum()` instead of `setMinimum()` in 7 places (copy-paste error). Time signature allowed numerator 0. Fixed to `setMinimum()`.
* **WIDGET-008** — `InstrumentChooser::accept()` called `hide()` instead of `QDialog::accept()`, leaving result code as Rejected. Fixed to call base class.
* **DLG-004** — `TransposeDialog` constructor didn't pass `parent` to `QDialog` base class. Added `: QDialog(parent)` initializer.
* **DLG-009** — `TempoDialog::accept()` integer overflow in smooth tempo interpolation for long ramps. Changed to `qint64` arithmetic.

#### Download Robustness (2 fixes)
* **DLG-007** — `DownloadSoundFontDialog` didn't flush remaining network buffer before closing file. Added `readAll()` flush in finished handler.
* **DLG-008** — Download write errors silently ignored (disk full → corrupt SoundFont). Added return value check on `write()`, abort on failure.

#### Deprecated/Wrong Qt6 API (2 fixes)
* **WIDGET-009** — `TrackListWidget::dropEvent()` used deprecated `QDropEvent::pos()`. Changed to `position().toPoint()`.
* **MISC-011** — `PaintWidget::enterEvent(QEvent*)` didn't match Qt6 signature `enterEvent(QEnterEvent*)`. Fixed signature.

#### Initialization & Hardening (5 fixes)
* **MISC-003** — `GraphicObject::shownInWidget` never initialized (undefined behavior on read). Initialized to `false`.
* **MISC-009** — `MidiSettingsWidget::_inputPorts/_outputPorts` pointer members uninitialized. Set to `nullptr`.
* **WIDGET-010** — `ChannelListItem::loudAction/soloAction` uninitialized for channel ≥ 16. Set to `nullptr`.
* **WIDGET-011** — `ChannelListItem::toggleVisibility()` silent catch-all exception handler. Added `qWarning()` logging.
* **MISC-006** — `OpenGLPaintWidget::paintGL()` used `static QSize lastSize` shared across all instances, causing unnecessary GPU reallocations. Changed to member `_lastPaintSize`.

#### UI & Cosmetic (3 fixes)
* **DLG-010** — `DeleteOverlapsDialog::paintEvent()` called `update()` on child widget, creating redundant repaint cycles. Removed override.
* **DLG-011** — `DeleteOverlapsDialog::resizeEvent()` didn't call base class `QDialog::resizeEvent()`. Fixed.
* **MISC-007** — `AutoUpdater::applyUpdate()` PowerShell command injection via single-quote in paths. Added `'` → `''` escaping.

### Technical Notes
* **Phase 19 files created:** `src/ai/ConversationStore.h/.cpp`
* **Phase 19 files modified:** `src/gui/MidiPilotWidget.h/.cpp`, `src/ai/AiClient.h/.cpp`
* **Bug sweep files modified (18):** `MainWindow.cpp`, `MatrixWidget.cpp`, `EventWidget.cpp`, `MiscWidget.cpp`, `RecordDialog.cpp/.h`, `TransposeDialog.cpp`, `SettingsDialog.cpp/.h`, `AboutDialog.cpp/.h`, `DownloadSoundFontDialog.cpp`, `TempoDialog.cpp`, `DeleteOverlapsDialog.cpp`, `GraphicObject.cpp`, `ClickButton.cpp`, `Appearance.cpp`, `TweakTarget.cpp`, `InstrumentChooser.cpp`, `TrackListWidget.cpp`, `ChannelListWidget.cpp`, `OpenGLPaintWidget.cpp/.h`, `PaintWidget.h`, `MidiSettingsWidget.h`, `AppearanceSettingsWidget.cpp/.h`, `AutoUpdater.cpp`
* **Bug report:** `Planning/03_bugs.md` — full scan results with verification status

</details>

---

## [1.1.8.1] - 2026-04-06 — Bugfix: FFXIV Channel Fixer

* Fixed undo crash when reverting Channel Fixer operations
* Restored Tier 3 guitar track renaming with reliable data source
* Added Tier 2 event cleanup (remove CC, PitchBend, etc.; keep Text/lyrics)
* Context menu "Move to Channel" now shows instrument names
* 4 bug fixes from v1.1.8 regression + 2 improvements

<details>
<summary>Full Changelog</summary>

### Fixed
* **Undo crash after Channel Fixer** — v1.1.8 introduced `toProtocol=false` as a performance optimization for bulk operations, but this caused the Protocol undo system to record an empty step (discarded by `endAction`). Undoing past the Channel Fixer operation crashed because no undo entry existed and deleted events left dangling pointers. Reverted to `toProtocol=true` (default) so all changes are properly recorded; removed `delete ev` on removed Program Changes since the Protocol system keeps events alive for undo
* **Tier 3 guitar track renaming restored** — v1.1.8.0 removed all Tier 3 renaming as a workaround for unreliable `chToVariant` data. Now that `guitarChannelMap` is built from `assignedChannel()` (the v1.1.8.0 fix), the reverse-map is reliable again. Tier 3 now detects when a guitar track's first note is on a different variant's channel and renames accordingly (e.g. Overdriven track with first note on Distortion channel → renamed to PowerChords)
* **Tier 3 crash / wrong channel on first run after Tier 2** — (carried forward from v1.1.8.0) Uses `track->assignedChannel()` as sole source of truth
* **Free channel reservation Tier 2 only** — (carried forward from v1.1.8.0)

### Added
* **Tier 2 event cleanup** — Tier 2 (Rebuild) now removes non-essential MIDI events (ControlChange, PitchBend, ChannelPressure, KeyPressure, SysEx, etc.) from all channels. FFXIV performance mode doesn't use these events. Text events (lyrics) and notes are preserved
* **Context menu channel names** — Right-click "Move to Channel" submenu now shows instrument names (e.g. `0: Piano`, `1: ElectricGuitarOverdriven`) matching the toolbar channel menus, instead of bare channel numbers

### Technical Notes
* **Undo root cause:** `toProtocol=false` prevents `MidiChannel::removeEvent`/`MidiEvent::moveToChannel` from calling `protocol(copy, this)`, so `startNewAction/endAction` recorded 0 items and silently discarded the step
* **Files modified:** `src/ai/FFXIVChannelFixer.cpp` (undo fix, rename restoration, event cleanup), `src/gui/MatrixWidget.cpp` (channel names in context menu)

</details>

---

## [1.1.8] - 2026-04-06 — Mewo Feature Sync

> *Cherry-picking the best upstream features: context menus, note presets, timeline markers, chord detection, MML import, drum presets, and a whole lotta polish.*

* Right-click context menu, note duration presets, smooth playback scrolling
* Timeline markers (CC/PC/Text), status bar with chord detection
* MML importer (.mml/.3mle), drum kit presets, DLS SoundFont support
* 7 bug fixes · 3 UI changes · Cherry-picked 16 features from Mewo upstream

<details>
<summary>Full Changelog</summary>

### Added
* **Right-Click Context Menu** — right-click on selected events in the piano roll for quick access to Quantize, Copy, Delete, Transpose, Move to Track/Channel, Scale, and Legato operations. Plain right-click opens the menu; Ctrl+Right-Click still creates notes
* **Note Duration Presets** — when the pencil tool is active, select a fixed note duration (whole, half, quarter, 8th, 16th, 32nd) from the toolbar with keyboard shortcuts (1-6). Shows a semi-transparent ghost preview on hover before clicking
* **Smooth Playback Scrolling** — playback cursor now smoothly scrolls the viewport instead of jumping by screen-widths. Toggle in Settings → Appearance or Additional Midi Settings
* **Timeline Markers** — visual CC, Program Change, and Text/Marker event indicators on the timeline:
  - Dashed vertical lines through the note area with event-colored markers
  - Dedicated 16px marker row below the ruler with labeled badges (PC0, CC7, lyrics, etc.)
  - Toggleable per type in Settings → Appearance
  - Color by Track or by Channel mode
* **Status Bar with Chord Detection** — bottom status bar showing selected note info, chord analysis (major/minor/7th/dim/aug/sus), tick position, and event count. Togglable in Settings → System & Performance
* **MML Importer** — import Music Macro Language (.mml/.3mle) text files as MIDI. Supports 3MLE format used by FFXIV bard performers with multi-track channels, tempo, octave, note length, volume, and program change commands
* **DrumKit Preset Mapping** — Split Channels dialog now includes a "Drum Kit Preset" dropdown for channel 9 with 17 standard GM drum kits (Standard, Room, Power, Electronic, etc.) that auto-insert the correct Program Change event
* **DLS SoundFont Support** — file dialog now accepts `.dls` (Downloadable Sound) files alongside `.sf2`/`.sf3`

### Fixed
* **Null-byte text event truncation** — MIDI text events containing `\0` null bytes no longer truncate or show `[]` boxes in the UI
* **SizeChangeTool improvements** — resize handle behavior refined for more precise note length editing
* **Measure numbers shifting with markers** — measure numbers now use a fixed Y position (`textY = 41`) so they don't jump when the timeline marker bar appears/disappears
* **TrackList/ChannelList stale toolbar colors** — added `refreshColors()` methods that properly update toolbar palettes on theme change, instead of just repainting
* **FFXIVChannelFixer memory leak** — Program Change events removed during channel fixing are now properly `delete`d after removal from the channel
* **FFXIVChannelFixer bulk operation performance** — `moveToChannel()` and `removeEvent()` now accept a `toProtocol` parameter; Channel Fixer passes `false` to skip hundreds of individual undo entries during batch operations
* **Appearance Settings UI** — collapsible Channel/Track Colors sections now use clickable arrow buttons (▶/▼) instead of checkboxes, readable in all themes. Settings panel wrapped in QScrollArea

### Changed
* **Timeline layout** — separate `MarkerArea` rect for the 16px marker row below the 50px ruler; cleaner hit-testing separation
* **Full-height vertical divider** — the divider between piano/header area and the note area now extends from top to bottom of the widget
* **Removed bottom/right border lines** — cleaner look without redundant edge borders on the piano roll
* Version bump to 1.1.8

### Technical Notes
* **Mewo upstream sync:** Cherry-picked 16 features/fixes from Meowchestra/MidiEditor commits `28eb14c..5080ff3` (62 commits after fork-point `25ebba3`)
* **New files:** `src/gui/ChordDetector.h`, `src/gui/StatusBarSettingsWidget.h/cpp`, `src/gui/SplitChannelsDialog.h/cpp`, `src/midi/DrumKitPreset.h/cpp`, `src/converter/MML/ThreeMleParser.h/cpp`, `src/converter/MML/MmlConverter.h/cpp`
* **Core modifications:** `MatrixWidget.h/cpp` (context menu, smooth scroll, marker bar, MarkerArea), `MainWindow.h/cpp` (status bar, duration presets, MML import), `Appearance.h/cpp` (marker settings, refreshColors), `MidiEvent.h/cpp` + `OnEvent.h/cpp` + `MidiChannel.h/cpp` (`toProtocol` parameter), `FFXIVChannelFixer.cpp` (leak fix + toProtocol), `TrackListWidget.h/cpp` + `ChannelListWidget.h/cpp` (refreshColors), `AppearanceSettingsWidget.cpp` (collapsible arrows, scrollable layout)

</details>

---

## [1.1.7] - 2026-04-05 — The Totally Unnecessary Glow Up

> *Adding Dark/Light Mode and a totally useless but cool MIDI Visualizer.*

* 7 dark/light QSS themes (Dark, Light, Sakura, AMOLED, Material Dark, System, Classic)
* Real-time 16-channel MIDI Visualizer in the toolbar
* 10 note bar color presets, Sakura piano keys, dark title bar (Windows)
* Dark mode icon adjustment, checkbox visibility fix
* 2 bug fixes · Theme restart mechanism

<details>
<summary>Full Changelog</summary>

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

</details>

---

## [1.1.6.1] - 2026-04-04 — Bugfix: Duplicate Guitar Track Channels

* Fixed duplicate guitar variants mapped to wrong channel in Fix X|V
* Fixed Tier 3 duplicate guitar notes not migrated to shared channel

<details>
<summary>Full Changelog</summary>

### Fixed
* **Fix X|V Channels: duplicate guitar variants mapped to wrong channel** — when two or more tracks shared the same guitar variant name (e.g. two "ElectricGuitarPowerChords" tracks), the second track was assigned its own channel instead of sharing the first occurrence's channel. This caused the duplicate track to receive a wrong program (e.g. Piano on CH3 instead of Distortion Guitar). Now all tracks with the same guitar variant name share a single channel with the correct program change, in both Rebuild (Tier 2) and Preserve (Tier 3) modes
* **Fix X|V Channels Tier 3: duplicate guitar notes not migrated** — in Preserve mode, duplicate guitar tracks had their notes stranded on the original channel. Added note migration for duplicate guitar tracks to move events to the shared target channel

### Changed
* Version bump to 1.1.6.1

</details>

---

## [1.1.6] - 2026-04-04 — Guitar Pro Import (GP1–GP8)

* Native Guitar Pro import: GP1–GP8 (.gtp, .gp3, .gp4, .gp5, .gpx, .gp)
* Fixed GP3/GP4/GP5 binary parser bugs, GP6 BCFZ decompression, GP7/GP8 ZIP extraction
* GP1/GP2 legacy parser ported from TuxGuitar reference
* Explode Chords toolbar icon

<details>
<summary>Full Changelog</summary>

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

</details>

---

## [1.1.5] - 2026-03-31 — Auto-Updater

* Seamless in-app auto-updater from GitHub Releases (Update Now / After Exit / Download / Skip)
* Self-update: renames running EXE, extracts new, restarts — no installer needed
* Fixed MidiPilot chat Ctrl+C copy and context menu styling

<details>
<summary>Full Changelog</summary>

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

</details>

---

## [1.1.4.1] - 2026-03-30 — Chat UX Fix

* Fixed MidiPilot chat Ctrl+C copy and styled context menu

<details>
<summary>Full Changelog</summary>

### Fixed
* **MidiPilot chat: Ctrl+C copy** — selected text in chat bubbles can now be copied with Ctrl+C (previously only right-click → Copy worked)
* **MidiPilot chat: context menu** — replaced the unstyled default system context menu with a compact dark-themed menu (Copy / Select All) that matches the application style

### Changed
* Version bump to 1.1.4.1

</details>

---

## [1.1.4] - 2026-03-29 — Split Channels to Tracks

* Split single-track multi-channel MIDI into one track per instrument
* Preview dialog with GM instrument names, auto-naming, drum track option
* Single undo action, toolbar migration for existing users

<details>
<summary>Full Changelog</summary>

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

</details>

---

## [1.1.3.1] - 2026-03-29 — Auto-Save

* Auto-save with sidecar `.autosave` backups after configurable idle period
* Crash recovery for untitled documents, orphaned backup detection
* Configurable interval (30–600s), toggle in Settings → System & Performance

<details>
<summary>Full Changelog</summary>

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

</details>

---

## [1.1.3] - 2026-03-28 — Prompt Architecture v2, Crash Fix & Provider Selector

* Provider selector in MidiPilot footer (OpenAI / OpenRouter / Gemini / Custom)
* Prompt v2: priority rules, validation block, timing reference, truncation fallback
* Fixed crash on New File during playback (use-after-free)

<details>
<summary>Full Changelog</summary>

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

</details>

---

## [1.1.2.2] - 2026-03-28 — Manual Update: Prompt Examples & CI

* Prompt Examples with screenshots and downloadable MIDI files
* Fixed broken download links, stray `n` artifact, navbar overflow
* CI dependency bumps (GitHub Actions v6/v7)

<details>
<summary>Full Changelog</summary>

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

</details>

---

## [1.1.2.1] - 2026-03-26 — Hotfix: SoundFont Persistence & CI Fix

* Fixed SoundFont stack lost when switching MIDI output
* Fixed CI/Release FluidSynth download 404

<details>
<summary>Full Changelog</summary>

### Fixed
* **SoundFont stack lost when switching MIDI output** — switching from FluidSynth to another output (e.g. Microsoft GS Wavetable) and back no longer loses loaded SoundFonts; the engine now preserves font paths across shutdown/reinitialize cycles
* **CI/Release workflow: FluidSynth download 404** — updated FluidSynth v2.5.2 asset URL to match upstream's renamed zip (`fluidsynth-v2.5.2-…` instead of `fluidsynth-2.5.2-…`)

</details>

---

## [1.1.2] - 2026-03-26 — FluidSynth & FFXIV SoundFont Mode

* Built-in FluidSynth synthesizer with SoundFont stack management
* FFXIV SoundFont Mode: melodic channels + per-note drum program injection
* Velocity normalization in Fix X|V Channels (all notes → 127)
* Version in title bar, DLS support, SoundFont download dialog
* 2 bug fixes (guitar channel migration, transpose dialog)

<details>
<summary>Full Changelog</summary>

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

</details>

---

## [1.1.1] - 2026-03-25 — Upstream merge

* Metronome rewrite: GM drum notes instead of WAV files
* Removed Qt6::Multimedia and Qt6::Xml dependencies
* Plugin path fix, rtmidi updated

<details>
<summary>Full Changelog</summary>

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

</details>

---

## [1.1.0] - 2026-03-25

* Fix X|V Channels: one-click deterministic FFXIV channel fixer
* 5-step algorithm with Rebuild/Preserve modes, guitar variant channels
* Rich result summary, progress dialog, single undo action
* Fixed QSettings mismatch, stale channel menus

<details>
<summary>Full Changelog</summary>

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

</details>

---

## [1.0.2] - 2026-03-24

* FFXIV Bard Mode system prompts rewritten for better LLM compliance

<details>
<summary>Full Changelog</summary>

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

</details>

---

## [1.0.1] - 2026-03-24

* Manual: Prompt Examples page with screenshots, MIDI downloads, demo videos
* New Chat button redesign + confirmation dialog
* FFXIV example prompts improved

<details>
<summary>Full Changelog</summary>

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

</details>

---

## [1.0.0] - 2026-03-20

* **MidiPilot AI Assistant** — integrated chat panel with Agent Mode (13 tools), Simple Mode, FFXIV Bard Mode
* Multi-provider support (OpenAI, Gemini, OpenRouter, Ollama, etc.), token tracking, editable system prompts
* FFXIV Validation tool, GM Drum Conversion tool, API logging, reasoning display
* Rebranded to MidiEditor AI with CI/CD, GitHub Pages manual
* Based on Meowchestra/MidiEditor v4.3.1 (all upstream features included)

<details>
<summary>Full Changelog</summary>

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
