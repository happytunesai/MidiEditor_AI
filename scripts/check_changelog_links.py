#!/usr/bin/env python3
"""Validate the manual links inside CHANGELOG.md.

CHANGELOG feature bullets may link to the manual passage that explains the
feature, as absolute site URLs (https://midieditor-ai.de/<page>.html#<anchor>)
so they work both on GitHub and on the generated changelog.html. This check
verifies every such link against the real manual sources: the page file must
exist in manual/, and the anchor must be an id= in that page. A dead link
fails the script (exit 1) - run it from the release checklist, same pattern
as dedash.py.
"""
import os
import re
import sys

sys.stdout.reconfigure(encoding="utf-8")

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CHANGELOG = os.path.join(ROOT, "CHANGELOG.md")
MANUAL = os.path.join(ROOT, "manual")
SITE = "https://midieditor-ai.de/"

link_re = re.compile(
    re.escape(SITE) + r"([A-Za-z0-9._-]+\.html)(?:#([A-Za-z0-9._-]+))?"
)
id_re = re.compile(r'id="([^"]+)"')

with open(CHANGELOG, encoding="utf-8") as f:
    text = f.read()

ids_cache = {}
def page_ids(page):
    if page not in ids_cache:
        path = os.path.join(MANUAL, page)
        if not os.path.isfile(path):
            ids_cache[page] = None
        else:
            with open(path, encoding="utf-8") as f:
                ids_cache[page] = set(id_re.findall(f.read()))
    return ids_cache[page]

checked = 0
dead = []
for m in link_re.finditer(text):
    page, anchor = m.group(1), m.group(2)
    checked += 1
    ids = page_ids(page)
    if ids is None:
        dead.append(f"{page}: page not found in manual/")
    elif anchor and anchor not in ids:
        dead.append(f"{page}#{anchor}: no such id in the page")

if dead:
    print(f"checked {checked} manual link(s) - DEAD LINKS:")
    for d in sorted(set(dead)):
        print(f"  {d}")
    sys.exit(1)
print(f"checked {checked} manual link(s) in CHANGELOG.md - all resolve")
