#!/usr/bin/env bash
# Patch upstream/Minecraft.World/stdafx.h to add an iOS branch in the
# platform-#elif chain that selects the 4J_* header set. Without this
# the chain falls through to the Orbis (PS4) `else` branch and tries
# to include Sony NP / FIOS2 headers we do not have on iOS.
#
# Idempotent: detects the iOS marker and bails if already applied. Run
# from CI before `cmake -S . -B build` so the world-probe target sees
# the patched stdafx.h.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
STDAFX="$REPO_ROOT/upstream/Minecraft.World/stdafx.h"

if [ ! -f "$STDAFX" ]; then
    echo "patch-upstream-stdafx: $STDAFX not found"
    exit 1
fi

if grep -q '__APPLE_IOS__' "$STDAFX"; then
    echo "patch-upstream-stdafx: iOS branch already present, nothing to do"
    exit 0
fi

# Insert an `elif __APPLE_IOS__` branch just before the catch-all `#else`
# (which selects Orbis). The new branch points at our iOS 4JLibs stubs.
python3 - "$STDAFX" <<'PY'
import re, sys
path = sys.argv[1]
with open(path, 'r', encoding='utf-8', errors='replace') as f:
    src = f.read()

# The block we want to extend looks like:
#     #elif defined __PSVITA__
#     <PSVita includes>
#     #else
#     <Orbis includes>
#     #endif
# Insert a new "elif defined __APPLE_IOS__" branch just before "#else".
needle = '#elif defined __PSVITA__'
if needle not in src:
    sys.exit("patch-upstream-stdafx: anchor `#elif defined __PSVITA__` not found in stdafx.h")

# Find the `#else` that follows the PSVita branch and insert before it.
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
