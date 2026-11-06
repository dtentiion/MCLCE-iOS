#!/usr/bin/env python3
"""ConsoleSaveFileOriginal::Flush writes the save out on iOS.

Flush compresses the save, then hands it to StorageManager only on the
platforms it lists (Windows64, the Sony consoles, Durango). Anywhere else it
falls through to just releasing the lock, so iOS saves stayed in memory and
never reached the .ms file. iOS joins the Windows64 set: the thumbnail call,
SetSaveImages + SaveSaveData and the SaveSaveDataCallback that goes with it.

Idempotent.
"""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
CPP = REPO / "upstream" / "Minecraft.World" / "ConsoleSaveFileOriginal.cpp"
HDR = REPO / "upstream" / "Minecraft.World" / "ConsoleSaveFileOriginal.h"

SONY_WIN = "defined __PS3__ || defined __ORBIS__ || defined __PSVITA__ || defined _DURANGO || defined _WINDOWS64"
THUMB = "( defined _XBOX || defined _DURANGO || defined _WINDOWS64 )"


def patch(path, pairs):
    src = path.read_text(encoding="utf-8", errors="replace")
    if f"{SONY_WIN} || defined __APPLE_IOS__" in src:
        print(f"already patched: {path}")
        return
    for old, new, count in pairs:
        n = src.count(old)
        if n != count:
            sys.exit(f"expected {count} of {old!r} in {path}, found {n}")
        src = src.replace(old, new)
    path.write_text(src, encoding="utf-8")
    print(f"patched: {path}")


for p in (CPP, HDR):
    if not p.exists():
        sys.exit(f"missing: {p}")

patch(CPP, [
    (f"#elif ({SONY_WIN})", f"#elif ({SONY_WIN} || defined __APPLE_IOS__)", 1),
    (f"#if ({SONY_WIN})", f"#if ({SONY_WIN} || defined __APPLE_IOS__)", 1),
    (f"#if {THUMB}", "#if ( defined _XBOX || defined _DURANGO || defined _WINDOWS64 || defined __APPLE_IOS__ )", 1),
])
patch(HDR, [
    (f"#if ({SONY_WIN})", f"#if ({SONY_WIN} || defined __APPLE_IOS__)", 1),
])
