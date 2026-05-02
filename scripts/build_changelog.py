#!/usr/bin/env python3
"""Build changelog.html from CHANGELOG.md for the MidiEditor AI website.

Usage:
    python scripts/build_changelog.py                     # default paths
    python scripts/build_changelog.py CHANGELOG.md manual/changelog.html

Parses the project CHANGELOG.md (version headers, bullet summaries,
<details> blocks with categorised bug fixes) and generates a fully styled
standalone HTML page that matches the site design.
"""

import html
import re
import sys
from pathlib import Path

# ── paths ──────────────────────────────────────────────────────────────
ROOT = Path(__file__).resolve().parent.parent
DEFAULT_INPUT = ROOT / "CHANGELOG.md"
DEFAULT_OUTPUT = ROOT / "manual" / "changelog.html"


# ── markdown helpers ───────────────────────────────────────────────────

def md_inline(text: str) -> str:
    """Convert inline markdown (bold, code, links) to HTML."""
    t = html.escape(text)
    # bold
    t = re.sub(r"\*\*(.+?)\*\*", r"<strong>\1</strong>", t)
    # inline code
    t = re.sub(r"`(.+?)`", r'<code class="hl">\1</code>', t)
    # links [text](url)
    t = re.sub(
        r"\[([^\]]+)\]\(([^)]+)\)",
        r'<a href="\2" target="_blank" rel="noopener noreferrer">\1</a>',
        t,
    )
    # em-dash
    t = t.replace(" --- ", " - ")
    t = t.replace(" -- ", " - ")
    return t


def classify_category(heading: str) -> str:
    """Return a CSS class based on the category heading text."""
    h = heading.lower()
    if "critical" in h or "crash" in h:
        return "cat-critical"
    if "memory leak" in h:
        return "cat-memory"
    if "low" in h or "minor" in h or "cosmetic" in h or "deferred" in h:
        return "cat-low"
    if "added" in h or "feature" in h:
        return "cat-feature"
    if "fixed" in h or "bug" in h or "fix" in h:
        return "cat-fix"
    if "changed" in h or "removed" in h:
        return "cat-changed"
    if "technical" in h or "note" in h:
        return "cat-tech"
    return "cat-medium"


def badge_for_category(cls: str) -> str:
    """Return an emoji/label badge for a category CSS class."""
    return {
        "cat-critical": "🔴 Critical",
        "cat-memory": "🟠 Memory",
        "cat-feature": "✨ Added",
        "cat-fix": "🔧 Fixed",
        "cat-changed": "🔄 Changed",
        "cat-low": "🟢 Low",
        "cat-tech": "📝 Notes",
        "cat-medium": "🟡 Medium",
    }.get(cls, "")


# ── parser ─────────────────────────────────────────────────────────────

def parse_changelog(text: str):
    """Yield (version, date, title, summary_bullets, details_html) tuples."""
    # Split on version headers: ## [x.y.z] - date - title
    # Accepts "-", em-dash, or en-dash as the date/title separator (legacy).
    version_re = re.compile(
        r"^## \[(.+?)\] - (\S+)(?:\s+[-\u2014\u2013]\s+(.+))?$", re.MULTILINE
    )
    splits = list(version_re.finditer(text))

    for i, m in enumerate(splits):
        version = m.group(1)
        date = m.group(2)
        title = m.group(3) or ""
        start = m.end()
        end = splits[i + 1].start() if i + 1 < len(splits) else len(text)
        body = text[start:end].strip()

        # Separate summary bullets (before <details>) from details block
        details_match = re.search(
            r"<details>.*?</details>", body, re.DOTALL
        )
        if details_match:
            summary_md = body[: details_match.start()].strip()
            details_md = details_match.group()
        else:
            summary_md = body
            details_md = ""

        # blockquote lines (> ...) become a subtitle
        blockquote = ""
        summary_lines = []
        for line in summary_md.split("\n"):
            stripped = line.strip()
            if stripped.startswith("> "):
                blockquote = stripped[2:].strip("*_ ")
            elif stripped.startswith("* "):
                summary_lines.append(stripped[2:])
            # skip --- and blank lines

        # Parse details block into categorised sections
        details_html = _parse_details(details_md) if details_md else ""

        yield version, date, title, blockquote, summary_lines, details_html


def _parse_details(block: str) -> str:
    """Parse the <details> block into styled HTML sections."""
    # Strip <details>, <summary>, </details>
    inner = re.sub(r"</?details>", "", block)
    inner = re.sub(r"<summary>.*?</summary>", "", inner, flags=re.DOTALL)
    inner = inner.strip()

    parts = []
    # Split on ### headings
    chunks = re.split(r"^### (.+)$", inner, flags=re.MULTILINE)
    # chunks[0] is text before first ###, then alternating heading/content

    for j in range(1, len(chunks), 2):
        heading = chunks[j].strip()
        content = chunks[j + 1] if j + 1 < len(chunks) else ""
        cat_cls = classify_category(heading)
        badge = badge_for_category(cat_cls)

        items_html = _parse_section_content(content.strip())

        parts.append(
            f'<div class="cl-cat {cat_cls}">'
            f'<h4><span class="cat-badge">{badge}</span> {md_inline(heading)}</h4>'
            f"{items_html}"
            f"</div>"
        )

    return "\n".join(parts)


def _parse_section_content(content: str) -> str:
    """Parse section content (may have #### sub-headings and bullet lists)."""
    lines = content.split("\n")
    parts = []
    current_sub = None
    current_items = []

    def flush():
        nonlocal current_sub, current_items
        if current_items:
            html_items = "".join(
                f"<li>{md_inline(item)}</li>" for item in current_items
            )
            if current_sub:
                parts.append(
                    f'<h5>{md_inline(current_sub)}</h5><ul>{html_items}</ul>'
                )
            else:
                parts.append(f"<ul>{html_items}</ul>")
        current_sub = None
        current_items = []

    for line in lines:
        stripped = line.strip()
        if stripped.startswith("#### "):
            flush()
            current_sub = stripped[5:]
        elif stripped.startswith("* "):
            current_items.append(stripped[2:])
        # skip empty lines and other content

    flush()
    return "".join(parts)


# ── HTML template ──────────────────────────────────────────────────────

HTML_HEAD = """\
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Changelog | MidiEditor AI</title>
<meta name="description" content="Full release history for MidiEditor AI - every version, feature, bug fix, and improvement documented.">
<meta property="og:title" content="Changelog - MidiEditor AI">
<meta property="og:description" content="Full release history for MidiEditor AI - every version, feature, and bug fix.">
<meta property="og:url" content="https://midieditor-ai.de/changelog.html">
<meta property="og:image" content="https://midieditor-ai.de/midipilot_ai_OG.png">
<link rel="icon" type="image/png" href="favicon.png">
<link rel="preconnect" href="https://github.com" crossorigin>
<meta name="theme-color" content="#0B1020">
<link rel="stylesheet" href="site.css">
<style>
    /* ─ changelog hero ─ */
    .cl-hero {
        padding: calc(var(--nav-height) + 3rem) 0 2.5rem;
        text-align: center;
        background: var(--gradient-hero);
    }
    .cl-hero h1 {
        font-size: 2.5rem; font-weight: 800;
        background: var(--gradient-accent);
        -webkit-background-clip: text;
        -webkit-text-fill-color: transparent;
        background-clip: text;
        margin-bottom: .5rem;
    }
    .cl-hero p { color: var(--text-muted); }

    /* ─ search / filter ─ */
    .cl-search {
        max-width: 900px; margin: 0 auto 2rem; padding: 0 1rem;
    }
    .cl-search input {
        width: 100%; padding: .65rem 1rem; font-size: .95rem;
        background: var(--bg-card); color: var(--text);
        border: 1px solid var(--border); border-radius: 8px;
        outline: none; transition: border-color .2s;
    }
    .cl-search input:focus { border-color: var(--accent); }
    .cl-search input::placeholder { color: var(--text-muted); }
    .cl-no-results {
        display: none; text-align: center; padding: 3rem 1rem;
        color: var(--text-muted); font-size: 1.1rem;
    }

    /* ─ timeline ─ */
    .cl-timeline {
        max-width: 900px; margin: 0 auto 4rem; padding: 0 1rem;
        position: relative;
    }
    .cl-timeline::before {
        content: ""; position: absolute; left: 20px; top: 0; bottom: 0;
        width: 2px; background: var(--border);
    }

    /* ─ version card ─ */
    .cl-version {
        position: relative; padding-left: 50px; margin-bottom: 2.5rem;
    }
    .cl-dot {
        position: absolute; left: 12px; top: 6px;
        width: 18px; height: 18px; border-radius: 50%;
        background: var(--accent); border: 3px solid var(--bg);
        z-index: 1;
    }
    .cl-version.latest .cl-dot {
        box-shadow: 0 0 12px rgba(0,184,255,.55);
    }
    .cl-version-head {
        display: flex; flex-wrap: wrap; align-items: baseline; gap: .5rem .75rem;
        margin-bottom: .5rem;
    }
    .cl-version-head h2 {
        font-size: 1.4rem; font-weight: 700; margin: 0;
        color: var(--text);
    }
    .cl-version-head h2 a {
        color: inherit; text-decoration: none;
    }
    .cl-version-head h2 a:hover { color: var(--accent); }
    .cl-date { color: var(--text-muted); font-size: .85rem; }
    .cl-badge-latest {
        display: inline-block; padding: 2px 10px; font-size: .7rem;
        font-weight: 700; text-transform: uppercase; letter-spacing: .5px;
        background: var(--accent); color: #fff; border-radius: 10px;
    }
    .cl-title {
        font-size: 1rem; color: var(--text-muted); margin-bottom: .75rem;
        font-style: italic;
    }
    .cl-blockquote {
        font-size: .85rem; color: var(--text-muted); font-style: italic;
        margin-bottom: .5rem; padding-left: .75rem;
        border-left: 2px solid var(--border);
    }

    /* summary bullets */
    .cl-summary { list-style: none; padding: 0; margin: 0 0 .75rem; }
    .cl-summary li {
        position: relative; padding-left: 1.25rem;
        margin-bottom: .35rem; line-height: 1.5; font-size: .9rem;
    }
    .cl-summary li::before {
        content: ""; position: absolute; left: 0; top: .55em;
        width: 6px; height: 6px; border-radius: 50%;
        background: var(--accent);
    }

    /* expand toggle */
    .cl-toggle {
        display: inline-block; cursor: pointer;
        color: var(--accent); font-size: .85rem; font-weight: 600;
        border: none; background: none; padding: 0;
        margin-bottom: .75rem; transition: color .2s;
    }
    .cl-toggle:hover { text-decoration: underline; }
    .cl-toggle .arrow { display: inline-block; transition: transform .2s; }
    .cl-toggle.open .arrow { transform: rotate(90deg); }

    /* detail panel */
    .cl-details {
        display: none; padding: 1rem 0 0;
        border-top: 1px solid var(--border);
    }
    .cl-details.open { display: block; }

    /* category cards */
    .cl-cat {
        background: var(--bg-card); border: 1px solid var(--border);
        border-radius: 10px; padding: 1rem 1.25rem; margin-bottom: 1rem;
        border-left: 3px solid var(--border);
    }
    .cl-cat h4 { font-size: .95rem; margin: 0 0 .5rem; color: var(--text); }
    .cl-cat h5 {
        font-size: .85rem; margin: .75rem 0 .35rem; color: var(--text-muted);
    }
    .cat-badge { font-size: .8rem; margin-right: .25rem; }
    .cl-cat ul {
        list-style: disc; padding-left: 1.25rem; margin: 0;
    }
    .cl-cat li {
        margin-bottom: .3rem; font-size: .85rem; line-height: 1.55;
    }
    code.hl {
        background: var(--code-bg, rgba(0,184,255,.12));
        padding: 1px 5px; border-radius: 4px; font-size: .82em;
    }

    /* category border colors (Phase 37.3 - brand-aligned) */
    .cat-critical { border-left-color: #FF4D6D; }
    .cat-memory   { border-left-color: #FFA94D; }
    .cat-feature  { border-left-color: #7C5CFF; }
    .cat-fix      { border-left-color: #00B8FF; }
    .cat-changed  { border-left-color: #25D6FF; }
    .cat-low      { border-left-color: #2CEAA3; }
    .cat-tech     { border-left-color: #8FA3B8; }
    .cat-medium   { border-left-color: #FFA94D; }

    /* responsive */
    @media (max-width: 600px) {
        .cl-timeline::before { left: 14px; }
        .cl-version { padding-left: 38px; }
        .cl-dot { left: 6px; width: 16px; height: 16px; }
        .cl-version-head h2 { font-size: 1.15rem; }
    }
</style>
</head>
<body>
"""

NAV = """\
<nav class="site-nav" role="navigation" aria-label="Main navigation">
<div class="container">
    <a href="index.html" class="nav-brand">
        <span class="brand-icon">&#x1F3B5;</span>
        <span>MidiEditor AI</span>
    </a>
    <button class="nav-hamburger" aria-label="Toggle menu" aria-expanded="false">&#x2630;</button>
    <ul class="nav-links">
        <li><a href="index.html#features">Features</a></li>
        <li><a href="index.html#showcase">Showcase</a></li>
        <li class="nav-dropdown">
            <span class="nav-dropdown-trigger">Docs &#x25BE;</span>
            <div class="nav-dropdown-menu">
                <a href="docs-index.html">Manual Overview</a>
                <a href="midipilot.html">MidiPilot AI</a>
                <a href="prompt-examples.html">Prompt Examples</a>
                <a href="ffxiv-channel-fixer.html">FFXIV Channel Fixer</a>
                <a href="guitar-pro.html">Guitar Pro Import</a>
                <a href="themes.html">Themes &amp; Appearance</a>
                <a href="soundfont.html">SoundFonts &amp; FluidSynth</a>
                <a href="setup.html">Setup &amp; Installation</a>
            </div>
        </li>
        <li><a href="changelog.html" class="active">Changelog</a></li>
        <li><a href="https://github.com/happytunesai/MidiEditor_AI" target="_blank" rel="noopener noreferrer">GitHub &#x2197;</a></li>
        <li><a href="download.html" class="nav-cta">&#x2B07; Download</a></li>
    </ul>
</div>
</nav>
"""

SKIP_LINK = """\
<a href="#main-content" class="skip-link">Skip to main content</a>
"""

HERO = """\
<main id="main-content">

<section class="cl-hero">
    <div class="container">
        <h1>&#x1F4DC; Changelog</h1>
        <p>Full release history - every version, feature, and bug fix.</p>
    </div>
</section>
"""

SEARCH = """\
<div class="cl-search">
    <input type="search" id="cl-filter" placeholder="Search changelog&hellip; (version, keyword, bug ID)" autocomplete="off">
</div>
<div class="cl-no-results" id="cl-no-results">No matching releases found.</div>
"""

FOOTER = """\
</main>

<footer class="site-footer">
    <div class="footer-links">
        <a href="https://github.com/happytunesai/MidiEditor_AI" target="_blank" rel="noopener noreferrer">GitHub Repo</a>
        <a href="https://github.com/happytunesai" target="_blank" rel="noopener noreferrer">All Projects</a>
        <a href="docs-index.html">Documentation</a>
        <a href="changelog.html">Changelog</a>
        <a href="https://github.com/happytunesai/MidiEditor_AI/issues" target="_blank" rel="noopener noreferrer">Issues</a>
        <a href="midipilot.html">MidiPilot</a>
        <a href="guitar-pro.html">Guitar Pro</a>
        <a href="https://www.happytunes.de" target="_blank" rel="noopener noreferrer">Happy Tunes &#x1F3B6;</a>
    </div>
    <p class="footer-copy">
        MidiEditor AI &copy; 2026 &middot;
        <a href="https://github.com/happytunesai/MidiEditor_AI/blob/main/LICENSE">License</a>
        &middot; Made with &#x266A; + &#x1F916;
        &middot; A <a href="https://www.happytunes.de" target="_blank" rel="noopener noreferrer">Happy Tunes</a> project
    </p>
</footer>
"""

SCRIPT = """\
<button class="scroll-top" aria-label="Scroll to top">&#x2191;</button>
<script src="site.js"></script>
<script>
/* Toggle details */
document.querySelectorAll('.cl-toggle').forEach(btn => {
    btn.addEventListener('click', () => {
        const panel = btn.nextElementSibling;
        const open = panel.classList.toggle('open');
        btn.classList.toggle('open', open);
        btn.querySelector('.arrow').textContent = open ? '▶' : '▶';
    });
});
/* Search filter */
const input = document.getElementById('cl-filter');
const versions = document.querySelectorAll('.cl-version');
const noResults = document.getElementById('cl-no-results');
input.addEventListener('input', () => {
    const q = input.value.toLowerCase().trim();
    let visible = 0;
    versions.forEach(v => {
        const text = v.textContent.toLowerCase();
        const show = !q || text.includes(q);
        v.style.display = show ? '' : 'none';
        if (show) visible++;
    });
    noResults.style.display = visible === 0 ? 'block' : 'none';
});
</script>
"""

HTML_TAIL = """\
</body>
</html>
"""


# ── build ──────────────────────────────────────────────────────────────

def build_version_html(
    idx: int, version: str, date: str, title: str,
    blockquote: str, summary_lines: list[str], details_html: str,
) -> str:
    latest_cls = " latest" if idx == 0 else ""
    out = [f'<article class="cl-version{latest_cls}" data-version="{html.escape(version)}">']
    out.append('<div class="cl-dot"></div>')

    # header
    out.append('<div class="cl-version-head">')
    gh_url = f"https://github.com/happytunesai/MidiEditor_AI/releases/tag/v{html.escape(version)}"
    out.append(f'<h2><a href="{gh_url}" target="_blank" rel="noopener noreferrer">v{html.escape(version)}</a></h2>')
    out.append(f'<span class="cl-date">{html.escape(date)}</span>')
    if idx == 0:
        out.append('<span class="cl-badge-latest">Latest</span>')
    out.append("</div>")

    if title:
        out.append(f'<div class="cl-title">{md_inline(title)}</div>')
    if blockquote:
        out.append(f'<div class="cl-blockquote">{md_inline(blockquote)}</div>')

    # summary bullets
    if summary_lines:
        out.append('<ul class="cl-summary">')
        for bullet in summary_lines:
            out.append(f"<li>{md_inline(bullet)}</li>")
        out.append("</ul>")

    # expandable details
    if details_html:
        out.append(
            '<button class="cl-toggle" type="button">'
            '<span class="arrow">▶</span> Show full changelog'
            "</button>"
        )
        out.append(f'<div class="cl-details">{details_html}</div>')

    out.append("</article>")
    return "\n".join(out)


def main():
    input_path = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_INPUT
    output_path = Path(sys.argv[2]) if len(sys.argv) > 2 else DEFAULT_OUTPUT

    if not input_path.exists():
        print(f"Error: {input_path} not found", file=sys.stderr)
        sys.exit(1)

    md_text = input_path.read_text(encoding="utf-8")
    versions = list(parse_changelog(md_text))

    print(f"Parsed {len(versions)} versions from {input_path.name}")

    # Extract latest version info
    latest_version, latest_date = versions[0][0], versions[0][1]
    print(f"Latest version: v{latest_version} ({latest_date})")

    # ── 1. Build changelog.html ──
    parts = [HTML_HEAD, NAV, SKIP_LINK, HERO, SEARCH, '<div class="cl-timeline">']

    for i, (version, date, title, blockquote, summary, details) in enumerate(versions):
        parts.append(
            build_version_html(i, version, date, title, blockquote, summary, details)
        )

    parts.append("</div>")  # cl-timeline
    parts.append(FOOTER)
    parts.append(SCRIPT)
    parts.append(HTML_TAIL)

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text("\n".join(parts), encoding="utf-8")
    print(f"Generated {output_path} ({output_path.stat().st_size:,} bytes)")

    # ── 2. Update version references in site pages ──
    manual_dir = ROOT / "manual"
    update_count = 0

    # Generic version pattern: matches x.y.z version strings
    # We replace any old version with the latest one in known contexts
    for page in ("index.html", "download.html", "docs-index.html"):
        filepath = manual_dir / page
        if not filepath.exists():
            continue
        content = filepath.read_text(encoding="utf-8")
        original = content

        # Replace version numbers in known patterns:
        # "softwareVersion": "x.y.z"   (JSON-LD)
        content = re.sub(
            r'("softwareVersion":\s*")\d+\.\d+\.\d+(")',
            rf"\g<1>{latest_version}\2",
            content,
        )
        # vX.Y.Z in display text (badges, buttons, headings)
        content = re.sub(
            r"(v)\d+\.\d+\.\d+",
            rf"\g<1>{latest_version}",
            content,
        )
        # GitHub releases/download/vX.Y.Z/MidiEditorAI-vX.Y.Z-win64.zip
        content = re.sub(
            r"(releases/download/v)\d+\.\d+\.\d+(/"
            r"MidiEditorAI-v)\d+\.\d+\.\d+(-win64\.zip)",
            rf"\g<1>{latest_version}\g<2>{latest_version}\3",
            content,
        )
        # GitHub releases/tag/vX.Y.Z
        content = re.sub(
            r"(releases/tag/v)\d+\.\d+\.\d+",
            rf"\g<1>{latest_version}",
            content,
        )
        # MidiEditorAI-vX.Y.Z-win64.zip (checksum labels etc.)
        content = re.sub(
            r"(MidiEditorAI-v)\d+\.\d+\.\d+(-win64\.zip)",
            rf"\g<1>{latest_version}\2",
            content,
        )
        # Date in download hero: "April 9, 2026" style - update from changelog
        # Parse date: 2026-04-09 -> April 9, 2026
        date_nice = _format_date(latest_date)
        if date_nice:
            content = re.sub(
                r"(Current Release:.*?(?:&mdash;|-)\s*).+?(</p>)",
                rf"\g<1>{date_nice}\2",
                content,
            )

        if content != original:
            filepath.write_text(content, encoding="utf-8")
            update_count += 1
            print(f"Updated version refs in {page}")

    if update_count == 0:
        print("Version references already up to date.")


def _format_date(iso_date: str) -> str:
    """Convert 2026-04-09 to 'April 9, 2026'."""
    months = [
        "", "January", "February", "March", "April", "May", "June",
        "July", "August", "September", "October", "November", "December",
    ]
    try:
        parts = iso_date.split("-")
        y, m, d = int(parts[0]), int(parts[1]), int(parts[2])
        return f"{months[m]} {d}, {y}"
    except (IndexError, ValueError):
        return ""


if __name__ == "__main__":
    main()
