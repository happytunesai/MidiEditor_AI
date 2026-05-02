#!/usr/bin/env python3
"""
dedash.py - Replace em-dashes with plain hyphens across manual, changelog, README.

Why: long em-dashes (--, U+2014) and the HTML entity &mdash; are a tell-tale
sign of AI-generated text. This script normalises them to a regular ASCII
hyphen "-" so the docs read more like hand-written prose.

Targets:
  - MidiEditor_AI/CHANGELOG.md
  - MidiEditor_AI/README.md
  - MidiEditor_AI/manual/**/*.html
  - MidiEditor_AI/manual/**/*.md

Replacements:
  U+2014 EM DASH       -> "-"
  U+2013 EN DASH       -> "-"
  &mdash;              -> "-"
  &ndash;              -> "-"
  &#8212; / &#x2014;   -> "-"
  &#8211; / &#x2013;   -> "-"

Usage:
  python scripts/dedash.py            # apply changes
  python scripts/dedash.py --dry-run  # report only
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

REPLACEMENTS = [
    ("\u2014", "-"),        # em dash
    ("\u2013", "-"),        # en dash
    ("&mdash;", "-"),
    ("&ndash;", "-"),
    ("&#8212;", "-"),
    ("&#8211;", "-"),
    ("&#x2014;", "-"),
    ("&#x2013;", "-"),
    ("&#X2014;", "-"),
    ("&#X2013;", "-"),
]


def collect_targets(root: Path) -> list[Path]:
    targets: list[Path] = []
    for name in ("CHANGELOG.md", "README.md"):
        p = root / name
        if p.is_file():
            targets.append(p)

    manual = root / "manual"
    if manual.is_dir():
        for pattern in ("*.html", "*.md"):
            targets.extend(sorted(manual.rglob(pattern)))
    return targets


def process_file(path: Path, dry_run: bool) -> tuple[int, dict[str, int]]:
    try:
        original = path.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        print(f"  SKIP (not utf-8): {path}")
        return 0, {}

    counts: dict[str, int] = {}
    text = original
    for needle, replacement in REPLACEMENTS:
        n = text.count(needle)
        if n:
            counts[needle] = n
            text = text.replace(needle, replacement)

    total = sum(counts.values())
    if total and not dry_run:
        path.write_text(text, encoding="utf-8")
    return total, counts


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dry-run", action="store_true",
                        help="report counts without modifying files")
    parser.add_argument("--root", type=Path, default=None,
                        help="MidiEditor_AI root (defaults to script's parent)")
    args = parser.parse_args()

    root = args.root or Path(__file__).resolve().parent.parent
    if not root.is_dir():
        print(f"Root not found: {root}", file=sys.stderr)
        return 2

    targets = collect_targets(root)
    if not targets:
        print("No target files found.")
        return 0

    grand_total = 0
    files_changed = 0
    print(f"Root: {root}")
    print(f"Mode: {'DRY-RUN' if args.dry_run else 'APPLY'}")
    print(f"Scanning {len(targets)} files...\n")

    for path in targets:
        total, counts = process_file(path, args.dry_run)
        if total:
            files_changed += 1
            grand_total += total
            rel = path.relative_to(root)
            detail = ", ".join(f"{k!r}:{v}" for k, v in counts.items())
            print(f"  {rel}  ({total} replacements)  [{detail}]")

    print()
    print(f"Files touched : {files_changed}")
    print(f"Total replaced: {grand_total}")
    if args.dry_run:
        print("(dry-run - no files were modified)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
