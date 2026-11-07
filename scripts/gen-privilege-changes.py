#!/usr/bin/env python3
"""Pull ClientConnection::displayPrivilegeChanges out of upstream.

When the LoginPacket or a PlayerInfoPacket gives the client player new
privileges, the client puts a chat line up for each one that changed. The
port's player is the server's own and has no client connection, so those
packets go to Game/MCLEPlayerInfo.cpp instead. ClientConnection.cpp doesn't
build on iOS; the body goes into
upstream/Minecraft.Client/iOS_PrivilegeChanges.inc and MCLEPlayerInfo.cpp
compiles it as a member of its own small class.

What changes:
- The player is the ServerPlayer, there's no MultiplayerLocalPlayer.
- Its pad is the connection's m_userIndex, ServerPlayer has no GetXboxPad.

Idempotent, rewrites the .inc every run.
"""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SRC = REPO / "upstream" / "Minecraft.Client" / "ClientConnection.cpp"
OUT = REPO / "upstream" / "Minecraft.Client" / "iOS_PrivilegeChanges.inc"

SIG = ("void ClientConnection::displayPrivilegeChanges(shared_ptr<MultiplayerLocalPlayer> player, "
       "unsigned int oldPrivileges)")
NEW_SIG = ("void MCLEPrivilegeConnection::displayPrivilegeChanges(shared_ptr<ServerPlayer> player, "
           "unsigned int oldPrivileges)")
REPLACEMENTS = [
    ("int userIndex = player->GetXboxPad();", "int userIndex = m_userIndex;", 1),
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
if src.count(SIG) != 1:
    sys.exit(f"{SIG!r} not found once in {SRC}")
at = src.index(SIG)
brace = src.index("{", at + len(SIG))
body = src[brace:body_end(src, brace)]
for old, new, count in REPLACEMENTS:
    n = body.count(old)
    if n != count:
        sys.exit(f"expected {count} of {old!r} in displayPrivilegeChanges, found {n}")
    body = body.replace(old, new)
OUT.write_text(NEW_SIG + "\n" + body + "\n", encoding="utf-8")
print(f"wrote {OUT}")
