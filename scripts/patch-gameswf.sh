#!/usr/bin/env bash
# Apply in-tree patches to the vendored GameSWF submodule so it compiles for
# iOS (libc++ clang, C++17).
#
# Patches applied:
#   1. Rename utility.h's inline fmax/fmin helpers to gs_fmax / gs_fmin.
#      On libc++, std::fmax is pulled into global scope via <cmath>, which
#      collides with GameSWF's same-named float inline. Renaming is cleaner
#      than fighting linkage rules.
#
# Idempotent: rerunning is a no-op once patched.

set -euo pipefail

ROOT="${1:-third_party/gameswf/GameSwfPort/GameSwf}"

if [[ ! -d "$ROOT" ]]; then
    echo "error: $ROOT not found. Is the gameswf submodule initialized?"
    exit 1
fi

# Rename fmax/fmin -> gs_fmax/gs_fmin everywhere in GameSWF source.
# Use word-boundary sed so we don't touch std::fmax or fmaxf or similar.
find "$ROOT" \( -name "*.cpp" -o -name "*.h" -o -name "*.hpp" \) -type f -print0 \
  | while IFS= read -r -d '' f; do
      # -i '' for BSD sed (macOS); -i for GNU. Detect and adapt.
      if sed --version >/dev/null 2>&1; then
          sed -i -E 's/\bfmax\b/gs_fmax/g; s/\bfmin\b/gs_fmin/g' "$f"
      else
          sed -i '' -E 's/\bfmax\b/gs_fmax/g; s/\bfmin\b/gs_fmin/g' "$f"
      fi
  done

echo "gameswf patch complete: fmax -> gs_fmax, fmin -> gs_fmin"
