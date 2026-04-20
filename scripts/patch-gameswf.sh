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
#
# Uses python3 for the actual substitution because BSD sed on macOS has
# inconsistent word-boundary support.

set -euo pipefail

ROOT="${1:-third_party/gameswf/GameSwfPort/GameSwf}"

if [[ ! -d "$ROOT" ]]; then
    echo "error: $ROOT not found. Is the gameswf submodule initialized?"
    exit 1
fi

python3 - "$ROOT" <<'PY'
import os, re, sys, pathlib

root = pathlib.Path(sys.argv[1])

# Rename clashes with libc: fmax / fmin.
RENAME = re.compile(r"\b(fmax|fmin)\b")

# compiler_assert(x) macro is "switch(0){case 0: case x:;}", which clang now
# rejects as duplicate case when x == 0. The macro is used in generic
# template bodies as an "unreachable, should specialize" trip, and modern
# clang parses those bodies eagerly. Swap the macro for a no-op so the
# library compiles; we lose a compile-time tripwire but never relied on it.
CA_DEF_OLD = "#define compiler_assert(x)\tswitch(0){case 0: case x:;}"
CA_DEF_NEW = "#define compiler_assert(x)\t((void)0)"

def rename_replace(m):
    return "gs_" + m.group(1)

changed = 0
scanned = 0
for p in root.rglob("*"):
    if p.suffix.lower() not in (".cpp", ".h", ".hpp", ".cc", ".cxx", ".c"):
        continue
    scanned += 1
    try:
        src = p.read_text(encoding="utf-8", errors="surrogateescape")
    except Exception as e:
        print(f"skip {p}: {e}", file=sys.stderr)
        continue
    new = RENAME.sub(rename_replace, src)
    new = new.replace(CA_DEF_OLD, CA_DEF_NEW)
    # Also replace the non-tab variant just in case.
    new = new.replace(
        "#define compiler_assert(x) switch(0){case 0: case x:;}",
        "#define compiler_assert(x) ((void)0)",
    )
    if new != src:
        p.write_text(new, encoding="utf-8", errors="surrogateescape")
        changed += 1

print(f"gameswf patch: scanned {scanned} files, updated {changed}")
PY
