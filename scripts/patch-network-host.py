#!/usr/bin/env python3
"""The host's side of LAN play, as the Windows64 build has it.

Hosting in upstream is in #ifdef _WINDOWS64 blocks: the platform network
manager opening the listening socket and advertising when a game is hosted,
its DoWork picking up the players who dropped, the player list giving a
small id back once a connection is done with, the pending connection
turning away a name that's taken. Those blocks are switched on here too.

The client's blocks stay Windows64's: joining, searching for games,
split-screen. There is no client here yet.

Windows64_Xuid.h has the ids. The host's legacy id is all a host needs
from it, the uid.dat code under it is the client's and uses the Windows
API, so only the first part is opened.

Idempotent.
"""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
CLIENT = REPO / "upstream" / "Minecraft.Client"
MARK = "MCLE_iOS_NET_HOST"
ON = "#if defined(_WINDOWS64) || defined(__APPLE_IOS__) // MCLE_iOS_NET_HOST\n"


def patch(path, pairs):
    if not path.exists():
        sys.exit(f"missing: {path}")
    src = path.read_text(encoding="utf-8", errors="replace")
    if MARK in src:
        print(f"already patched: {path}")
        return
    nl = "\r\n" if "\r\n" in src else "\n"
    for old, new in pairs:
        old = old.replace("\n", nl)
        new = new.replace("\n", nl)
        if src.count(old) != 1:
            sys.exit(f"anchor found {src.count(old)} times in {path.name}: {old[:70]!r}")
        src = src.replace(old, new)
    path.write_text(src, encoding="utf-8", errors="replace", newline="")
    print(f"patched: {path}")


def on(rest):
    """An #ifdef _WINDOWS64 followed by rest, switched on."""
    return ("#ifdef _WINDOWS64\n" + rest, ON + rest)


patch(CLIENT / "Windows64" / "Windows64_Xuid.h", [
    on("\n#include <string>\n"),
    ("#include <Windows.h>\n", "#ifdef _WINDOWS64\n#include <Windows.h>\n#endif\n"),
    ("\t// ./uid.dat\n\tinline bool BuildUidFilePath(",
     "#ifdef _WINDOWS64\n\t// ./uid.dat\n\tinline bool BuildUidFilePath("),
    ("\t\ts_cached = true;\n\t\treturn s_xuid;\n\t}\n}\n",
     "\t\ts_cached = true;\n\t\treturn s_xuid;\n\t}\n#endif\n}\n"),
])

patch(CLIENT / "Common" / "Network" / "PlatformNetworkManagerStub.h", [
    on("\tbool\t\t\tm_bJoinPending;\n"),
])

patch(CLIENT / "Common" / "Network" / "PlatformNetworkManagerStub.cpp", [
    on('#include "../../Windows64/Network/WinsockNetLayer.h"\n'),
    on("\tm_bJoinPending = false;\n"),
    # DoWork: the host's half, up to where the client's starts
    on("\textern QNET_STATE _iQNetStubState;\n\tif (_iQNetStubState == QNET_STATE_SESSION_STARTING && app.GetGameStarted())\n"),
    ("\t// Client-side host disconnect detection:\n",
     "#endif\n#ifdef _WINDOWS64\n\t// Client-side host disconnect detection:\n"),
    on("\tMinecraftServer* server = MinecraftServer::getInstance();\n"),
    on("\tWinsockNetLayer::StopAdvertising();\n"),
    on("\tWinsockNetLayer::Shutdown();\n"),
    on("\tIQNet::m_player[0].m_smallId = 0;\n"),
    on("\tint port = WIN64_NET_DEFAULT_PORT;\n"),
    on("\textern QNET_STATE _iQNetStubState;\n\t_iQNetStubState = QNET_STATE_GAME_PLAY;\n"),
])

patch(CLIENT / "PlayerList.cpp", [
    on('#include "Windows64/Network/WinsockNetLayer.h"\n'),
    ('#if defined(_XBOX) || defined(_WINDOWS64)\n#include "Xbox/Network/NetworkPlayerXbox.h"\n',
     '#if defined(_XBOX) || defined(_WINDOWS64) || defined(__APPLE_IOS__) // MCLE_iOS_NET_HOST\n#include "Xbox/Network/NetworkPlayerXbox.h"\n'),
    on("\tif (networkPlayer != nullptr)\n\t{\n\t\tNetworkPlayerXbox* nxp"),
    on("\t{\n\t\t// Use packet-supplied identity from LoginPacket.\n"),
    on("\t\t// The old Connection's read/write threads are now dead"),
])

# No wait between one player's chunks and the next's, as on Windows64
patch(CLIENT / "MinecraftServer.h", [
    ("#if defined(_WINDOWS64)\n#define MINECRAFT_SERVER_SLOW_QUEUE_DELAY 0",
     "#if defined(_WINDOWS64) || defined(__APPLE_IOS__) // MCLE_iOS_NET_HOST\n#define MINECRAFT_SERVER_SLOW_QUEUE_DELAY 0"),
])

patch(CLIENT / "PendingConnection.cpp", [
    on("bool g_bRejectDuplicateNames = true;\n"),
    on("\telse if (g_bRejectDuplicateNames)\n"),
])
