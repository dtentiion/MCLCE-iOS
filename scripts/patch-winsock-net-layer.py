#!/usr/bin/env python3
"""Windows64/Network/WinsockNetLayer builds here too.

It is the layer LAN play runs on in the Windows64 build (LCEMP's,
https://github.com/LCEMP/LCEMP): TCP for the game, a UDP broadcast to be
found by. The Winsock names it uses come from compat_headers/WinSock2.h,
and what's left is patched below:

- the files are inside #ifdef _WINDOWS64
- the advert's host name is wchar_t on the wire, which is 16 bits on
  Windows and 32 here, so the struct takes uint16_t and the name is copied
  in and out a unit at a time
- sprintf_s in its array form, lengths that are socklen_t, a receive
  timeout that's a timeval
- JoinGame tells the client's Minecraft that the connection failed. There
  is no client here to tell yet.

Idempotent.
"""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
NET = REPO / "upstream" / "Minecraft.Client" / "Windows64" / "Network"
MARK = "MCLE_iOS_NET"


def patch(path, pairs):
    if not path.exists():
        sys.exit(f"missing: {path}")
    src = path.read_text(encoding="utf-8", errors="replace")
    if MARK in src:
        print(f"already patched: {path}")
        return
    nl = "\r\n" if "\r\n" in src else "\n"
    for old, new, count in pairs:
        old = old.replace("\n", nl)
        new = new.replace("\n", nl)
        if src.count(old) < 1 or (count and src.count(old) != count):
            sys.exit(f"anchor found {src.count(old)} times in {path.name}: {old[:60]!r}")
        src = src.replace(old, new) if count else src.replace(old, new, 1)
    path.write_text(src, encoding="utf-8", errors="replace", newline="")
    print(f"patched: {path}")


patch(NET / "WinsockNetLayer.h", [
    ("#ifdef _WINDOWS64\n",
     "#if defined(_WINDOWS64) || defined(__APPLE_IOS__) // MCLE_iOS_NET\n", 1),
    ('#include "..\\..\\..\\Minecraft.World\\DisconnectPacket.h"\n',
     '#include "../../../Minecraft.World/DisconnectPacket.h"\n', 1),
    # the first hostName is the advert's, the second the discovered session's
    ("\tWORD gamePort;\n\twchar_t hostName[32];\n",
     "\tWORD gamePort;\n"
     "#ifdef __APPLE_IOS__\n"
     "\tuint16_t hostName[32]; // wchar_t where this layout is from\n"
     "#else\n"
     "\twchar_t hostName[32];\n"
     "#endif\n", 1),
])

patch(NET / "WinsockNetLayer.cpp", [
    ("#ifdef _WINDOWS64\n",
     "#if defined(_WINDOWS64) || defined(__APPLE_IOS__) // MCLE_iOS_NET\n", 1),
    ('#include "../4JLibs/inc/4J_Profile.h"\n',
     "#ifndef __APPLE_IOS__\n"
     '#include "../4JLibs/inc/4J_Profile.h"\n'
     "#endif\n", 1),
    ("static bool RecvExact(SOCKET sock, BYTE* buf, int len);\n",
     "static bool RecvExact(SOCKET sock, BYTE* buf, int len);\n"
     "\n"
     "#ifdef __APPLE_IOS__\n"
     "// The advert's host name, 16 bit units on the wire\n"
     "static void mcleNameToWire(uint16_t *dst, const wchar_t *src)\n"
     "{\n"
     "\tint i = 0;\n"
     "\tfor (; i < 31 && src[i] != 0; ++i) dst[i] = static_cast<uint16_t>(src[i]);\n"
     "\tfor (; i < 32; ++i) dst[i] = 0;\n"
     "}\n"
     "\n"
     "static void mcleNameFromWire(wchar_t *dst, const uint16_t *src)\n"
     "{\n"
     "\tint i = 0;\n"
     "\tfor (; i < 31 && src[i] != 0; ++i) dst[i] = static_cast<wchar_t>(src[i]);\n"
     "\tfor (; i < 32; ++i) dst[i] = 0;\n"
     "}\n"
     "#endif\n", 1),
    ('\tsprintf_s(portStr, "%d", port);\n',
     '\tsprintf_s(portStr, sizeof(portStr), "%d", port);\n', 2),
    ('\tsprintf_s(portStr, "%d", s_joinPort);\n',
     '\tsprintf_s(portStr, sizeof(portStr), "%d", s_joinPort);\n', 1),
    ('\tsprintf_s(portStr, "%d", g_Win64MultiplayerPort);\n',
     '\tsprintf_s(portStr, sizeof(portStr), "%d", g_Win64MultiplayerPort);\n', 1),
    ("\t\t\tMinecraft::GetInstance()->connectionDisconnected(ProfileManager.GetPrimaryPad(), (DisconnectPacket::eDisconnectReason)reason);\n",
     "#ifndef __APPLE_IOS__\n"
     "\t\t\tMinecraft::GetInstance()->connectionDisconnected(ProfileManager.GetPrimaryPad(), (DisconnectPacket::eDisconnectReason)reason);\n"
     "#endif\n", 1),
    ("\t\tint remoteAddressLength = sizeof(remoteAddress);\n",
     "\t\tsocklen_t remoteAddressLength = sizeof(remoteAddress);\n", 1),
    ("\t\tint senderLen = sizeof(senderAddr);\n",
     "\t\tsocklen_t senderLen = sizeof(senderAddr);\n", 1),
    ("\twcsncpy_s(s_advertiseData.hostName, 32, hostName, _TRUNCATE);\n",
     "#ifdef __APPLE_IOS__\n"
     "\tmcleNameToWire(s_advertiseData.hostName, hostName);\n"
     "#else\n"
     "\twcsncpy_s(s_advertiseData.hostName, 32, hostName, _TRUNCATE);\n"
     "#endif\n", 1),
    ("\tDWORD timeout = 500;\n"
     "\tsetsockopt(s_discoverySock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));\n",
     "#ifdef __APPLE_IOS__\n"
     "\tstruct timeval timeout;\n"
     "\ttimeout.tv_sec = 0;\n"
     "\ttimeout.tv_usec = 500 * 1000;\n"
     "#else\n"
     "\tDWORD timeout = 500;\n"
     "#endif\n"
     "\tsetsockopt(s_discoverySock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));\n", 1),
    ("\t\t\t\twcsncpy_s(s_discoveredSessions[i].hostName, 32, broadcast->hostName, _TRUNCATE);\n",
     "#ifdef __APPLE_IOS__\n"
     "\t\t\t\tmcleNameFromWire(s_discoveredSessions[i].hostName, broadcast->hostName);\n"
     "#else\n"
     "\t\t\t\twcsncpy_s(s_discoveredSessions[i].hostName, 32, broadcast->hostName, _TRUNCATE);\n"
     "#endif\n", 1),
    ("\t\t\twcsncpy_s(session.hostName, 32, broadcast->hostName, _TRUNCATE);\n",
     "#ifdef __APPLE_IOS__\n"
     "\t\t\tmcleNameFromWire(session.hostName, broadcast->hostName);\n"
     "#else\n"
     "\t\t\twcsncpy_s(session.hostName, 32, broadcast->hostName, _TRUNCATE);\n"
     "#endif\n", 1),
])
