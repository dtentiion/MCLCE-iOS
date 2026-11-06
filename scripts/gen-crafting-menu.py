#!/usr/bin/env python3
"""Pull IUIScene_CraftingMenu's body out of upstream.

IUIScene_CraftingMenu.cpp is the crafting menu's recipe groups, the
horizontal and vertical recipe slots, the ingredients and the crafting
itself, but it includes the Iggy UI.h, so it can't be built as it is. Its
functions are copied into upstream/Minecraft.Client/iOS_CraftingMenu.inc
and compiled by Game/MCLECraftingMenu.cpp against the same `ui` stand-in
the container menus use.

What changes:
- The local player here is the server player. m_pPlayer is a LocalPlayer,
  so the player comes from mcle_crafting_player() instead.
- Crafting goes to mcle_craft_item, the server's
  PlayerConnection::handleCraftItem. handleCraftItem would send the
  recipe to a server that is this same player, and the client's own copy
  of the crafting (onCraftedBy, taking the ingredients, giving the item,
  the achievements) would then happen twice, so it's left out. The menu
  still plays the sound and refreshes the recipes after.
- FormatHTMLString is MCLEHowToPlay.cpp's copy of CMinecraftApp's.
- The tutorial is Game/MCLETutorial.cpp's, there's no game mode in
  Minecraft::localgameModes to ask for it.

Idempotent, rewrites the .inc every run.
"""
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SRC = REPO / "upstream" / "Minecraft.Client" / "Common" / "UI" / "IUIScene_CraftingMenu.cpp"
OUT = REPO / "upstream" / "Minecraft.Client" / "iOS_CraftingMenu.inc"

START = "Recipy::_eGroupType IUIScene_CraftingMenu::m_GroupTypeMapping4GridA[IUIScene_CraftingMenu::m_iMaxGroup2x2]=\n"

# The tutorial the game mode has, and the game mode's being there
TUTORIAL = [
    ("pMinecraft->localgameModes[getPad()] != nullptr", "mcle_tutorial() != nullptr", 1),
    ("pMinecraft->localgameModes[iPad] != nullptr", "mcle_tutorial() != nullptr", 2),
    ("pMinecraft->localgameModes[getPad()]->getTutorial()", "mcle_tutorial()", 1),
    ("pMinecraft->localgameModes[iPad]->getTutorial()", "mcle_tutorial()", 2),
]

CRAFT_CALL = "pMinecraft->localgameModes[iPad]->handleCraftItem(iRecipe,m_pPlayer);"
CLIENT_CRAFTED = ("pTempItemInst->onCraftedBy(m_pPlayer->level, dynamic_pointer_cast<Player>( "
                  "m_pPlayer->shared_from_this() ), pTempItemInst->count );")
CLIENT_TAKE_FROM = "// and remove those resources from your inventory"
CLIENT_TAKE_TO = "// We've used some ingredients from our inventory, so update the recipes we can make"


def fail(msg):
    print(msg)
    sys.exit(1)


src = SRC.read_text(encoding="utf-8", errors="replace").replace("\r\n", "\n")
if src.count(START) != 1:
    fail(f"group table not found once in {SRC}")
block = src[src.index(START):]

for old, new, count in TUTORIAL:
    if block.count(old) != count:
        fail(f"expected {count} of {old!r}, found {block.count(old)}")
    block = block.replace(old, new)

if block.count(CRAFT_CALL) != 2:
    fail(f"expected 2 handleCraftItem calls, found {block.count(CRAFT_CALL)}")
block = block.replace(CRAFT_CALL, "mcle_craft_item(iRecipe);")

lines = block.split("\n")
crafted = [i for i, l in enumerate(lines) if l.strip() == CLIENT_CRAFTED]
take_from = [i for i, l in enumerate(lines) if l.strip() == CLIENT_TAKE_FROM]
take_to = [i for i, l in enumerate(lines) if l.strip() == CLIENT_TAKE_TO]
if len(crafted) != 1 or len(take_from) != 1 or len(take_to) != 1 or not crafted[0] < take_from[0] < take_to[0]:
    fail("the client's own crafting isn't where it was")
indent = lines[take_from[0]][:len(lines[take_from[0]]) - len(lines[take_from[0]].lstrip())]
lines[take_from[0]:take_to[0]] = [
    indent + "// mcle_craft_item took the ingredients and gave the item, the way",
    indent + "// PlayerConnection::handleCraftItem does",
    "",
]
lines[crafted[0]] = (lines[crafted[0]][:len(lines[crafted[0]]) - len(lines[crafted[0]].lstrip())]
                     + "// onCraftedBy runs in mcle_craft_item")
block = "\n".join(lines)

if block.count("app.FormatHTMLString(") != 1:
    fail("FormatHTMLString call not found once")
block = block.replace("app.FormatHTMLString(", "mcle_format_html_string(")

if "m_pPlayer" not in block:
    fail("m_pPlayer not used any more")
block = re.sub(r"\bm_pPlayer\b", "mcle_crafting_player()", block)

code = "\n".join(l for l in block.split("\n") if not l.lstrip().startswith("//"))
if "localgameModes" in code or "TutorialMode" in code or "handleCraftItem" in code:
    fail("game mode code left in the crafting menu body")

OUT.write_text(block, encoding="utf-8")
print(f"wrote {OUT} ({block.count(chr(10))} lines)")
