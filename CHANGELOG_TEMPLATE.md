# Changelog Entry Template

Use this template for **every** new entry in `CHANGELOG.md`. The format mirrors
`[1.3.2.2]` and `[1.3.2]` and is the canonical convention for this project.

## Structure

```markdown
## [VERSION] - YYYY-MM-DD — Title (Hotfix / Feature / Bugfix Release / etc.)

### Summary
* **Bold one-line problem or feature statement** — short prose explanation, with a BUG-ID or PHASE-ID in parentheses where relevant (BUG-ID-001)
* **...** — ...
[5–10 bullets, each starts with a bold problem/feature statement followed by an em-dash and a one-sentence description]

<details>
<summary>Full Changelog — Title</summary>

### Bug Fixes
* **Fixed [problem] (BUG-ID)** — Detailed paragraph explaining: what was wrong, root cause (cite file/function/line where useful), why it happened, what changed, and a "Regression since vX.Y.Z" note when applicable.
* **Fixed ...** — ...

### New Features
* **Feature name** — Detailed description of what was added, why, and how it works under the hood.

### [Optional sections, use only when relevant]
### Audit
### Documentation
### Test Harness
### Technical Notes
### Website / Manual
### Files Added/Modified

### Files Modified
* `path/to/file.ext` — short description of the change
* ...

</details>

---
```

## Rules

1. **Always include `### Summary`** — bullet list, every bullet starts with a bold
   problem/feature statement, followed by an em-dash (`—`) and a single sentence.
   Include the BUG-ID / PHASE-ID in parentheses when one exists.
2. **Always wrap details in `<details><summary>…</summary>` … `</details>`**
   so the changelog page stays readable when a release lands.
3. **Detailed `### Bug Fixes` paragraphs** — explain what, why, root cause, fix,
   and regression note. Cite the file/function. One bullet per fix.
4. **`### Files Modified` is mandatory** at the bottom of the `<details>` block —
   one bullet per file with a one-line description.
5. **End every entry with `---`** as a horizontal rule separator.
6. **BUG-ID convention** — short uppercase tag + numeric suffix (`PASTE-001`,
   `AI-008-FIX`, `V131-P2-03`, `FFXIV-RENAME-001`). Re-use existing prefixes when
   the fix is in the same area.

## Reference entries

Use these as examples when in doubt:

* `[1.3.2.2] - 2026-04-17 — Hotfix: Paste Selection + v1.3.1 Audit Fallout` —
  the gold-standard hotfix format (Summary + `<details>` + Bug Fixes + Audit + Files Modified).
* `[1.3.2] - 2026-04-15 — MCP Server, Documentation System & Prompt Architecture v3` —
  the gold-standard feature-release format (Summary + multi-section `<details>`).
* `[1.2.1] - 2026-04-09 — Stability & Bugfix Release` —
  large bug-sweep format with grouped categories inside the `<details>` block.
