#!/usr/bin/env python3
"""Let the texture pack classes build on iOS.

TexturePackRepository.cpp, AbstractTexturePack.cpp, DLCTexturePack.cpp:
- The Iggy UI header they include for the skin reload calls is swapped for
  iOS_TexturePacks.h, which has those calls.

File.cpp:
- A path on a mounted DLC drive ("TPACK:\\Data\\x16Data.pck") becomes the
  pack's folder, as Windows64 does through StorageManager's mounts. Only
  paths with a drive name go to the storage, the rest are left alone.

Idempotent.
"""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
CLIENT = REPO / "upstream" / "Minecraft.Client"
WORLD = REPO / "upstream" / "Minecraft.World"


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


UI_HEADER = ('#include "Common/UI/UI.h"', [
    "#ifdef __APPLE_IOS__",
    '#include "iOS_TexturePacks.h"',
    "#else",
    '#include "Common/UI/UI.h"',
    "#endif",
])

for name in ("TexturePackRepository.cpp", "AbstractTexturePack.cpp", "DLCTexturePack.cpp"):
    patch(CLIENT / name, "iOS_TexturePacks.h", [UI_HEADER])

patch(WORLD / "File.cpp", "__APPLE_IOS__", [
    ("\tm_abstractPathName = convStringToWstring(finalPath);\n#elif defined(_DURANGO)", [
        "\tm_abstractPathName = convStringToWstring(finalPath);",
        "#elif defined(__APPLE_IOS__)",
        "\tif(m_abstractPathName.find(L':') != wstring::npos)",
        "\t{",
        "\t\tstring path(m_abstractPathName.begin(), m_abstractPathName.end());",
        "\t\tstring finalPath = StorageManager.GetMountedPath(path);",
        "\t\tif(finalPath.size() != 0) m_abstractPathName = convStringToWstring(finalPath);",
        "\t}",
        "#elif defined(_DURANGO)",
    ]),
])
