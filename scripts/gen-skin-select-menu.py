#!/usr/bin/env python3
"""Pull UIScene_SkinSelectMenu and UIControl_PlayerSkinPreview out of upstream.

Both include the Iggy UI headers, so they can't be built as they are. The
skin and pack navigation, selecting a skin, the favourites and the preview's
model rendering are copied into upstream/Minecraft.Client/iOS_SkinSelectMenu.inc
and compiled by Game/MCLESkinSelectMenu.cpp, which declares the two classes
again with the controls standing in for Iggy's.

What's left out of UIScene_SkinSelectMenu:
- The calls that are only Iggy: setCharacterSelected, setCharacterLocked,
  the three tab labels and customDraw. MCLESkinSelectMenu.cpp has them.
- getMoviePath and updateComponents, the view controller loads the movie
  and hides the logo for the scene.
- HandleDLCInstalled, handleReload and the PS Vita touch input. Nothing
  calls them here.
- HandleDLCMountingComplete's bInGame, which nothing reads.

What changes in UIControl_PlayerSkinPreview:
- The constructor reads the screen size off Minecraft, which isn't made
  until a world loads. It gets the 1920x1080 the movie is laid out for.

Idempotent, rewrites the .inc every run.
"""
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
UI = REPO / "upstream" / "Minecraft.Client" / "Common" / "UI"
SCENE = UI / "UIScene_SkinSelectMenu.cpp"
PREVIEW = UI / "UIControl_PlayerSkinPreview.cpp"
OUT = REPO / "upstream" / "Minecraft.Client" / "iOS_SkinSelectMenu.inc"

SCENE_FUNCTIONS = [
    "UIScene_SkinSelectMenu",
    "updateTooltips",
    "tick",
    "handleAnimationEnd",
    "handleInput",
    "InputActionOK",
    "handleSkinIndexChanged",
    "getTextureId",
    "getNextSkinIndex",
    "getPreviousSkinIndex",
    "handlePackIndexChanged",
    "updatePackDisplay",
    "getNextPackIndex",
    "getPreviousPackIndex",
    "showNotOnlineDialog",
    "UnlockSkinReturned",
    "AddFavoriteSkin",
    "HandleDLCMountingComplete",
]

# HandleDLCMountingComplete's bInGame is only read under TO_BE_IMPLEMENTED,
# and there's no Minecraft to ask at the front end before a world loads
IN_GAME = "\tbool bInGame=(Minecraft::GetInstance()->level!=nullptr);\n"

PREVIEW_FUNCTIONS = [
    "UIControl_PlayerSkinPreview",
    "tick",
    "SetTexture",
    "SetFacing",
    "CycleNextAnimation",
    "CyclePreviousAnimation",
    "render",
    "bindTexture",
]

SCREEN_SIZE = """	Minecraft *pMinecraft=Minecraft::GetInstance();

	ScreenSizeCalculator ssc(pMinecraft->options, pMinecraft->width_phys, pMinecraft->height_phys);
	m_fScreenWidth=static_cast<float>(pMinecraft->width_phys);
	m_fRawWidth=static_cast<float>(ssc.rawWidth);
	m_fScreenHeight=static_cast<float>(pMinecraft->height_phys);
	m_fRawHeight=static_cast<float>(ssc.rawHeight);
"""

SCREEN_SIZE_IOS = """	// The movie's 1920x1080, there's no Minecraft yet at the front end
	mcle_skin_preview_screen(m_fScreenWidth, m_fRawWidth, m_fScreenHeight, m_fRawHeight);
"""


def fail(msg):
    print(msg)
    sys.exit(1)


def body_end(text, open_brace):
    """Index just past the brace matching the one at open_brace, skipping
    comments, strings and character literals."""
    depth = 0
    i = open_brace
    n = len(text)
    while i < n:
        c = text[i]
        if text.startswith("//", i):
            i = text.index("\n", i)
            continue
        if text.startswith("/*", i):
            i = text.index("*/", i) + 2
            continue
        if c == '"' or c == "'":
            i += 1
            while text[i] != c:
                i += 2 if text[i] == "\\" else 1
            i += 1
            continue
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1
    fail("unbalanced braces")


def definitions(text, cls):
    """(name, start, end) of each member function cls defines, in order."""
    head = re.compile(r"^(?:[A-Za-z_][^\n;{}]*?\b)?" + cls + r"::(~?\w+)\s*\(", re.M)
    out = []
    for m in head.finditer(text):
        open_brace = text.index("{", m.end())
        if ";" in text[m.end():open_brace]:
            continue
        end = body_end(text, open_brace)
        line_end = text.find("\n", end)
        out.append((m.group(1), m.start(), len(text) if line_end < 0 else line_end + 1))
    return out


def pick(text, cls, wanted):
    found = definitions(text, cls)
    names = {name for name, _, _ in found}
    missing = [w for w in wanted if w not in names]
    if missing:
        fail(f"{cls}: not found: {', '.join(missing)}")
    return "\n".join(text[s:e] for name, s, e in found if name in wanted)


def defines(text, prefix):
    lines = [l for l in text.split("\n") if l.startswith("#define " + prefix) or l.startswith("//#define " + prefix)]
    if not lines:
        fail(f"no {prefix} defines")
    return "\n".join(lines) + "\n"


scene = SCENE.read_text(encoding="utf-8", errors="replace").replace("\r\n", "\n")
preview = PREVIEW.read_text(encoding="utf-8", errors="replace").replace("\r\n", "\n")

TABLE = "const WCHAR *UIScene_SkinSelectMenu::wchDefaultNamesA[]=\n"
if scene.count(TABLE) != 1:
    fail(f"default skin names not found once in {SCENE}")
table_start = scene.index(TABLE)
table = scene[table_start:scene.index("};\n", table_start) + 3]

if preview.count(SCREEN_SIZE) != 1:
    fail(f"screen size block not found once in {PREVIEW}")
preview = preview.replace(SCREEN_SIZE, SCREEN_SIZE_IOS)

if scene.count(IN_GAME) != 1:
    fail(f"bInGame not found once in {SCENE}")
scene = scene.replace(IN_GAME, "")

out = "\n".join([
    defines(scene, "SKIN_SELECT_"),
    table,
    pick(scene, "UIScene_SkinSelectMenu", SCENE_FUNCTIONS),
    defines(preview, "SKIN_PREVIEW_"),
    pick(preview, "UIControl_PlayerSkinPreview", PREVIEW_FUNCTIONS),
])

code = "\n".join(l for l in out.split("\n") if not l.lstrip().startswith("//"))
for gone in ["IggyPlayerCallMethodRS", "getMoviePath", "m_parentLayer", "ScreenSizeCalculator"]:
    if gone in code:
        fail(f"{gone} left in the skin select body")

OUT.write_text(out, encoding="utf-8")
print(f"wrote {OUT} ({out.count(chr(10))} lines)")
