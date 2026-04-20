#!/usr/bin/env bash
# Apply in-tree patches to the vendored GameSWF submodule so it compiles for
# iOS (libc++ clang, C++17).
#
# Rationale: GameSWF predates C++11 and its utility.h redeclares libc math
# functions like fmax / fmin without noexcept, which libc++ rejects. Rather
# than forking the submodule, we patch it at build time. Idempotent.

set -euo pipefail

ROOT="${1:-third_party/gameswf/GameSwfPort/GameSwf}"
UTIL="$ROOT/base/utility.h"

if [[ ! -f "$UTIL" ]]; then
    echo "error: $UTIL not found. Is the gameswf submodule initialized?"
    exit 1
fi

# Patch fmax / fmin (and their 64-bit cousins if present) to add noexcept.
# Idempotent: if already patched, the sed is a no-op.
python3 - "$UTIL" <<'PY'
import sys, re, pathlib
p = pathlib.Path(sys.argv[1])
src = p.read_text()

# Matches: inline <type> <name>(<args>) { ... }
# We add ' noexcept' before the body when name is fmax/fmin and it's missing.
def patch(match):
    prefix, name, args, body = match.group(1), match.group(2), match.group(3), match.group(4)
    if ' noexcept' in prefix or 'noexcept' in args:
        return match.group(0)
    return f"{prefix}{name}({args}) noexcept {body}"

new = re.sub(
    r"(inline\s+(?:float|double)\s+)(fmax|fmin)\s*\(([^)]*)\)\s*(\{[^}]*\})",
    patch,
    src,
)
if new != src:
    p.write_text(new)
    print(f"patched {p}")
else:
    print(f"already patched: {p}")
PY

echo "gameswf patch complete"
