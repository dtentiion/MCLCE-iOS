#!/usr/bin/env python3
"""Pull IUIScene_TradingMenu's body out of upstream.

IUIScene_TradingMenu.cpp is the trading menu's offer bar, the selected
offer's request slots and description, and the trade itself, but it
includes the Iggy UI.h, so it can't be built as it is. Its functions are
copied into upstream/Minecraft.Client/iOS_TradingMenu.inc and compiled by
Game/MCLETradingMenu.cpp against the `ui` stand-in the container menus use.

What changes:
- The local player here is the server player, the menu's player comes
  from mcle_trading_player().
- A trade goes to mcle_trade_item, the server's
  PlayerConnection::handleTradeItem. The client takes the payment and
  gives the item on its own copy of the inventory before sending the
  TradeItemPacket; that copy is the server's inventory here, so the
  client's half is left out and the trade is made once. The client
  merchant still counts the use, as on console.
- The TRADER_SELECTION_PACKET goes to mcle_trader_selection, the server's
  PlayerConnection::handleCustomPayload for it, as the server is this
  same player.
- The tutorial is Game/MCLETutorial.cpp's, there's no game mode in
  Minecraft::localgameModes to ask for it.

Idempotent, rewrites the .inc every run.
"""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SRC = REPO / "upstream" / "Minecraft.Client" / "Common" / "UI" / "IUIScene_TradingMenu.cpp"
OUT = REPO / "upstream" / "Minecraft.Client" / "iOS_TradingMenu.inc"

START = "IUIScene_TradingMenu::IUIScene_TradingMenu()\n"

REPLACEMENTS = [
    ("shared_ptr<MultiplayerLocalPlayer> player = Minecraft::GetInstance()->localplayers[getPad()];",
     "shared_ptr<ServerPlayer> player = mcle_trading_player();", 3),
    ("m_merchant->getOffers(Minecraft::GetInstance()->localplayers[getPad()])",
     "m_merchant->getOffers(mcle_trading_player())", 3),
    ("Minecraft::GetInstance()->getConnection(getPad())->send(std::make_shared<CustomPayloadPacket>"
     "(CustomPayloadPacket::TRADER_SELECTION_PACKET, rawOutput.toByteArray()));",
     "mcle_trader_selection(rawOutput.toByteArray());", 2),
    ("pMinecraft->localgameModes[getPad()] != nullptr", "mcle_tutorial() != nullptr", 1),
    ("pMinecraft->localgameModes[getPad()]->getTutorial()", "mcle_tutorial()", 1),
]

CLIENT_TRADE_FROM = "// Remove the items we are purchasing with"
CLIENT_TRADE_TO = "player->connection->send(std::make_shared<TradeItemPacket>(m_menu->containerId, actualShopItem));"


def fail(msg):
    print(msg)
    sys.exit(1)


src = SRC.read_text(encoding="utf-8", errors="replace").replace("\r\n", "\n")
if src.count(START) != 1:
    fail(f"constructor not found once in {SRC}")
block = src[src.index(START):]

for old, new, count in REPLACEMENTS:
    n = block.count(old)
    if n != count:
        fail(f"expected {count} of {old!r}, found {n}")
    block = block.replace(old, new)

lines = block.split("\n")
trade_from = [i for i, l in enumerate(lines) if l.strip() == CLIENT_TRADE_FROM]
trade_to = [i for i, l in enumerate(lines) if l.strip() == CLIENT_TRADE_TO]
if len(trade_from) != 1 or len(trade_to) != 1 or not trade_from[0] < trade_to[0]:
    fail("the client's own trade isn't where it was")
indent = lines[trade_from[0]][:len(lines[trade_from[0]]) - len(lines[trade_from[0]].lstrip())]
lines[trade_from[0]:trade_to[0] + 1] = [
    indent + "// mcle_trade_item takes the payment and gives the item, the way",
    indent + "// PlayerConnection::handleTradeItem does",
    indent + "mcle_trade_item(m_menu->containerId, actualShopItem);",
]
block = "\n".join(lines)

code = "\n".join(l for l in block.split("\n") if not l.lstrip().startswith("//"))
if "localplayers" in code or "localgameModes" in code or "connection->" in code or "getConnection" in code:
    fail("client player code left in the trading menu body")

OUT.write_text(block, encoding="utf-8")
print(f"wrote {OUT} ({block.count(chr(10))} lines)")
