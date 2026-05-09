#!/usr/bin/env python3
"""Replace `minecraft->player->m_iScreenSection` with VIEWPORT_TYPE_FULLSCREEN
in upstream Gui.cpp. Our ServerPlayer is not a LocalPlayer (which is the
class that owns m_iScreenSection), so the access reads garbage memory.

iOS is single-player, no split-screen, so the fullscreen branch is the only
one that ever fires anyway. Hardcoding it here keeps Gui::render parity-
correct in behaviour while sidestepping the field-layout mismatch.

Idempotent.
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
TARGET = REPO_ROOT / "upstream" / "Minecraft.Client" / "Gui.cpp"

if not TARGET.exists():
    sys.exit(f"missing: {TARGET}")

src = TARGET.read_text(encoding="utf-8", errors="replace")
if "MCLE_iOS_SCREEN_SECTION" in src:
    print(f"already patched: {TARGET}")
    sys.exit(0)

# Replace every occurrence of `minecraft->player->m_iScreenSection`
# with the FULLSCREEN constant. Use a static const so the type matches
# whatever the surrounding expression expects.
needle = "minecraft->player->m_iScreenSection"
replacement = "C4JRender::VIEWPORT_TYPE_FULLSCREEN /* MCLE_iOS_SCREEN_SECTION */"
count = src.count(needle)
if count == 0:
    sys.exit(f"anchor `{needle}` not found in {TARGET}")
src = src.replace(needle, replacement)
TARGET.write_text(src, encoding="utf-8")
print(f"patched {count} site(s) in {TARGET}")
