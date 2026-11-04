#!/usr/bin/env python3
"""No dynamic_pointer_cast<LocalPlayer> on iOS.

LocalPlayer.cpp isn't built (the local player is the server's
ServerPlayer), so there's no typeinfo for it to cast with, and the cast
could only ever give nullptr.

LevelRenderer::renderEntities: only casts when the camera player is
eTYPE_LOCALPLAYER, which a ServerPlayer never is, so it's just nullptr.

ItemInHandRenderer::render: the split screen fudge stays off, the hand
sway reads LocalPlayer's xBob / yBob from Game/MCLEPlayerInput.cpp, and the
Display Hand setting is read for pad 0 instead of through the null player.

Idempotent.
"""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
CLIENT = REPO / "upstream" / "Minecraft.Client"


def patch(path, marker, edits):
    src = path.read_text(encoding="utf-8", errors="replace")
    if marker in src:
        print(f"already patched: {path}")
        return
    nl = "\r\n" if "\r\n" in src else "\n"
    done = 0
    for old, new, count in edits:
        old = old.replace("\n", nl)
        new = new.replace("\n", nl)
        if src.count(old) != count:
            print(f"expected {count} in {path}: {old.splitlines()[0]}")
            sys.exit(1)
        src = src.replace(old, new)
        done += count
    path.write_text(src, encoding="utf-8", errors="replace", newline="")
    print(f"{path.name}: {done} edits")


LR_CAST = ("\t\t\tshared_ptr<LocalPlayer> localplayer = mc->cameraTargetPlayer->instanceof(eTYPE_LOCALPLAYER) ? "
           "dynamic_pointer_cast<LocalPlayer>(mc->cameraTargetPlayer) : nullptr;")
patch(CLIENT / "LevelRenderer.cpp", "shared_ptr<LocalPlayer> localplayer = nullptr;", [
    (LR_CAST + "\n",
     "#ifdef __APPLE_IOS__\n"
     "\t\t\tshared_ptr<LocalPlayer> localplayer = nullptr;\n"
     "#else\n" + LR_CAST + "\n#endif\n", 1),
])

IIH_CAST = "\tshared_ptr<LocalPlayer> localPlayer = dynamic_pointer_cast<LocalPlayer>(player);"
IIH_BOB = ("    if (localPlayer)\n"
           "\t{\n"
           "        float xrr = localPlayer->xBobO + (localPlayer->xBob - localPlayer->xBobO) * a;\n"
           "        float yrr = localPlayer->yBobO + (localPlayer->yBob - localPlayer->yBobO) * a;\n")
patch(CLIENT / "ItemInHandRenderer.cpp", "mcle_player_bob(", [
    (IIH_CAST + "\n",
     "#ifdef __APPLE_IOS__\n"
     "\tshared_ptr<LocalPlayer> localPlayer = nullptr;\n"
     "#else\n" + IIH_CAST + "\n#endif\n", 1),
    (IIH_BOB,
     "#ifdef __APPLE_IOS__\n"
     "    {\n"
     "        float xrr, yrr;\n"
     "        mcle_player_bob(a, &xrr, &yrr);\n"
     "#else\n" + IIH_BOB + "#endif\n", 1),
    ("app.GetGameSettings(localPlayer->GetXboxPad(),eGameSetting_DisplayHand)",
     "app.GetGameSettings(0,eGameSetting_DisplayHand)", 2),
    ("#include \"MultiPlayerLocalPlayer.h\"\n",
     "#include \"MultiPlayerLocalPlayer.h\"\n"
     "#ifdef __APPLE_IOS__\n"
     "void mcle_player_bob(float a, float *xBob, float *yBob);\n"
     "#endif\n", 1),
])
