#!/usr/bin/env python3
"""Pull CMinecraftApp's HTML text formatting out of upstream.

FormatHTMLString turns the {*...*} tags in How to Play and tutorial text
into HTML: colours from the skin's colour table, line breaks, and button
glyph <img> tags from the control scheme (GetActionReplacement,
GetVKReplacement, GetIconReplacement). Consoles_App.cpp doesn't build on
iOS, so those four bodies are copied into
upstream/Minecraft.Client/iOS_FormatHTML.inc and Game/MCLEAppText.cpp
compiles them as members of its own small class.

Idempotent, rewrites the .inc every run.
"""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SRC = REPO / "upstream" / "Minecraft.Client" / "Common" / "Consoles_App.cpp"
OUT = REPO / "upstream" / "Minecraft.Client" / "iOS_FormatHTML.inc"

FUNCS = [
    "wstring CMinecraftApp::FormatHTMLString(int iPad, const wstring &desc, int shadowColour /*= 0xFFFFFFFF*/)",
    "wstring CMinecraftApp::GetActionReplacement(int iPad, unsigned char ucAction)",
    "wstring CMinecraftApp::GetVKReplacement(unsigned int uiVKey)",
    "wstring CMinecraftApp::GetIconReplacement(unsigned int uiIcon)",
]


def body_end(src, start):
    """Index just past the brace that closes the one at src[start]."""
    depth = 0
    i = start
    n = len(src)
    while i < n:
        c = src[i]
        if c == '/' and src.startswith('//', i):
            i = src.index('\n', i)
            continue
        if c == '/' and src.startswith('/*', i):
            i = src.index('*/', i) + 2
            continue
        if c == '"' or c == "'":
            q = c
            i += 1
            while src[i] != q:
                i += 2 if src[i] == '\\' else 1
            i += 1
            continue
        if c == '{':
            depth += 1
        elif c == '}':
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1
    sys.exit("unbalanced braces")


src = SRC.read_text(encoding="utf-8", errors="replace").replace("\r\n", "\n")
out = []
for sig in FUNCS:
    if src.count(sig) != 1:
        sys.exit(f"{sig!r} not found once in {SRC}")
    at = src.index(sig)
    brace = src.index("{", at + len(sig))
    end = body_end(src, brace)
    # Same function, member of the port's MCLEAppText instead
    out.append(sig.replace("CMinecraftApp::", "MCLEAppText::").replace(" /*= 0xFFFFFFFF*/", "") + "\n" + src[brace:end] + "\n")
OUT.write_text("\n".join(out), encoding="utf-8")
print(f"wrote {OUT} ({len(out)} functions)")
