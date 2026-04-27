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
CLIENT_STDAFX="$REPO_ROOT/upstream/Minecraft.Client/stdafx.h"
FILEHEADER="$REPO_ROOT/upstream/Minecraft.World/FileHeader.h"
ARRWITHLEN="$REPO_ROOT/upstream/Minecraft.World/ArrayWithLength.h"
DOSCPP="$REPO_ROOT/upstream/Minecraft.World/DataOutputStream.cpp"
DISCPP="$REPO_ROOT/upstream/Minecraft.World/DataInputStream.cpp"
COMPCPP="$REPO_ROOT/upstream/Minecraft.World/compression.cpp"
LEVELH="$REPO_ROOT/upstream/Minecraft.World/Level.h"
ZCSCPP="$REPO_ROOT/upstream/Minecraft.World/ZonedChunkStorage.cpp"
SNDENG="$REPO_ROOT/upstream/Minecraft.Client/Common/Audio/Consoles_SoundEngine.h"
STRHLP="$REPO_ROOT/upstream/Minecraft.World/StringHelpers.cpp"
LDATAC="$REPO_ROOT/upstream/Minecraft.World/LevelData.cpp"

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

for f in "$LEVELH" "$CLIENT_STDAFX" "$SNDENG" "$STRHLP" "$LDATAC"; do
    if [ ! -f "$f" ]; then
        echo "patch-upstream-stdafx: $f not found"
        exit 1
    fi
done

if grep -q '__APPLE_IOS__' "$STDAFX" && grep -q '__APPLE_IOS__' "$FILEHEADER" && grep -q '__APPLE_IOS__' "$ARRWITHLEN" && grep -q '__APPLE_IOS__' "$DOSCPP" && grep -q '__APPLE_IOS__' "$DISCPP" && grep -q '__APPLE_IOS__' "$COMPCPP" && grep -q '__APPLE_IOS__' "$LEVELH" && grep -q '__APPLE_IOS__' "$CLIENT_STDAFX" && grep -q '__APPLE_IOS__' "$SNDENG" && grep -q '__APPLE_IOS__' "$STRHLP" && grep -q 'ChunkSource\.h' "$LDATAC"; then
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
patch_stdafx_no_op() {
    local path="$1"
    local label="$2"
    if grep -q '__APPLE_IOS__' "$path"; then
        echo "patch-upstream-stdafx: ${label} already patched, skipping"
        return
    fi
    python3 - "$path" "$label" <<'PY'
import sys
path = sys.argv[1]
label = sys.argv[2]
with open(path, 'r', encoding='utf-8', errors='replace') as f:
    src = f.read()
pragma = '#pragma once'
idx = src.find(pragma)
if idx < 0:
    sys.exit(f"patch-upstream-stdafx: `#pragma once` not found in {label}")
split_at = idx + len(pragma)
prefix = src[:split_at]
body   = src[split_at:]
wrapped = (
    prefix
    + f'\n\n// Skip upstream stdafx body on iOS - iOS_stdafx.h is force-included.\n'
    + '#ifndef __APPLE_IOS__\n'
    + body
    + '\n#endif // !__APPLE_IOS__\n'
)
with open(path, 'w', encoding='utf-8', newline='\n') as f:
    f.write(wrapped)
print(f"patch-upstream-stdafx: wrapped {label} body in #ifndef __APPLE_IOS__")
PY
}

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

# Same treatment for Minecraft.Client/stdafx.h. It pulls Win32 / Xbox /
# Sony platform headers per #ifdef chain; iOS has no branch so we skip
# the whole body and rely on iOS_stdafx.h to provide what we need.
patch_stdafx_no_op "$CLIENT_STDAFX" "Minecraft.Client/stdafx.h"

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
# Always run; each replace below is idempotent (no-op if already patched).
python3 - "$COMPCPP" <<'PY'
import sys
path = sys.argv[1]
with open(path, 'r', encoding='utf-8', errors='replace') as f:
    src = f.read()

needle = '#if defined __ORBIS__ || defined __PS3__ || defined _DURANGO || defined _WIN64'
patched = src
if needle in patched and (needle + ' || defined __APPLE_IOS__') not in patched:
    patched = patched.replace(needle, needle + ' || defined __APPLE_IOS__', 1)
# Compress/Decompress chains pick zlib for the Win64/Orbis/Vita/Durango set
# and fall through to XMemCompress/XMemDecompress in the #else. Route iOS
# into the zlib arms so we don't need Microsoft's XMem* APIs.
patterns = [
    # Compress/Decompress chains - bodies use zlib ::compress/::uncompress.
    '#if defined __ORBIS__ || defined _DURANGO || defined _WIN64 || defined __PSVITA__',
    # ZLIBRLE / PS3ZLIB cases in DecompressWithType - bodies use zlib too.
    '#if (defined __ORBIS__ || defined __PS3__ || defined _DURANGO || defined _WIN64)',
    '#if (defined __ORBIS__ || defined __PSVITA__ || defined _DURANGO || defined _WIN64)',
    # NOTE: do NOT add iOS to '#if (defined _XBOX || defined _DURANGO || defined _WIN64)'.
    # That guards the LZXRLE case which calls XMemDecompress, an Xbox-only API.
    # iOS should fall through to the #else assert(0) arm.
]
for n in patterns:
    if n.endswith(')'):
        rep = n[:-1] + ' || defined __APPLE_IOS__)'
    else:
        rep = n + ' || defined __APPLE_IOS__'
    patched = patched.replace(n, rep)
# Compression ctor/dtor allocate XMem contexts on every non-Sony platform.
# Add iOS to the exclusion lists so the XMemCreate/Destroy calls are skipped.
patched = patched.replace(
    '#if !(defined __ORBIS__ || defined __PS3__)',
    '#if !(defined __ORBIS__ || defined __PS3__ || defined __APPLE_IOS__)',
)
patched = patched.replace(
    '#if !(defined __ORBIS__ || defined __PS3__ || defined __PSVITA__)',
    '#if !(defined __ORBIS__ || defined __PS3__ || defined __PSVITA__ || defined __APPLE_IOS__)',
)
with open(path, 'w', encoding='utf-8', newline='\n') as f:
    f.write(patched)
print(f"patch-upstream-stdafx: routed iOS to zlib branches in {path}")
PY

# Level.h is missing a `static const int DEPTH` member - the only place
# it is referenced is ZonedChunkStorage.cpp:21, which uses it to size
# CHUNK_SIZE = CHUNK_WIDTH * CHUNK_WIDTH * Level::DEPTH. Upstream's
# Level.h has maxBuildHeight = 256 which is the world's vertical
# resolution; alias DEPTH to that value so ZonedChunkStorage compiles
# under parity (rather than us shimming Level::DEPTH out of band).
if grep -q '__APPLE_IOS__' "$LEVELH"; then
    echo "patch-upstream-stdafx: Level.h already patched, skipping"
else
python3 - "$LEVELH" <<'PY'
import sys
path = sys.argv[1]
with open(path, 'r', encoding='utf-8', errors='replace') as f:
    src = f.read()

# Insert DEPTH right after maxBuildHeight so the constants stay grouped.
needle = 'static const int maxBuildHeight = 256;'
if needle not in src:
    sys.exit("patch-upstream-stdafx: maxBuildHeight anchor not found in Level.h")
addition = (
    needle
    + '\n#ifdef __APPLE_IOS__\n'
    + '\tstatic const int DEPTH = maxBuildHeight; // 4J - referenced by ZonedChunkStorage\n'
    + '#endif'
)
patched = src.replace(needle, addition, 1)
with open(path, 'w', encoding='utf-8', newline='\n') as f:
    f.write(patched)
print(f"patch-upstream-stdafx: added Level::DEPTH to {path}")
PY
fi

# ZonedChunkStorage.cpp:98 does `lc->unsaved = false` on a LevelChunk
# but upstream LevelChunk.h has the field as private `m_unsaved` with a
# `setUnsaved(bool)` setter. The line does not compile against this
# revision of LevelChunk.h. Patch the call site to use the setter.
if grep -q 'setUnsaved(false)' "$ZCSCPP" 2>/dev/null; then
    echo "patch-upstream-stdafx: ZonedChunkStorage.cpp already patched, skipping"
elif [ -f "$ZCSCPP" ]; then
python3 - "$ZCSCPP" <<'PY'
import sys
path = sys.argv[1]
with open(path, 'r', encoding='utf-8', errors='replace') as f:
    src = f.read()
needle = 'lc->unsaved = false;'
if needle not in src:
    sys.exit(f"patch-upstream-stdafx: lc->unsaved anchor not found in {path}")
patched = src.replace(needle, 'lc->setUnsaved(false);', 1)
with open(path, 'w', encoding='utf-8', newline='\n') as f:
    f.write(patched)
print(f"patch-upstream-stdafx: rewrote lc->unsaved as setUnsaved(false) in {path}")
PY
fi

# Consoles_SoundEngine.h has a per-platform if/elif chain that picks an
# RAD Miles `mss.h` for audio. iOS isn't covered, so it falls through to
# the `#else // PS4` branch and pulls Orbis/Miles/include/mss.h, which
# requires RAD platform macros (RADDEFSTART/RADDEFEND) that conflict
# with our Iggy stub's RADLINK / RADEXPLINK guard logic. iOS uses
# miniaudio (Common/Audio/SoundEngine.cpp) instead, so we want the
# RAD Miles include to be skipped entirely on iOS. Insert an iOS branch
# before the `#else // PS4` arm that does nothing.
if grep -q '__APPLE_IOS__' "$SNDENG"; then
    echo "patch-upstream-stdafx: Consoles_SoundEngine.h already patched, skipping"
else
python3 - "$SNDENG" <<'PY'
import sys
path = sys.argv[1]
with open(path, 'r', encoding='utf-8', errors='replace') as f:
    src = f.read()

# Insert iOS branch right before the bare `#else` that catches PS4.
# Anchor: the line `#elif defined _WINDOWS64`. Insert iOS branch
# AFTER the trailing `#else // PS4` so iOS gets its own arm with no
# Miles include.
needle = '#elif defined _WINDOWS64\n#else // PS4'
if needle not in src:
    sys.exit("patch-upstream-stdafx: Consoles_SoundEngine.h anchor not found")
patched = src.replace(
    needle,
    '#elif defined _WINDOWS64\n'
    + '#elif defined __APPLE_IOS__\n'
    + '// iOS uses miniaudio (Common/Audio/SoundEngine.cpp) - skip RAD Miles\n'
    + '#else // PS4',
    1,
)
with open(path, 'w', encoding='utf-8', newline='\n') as f:
    f.write(patched)
print(f"patch-upstream-stdafx: inserted iOS branch into {path}")
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

# StringHelpers.cpp:wstringtofilename swaps '/' for '\' on every non-PS/Orbis
# build. iOS uses POSIX paths, so add iOS to the no-swap branch.
if grep -q '__APPLE_IOS__' "$STRHLP"; then
    echo "patch-upstream-stdafx: StringHelpers.cpp already patched, skipping"
else
python3 - "$STRHLP" <<'PY'
import sys
path = sys.argv[1]
with open(path, 'r', encoding='utf-8', errors='replace') as f:
    src = f.read()

needle = '#if defined __PS3__ || defined __ORBIS__'
if needle not in src:
    sys.exit("patch-upstream-stdafx: StringHelpers anchor not found")
patched = src.replace(
    needle,
    '#if defined __PS3__ || defined __ORBIS__ || defined __APPLE_IOS__',
    1,
)
with open(path, 'w', encoding='utf-8', newline='\n') as f:
    f.write(patched)
print(f"patch-upstream-stdafx: inserted iOS branch into {path}")
PY
fi

# LevelData.cpp uses HELL_LEVEL_MAX_WIDTH / HELL_LEVEL_MAX_SCALE inline at
# lines 160-164, but doesn't include the header that defines them
# (ChunkSource.h). MSVC likely has a precompiled-header that pulls it in
# transitively; iOS clang doesn't. Add the include after the existing ones.
if grep -q 'ChunkSource\.h' "$LDATAC"; then
    echo "patch-upstream-stdafx: LevelData.cpp already patched, skipping"
else
python3 - "$LDATAC" <<'PY'
import sys
path = sys.argv[1]
with open(path, 'r', encoding='utf-8', errors='replace') as f:
    src = f.read()

needle = '#include "LevelSettings.h"'
if needle not in src:
    sys.exit("patch-upstream-stdafx: LevelData.cpp anchor not found")
patched = src.replace(
    needle,
    needle + '\n#include "ChunkSource.h"  // HELL_LEVEL_MAX_WIDTH/SCALE',
    1,
)
with open(path, 'w', encoding='utf-8', newline='\n') as f:
    f.write(patched)
print(f"patch-upstream-stdafx: inserted ChunkSource.h include into {path}")
PY
fi

# DirectoryLevelStorage.cpp uses std::to_wstring(PlayerUID) in two places
# (lines 401, 450), which Sony/Xbox supplies but iOS doesn't. The Durango
# (_DURANGO) branch right above uses xuid.toString() which is what we want.
# Add __APPLE_IOS__ to those _DURANGO arms.
DLSCPP="$REPO_ROOT/upstream/Minecraft.World/DirectoryLevelStorage.cpp"
if grep -q '__APPLE_IOS__' "$DLSCPP"; then
    echo "patch-upstream-stdafx: DirectoryLevelStorage.cpp already patched, skipping"
else
python3 - "$DLSCPP" <<'PY'
import sys
path = sys.argv[1]
with open(path, 'r', encoding='utf-8', errors='replace') as f:
    src = f.read()
needle = '#elif defined(_DURANGO)'
if needle not in src:
    sys.exit("patch-upstream-stdafx: DLSCPP _DURANGO anchor not found")
patched = src.replace(needle, '#elif defined(_DURANGO) || defined(__APPLE_IOS__)')
with open(path, 'w', encoding='utf-8', newline='\n') as f:
    f.write(patched)
print(f"patch-upstream-stdafx: routed iOS through Durango branch in {path}")
PY
fi

# StringHelpers.h's _fromString<PlayerUID> template was the second blocker;
# fixed instead by adding `operator>>(wistream&, PlayerUID&)` to the iOS
# PlayerUID stub in 4JLibs/inc/4J_Profile.h. No source patch needed here.

# LevelGenerationOptions.h's class is redefined when both the iOS_stdafx.h
# stub and the real header are visible (e.g. ConsoleSaveFileOriginal.cpp
# pulls the real one). iOS only needs the stub, so wrap the real header
# body in #ifndef __APPLE_IOS__.
LGOHDR="$REPO_ROOT/upstream/Minecraft.Client/Common/GameRules/LevelGenerationOptions.h"
if grep -q '__APPLE_IOS__' "$LGOHDR"; then
    echo "patch-upstream-stdafx: LevelGenerationOptions.h already patched, skipping"
else
python3 - "$LGOHDR" <<'PY'
import sys
path = sys.argv[1]
with open(path, 'r', encoding='utf-8', errors='replace') as f:
    src = f.read()
pragma = '#pragma once'
idx = src.find(pragma)
if idx < 0:
    sys.exit("patch-upstream-stdafx: #pragma once not found in LevelGenerationOptions.h")
split_at = idx + len(pragma)
prefix = src[:split_at]
body   = src[split_at:]
wrapped = (
    prefix
    + '\n\n// iOS provides a stub LevelGenerationOptions in iOS_stdafx.h\n'
    + '#ifndef __APPLE_IOS__\n'
    + body
    + '\n#endif // !__APPLE_IOS__\n'
)
with open(path, 'w', encoding='utf-8', newline='\n') as f:
    f.write(wrapped)
print(f"patch-upstream-stdafx: wrapped LevelGenerationOptions.h body in #ifndef __APPLE_IOS__")
PY
fi

# WeighedTreasure.h forward-declares DispenserTileEntity in a shared_ptr<>.
# The real header isn't reachable from EnchantedBookItem.cpp's include set on
# iOS, so add a forward declaration at the top.
WTR="$REPO_ROOT/upstream/Minecraft.World/WeighedTreasure.h"
if grep -q '^class DispenserTileEntity;' "$WTR"; then
    echo "patch-upstream-stdafx: WeighedTreasure.h already patched, skipping"
else
python3 - "$WTR" <<'PY'
import sys
path = sys.argv[1]
with open(path, 'r', encoding='utf-8', errors='replace') as f:
    src = f.read()
needle = '#include "WeighedRandom.h"'
if needle not in src:
    sys.exit("patch-upstream-stdafx: WeighedRandom anchor not found")
patched = src.replace(needle, needle + '\nclass DispenserTileEntity;', 1)
with open(path, 'w', encoding='utf-8', newline='\n') as f:
    f.write(patched)
print(f"patch-upstream-stdafx: forward-declared DispenserTileEntity in {path}")
PY
fi

# FlatGeneratorInfo.cpp uses Biome::plains->id but only forward-declares
# Biome via its header chain. Add a direct Biome.h include.
FGIC="$REPO_ROOT/upstream/Minecraft.World/FlatGeneratorInfo.cpp"
if grep -q '#include "Biome.h"' "$FGIC"; then
    echo "patch-upstream-stdafx: FlatGeneratorInfo.cpp already patched, skipping"
else
python3 - "$FGIC" <<'PY'
import sys
path = sys.argv[1]
with open(path, 'r', encoding='utf-8', errors='replace') as f:
    src = f.read()
needle = '#include "FlatGeneratorInfo.h"'
if needle not in src:
    sys.exit("patch-upstream-stdafx: FlatGeneratorInfo anchor not found")
patched = src.replace(needle, needle + '\n#include "Biome.h"  // Biome::plains', 1)
with open(path, 'w', encoding='utf-8', newline='\n') as f:
    f.write(patched)
print(f"patch-upstream-stdafx: added Biome.h include to {path}")
PY
fi

# EntitySelector.cpp dereferences shared_ptr<Entity> but only includes the
# forward-decl chain. Add Entity.h directly.
ESCPP="$REPO_ROOT/upstream/Minecraft.World/EntitySelector.cpp"
if grep -q '#include "Entity.h"' "$ESCPP"; then
    echo "patch-upstream-stdafx: EntitySelector.cpp already patched, skipping"
else
python3 - "$ESCPP" <<'PY'
import sys
path = sys.argv[1]
with open(path, 'r', encoding='utf-8', errors='replace') as f:
    src = f.read()
needle = '#include "EntitySelector.h"'
if needle not in src:
    sys.exit("patch-upstream-stdafx: EntitySelector anchor not found")
patched = src.replace(needle, needle + '\n#include "Entity.h"  // shared_ptr<Entity> deref', 1)
with open(path, 'w', encoding='utf-8', newline='\n') as f:
    f.write(patched)
print(f"patch-upstream-stdafx: added Entity.h include to {path}")
PY
fi

# SavedDataStorage.cpp uses typeid() on three SavedData subclasses but only
# pulls forward-decl headers. Add the three concrete headers directly.
SDSC="$REPO_ROOT/upstream/Minecraft.World/SavedDataStorage.cpp"
if grep -q '#include "MapItemSavedData.h"' "$SDSC"; then
    echo "patch-upstream-stdafx: SavedDataStorage.cpp already patched, skipping"
else
python3 - "$SDSC" <<'PY'
import sys
path = sys.argv[1]
with open(path, 'r', encoding='utf-8', errors='replace') as f:
    src = f.read()
needle = '#include "SavedDataStorage.h"'
if needle not in src:
    sys.exit("patch-upstream-stdafx: SavedDataStorage anchor not found")
add = (
    needle
    + '\n#include "MapItemSavedData.h"'
    + '\n#include "StructureFeatureSavedData.h"'
    + '\n#include "Villages.h"'
)
patched = src.replace(needle, add, 1)
with open(path, 'w', encoding='utf-8', newline='\n') as f:
    f.write(patched)
print(f"patch-upstream-stdafx: added SavedData headers to {path}")
PY
fi

# ConsoleSchematicFile.h uses Compression::ECompressionTypes inline but only
# forward-declares peers. Add a direct compression.h include so every
# GameRules .cpp that pulls this header sees the Compression class.
CSCH="$REPO_ROOT/upstream/Minecraft.Client/Common/GameRules/ConsoleSchematicFile.h"
if grep -q '"compression.h"' "$CSCH"; then
    echo "patch-upstream-stdafx: ConsoleSchematicFile.h already patched, skipping"
else
python3 - "$CSCH" <<'PY'
import sys
path = sys.argv[1]
with open(path, 'r', encoding='utf-8', errors='replace') as f:
    src = f.read()
needle = '#include "../../../Minecraft.World/ArrayWithLength.h"'
if needle not in src:
    sys.exit("patch-upstream-stdafx: ConsoleSchematicFile anchor not found")
patched = src.replace(needle,
    needle + '\n#include "../../../Minecraft.World/compression.h"  // Compression::ECompressionTypes',
    1,
)
with open(path, 'w', encoding='utf-8', newline='\n') as f:
    f.write(patched)
print(f"patch-upstream-stdafx: added compression.h include to {path}")
PY
fi

# NetherStalkTile.cpp references Tile::hellSand_Id which was renamed to
# soulsand_Id in the public header but the .cpp wasn't updated. Patch it.
NSTC="$REPO_ROOT/upstream/Minecraft.World/NetherStalkTile.cpp"
if grep -q 'soulsand_Id' "$NSTC"; then
    echo "patch-upstream-stdafx: NetherStalkTile.cpp already patched, skipping"
else
python3 - "$NSTC" <<'PY'
import sys
path = sys.argv[1]
with open(path, 'r', encoding='utf-8', errors='replace') as f:
    src = f.read()
patched = src.replace('Tile::hellSand_Id', 'Tile::soulsand_Id')
with open(path, 'w', encoding='utf-8', newline='\n') as f:
    f.write(patched)
print(f"patch-upstream-stdafx: renamed hellSand_Id -> soulsand_Id in {path}")
PY
fi

# ClothTileItem.cpp uses ClothTile::getTileDataForItemAuxValue but only
# pulls the umbrella net.minecraft.world.level.tile.h header. Add a direct
# ClothTile.h include.
CTIC="$REPO_ROOT/upstream/Minecraft.World/ClothTileItem.cpp"
if grep -q '#include "ClothTile.h"' "$CTIC"; then
    echo "patch-upstream-stdafx: ClothTileItem.cpp already patched, skipping"
else
python3 - "$CTIC" <<'PY'
import sys
path = sys.argv[1]
with open(path, 'r', encoding='utf-8', errors='replace') as f:
    src = f.read()
needle = '#include "ClothTileItem.h"'
if needle not in src:
    sys.exit("patch-upstream-stdafx: ClothTileItem anchor not found")
patched = src.replace(needle, '#include "ClothTile.h"\n' + needle, 1)
with open(path, 'w', encoding='utf-8', newline='\n') as f:
    f.write(patched)
print(f"patch-upstream-stdafx: added ClothTile.h include to {path}")
PY
fi

# ZonedChunkStorage.cpp:101 assigns to LevelChunk::blocks directly but the
# field was made private and replaced with setBlockData(). Rewrite that one
# assignment.
ZCS="$REPO_ROOT/upstream/Minecraft.World/ZonedChunkStorage.cpp"
if grep -q 'setBlockData(zoneIo->read' "$ZCS"; then
    echo "patch-upstream-stdafx: ZonedChunkStorage.cpp blocks=>setBlockData already patched, skipping"
else
python3 - "$ZCS" <<'PY'
import sys
path = sys.argv[1]
with open(path, 'r', encoding='utf-8', errors='replace') as f:
    src = f.read()
old = 'lc->blocks = zoneIo->read(CHUNK_SIZE)->array();'
new = 'lc->setBlockData(zoneIo->read(CHUNK_SIZE)->array());'
if old not in src:
    sys.exit("patch-upstream-stdafx: ZonedChunkStorage blocks anchor not found")
patched = src.replace(old, new, 1)
with open(path, 'w', encoding='utf-8', newline='\n') as f:
    f.write(patched)
print(f"patch-upstream-stdafx: rewrote lc->blocks= as setBlockData() in {path}")
PY
fi
