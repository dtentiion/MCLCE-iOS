#!/usr/bin/env python3
"""Let DLCManager.cpp build and read .pck files on iOS.

- The Iggy UI header it includes for the corrupt DLC message is swapped for
  iOS_DLC.h, which has a stand-in for that one call.
- A .pck's strings are UTF-16 laid out for Windows' 2 byte WCHAR, and
  wchar_t is 4 bytes here. The file is rewritten with this build's structs
  (mcle_dlc_widen_pck) once it's read, before it's processed.
- A texture pack found in a .pck isn't handed to TexturePackRepository,
  which isn't built. It's still added to its parent pack.

Idempotent.
"""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SRC = REPO / "upstream" / "Minecraft.Client" / "Common" / "DLC" / "DLCManager.cpp"

if not SRC.exists():
    sys.exit(f"missing: {SRC}")
src = SRC.read_text(encoding="utf-8", errors="replace")
if "mcle_dlc_widen_pck" in src:
    print(f"already patched: {SRC}")
    sys.exit(0)

nl = "\r\n" if "\r\n" in src else "\n"

EDITS = [
    (
        '#include "Common/UI/UI.h"',
        nl.join([
            "#ifdef __APPLE_IOS__",
            '#include "iOS_DLC.h"',
            "#else",
            '#include "Common/UI/UI.h"',
            "#endif",
        ]),
    ),
    (
        "\treturn processDLCDataFile(dwFilesProcessed, pbData, bytesRead, pack);",
        nl.join([
            "#ifdef __APPLE_IOS__",
            "\tpbData = mcle_dlc_widen_pck(pbData, bytesRead);",
            "#endif",
            "\treturn processDLCDataFile(dwFilesProcessed, pbData, bytesRead, pack);",
        ]),
    ),
    (
        "\t\t\t\t\tMinecraft::GetInstance()->skins->addTexturePackFromDLC(dlcTexturePack, dlcTexturePack->GetPackId() );",
        nl.join([
            "#ifndef __APPLE_IOS__",
            "\t\t\t\t\tMinecraft::GetInstance()->skins->addTexturePackFromDLC(dlcTexturePack, dlcTexturePack->GetPackId() );",
            "#endif",
        ]),
    ),
]

for old, new in EDITS:
    if src.count(old) != 1:
        sys.exit(f"anchor not found once in {SRC}: {old.strip()}")
    src = src.replace(old, new)

SRC.write_text(src, encoding="utf-8", newline="")
print(f"patched: {SRC}")
