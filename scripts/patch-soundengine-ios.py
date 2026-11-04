#!/usr/bin/env python3
"""Let the Windows64 miniaudio SoundEngine build for iOS.

iOS compiles Common/Audio/SoundEngine.cpp through Game/MCLESound.cpp. The
Windows64 code is already miniaudio + stb_vorbis, so this only:
- uses float AUDIO_INFO and MiniAudioSound on iOS too
- drops the Consoles_App / Windows64_App / lce_filesystem includes (the
  app stub and MCLESound.cpp cover them)
- leaves the miniaudio and stb_vorbis implementations to MCLE_iOS_Audio.mm,
  which compiles the same miniaudio version
- adds iOS sound/music paths, filled with the Documents path at startup
- compiles out the DLC mash-up pack audio branch, there are no DLC packs on iOS
- names ScheduledSound through ConsoleSoundEngine, everywhere but
  MCLESound.cpp SoundEngine is the stub

Idempotent.
"""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
AUDIO = REPO / "upstream" / "Minecraft.Client" / "Common" / "Audio"
CPP = AUDIO / "SoundEngine.cpp"
HDR = AUDIO / "SoundEngine.h"
CONSOLE_CPP = AUDIO / "Consoles_SoundEngine.cpp"
MARK = "MCLE_iOS_SOUNDENGINE"


def patch(path, edits):
    src = path.read_text(encoding="utf-8", errors="replace")
    if MARK in src:
        print(f"already patched: {path}")
        return
    nl = "\r\n" if "\r\n" in src else "\n"
    for old, new in edits:
        old, new = old.replace("\n", nl), new.replace("\n", nl)
        if src.count(old) != 1:
            sys.exit(f"{path.name}: anchor not found once: {old[:60]!r}")
        src = src.replace(old, new)
    path.write_text(src, encoding="utf-8", errors="replace", newline="")
    print(f"patched: {path}")


patch(HDR, [
    ("\t#ifndef _WINDOWS64\n\tF32 x,y,z,volume,pitch;",
     "\t#if !defined _WINDOWS64 && !defined __APPLE_IOS__ // " + MARK + "\n\tF32 x,y,z,volume,pitch;"),
    ("#ifdef _WINDOWS64\nstruct MiniAudioSound",
     "#if defined _WINDOWS64 || defined __APPLE_IOS__\nstruct MiniAudioSound"),
])

patch(CPP, [
    ('#include "../Consoles_App.h"\n',
     '#ifndef __APPLE_IOS__ // ' + MARK + '\n#include "../Consoles_App.h"\n#endif\n'),
    ('#include "../../Minecraft.Client/Windows64/Windows64_App.h"\n',
     '#ifndef __APPLE_IOS__\n#include "../../Minecraft.Client/Windows64/Windows64_App.h"\n#endif\n'),
    ('#include "stb_vorbis.h"\n',
     '#ifdef __APPLE_IOS__\n#define STB_VORBIS_HEADER_ONLY\n#endif\n#include "stb_vorbis.h"\n'),
    ('#define MINIAUDIO_IMPLEMENTATION\n',
     '#ifndef __APPLE_IOS__\n#define MINIAUDIO_IMPLEMENTATION\n#endif\n'),
    ('#include <lce_filesystem\\lce_filesystem.h>\n',
     '#ifndef __APPLE_IOS__\n#include <lce_filesystem\\lce_filesystem.h>\n#endif\n'),
    ('#ifdef _WINDOWS64\nchar SoundEngine::m_szSoundPath[]={"Windows64Media\\\\Sound\\\\"};',
     '#if defined __APPLE_IOS__\n'
     '// Absolute Documents paths, filled in when the engine is made\n'
     'char SoundEngine::m_szSoundPath[256]={"Windows64Media/Sound/"};\n'
     'char SoundEngine::m_szMusicPath[256]={"music/"};\n'
     'char SoundEngine::m_szRedistName[]={"redist64"};\n'
     '#elif defined _WINDOWS64\nchar SoundEngine::m_szSoundPath[]={"Windows64Media\\\\Sound\\\\"};'),
    ('    sprintf_s(basePath, "Windows64Media/Sound/%s", (char*)szSoundName);',
     '    sprintf_s(basePath, "%s%s", m_szSoundPath, (char*)szSoundName);'),
    ('    sprintf_s(basePath, "Windows64Media/Sound/%s/%s", soundDir, ConvertSoundPathToName(name));',
     '    sprintf_s(basePath, "%s%s/%s", m_szSoundPath, soundDir, ConvertSoundPathToName(name));'),
    # The DLC types it uses aren't all declared here, so compile it out
    ('\t\t\tif(Minecraft::GetInstance()->skins->getSelected()->hasAudio())\n',
     '#ifndef __APPLE_IOS__\n'
     '\t\t\tif(Minecraft::GetInstance()->skins->getSelected()->hasAudio())\n'),
    ('\t\t\t}\n\t\t\telse\n\t\t\t{\t\n\t\t\t\t// 4J-PB - if this is a PS3 disc patch',
     '\t\t\t}\n\t\t\telse\n#endif\n\t\t\t{\t\n\t\t\t\t// 4J-PB - if this is a PS3 disc patch'),
])

# ScheduledSound is ConsoleSoundEngine's, the derived name only resolves
# when the real SoundEngine is in scope
src = CONSOLE_CPP.read_text(encoding="utf-8", errors="replace")
if "new SoundEngine::ScheduledSound" in src or "	SoundEngine::ScheduledSound" in src:
    src = src.replace("SoundEngine::ScheduledSound", "ConsoleSoundEngine::ScheduledSound")
    src = src.replace("ConsoleConsoleSoundEngine::", "ConsoleSoundEngine::")
    CONSOLE_CPP.write_text(src, encoding="utf-8", errors="replace", newline="")
    print(f"patched: {CONSOLE_CPP}")
else:
    print(f"already patched: {CONSOLE_CPP}")
