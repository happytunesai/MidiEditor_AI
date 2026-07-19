# Playground - MidiEditor AI

Experimental work that is merged on `main` but not part of any official release:
source-only, community-maintained, no prebuilt binaries and no auto-updates.
The official [CHANGELOG](CHANGELOG.md) documents releases only; this file keeps
every experimental merge documented (PR, dates, status) until an entry
graduates into a release section of the CHANGELOG.

---

## macOS build-from-source support

**PR:** [#12](https://github.com/happytunesai/MidiEditor_AI/pull/12) · **Merged:** 2026-07-14 · **Status:** experimental

The project builds and runs on macOS: `make mac-setup && make mac-build`
(Homebrew Bundle with Qt 6 and FluidSynth, CMake bundle configuration, optional
local `.app` packaging via `make mac-app`). No prebuilt binaries and no
auto-updates; a build-only macOS CI job guards the platform against
regressions. Data-path migration for any *distributed* macOS build is tracked
in [issue #13](https://github.com/happytunesai/MidiEditor_AI/issues/13).

Note: the FluidSynth audio-driver hardening from the same PR benefits all
platforms and therefore ships as regular release content (see CHANGELOG,
2.1.0).

---

## macOS app icon and bundle metadata

**PR:** [#14](https://github.com/happytunesai/MidiEditor_AI/pull/14) · **Merged:** 2026-07-19 · **Status:** experimental

The official app icon as `MidiEditorAI.icns` (bundled into `Resources/`) plus a
custom `macos/Info.plist.in`: bundle identity, MIDI file-type associations
(`.mid`/`.midi`/`.smf` as Editor role) and the matching UTI declaration, wired
through the `MACOSX_BUNDLE_*` CMake properties. The local `.app` from
`make mac-app` now looks and registers like a native macOS application.
