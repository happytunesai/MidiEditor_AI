#!/usr/bin/env python3
"""Inject the doc sub-nav bar into all manual content pages.

Run from repo root:
    python scripts/inject_doc_subnav.py

Idempotent — removes any existing doc-subnav before injecting.
"""

import pathlib, re

MANUAL = pathlib.Path(__file__).resolve().parent.parent / "manual"

# (filename, icon, label)  — order = button order
NAV_ITEMS = [
    ("docs-index.html",           "📖", "Index"),
    ("midipilot.html",            "🤖", "MidiPilot"),
    ("prompt-examples.html",      "💬", "Prompts"),
    ("ffxiv-channel-fixer.html",  "🎮", "Fix XIV"),
    ("split-channels.html",       "✂️",  "Split"),
    ("themes.html",               "🎨", "Themes"),
    ("soundfont.html",            "🔊", "SoundFonts"),
    ("guitar-pro.html",           "🎸", "Guitar Pro"),
    ("midi-overview.html",        "🎹", "MIDI"),
    ("editor-and-components.html","🖥️", "Editor"),
    ("setup.html",                "⚙️",  "Setup"),
    ("editing-midi-files.html",   "✏️",  "Editing"),
    ("playback.html",             "▶️",  "Playback"),
    ("export-audio.html",         "💾", "Export"),
    ("menu-tools.html",           "🔧", "Tools"),
]

ALL_DOC_FILES = [item[0] for item in NAV_ITEMS]

MARKER_START = "<!-- doc-subnav:start -->"
MARKER_END   = "<!-- doc-subnav:end -->"


def build_subnav_html(active_file: str) -> str:
    """Return the full sub-nav HTML block for a given page."""
    buttons = []
    for fname, icon, label in NAV_ITEMS:
        cls = ' class="active"' if fname == active_file else ""
        buttons.append(
            f'    <a href="{fname}"{cls}>'
            f'<span class="icon">{icon}</span>{label}</a>'
        )
    inner = "\n".join(buttons)
    return (
        f'{MARKER_START}\n'
        f'<div class="doc-subnav" role="navigation" aria-label="Manual sections">\n'
        f'{inner}\n'
        f'</div>\n'
        f'{MARKER_END}'
    )


def inject(filepath: pathlib.Path, active_file: str) -> bool:
    """Inject (or replace) the doc sub-nav in one file. Returns True if changed."""
    text = filepath.read_text(encoding="utf-8")

    # Remove old injection if present
    old_pat = re.compile(
        rf'{re.escape(MARKER_START)}.*?{re.escape(MARKER_END)}\n?',
        re.DOTALL,
    )
    text = old_pat.sub("", text)

    # Find insertion point: right after </nav> and before <a … skip-link
    insert_pat = re.compile(r'(</nav>\s*\n)(\s*<a href="#main-content")')
    m = insert_pat.search(text)
    if not m:
        print(f"  SKIP {filepath.name}: insertion point not found")
        return False

    subnav = build_subnav_html(active_file)
    replacement = m.group(1) + "\n" + subnav + "\n\n" + m.group(2).lstrip()
    text = text[:m.start()] + replacement + text[m.end():]

    filepath.write_text(text, encoding="utf-8")
    return True


def main():
    changed = 0
    for fname in ALL_DOC_FILES:
        fp = MANUAL / fname
        if not fp.exists():
            print(f"  MISSING {fname}")
            continue
        if inject(fp, fname):
            print(f"  OK  {fname}")
            changed += 1
    print(f"\nDone — {changed}/{len(ALL_DOC_FILES)} files updated.")


if __name__ == "__main__":
    main()
