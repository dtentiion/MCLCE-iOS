# mcle_ios_game: the iOS top-level simulation shell. Bypasses the
# upstream Minecraft.cpp construction path (blocked on tutorial / trial
# / UI cascade) and instead drives the upstream Level / Mob / NBT path
# directly from a sandboxed iOS save folder.
#
# Same compile-options as mcle_world_probe so the shell sees the same
# pre-included iOS_stdafx + Win32 type aliases + upstream gameplay
# headers as the rest of the linked simulation library.

set(MCLE_GAME_SOURCES
  MCLEGameLoop.cpp
)

add_library(mcle_ios_game STATIC
  ${MCLE_GAME_SOURCES}
)

target_include_directories(mcle_ios_game PRIVATE
  ${UPSTREAM_DIR}/Minecraft.World
  ${UPSTREAM_DIR}/Minecraft.World/x64headers
  ${UPSTREAM_DIR}/Minecraft.Client
  ${UPSTREAM_DIR}/Minecraft.Client/Common
  ${UPSTREAM_DIR}/Minecraft.Client/Common/GameRules
  ${UPSTREAM_DIR}/Minecraft.Client/Common/DLC
  ${UPSTREAM_DIR}/Minecraft.Client/Common/Network
  ${UPSTREAM_DIR}/Minecraft.Client/Common/Audio
  ${UPSTREAM_DIR}/Minecraft.Client/Common/Tutorial
  ${UPSTREAM_DIR}/Minecraft.Client/Common/Colours
  ${UPSTREAM_DIR}/include
  ${CMAKE_SOURCE_DIR}/Minecraft.Client/iOS
  ${CMAKE_SOURCE_DIR}/Minecraft.Client/iOS/compat_headers
)

target_compile_options(mcle_ios_game PRIVATE
  -include "${CMAKE_SOURCE_DIR}/Minecraft.Client/iOS/iOS_stdafx.h"
  -fdeclspec
)

target_compile_definitions(mcle_ios_game PRIVATE
  _LIB
  _LARGE_WORLDS
  _CONTENT_PACKAGE
)

# Pin to C++14 to match the probe library; std::byte vs upstream byte
# typedef would clash under C++17 the same way it does in the probe.
set_target_properties(mcle_ios_game PROPERTIES
  CXX_STANDARD 14
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
)
