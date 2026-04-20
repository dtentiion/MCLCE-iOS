#!/usr/bin/env bash
# Convert one or more .iggy files to standard .swf using the JPEXS Free Flash
# Decompiler command-line tool.
#
# Why: the LCE UI is authored in Flash and shipped as .iggy files (a 4J
# wrapper around SWF). GameSWF cannot read .iggy directly but it can read
# .swf. Converting at build time lets us stay on an actively maintained
# SWF runtime without writing an iggy parser.
#
# Requires: Java 8+ and ffdec.jar (JPEXS CLI). Download from
# https://github.com/jindrapetrik/jpexs-decompiler/releases
#
# This script is BUILD INFRASTRUCTURE. It does NOT run in CI because the
# .iggy source files are game assets that are not redistributed here.
# You run it locally if / when you have a set of .iggy files to convert.
#
# Usage:
#   ./scripts/iggy-to-swf.sh <ffdec.jar> <input-dir> <output-dir>
#
# Example:
#   ./scripts/iggy-to-swf.sh \
#     ~/Downloads/ffdec.jar \
#     ~/games/lce/Media/iggy \
#     out/swf

set -euo pipefail

FFDEC_JAR="${1:?usage: iggy-to-swf.sh <ffdec.jar> <input-dir> <output-dir>}"
INPUT_DIR="${2:?missing input dir}"
OUTPUT_DIR="${3:?missing output dir}"

if ! command -v java >/dev/null 2>&1; then
    echo "error: java not found. Install Java 8 or newer."
    exit 1
fi

if [[ ! -f "$FFDEC_JAR" ]]; then
    echo "error: ffdec.jar not found at $FFDEC_JAR"
    echo "download from https://github.com/jindrapetrik/jpexs-decompiler/releases"
    exit 1
fi

mkdir -p "$OUTPUT_DIR"

shopt -s nullglob
files=("$INPUT_DIR"/*.iggy "$INPUT_DIR"/*.IGGY)
if [[ ${#files[@]} -eq 0 ]]; then
    echo "no .iggy files found in $INPUT_DIR"
    exit 1
fi

for iggy in "${files[@]}"; do
    name=$(basename "$iggy" .iggy)
    name=$(basename "$name" .IGGY)
    swf_out="$OUTPUT_DIR/$name.swf"
    echo "[iggy->swf] $iggy -> $swf_out"
    # JPEXS CLI: --iggyToSwf takes an iggy file path and a swf output path.
    java -jar "$FFDEC_JAR" --iggyToSwf "$iggy" "$swf_out"
done

echo ""
echo "ok: converted ${#files[@]} iggy files to $OUTPUT_DIR"
