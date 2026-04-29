#!/usr/bin/env python3
"""Drop checkpoint prints into ConsoleSaveFileOriginal::closeHandle and
::finalizeWrite so the post-NBT-parse hang gets narrowed from "the
close path" to "this exact line in the close path".
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
TARGET = REPO_ROOT / "upstream" / "Minecraft.World" / "ConsoleSaveFileOriginal.cpp"

src = TARGET.read_text(encoding="utf-8", errors="replace")
if "CSFO_CKPT" in src:
    print(f"already patched: {TARGET}")
    sys.exit(0)

edits = [
    (
        "BOOL ConsoleSaveFileOriginal::closeHandle( FileEntry *file )\n{\n"
        "\tLockSaveAccess();\n"
        "\tfinalizeWrite();\n"
        "\tReleaseSaveAccess();\n"
        "\n"
        "\treturn TRUE;\n"
        "}",
        "BOOL ConsoleSaveFileOriginal::closeHandle( FileEntry *file )\n{\n"
        '\tapp.DebugPrintf("CSFO_CKPT closeHandle enter");\n'
        "\tLockSaveAccess();\n"
        '\tapp.DebugPrintf("CSFO_CKPT closeHandle locked");\n'
        "\tfinalizeWrite();\n"
        '\tapp.DebugPrintf("CSFO_CKPT closeHandle finalizeWrite done");\n'
        "\tReleaseSaveAccess();\n"
        '\tapp.DebugPrintf("CSFO_CKPT closeHandle released, returning");\n'
        "\n"
        "\treturn TRUE;\n"
        "}",
    ),
    (
        "void ConsoleSaveFileOriginal::finalizeWrite()\n{\n"
        "\tLockSaveAccess();",
        "void ConsoleSaveFileOriginal::finalizeWrite()\n{\n"
        '\tapp.DebugPrintf("CSFO_CKPT finalizeWrite enter");\n'
        "\tLockSaveAccess();\n"
        '\tapp.DebugPrintf("CSFO_CKPT finalizeWrite recursive-locked");',
    ),
    (
        "header.WriteHeader( pvSaveMem );\n"
        "\tReleaseSaveAccess();\n"
        "}\n"
        "\n"
        "void ConsoleSaveFileOriginal::MoveDataBeyond",
        '\tapp.DebugPrintf("CSFO_CKPT finalizeWrite calling WriteHeader");\n'
        "\theader.WriteHeader( pvSaveMem );\n"
        '\tapp.DebugPrintf("CSFO_CKPT finalizeWrite WriteHeader done");\n'
        "\tReleaseSaveAccess();\n"
        "}\n"
        "\n"
        "void ConsoleSaveFileOriginal::MoveDataBeyond",
    ),
]

patched = src
for old, new in edits:
    if old not in patched:
        sys.exit(f"anchor missing: {old[:80]!r}")
    patched = patched.replace(old, new, 1)

TARGET.write_text(patched, encoding="utf-8", newline="\n")
print(f"patched {TARGET}")
