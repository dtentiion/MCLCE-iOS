#!/usr/bin/env python3
"""Pull the Tutorial and the tasks that use the UI out of upstream.

Tutorial.cpp, ChoiceTask.cpp, InfoTask.cpp, ChangeStateConstraint.cpp and
XuiCraftingTask.cpp include the Iggy UI.h, the Windows64 keyboard or the
ClientConnection, so they can't be built as they are. Their bodies are
copied into upstream/Minecraft.Client/iOS_Tutorial.inc and compiled by
Game/MCLETutorial.cpp against a stand-in for the `ui` calls they make,
which go to Game/MCLETutorialPopup.cpp's UIComponent_TutorialPopup.

What changes:
- The tutorial's completion bits come from the settings store instead of
  the profile's GAME_SETTINGS (mcle_tutorial_profile_data).
- The tasks read the pad as the tutorial's own. mc->player is the
  ServerPlayer aliased as a MultiplayerLocalPlayer, so GetXboxPad would
  read whatever ServerPlayer has at LocalPlayer's m_iPad.
- ChoiceTask::isCompleted returns false with a menu up instead of running
  off the end of the function.
- ChangeStateConstraint's PlayerInfoPacket goes to the server's
  handlePlayerInfo for this same player, there's no client connection to
  send it on, and the game mode it checks for is the tutorial's.
- XuiCraftingTask finds the crafting menu through the scene the
  tutorial was given (mcle_tutorial_crafting_scene).

Idempotent, rewrites the .inc every run.
"""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
TUT = REPO / "upstream" / "Minecraft.Client" / "Common" / "Tutorial"
OUT = REPO / "upstream" / "Minecraft.Client" / "iOS_Tutorial.inc"


def fail(msg):
    print(msg)
    sys.exit(1)


def body(name):
    """The file after its includes."""
    path = TUT / name
    src = path.read_text(encoding="utf-8", errors="replace").replace("\r\n", "\n")
    lines = src.split("\n")
    last = -1
    for i, line in enumerate(lines):
        if line.lstrip().startswith("#include"):
            last = i
    if last < 0:
        fail(f"no includes in {path}")
    # an #if around the last include closes after it
    rest = lines[last + 1:]
    while rest and rest[0].strip() in ("#endif", ""):
        rest = rest[1:]
    return "\n".join(rest)


def replace(text, old, new, count, where):
    n = text.count(old)
    if n != count:
        fail(f"expected {count} of {old.strip()!r} in {where}, found {n}")
    return text.replace(old, new)


def replace_block(text, head, new_line, where):
    """Swap the statement `head` starts and its braced block for new_line."""
    at = text.find(head)
    if at < 0:
        return text, False
    line_start = text.rfind("\n", 0, at) + 1
    indent = text[line_start:at]
    i = text.index("{", at)
    depth = 0
    while True:
        c = text[i]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                break
        i += 1
    line_end = text.index("\n", i) + 1
    return text[:line_start] + indent + new_line + "\n" + text[line_end:], True


# Tutorial.cpp
tutorial = body("Tutorial.cpp")
tutorial = replace(tutorial, "static_cast<GAME_SETTINGS *>(ProfileManager.GetGameDefinedProfileData(",
                   "static_cast<GAME_SETTINGS *>(mcle_tutorial_profile_data(", 3, "Tutorial.cpp")
tutorial = tutorial.replace("GAME_SETTINGS", "MCLETutorialProfile")

# ChoiceTask.cpp / InfoTask.cpp
choice = body("ChoiceTask.cpp")
choice = replace(choice, "pMinecraft->player->GetXboxPad()", "tutorial->getPad()", 2, "ChoiceTask.cpp")
# With a menu up isCompleted runs off its end; clang takes that path as one
# that never happens. It's waiting on handleUIInput, so not completed yet.
choice = replace(choice,
                 "\t\t// If a menu is displayed, then we use the handleUIInput to complete the task\n\t}\n",
                 "\t\t// If a menu is displayed, then we use the handleUIInput to complete the task\n"
                 "\t\treturn false;\n\t}\n", 1, "ChoiceTask.cpp")
info = body("InfoTask.cpp")
info = replace(info, "pMinecraft->player->GetXboxPad()", "tutorial->getPad()", 3, "InfoTask.cpp")

# ChangeStateConstraint.cpp
change = body("ChangeStateConstraint.cpp")
SEND = "if(player != nullptr && player->connection && player->connection->getNetworkPlayer() != nullptr)"
sent = 0
while True:
    change, found = replace_block(change, SEND, "if(player != nullptr) mcle_tutorial_player_info(playerPrivs);",
                                  "ChangeStateConstraint.cpp")
    if not found:
        break
    sent += 1
if sent != 3:
    fail(f"expected 3 PlayerInfoPacket sends in ChangeStateConstraint.cpp, found {sent}")
change = replace(change, "if(minecraft->localgameModes[iPad] != nullptr)", "if(mcle_tutorial() != nullptr)", 1,
                 "ChangeStateConstraint.cpp")
if "->connection" in change:
    fail("connection use left in ChangeStateConstraint.cpp")

# XuiCraftingTask.cpp
crafting = body("XuiCraftingTask.cpp")
crafting = replace(crafting,
                   "UIScene_CraftingMenu *craftScene = reinterpret_cast<UIScene_CraftingMenu *>(tutorial->getScene());",
                   "IUIScene_CraftingMenu *craftScene = mcle_tutorial_crafting_scene(tutorial->getScene());", 1,
                   "XuiCraftingTask.cpp")

parts = [
    ("Tutorial.cpp", tutorial),
    ("ChoiceTask.cpp", choice),
    ("InfoTask.cpp", info),
    ("ChangeStateConstraint.cpp", change),
    ("XuiCraftingTask.cpp", crafting),
]
out = ""
for name, text in parts:
    out += f"// ---- {name}\n" + text.strip("\n") + "\n\n"
OUT.write_text(out, encoding="utf-8")
print(f"wrote {OUT} ({out.count(chr(10))} lines)")
