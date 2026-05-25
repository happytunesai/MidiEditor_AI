# tests/data — Local fixture directory

This folder is **gitignored** (except this README and `.gitkeep`). Files placed here stay on your machine and are never pushed to GitHub.

> **Status:** as of v1.7.1, **no test in the suite uses this folder.** All 41 test executables build their fixtures programmatically (in-memory structures or `QTemporaryFile`) per the recommendation at the bottom of this page. The Tier-2 corpus-test pattern below exists for when that's not enough; until then it's documentation, not a live code path.

## Why

Most real-world test fixtures (Guitar Pro tabs, MusicXML engravings, MIDI files from third parties) carry copyright on the *transcription* even when the underlying composition is public domain. To keep the repository legally clean, we do not ship such files.

## How tests use this folder (Tier-2 corpus pattern)

Tests that need a real-world file should:

1. Read the path from the `MIDIEDITOR_FIXTURES` environment variable, falling back to the in-tree `tests/data/` directory when unset.
2. Skip themselves with `QSKIP("...")` when the requested fixture is missing — never hard-fail.

The default fallback is wired in via the `MIDIEDITOR_TESTS_DATA_DIR` compile-definition (set in `tests/CMakeLists.txt` to `${CMAKE_CURRENT_SOURCE_DIR}/data`), so the snippet below works in any test target without per-test wiring:

```cpp
const QString fixturesDir = qEnvironmentVariable(
    QStringLiteral("MIDIEDITOR_FIXTURES"),
    QStringLiteral(MIDIEDITOR_TESTS_DATA_DIR));
const QString file = fixturesDir + QStringLiteral("/Mozart - Alla Turca.gp5");
if (!QFile::exists(file))
    QSKIP("Fixture not present; set MIDIEDITOR_FIXTURES or drop the file in tests/data/");
```

Whenever possible, prefer **programmatic fixtures** (build the structure in C++ and write to `QTemporaryFile`) over committed or local files. That's what every current test does.

## Bringing your own fixtures

To run corpus tests, drop your own files into this directory or set:

```
set MIDIEDITOR_FIXTURES=C:\path\to\my\fixtures
```

Suggested sources for license-clean material:

- [Mutopia Project](https://www.mutopiaproject.org/) — public-domain re-engravings (MIDI, MusicXML, LilyPond)
- [IMSLP](https://imslp.org/) — public-domain scores; check per-file license
- Files you transcribed yourself in MuseScore / TuxGuitar / Guitar Pro

## What lives here today (developer-local)

Anything you place in this folder is invisible to git. Keep your own list if you need one — do not add file names to this README, since other contributors will have a different set.
