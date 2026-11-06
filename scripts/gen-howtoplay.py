#!/usr/bin/env python3
"""Pull the How to Play pages out of upstream.

UIScene_HowToPlay / UIScene_HowToPlayMenu need Iggy to build, so their
tables are copied into upstream/Minecraft.Client/iOS_HowToPlay.inc for
Game/MCLEHowToPlay.cpp: the label enum and page def struct from the
header, which SWF label each one is, the page and Flash page tables, the
constructor's label text, and the menu's button to page table.

Idempotent, rewrites the .inc every run.
"""
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
UI = REPO / "upstream" / "Minecraft.Client" / "Common" / "UI"
HDR = UI / "UIScene_HowToPlay.h"
CPP = UI / "UIScene_HowToPlay.cpp"
MENU = UI / "UIScene_HowToPlayMenu.cpp"
OUT = REPO / "upstream" / "Minecraft.Client" / "iOS_HowToPlay.inc"


def read(p):
    if not p.exists():
        sys.exit(f"missing: {p}")
    return p.read_text(encoding="utf-8", errors="replace").replace("\r\n", "\n")


def between(src, start, end, what):
    if src.count(start) != 1:
        sys.exit(f"{what}: start not found once")
    a = src.index(start)
    b = src.index(end, a)
    return src[a:b + len(end)]


hdr = read(HDR)
cpp = read(CPP)
menu = read(MENU)

label_enum = between(hdr, "\tenum EHowToPlayLabelControls", "};", "label enum")
page_struct = between(hdr, "\tstruct SHowToPlayPageDef", "};", "page def struct")

names = re.findall(r'UI_MAP_ELEMENT\(\s*m_labels\[\s*(eHowToPlay_Label\w+)\s*\]\s*,\s*"(\w+)"\s*\)', hdr)
if len(names) < 10:
    sys.exit("label names not found")

page_defs = between(cpp, "static UIScene_HowToPlay::SHowToPlayPageDef gs_aPageDefs[ eHowToPlay_NumPages ] =", "};", "page defs")
flash_map = between(cpp, "int gs_pageToFlashMapping[eHowToPlay_NumPages] =", "};", "flash mapping")
labels = between(cpp, "\twstring inventoryString = app.GetString(IDS_INVENTORY);", "m_labels[ eHowToPlay_LabelTrading_VillagerOffers].init(wsTemp.c_str());", "label init")
scene_map = between(menu, "unsigned int UIScene_HowToPlayMenu::m_uiHTPSceneA[]=", "};", "menu scene table")
scene_map = scene_map.replace("UIScene_HowToPlayMenu::m_uiHTPSceneA", "gs_uiHTPSceneA")

out = []
out.append("// UIScene_HowToPlay.h\nstruct UIScene_HowToPlay {\n" + label_enum + "\n" + page_struct + "\n")
out.append("\t// The constructor's label text\n\tstatic void initLabels(MCLEHowToPlayLabel *m_labels) {\n" + labels + "\n\t}\n};\n")
out.append("// UI_MAP_ELEMENT for the labels\nstatic const struct { int label; const char *name; } gs_labelNames[] = {\n" +
           "".join(f"\t{{ UIScene_HowToPlay::{e}, \"{n}\" }},\n" for e, n in names) + "};\n")
out.append(page_defs.replace("static UIScene_HowToPlay::", "static const UIScene_HowToPlay::") + "\n")
out.append(flash_map.replace("int gs_pageToFlashMapping", "static const int gs_pageToFlashMapping") + "\n")
out.append(scene_map.replace("unsigned int gs_uiHTPSceneA", "static const unsigned int gs_uiHTPSceneA") + "\n")
OUT.write_text("\n".join(out), encoding="utf-8")
print(f"wrote {OUT} ({len(names)} labels)")
