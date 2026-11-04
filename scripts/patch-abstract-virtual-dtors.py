#!/usr/bin/env python3
"""Give the abstract base classes clang warns about a virtual destructor.

clang flags every `delete` on these as -Wdelete-abstract-non-virtual-dtor,
and at runtime the deleting destructor it emits is a trap (brk #1), same as
the BaseMobSpawner crash. MSVC just calls the base dtor, so it never showed
on Windows.

Idempotent.
"""
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
UP = REPO / "upstream"

TARGETS = [
    ("Minecraft.World/Icon.h", "Icon"),
    ("Minecraft.World/EntitySelector.h", "EntitySelector"),
    ("Minecraft.World/Position.h", "Position"),
    ("Minecraft.World/StructureStart.h", "StructureStart"),
    ("Minecraft.Client/MemTextureProcessor.h", "MemTextureProcessor"),
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
