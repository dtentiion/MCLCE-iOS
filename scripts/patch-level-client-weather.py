#!/usr/bin/env python3
"""The client's side of a level gets the client level's rain.

A client's level has a rain level of its own. ClientConnection::handleGameEvent
sets it to 1 or 0 outright when the server says the rain has started or
stopped, and nothing tells a client about thunder, so its thunder level stays
at 0. The server's goes up and down a step a tick.

There's one level here, the server's, and the sky, the fog, the light and
the rain itself were all drawn from its levels: rain faded in and out over
five seconds and a thunderstorm darkened the sky, neither of which a client
shows. On iOS Level::getRainLevel and getThunderLevel ask the port first,
which answers with the client's levels when it's the client's side asking
(Game/MCLEWeather.cpp).

Idempotent.
"""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
CPP = REPO / "upstream" / "Minecraft.World" / "Level.cpp"
MARK = "MCLE_iOS_CLIENT_WEATHER"

PAIRS = [
    ("float Level::getThunderLevel(float a)\n"
     "{\n"
     "\treturn (oThunderLevel + (thunderLevel - oThunderLevel) * a) * getRainLevel(a);\n",
     "#ifdef __APPLE_IOS__\n"
     "// MCLE_iOS_CLIENT_WEATHER: Game/MCLEWeather.cpp\n"
     "extern \"C\" int mcle_client_weather_level(const void *level, int thunder, float a, float *out);\n"
     "#endif\n"
     "\n"
     "float Level::getThunderLevel(float a)\n"
     "{\n"
     "#ifdef __APPLE_IOS__\n"
     "\tfloat clientLevel;\n"
     "\tif (mcle_client_weather_level(this, 1, a, &clientLevel)) return clientLevel;\n"
     "#endif\n"
     "\treturn (oThunderLevel + (thunderLevel - oThunderLevel) * a) * getRainLevel(a);\n"),
    ("float Level::getRainLevel(float a)\n"
     "{\n"
     "\treturn oRainLevel + (rainLevel - oRainLevel) * a;\n",
     "float Level::getRainLevel(float a)\n"
     "{\n"
     "#ifdef __APPLE_IOS__\n"
     "\tfloat clientLevel;\n"
     "\tif (mcle_client_weather_level(this, 0, a, &clientLevel)) return clientLevel;\n"
     "#endif\n"
     "\treturn oRainLevel + (rainLevel - oRainLevel) * a;\n"),
]

if not CPP.exists():
    sys.exit(f"missing: {CPP}")
src = CPP.read_text(encoding="utf-8", errors="replace")
if MARK in src:
    print(f"already patched: {CPP}")
    sys.exit(0)
nl = "\r\n" if "\r\n" in src else "\n"
for old, new in PAIRS:
    o = old.replace("\n", nl)
    if src.count(o) != 1:
        sys.exit(f"anchor not found once in {CPP}")
    src = src.replace(o, new.replace("\n", nl))
CPP.write_text(src, encoding="utf-8", errors="replace", newline="")
print(f"patched: {CPP}")
