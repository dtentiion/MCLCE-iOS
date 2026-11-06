#!/usr/bin/env python3
"""Pull ClientConnection::handleChat out of upstream.

The port's player is the server's own and has no client connection, so
chat packets sent to it (bed messages, death messages, joins) are handed
to a copy of the client's handleChat instead. ClientConnection.cpp
doesn't build on iOS; the body goes into
upstream/Minecraft.Client/iOS_HandleChat.inc and Game/MCLEChat.cpp
compiles it as a member of its own small class. The CMinecraftApp text
helpers it calls come from Game/MCLEHowToPlay.cpp's copies.

Idempotent, rewrites the .inc every run.
"""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SRC = REPO / "upstream" / "Minecraft.Client" / "ClientConnection.cpp"
OUT = REPO / "upstream" / "Minecraft.Client" / "iOS_HandleChat.inc"

SIG = "void ClientConnection::handleChat(shared_ptr<ChatPacket> packet)"
CALLS = {
    "app.EscapeHTMLString(": "mcle_escape_html_string(",
    "app.FormatChatMessage(": "mcle_format_chat_message(",
    "app.getEntityName(": "mcle_entity_name(",
    "app.SetGlobalXuiAction(": "mcle_set_global_xui_action(",
}


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
if src.count(SIG) != 1:
    sys.exit(f"{SIG!r} not found once in {SRC}")
at = src.index(SIG)
brace = src.index("{", at + len(SIG))
body = src[brace:body_end(src, brace)]
for old, new in CALLS.items():
    if old not in body:
        sys.exit(f"{old!r} not in handleChat")
    body = body.replace(old, new)
OUT.write_text(SIG.replace("ClientConnection::", "MCLEChatConnection::") + "\n" + body + "\n", encoding="utf-8")
print(f"wrote {OUT}")
