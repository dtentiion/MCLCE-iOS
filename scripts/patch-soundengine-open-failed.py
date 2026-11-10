#!/usr/bin/env python3
"""A music track that can't be opened is passed over.

SoundEngine::playMusicUpdate goes back to Idle when a stream fails to open,
with the same track still picked and no delay, so the next tick starts the
open thread on it again, and the one after. A track the engine can't decode
(the Halo pack's are Ogg Opus, miniaudio here reads Vorbis) was tried some
thirty times a second for as long as its pack was on.

On iOS a failed open ends like a track that has played out: Completed picks
another after its usual wait.

Idempotent.
"""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
CPP = REPO / "upstream" / "Minecraft.Client" / "Common" / "Audio" / "SoundEngine.cpp"
MARK = "MCLE_iOS_STREAM_OPEN_FAILED"

OLD = ("\t\t\t\tm_StreamState = eMusicStreamState_Idle;\n"
       "\t\t\t\tbreak;\n"
       "\t\t\t}\n"
       "\t\t\t\n"
       "\t\t\tif (m_StreamingAudioInfo.bIs3D)\n")
NEW = ("#ifdef __APPLE_IOS__\n"
       "\t\t\t\t// MCLE_iOS_STREAM_OPEN_FAILED: on to another track, after the wait\n"
       "\t\t\t\tm_StreamState = eMusicStreamState_Completed;\n"
       "#else\n"
       "\t\t\t\tm_StreamState = eMusicStreamState_Idle;\n"
       "#endif\n"
       "\t\t\t\tbreak;\n"
       "\t\t\t}\n"
       "\t\t\t\n"
       "\t\t\tif (m_StreamingAudioInfo.bIs3D)\n")

if not CPP.exists():
    sys.exit(f"missing: {CPP}")
src = CPP.read_text(encoding="utf-8", errors="replace")
if MARK in src:
    print(f"already patched: {CPP}")
    sys.exit(0)
nl = "\r\n" if "\r\n" in src else "\n"
old = OLD.replace("\n", nl)
if src.count(old) != 1:
    sys.exit(f"anchor not found once in {CPP}")
src = src.replace(old, NEW.replace("\n", nl))
CPP.write_text(src, encoding="utf-8", errors="replace", newline="")
print(f"patched: {CPP}")
