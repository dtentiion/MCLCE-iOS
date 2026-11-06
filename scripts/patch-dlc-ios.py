#!/usr/bin/env python3
"""Let the DLC classes build and read .pck files on iOS.

DLCManager.cpp:
- The Iggy UI header it includes for the corrupt DLC message is swapped for
  iOS_DLC.h, which has a stand-in for that one call.
- A .pck's strings are UTF-16 laid out for Windows' 2 byte WCHAR, and
  wchar_t is 4 bytes here. The file is rewritten with this build's structs
  (mcle_dlc_widen_pck) once it's read, before it's processed.
- A path on a mounted drive ("TPACK:\\Data\\x16Data.pck") is read from the
  pack's folder, the way the Windows64 build looks it up in the storage.

DLCSkinFile.cpp:
- A skin's BOX and ANIM values are read with GCC's swscanf, as on PS3. iOS
  maps swscanf_s to swscanf, which would take the buffer size for a pointer.

DLCPack.cpp:
- A mash-up pack's game rules header (its world template) is skipped, the
  mash-up worlds aren't offered on iOS yet.

Idempotent.
"""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
DLC = REPO / "upstream" / "Minecraft.Client" / "Common" / "DLC"


def patch(path, marker, edits):
    if not path.exists():
        sys.exit(f"missing: {path}")
    src = path.read_text(encoding="utf-8", errors="replace")
    if marker in src:
        print(f"already patched: {path}")
        return
    nl = "\r\n" if "\r\n" in src else "\n"
    for old, new in edits:
        old = old.replace("\n", nl)
        if src.count(old) != 1:
            sys.exit(f"anchor not found once in {path}: {old.strip()}")
        src = src.replace(old, nl.join(new))
    path.write_text(src, encoding="utf-8", newline="")
    print(f"patched: {path}")


patch(DLC / "DLCManager.cpp", "mcle_dlc_widen_pck", [
    ('#include "Common/UI/UI.h"', [
        "#ifdef __APPLE_IOS__",
        '#include "iOS_DLC.h"',
        "#else",
        '#include "Common/UI/UI.h"',
        "#endif",
    ]),
    ("\treturn processDLCDataFile(dwFilesProcessed, pbData, bytesRead, pack);", [
        "#ifdef __APPLE_IOS__",
        "\tpbData = mcle_dlc_widen_pck(pbData, bytesRead);",
        "#endif",
        "\treturn processDLCDataFile(dwFilesProcessed, pbData, bytesRead, pack);",
    ]),
    ("\telse if (fromArchive) return false;\n\n#ifdef _WINDOWS64\n", [
        "\telse if (fromArchive) return false;",
        "",
        "#if defined(_WINDOWS64) || defined(__APPLE_IOS__)",
        "",
    ]),
    ("\twstring wPath = convStringToWstring(path);\n\n#ifdef _WINDOWS64\n", [
        "\twstring wPath = convStringToWstring(path);",
        "",
        "#if defined(_WINDOWS64) || defined(__APPLE_IOS__)",
        "",
    ]),
])

PS3_SCANF = "#ifdef __PS3__\n{tabs}// 4J Stu - The Xbox version used swscanf_s which isn't available in GCC."
patch(DLC / "DLCSkinFile.cpp", "__APPLE_IOS__", [
    (PS3_SCANF.format(tabs="\t\t\t"), [
        "#if defined(__PS3__) || defined(__APPLE_IOS__)",
        "\t\t\t// 4J Stu - The Xbox version used swscanf_s which isn't available in GCC.",
    ]),
    (PS3_SCANF.format(tabs="\t\t"), [
        "#if defined(__PS3__) || defined(__APPLE_IOS__)",
        "\t\t// 4J Stu - The Xbox version used swscanf_s which isn't available in GCC.",
    ]),
])

patch(DLC / "DLCPack.cpp", "__APPLE_IOS__", [
    ('#include "DLCGameRulesHeader.h"', [
        "#ifndef __APPLE_IOS__",
        '#include "DLCGameRulesHeader.h"',
        "#endif",
    ]),
    ("\tcase DLCManager::e_DLCType_GameRulesHeader:\n\t\tnewFile = new DLCGameRulesHeader(path);\n\t\tbreak;", [
        "#ifndef __APPLE_IOS__",
        "\tcase DLCManager::e_DLCType_GameRulesHeader:",
        "\t\tnewFile = new DLCGameRulesHeader(path);",
        "\t\tbreak;",
        "#endif",
    ]),
])
