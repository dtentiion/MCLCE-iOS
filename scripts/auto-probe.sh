#!/usr/bin/env bash
# Auto-probe: try compiling every upstream/Minecraft.World/*.cpp file
# individually against the iOS toolchain with our existing shim
# infrastructure. Output: a CMakeLists-ready list of files that
# compile clean.
#
# Run on a Mac CI runner (not on Windows). Driven by the
# .github/workflows/auto-probe.yml workflow.
#
# Usage: ./scripts/auto-probe.sh [output-file]

set -uo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT_FILE="${1:-$REPO_ROOT/auto-probe-greens.txt}"
MW_DIR="$REPO_ROOT/upstream/Minecraft.World"
MC_DIR="$REPO_ROOT/upstream/Minecraft.Client"
LOG_DIR="$REPO_ROOT/auto-probe-logs"
SHIM="$REPO_ROOT/Minecraft.Client/iOS/iOS_stdafx.h"

mkdir -p "$LOG_DIR"
> "$OUT_FILE"

# Locate iOS SDK + clang. CI runner has Xcode 15.x, we use iPhoneOS SDK.
SDK="$(xcrun --sdk iphoneos --show-sdk-path)"
CXX="$(xcrun --sdk iphoneos -f clang++)"
ARCH="-arch arm64 -isysroot $SDK -mios-version-min=14.0"

# Same compile flags as the WorldProbe target.
CXXFLAGS="-std=c++14 -fdeclspec -fsyntax-only \
  -include $SHIM \
  -D_LIB -D_LARGE_WORLDS -D_CONTENT_PACKAGE \
  -D__APPLE_IOS__=1 -D_IOS=1 -DPLATFORM_IOS=1 \
  -DMCLE_PROBE_BUILD=1 \
  -I $MW_DIR \
  -I $MW_DIR/x64headers \
  -I $MC_DIR \
  -I $MC_DIR/Common \
  -I $REPO_ROOT/upstream/include \
  -I $REPO_ROOT/Minecraft.Client/iOS \
  -I $REPO_ROOT/Minecraft.Client/iOS/compat_headers \
  -ferror-limit=10 \
  -w"

# Run patch script first so iOS branches are present in upstream.
chmod +x "$REPO_ROOT/scripts/patch-upstream-stdafx.sh"
"$REPO_ROOT/scripts/patch-upstream-stdafx.sh"

total=0
green=0
red=0

# Probe set 1: upstream/Minecraft.World/*.cpp  (the gameplay/world simulation)
cd "$MW_DIR"
for src in *.cpp; do
    total=$((total + 1))
    log="$LOG_DIR/MW_${src%.cpp}.log"
    if "$CXX" $ARCH $CXXFLAGS "$src" > "$log" 2>&1; then
        echo "GREEN: Minecraft.World/$src"
        echo "Minecraft.World/$src" >> "$OUT_FILE"
        green=$((green + 1))
        rm -f "$log"
    else
        red=$((red + 1))
        head -3 "$log" >> "$LOG_DIR/_failures.txt"
        echo "  -> Minecraft.World/$src" >> "$LOG_DIR/_failures.txt"
    fi
done

# Probe set 2: upstream/Minecraft.Client/Common/**/*.cpp (platform-agnostic
# client + game-rules + DLC + Network code). Excludes the renderer-heavy
# Minecraft.Client/ root for now (those need Phase D's GL ES bringup).
while IFS= read -r src; do
    total=$((total + 1))
    rel="${src#$REPO_ROOT/upstream/}"
    safe="${rel//\//_}"
    log="$LOG_DIR/${safe%.cpp}.log"
    if "$CXX" $ARCH $CXXFLAGS "$src" > "$log" 2>&1; then
        echo "GREEN: $rel"
        echo "$rel" >> "$OUT_FILE"
        green=$((green + 1))
        rm -f "$log"
    else
        red=$((red + 1))
        head -3 "$log" >> "$LOG_DIR/_failures.txt"
        echo "  -> $rel" >> "$LOG_DIR/_failures.txt"
    fi
done < <(find "$MC_DIR/Common" -name "*.cpp")

echo
echo "==== auto-probe summary ===="
echo "total:  $total"
echo "green:  $green"
echo "red:    $red"
echo "greens written to: $OUT_FILE"
echo "failure summaries: $LOG_DIR/_failures.txt"
