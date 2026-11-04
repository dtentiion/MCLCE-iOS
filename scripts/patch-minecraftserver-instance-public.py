#!/usr/bin/env python3
"""Let the iOS game loop set MinecraftServer::server.

MinecraftServer::main does `server = new MinecraftServer()`. iOS makes its
server in Game/MCLEGameLoop.cpp instead, so getInstance() stayed null and
everything asking for it (fire spread, tracking checks...) crashed. The
static is private, so it's made public for iOS only.

Idempotent.
"""
import sys
from pathlib import Path

TARGET = Path(__file__).resolve().parent.parent / "upstream" / "Minecraft.Client" / "MinecraftServer.h"

src = TARGET.read_text(encoding="utf-8", errors="replace")
nl = "\r\n" if "\r\n" in src else "\n"
old = "\t//4J Added" + nl + "\tstatic MinecraftServer *server;" + nl
new = ("\t//4J Added" + nl + "#ifdef __APPLE_IOS__" + nl + "public:" + nl + "#endif" + nl +
       "\tstatic MinecraftServer *server;" + nl + "#ifdef __APPLE_IOS__" + nl + "private:" + nl + "#endif" + nl)
if new in src:
    print(f"already patched: {TARGET}")
    sys.exit(0)
if src.count(old) != 1:
    sys.exit(f"anchor not found in {TARGET}")
TARGET.write_text(src.replace(old, new, 1), encoding="utf-8", errors="replace", newline="")
print(f"patched {TARGET}")
