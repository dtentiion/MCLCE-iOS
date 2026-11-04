#!/usr/bin/env python3
"""Give the abstract base classes clang traps on a virtual destructor.

Deleting through a pointer to an abstract class with a non-virtual,
non-trivial destructor is UB, and clang turns the inlined destructor into
a trap (brk #1), same as the BaseMobSpawner crash. MSVC just calls the base
dtor, so it never showed on Windows.

Only classes whose destructor does something are listed. A trivial one
(Icon, Position, EntitySelector, MemTextureProcessor) compiles to a plain
free on clang too, which is what MSVC does. Making those virtual would run
derived dtors upstream never runs, e.g. ~StitchedTexture walks a null
frames list when loadUVs deletes the old clock/compass icons.

Idempotent.
"""
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
UP = REPO / "upstream"

TARGETS = [
    ("Minecraft.World/StructureStart.h", "StructureStart"),
    ("Minecraft.Client/Common/GameRules/GameRuleDefinition.h", "GameRuleDefinition"),
]

for rel, name in TARGETS:
    path = UP / rel
    if not path.exists():
        sys.exit(f"missing: {path}")
    src = path.read_text(encoding="utf-8", errors="replace")
    if re.search(r"virtual\s+~" + name + r"\s*\(", src):
        print(f"already patched: {rel}")
        continue

    # Declared but not virtual: just add the keyword
    new, n = re.subn(r"(\n[ \t]*)~" + name + r"\s*\(", r"\1virtual ~" + name + "(", src, count=1)
    if n == 0:
        # No dtor declared: add one under the first public: in the class body
        cls = re.search(r"\nclass " + name + r"\b[^;{]*\{", src)
        if not cls:
            sys.exit(f"class {name} not found in {rel}")
        pub = re.compile(r"\n[ \t]*public:[ \t]*\n").search(src, cls.end())
        if not pub:
            sys.exit(f"no public: in {name}")
        new = src[:pub.end()] + "\tvirtual ~" + name + "() {}\n" + src[pub.end():]

    path.write_text(new, encoding="utf-8", errors="replace", newline="")
    print(f"patched: {rel}")
