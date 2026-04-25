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
ARRWITHLEN="$REPO_ROOT/upstream/Minecraft.World/ArrayWithLength.h"
DOSCPP="$REPO_ROOT/upstream/Minecraft.World/DataOutputStream.cpp"
DISCPP="$REPO_ROOT/upstream/Minecraft.World/DataInputStream.cpp"
COMPCPP="$REPO_ROOT/upstream/Minecraft.World/compression.cpp"

if [ ! -f "$STDAFX" ]; then
    echo "patch-upstream-stdafx: $STDAFX not found"
    exit 1
fi
if [ ! -f "$FILEHEADER" ]; then
    echo "patch-upstream-stdafx: $FILEHEADER not found"
    exit 1
fi
if [ ! -f "$ARRWITHLEN" ]; then
    echo "patch-upstream-stdafx: $ARRWITHLEN not found"
    exit 1
fi
for f in "$DOSCPP" "$DISCPP" "$COMPCPP"; do
    if [ ! -f "$f" ]; then
        echo "patch-upstream-stdafx: $f not found"
        exit 1
    fi
done

if grep -q '__APPLE_IOS__' "$STDAFX" && grep -q '__APPLE_IOS__' "$FILEHEADER" && grep -q '__APPLE_IOS__' "$ARRWITHLEN" && grep -q '__APPLE_IOS__' "$DOSCPP" && grep -q '__APPLE_IOS__' "$DISCPP" && grep -q '__APPLE_IOS__' "$COMPCPP"; then
    echo "patch-upstream-stdafx: iOS branches already present in all files, nothing to do"
    exit 0
fi

# Wrap the entire body of upstream's stdafx.h in `#ifndef __APPLE_IOS__`
# so it becomes a no-op on iOS. Upstream's stdafx.h was written for the
# full app build and pulls in ~30 Minecraft.Client/Common/*.h headers
# (Consoles_App, GameNetworkManager, UIStructs, App_structs, ...) plus
# their dependencies, each of which assumes a full Win32 / SCE / 4J
# environment we are not trying to provide right now.
#
# Our force-included iOS_stdafx.h already provides the std headers and
# Win32 type aliases AABB.cpp / Vec3.cpp / similar low-level translation
# units actually need. As the probe set grows and a file genuinely
# needs something stdafx.h would have pulled in, we add it directly to
# iOS_stdafx.h or include it from the file's own header.
if grep -q '__APPLE_IOS__' "$STDAFX"; then
    echo "patch-upstream-stdafx: stdafx.h already patched, skipping"
else
python3 - "$STDAFX" <<'PY'
import sys
path = sys.argv[1]
with open(path, 'r', encoding='utf-8', errors='replace') as f:
    src = f.read()

# Wrap everything after the leading `#pragma once` in `#ifndef
# __APPLE_IOS__ ... #endif`. The marker comment also serves as the
# idempotency check (grep for __APPLE_IOS__ above).
pragma = '#pragma once'
idx = src.find(pragma)
if idx < 0:
    sys.exit("patch-upstream-stdafx: `#pragma once` not found in stdafx.h")
split_at = idx + len(pragma)
prefix = src[:split_at]
body   = src[split_at:]

wrapped = (
    prefix
    + '\n\n// Skip upstream stdafx body on iOS - iOS_stdafx.h is force-included\n'
    + '// and provides only what the probe set actually needs. See the\n'
    + '// patch-upstream-stdafx.sh comment for the full reasoning.\n'
    + '#ifndef __APPLE_IOS__\n'
    + body
    + '\n#endif // !__APPLE_IOS__\n'
)

with open(path, 'w', encoding='utf-8', newline='\n') as f:
    f.write(wrapped)
print(f"patch-upstream-stdafx: wrapped stdafx.h body in #ifndef __APPLE_IOS__")
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
# ArrayWithLength.h has a `#include "ItemInstance.h"` near the bottom that
# only exists to define ItemInstanceArray. ItemInstance.h transitively
# pulls in com.mojang.nbt -> NbtIo -> CompoundTag -> ByteArrayTag -> Tag
# -> InputOutputStream -> DataInput, which references PlayerUID and
# System (the latter creating a circular include loop because we are
# already in System.h's chain when we reach ByteArrayTag's inline body).
# Skip the ItemInstance include on iOS - the probe set does not exercise
# ItemInstance and we will land it as a separate piece later. Wrap with
# a marker comment so grep can detect idempotency.
if grep -q '__APPLE_IOS__' "$ARRWITHLEN"; then
    echo "patch-upstream-stdafx: ArrayWithLength.h already patched, skipping"
else
python3 - "$ARRWITHLEN" <<'PY'
import sys
path = sys.argv[1]
with open(path, 'r', encoding='utf-8', errors='replace') as f:
    src = f.read()

needle = '#include "ItemInstance.h"'
if needle not in src:
    sys.exit("patch-upstream-stdafx: anchor `#include \"ItemInstance.h\"` not found in ArrayWithLength.h")

# Wrap the ItemInstance include + its dependent typedef. The line right
# after the include is `typedef arrayWithLength<shared_ptr<ItemInstance> > ItemInstanceArray;`
# so we wrap both lines.
old = needle + '\ntypedef arrayWithLength<shared_ptr<ItemInstance> > ItemInstanceArray;'
new = (
    '#ifndef __APPLE_IOS__  // probe skips ItemInstance to avoid NBT cascade\n'
    + old + '\n'
    + '#endif // !__APPLE_IOS__'
)
if old not in src:
    sys.exit("patch-upstream-stdafx: ItemInstance include + typedef pair not found verbatim in ArrayWithLength.h")
patched = src.replace(old, new, 1)

with open(path, 'w', encoding='utf-8', newline='\n') as f:
    f.write(patched)
print(f"patch-upstream-stdafx: skipped ItemInstance include on iOS in {path}")
PY
fi

# DataOutputStream.cpp / DataInputStream.cpp gate writePlayerUID /
# readPlayerUID on platform macros to either (a) write/read 16 raw
# bytes (Sony platforms - PS3, Orbis, Vita), (b) write/read a string
# (Durango), or (c) write/read a Long (everywhere else). iOS
# PlayerUID is a 16-byte struct, so put __APPLE_IOS__ in the Sony
# branch where the for-loop write-each-byte path matches.
add_ios_to_sony_branch() {
    local file="$1"
    if grep -q '__APPLE_IOS__' "$file"; then
        echo "patch-upstream-stdafx: $(basename "$file") already patched, skipping"
        return
    fi
    python3 - "$file" <<'PY'
import re, sys
path = sys.argv[1]
with open(path, 'r', encoding='utf-8', errors='replace') as f:
    src = f.read()

# Match both variants: `defined (__PSVITA__)` with space and
# `defined(__PSVITA__)` without. Two upstream files use different
# whitespace styles in the same conditional.
pat = re.compile(
    r'#if defined\(__PS3__\) \|\| defined\(__ORBIS__\) \|\| defined ?\(__PSVITA__\)'
)
m = pat.search(src)
if not m:
    sys.exit(f"patch-upstream-stdafx: Sony branch anchor not found in {path}")

# Re-emit with iOS appended, preserving whatever whitespace style the
# original had (we just append `|| defined(__APPLE_IOS__)`).
new_text = m.group(0) + ' || defined(__APPLE_IOS__)'
patched = src[:m.start()] + new_text + src[m.end():]
with open(path, 'w', encoding='utf-8', newline='\n') as f:
    f.write(patched)
print(f"patch-upstream-stdafx: added __APPLE_IOS__ to Sony branch in {path}")
PY
}
add_ios_to_sony_branch "$DOSCPP"
add_ios_to_sony_branch "$DISCPP"

# compression.cpp picks zlib headers per platform. The first chain
# (line 3) selects upstream's bundled Common/zlib for Orbis / PS3 /
# Durango / Win64. iOS has the same file accessible, so add iOS to
# this same set so the standard zlib.h is included.
if grep -q '__APPLE_IOS__' "$COMPCPP"; then
    echo "patch-upstream-stdafx: compression.cpp already patched, skipping"
else
python3 - "$COMPCPP" <<'PY'
import sys
path = sys.argv[1]
with open(path, 'r', encoding='utf-8', errors='replace') as f:
    src = f.read()

needle = '#if defined __ORBIS__ || defined __PS3__ || defined _DURANGO || defined _WIN64'
if needle not in src:
    sys.exit(f"patch-upstream-stdafx: zlib branch anchor not found in {path}")
new = needle + ' || defined __APPLE_IOS__'
patched = src.replace(needle, new, 1)
with open(path, 'w', encoding='utf-8', newline='\n') as f:
    f.write(patched)
print(f"patch-upstream-stdafx: added __APPLE_IOS__ to zlib branch in {path}")
PY
fi

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
