#!/usr/bin/env bash
# Translate an HLSL shader to Metal Shading Language via SPIR-V.
#
# Pipeline:
#   HLSL  --glslang-->  SPIR-V  --spirv-cross-->  MSL  --metal-->  .metallib
#
# Requires: glslang, spirv-cross, xcrun metal (Xcode). On macOS:
#   brew install glslang spirv-cross
#
# glslangValidator is used instead of DXC because DXC is not packaged on
# Homebrew. glslang has HLSL frontend support via -D.
#
# Usage:
#   ./scripts/translate-shader.sh <shader.hlsl> <stage> <entry> <out-dir>
#   stage:  vertex | fragment | compute
#   entry:  HLSL entry point, e.g. main
#
# Example:
#   ./scripts/translate-shader.sh \
#     third_party/4JLibs/Windows_Libs/Dev/Render/shaders/main_VS.hlsl \
#     vertex main out/shaders

set -euo pipefail

HLSL_PATH="${1:?usage: translate-shader.sh <hlsl> <stage> <entry> <outdir>}"
STAGE="${2:?missing stage}"
ENTRY="${3:?missing entry}"
OUTDIR="${4:?missing outdir}"

case "$STAGE" in
  vertex)   GLSLANG_STAGE="vert" ;;
  fragment) GLSLANG_STAGE="frag" ;;
  compute)  GLSLANG_STAGE="comp" ;;
  *) echo "unknown stage: $STAGE"; exit 2 ;;
esac

mkdir -p "$OUTDIR"
BASE="$(basename "$HLSL_PATH" | sed -E 's/\.(hlsl|fx)$//')"
SPV="$OUTDIR/$BASE.spv"
MSL="$OUTDIR/$BASE.metal"
AIR="$OUTDIR/$BASE.air"
METALLIB="$OUTDIR/$BASE.metallib"

# 1. HLSL -> SPIR-V via glslangValidator with the HLSL frontend (-D).
echo "[glslang] $HLSL_PATH -> $SPV"
glslangValidator -D -e "$ENTRY" -V -S "$GLSLANG_STAGE" \
    "$HLSL_PATH" -o "$SPV"

# 2. SPIR-V -> MSL via spirv-cross.
echo "[spirv-cross] $SPV -> $MSL"
spirv-cross "$SPV" --msl --msl-version 20200 --output "$MSL"

# 3. MSL -> AIR (Metal Air intermediate) via xcrun metal.
echo "[metal] $MSL -> $AIR"
xcrun -sdk iphoneos metal -c "$MSL" -o "$AIR"

# 4. AIR -> .metallib via xcrun metallib.
echo "[metallib] $AIR -> $METALLIB"
xcrun -sdk iphoneos metallib "$AIR" -o "$METALLIB"

echo ""
echo "ok: $METALLIB ($(stat -f%z "$METALLIB" 2>/dev/null || stat -c%s "$METALLIB") bytes)"
