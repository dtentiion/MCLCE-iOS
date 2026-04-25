#!/usr/bin/env bash
# Patch upstream Minecraft.World source files to add iOS branches to
# their platform-conditional chains. Without these, the chains fall
# through to the Orbis (PS4) catch-all and pull in Sony NP / FIOS2
# headers we do not have on iOS, plus leave platform enum values
# undefined.
#
# Files touched (per-file blocks below, all idempotent):
#   - Minecraft.World/stdafx.h        (4JLibs platform-elif chain)
#   - Minecraft.World/FileHeader.h    (SAVE_FILE_PLATFORM_LOCAL gate)
#
# Run from CI before `cmake -S . -B build` so the world-probe target
# sees the patched files.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
STDAFX="$REPO_ROOT/upstream/Minecraft.World/stdafx.h"
FILEHEADER="$REPO_ROOT/upstream/Minecraft.World/FileHeader.h"

if [ ! -f "$STDAFX" ]; then
    echo "patch-upstream-stdafx: $STDAFX not found"
    exit 1
fi
if [ ! -f "$FILEHEADER" ]; then
    echo "patch-upstream-stdafx: $FILEHEADER not found"
    exit 1
fi

if grep -q '__APPLE_IOS__' "$STDAFX" && grep -q '__APPLE_IOS__' "$FILEHEADER"; then
    echo "patch-upstream-stdafx: iOS branches already present in both files, nothing to do"
    exit 0
fi

# Insert an `elif __APPLE_IOS__` branch just before the catch-all `#else`
# (which selects Orbis). The new branch points at our iOS 4JLibs stubs.
if grep -q '__APPLE_IOS__' "$STDAFX"; then
    echo "patch-upstream-stdafx: stdafx.h already patched, skipping"
else
python3 - "$STDAFX" <<'PY'
import re, sys
path = sys.argv[1]
with open(path, 'r', encoding='utf-8', errors='replace') as f:
    src = f.read()

# The block we want to extend is the 4J_* platform chain:
#     #elif defined __PSVITA__
#     #include "../Minecraft.Client/PSVita/4JLibs/inc/4J_Profile.h"
#     ...
#     #else
#     #include "../Minecraft.Client/Orbis/4JLibs/inc/4J_Profile.h"
#     ...
#     #endif
# `#elif defined __PSVITA__` appears twice in stdafx.h (once in the std-
# headers block, once here), so anchor on the more specific 4JLibs line
# instead. That uniquely identifies the right block.
needle = '#include "../Minecraft.Client/PSVita/4JLibs/inc/4J_Profile.h"'
if needle not in src:
    sys.exit("patch-upstream-stdafx: anchor `PSVita 4J_Profile.h include` not found in stdafx.h")

# Find the `#else` that follows the PSVita 4J block and insert before it.
psvita_at = src.index(needle)
else_at = src.index('#else', psvita_at)
ios_branch = (
    '#elif defined __APPLE_IOS__\n'
    '// iOS 4JLibs stubs live in the project root, not inside the upstream\n'
    '// submodule, so the relative path escapes upstream/ via `../..`.\n'
    '#include "../../Minecraft.Client/iOS/4JLibs/inc/4J_Profile.h"\n'
    '#include "../../Minecraft.Client/iOS/4JLibs/inc/4J_Render.h"\n'
    '#include "../../Minecraft.Client/iOS/4JLibs/inc/4J_Storage.h"\n'
    '#include "../../Minecraft.Client/iOS/4JLibs/inc/4J_Input.h"\n'
)
patched = src[:else_at] + ios_branch + src[else_at:]

with open(path, 'w', encoding='utf-8', newline='\n') as f:
    f.write(patched)
print(f"patch-upstream-stdafx: inserted iOS branch into {path}")
PY
fi

# FileHeader.h: enum ESavePlatform has a #if/#elif chain that picks one
# of SAVE_FILE_PLATFORM_X360 / XBONE / PS3 / PS4 / PSVITA / WIN64 as
# SAVE_FILE_PLATFORM_LOCAL based on platform macros. iOS has no branch
# so SAVE_FILE_PLATFORM_LOCAL ends up undeclared. Insert an iOS branch
# right before the closing `#endif` that maps it to PS4 (the Orbis
# value). The save-file format is the same across console; the only
# place the value matters is when reading saves that originated on
# this device, and we are unlikely to ever ship a "save from iOS"
# format distinct from PS4 since LCE's save layer is platform-tagged
# but format-shared.
if grep -q '__APPLE_IOS__' "$FILEHEADER"; then
    echo "patch-upstream-stdafx: FileHeader.h already patched, skipping"
else
python3 - "$FILEHEADER" <<'PY'
import sys
path = sys.argv[1]
with open(path, 'r', encoding='utf-8', errors='replace') as f:
    src = f.read()

needle = '#elif defined _WINDOWS64\n\tSAVE_FILE_PLATFORM_LOCAL = SAVE_FILE_PLATFORM_WIN64'
if needle not in src:
    sys.exit("patch-upstream-stdafx: anchor `_WINDOWS64 SAVE_FILE_PLATFORM_LOCAL` not found in FileHeader.h")

ios_branch = (
    '\n#elif defined __APPLE_IOS__\n'
    '\tSAVE_FILE_PLATFORM_LOCAL = SAVE_FILE_PLATFORM_PS4'
)
patched = src.replace(needle, needle + ios_branch, 1)

with open(path, 'w', encoding='utf-8', newline='\n') as f:
    f.write(patched)
print(f"patch-upstream-stdafx: inserted iOS branch into {path}")
PY
fi
